/**************************************************************************/
/*  yeet_ai_streaming.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "scene/gui/rich_text_label.h"

#include "core/crypto/crypto.h"
#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"

// ── Anonymous-namespace helpers ──────────────────────────────────────────────

static bool yeet_parse_url(const String &p_url, String &r_host, int &r_port, String &r_path, bool &r_use_tls) {
	int scheme_end = p_url.find("://");
	if (scheme_end < 0) {
		return false;
	}
	const String scheme = p_url.substr(0, scheme_end).to_lower().strip_edges();
	r_use_tls = (scheme == "https");
	const String rest = p_url.substr(scheme_end + 3);
	int slash = rest.find("/");
	String host_part;
	if (slash < 0) {
		host_part = rest;
		r_path = "/";
	} else {
		host_part = rest.substr(0, slash);
		r_path = rest.substr(slash);
	}
	int colon = host_part.rfind(":");
	if (colon >= 0) {
		r_host = host_part.substr(0, colon).strip_edges();
		r_port = host_part.substr(colon + 1).strip_edges().to_int();
	} else {
		r_host = host_part.strip_edges();
		r_port = r_use_tls ? 443 : 80;
	}
	return !r_host.is_empty();
}

// Streaming used TLSOptions::client_unsafe() for all https:// URLs. In mbedTLS that clears the TLS
// hostname/SNI field, and many remote APIs (CDN, reverse proxy) require SNI to complete the handshake.
// Use TLSOptions::client() for public hosts (proper SNI + default CA bundle); keep unsafe for LAN/loopback.
static bool yeet_stream_host_prefers_insecure_tls(const String &p_host) {
	const String h = p_host.to_lower().strip_edges();
	if (h == "localhost" || h == "127.0.0.1" || h == "::1") {
		return true;
	}
	if (h.begins_with("127.")) {
		return true;
	}
	if (h.begins_with("10.")) {
		return true;
	}
	if (h.begins_with("192.168.")) {
		return true;
	}
	if (h.begins_with("172.")) {
		const PackedStringArray parts = h.split(".");
		if (parts.size() >= 2) {
			const int o2 = parts[1].to_int();
			if (o2 >= 16 && o2 <= 31) {
				return true;
			}
		}
	}
	return false;
}

static String yeet_describe_http_client_status(HTTPClient::Status p_s) {
	switch (p_s) {
		case HTTPClient::STATUS_CANT_RESOLVE:
			return TTR("Could not resolve the hostname (DNS). Check the URL and network.");
		case HTTPClient::STATUS_CANT_CONNECT:
			return TTR("TCP connect failed (refused or timeout). Check host, port, and that the inference server is running.");
		case HTTPClient::STATUS_TLS_HANDSHAKE_ERROR:
			return TTR("TLS handshake failed. Local servers (Ollama, LM Studio, vLLM) usually need http://127.0.0.1:PORT/... not https://. For a remote https:// API, check the URL in a browser, try another network, and disable VPN/firewall/proxy interference.");
		case HTTPClient::STATUS_CONNECTION_ERROR:
			return TTR("Connection error after connect.");
		default:
			return vformat(TTR("HTTP client status code %d."), (int)p_s);
	}
}

// ── Streaming methods ────────────────────────────────────────────────────────

void YeetAIDock::_stream_thread_trampoline(void *p_user) {
	static_cast<YeetAIDock *>(p_user)->_stream_thread_body();
}

void YeetAIDock::_start_streaming() {
	_stream_accumulated = "";
	_stream_done_flag = false;
	_stream_error_flag = false;
	_stream_error_msg = "";
	_stream_should_stop = false;
	_stream_active = true;
	_stream_tool_call_accumulator.clear();
	_stream_has_tool_calls = false;

	stream_label->clear();
	stream_label->set_visible(true);

	_stream_thread.start(&YeetAIDock::_stream_thread_trampoline, this);
}

void YeetAIDock::_cancel_streaming() {
	if (!_stream_active && !_stream_thread.is_started()) {
		return;
	}
	_stream_should_stop = true;
	if (_stream_thread.is_started()) {
		_stream_thread.wait_to_finish();
	}
	_stream_active = false;
	{
		MutexLock lock(_stream_mutex);
		_pending_chunks.clear();
		_stream_done_flag = false;
		_stream_error_flag = false;
		_stream_tool_call_accumulator.clear();
		_stream_has_tool_calls = false;
	}
}

void YeetAIDock::_stream_thread_body() {
	String host;
	int port = 80;
	String path;
	bool use_tls = false;

	if (!yeet_parse_url(_stream_endpoint, host, port, path, use_tls)) {
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = "Failed to parse endpoint URL: " + _stream_endpoint;
		_stream_done_flag = true;
		return;
	}

	HTTPClient *client = HTTPClient::create();
	client->set_blocking_mode(false);

	Ref<TLSOptions> tls_opts;
	if (use_tls) {
		if (yeet_stream_host_prefers_insecure_tls(host)) {
			tls_opts = TLSOptions::client_unsafe(Ref<X509Certificate>());
		} else {
			tls_opts = TLSOptions::client();
		}
	}

	Error conn_err = client->connect_to_host(host, port, tls_opts);
	if (conn_err != OK) {
		memdelete(client);
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat("connect_to_host failed (err %d)", (int)conn_err);
		_stream_done_flag = true;
		return;
	}

	// Poll until connected
	while (true) {
		if (_stream_should_stop) {
			memdelete(client);
			MutexLock lock(_stream_mutex);
			_stream_done_flag = true;
			return;
		}
		HTTPClient::Status s = client->get_status();
		if (s == HTTPClient::STATUS_CONNECTED) {
			break;
		}
		if (s == HTTPClient::STATUS_CONNECTING || s == HTTPClient::STATUS_RESOLVING) {
			client->poll();
			OS::get_singleton()->delay_usec(5000);
			continue;
		}
		memdelete(client);
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat(TTR("Connection failed (status %d): %s"), (int)s, yeet_describe_http_client_status(s));
		_stream_done_flag = true;
		return;
	}

	// Send the HTTP POST request
	const CharString body_utf8 = _stream_req_body.utf8();
	Error req_err = client->request(
			HTTPClient::METHOD_POST,
			path,
			_stream_req_headers,
			reinterpret_cast<const uint8_t *>(body_utf8.get_data()),
			body_utf8.length());

	if (req_err != OK) {
		memdelete(client);
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat("Request send failed (err %d)", (int)req_err);
		_stream_done_flag = true;
		return;
	}

	// Poll until the server starts responding
	while (true) {
		if (_stream_should_stop) {
			memdelete(client);
			MutexLock lock(_stream_mutex);
			_stream_done_flag = true;
			return;
		}
		HTTPClient::Status s = client->get_status();
		if (s == HTTPClient::STATUS_BODY || s == HTTPClient::STATUS_CONNECTED) {
			break;
		}
		if (s == HTTPClient::STATUS_REQUESTING) {
			client->poll();
			OS::get_singleton()->delay_usec(5000);
			continue;
		}
		memdelete(client);
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat(TTR("Waiting for response failed (status %d): %s"), (int)s, yeet_describe_http_client_status(s));
		_stream_done_flag = true;
		return;
	}

	if (!client->has_response()) {
		memdelete(client);
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = "No response from server.";
		_stream_done_flag = true;
		return;
	}

	const int resp_code = client->get_response_code();
	if (resp_code < 200 || resp_code >= 300) {
		String err_body;
		while (client->get_status() == HTTPClient::STATUS_BODY && !_stream_should_stop) {
			client->poll();
			PackedByteArray chunk = client->read_response_body_chunk();
			if (!chunk.is_empty()) {
				err_body += String::utf8(reinterpret_cast<const char *>(chunk.ptr()), chunk.size());
			}
			OS::get_singleton()->delay_usec(1000);
		}
		memdelete(client);
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat("HTTP %d: %s", resp_code, err_body.substr(0, 400));
		_stream_done_flag = true;
		return;
	}

	// ── Read response body ─────────────────────────────────────────────────────
	if (!_stream_expects_sse) {
		// Non-streaming response: read the entire body as a single JSON object.
		String response_body;
		while (client->get_status() == HTTPClient::STATUS_BODY && !_stream_should_stop) {
			client->poll();
			PackedByteArray raw = client->read_response_body_chunk();
			if (!raw.is_empty()) {
				response_body += String::utf8(reinterpret_cast<const char *>(raw.ptr()), raw.size());
			}
			OS::get_singleton()->delay_usec(1000);
		}
		memdelete(client);
		MutexLock lock(_stream_mutex);
		// Store the full response body as a single "chunk" so _finalize_stream can parse it.
		if (!response_body.strip_edges().is_empty()) {
			_pending_chunks.push_back(response_body);
		}
		_stream_done_flag = true;
		return;
	}

	// ── Read SSE body ──────────────────────────────────────────────────────────
	String sse_buf;
	bool sse_done = false;

	while (!sse_done && client->get_status() == HTTPClient::STATUS_BODY) {
		if (_stream_should_stop) {
			break;
		}
		client->poll();
		PackedByteArray raw = client->read_response_body_chunk();
		if (raw.is_empty()) {
			OS::get_singleton()->delay_usec(1000);
			continue;
		}
		sse_buf += String::utf8(reinterpret_cast<const char *>(raw.ptr()), raw.size());

		// Process all complete lines in the buffer
		while (!sse_done) {
			int nl = sse_buf.find("\n");
			if (nl < 0) {
				break;
			}
			String line = sse_buf.substr(0, nl).strip_edges();
			sse_buf = sse_buf.substr(nl + 1);

			if (!line.begins_with("data:")) {
				continue;
			}
			const String sse_payload = line.substr(5).strip_edges();
			if (sse_payload == "[DONE]") {
				sse_done = true;
				break;
			}

			// Parse delta JSON
			Ref<JSON> jobj;
			jobj.instantiate();
			if (jobj->parse(sse_payload) != OK) {
				continue;
			}
			const Variant parsed = jobj->get_data();
			if (parsed.get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary d = parsed;
			const Array choices = d.get("choices", Array());
			if (choices.is_empty()) {
				continue;
			}
			const Variant c0v = choices[0];
			if (c0v.get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary c0 = c0v;
			const Variant dv = c0.get("delta", Variant());
			if (dv.get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary delta = dv;

			// ── Accumulate streaming tool calls ──────────────────────────────
			const Array delta_tool_calls = delta.get("tool_calls", Array());
			if (!delta_tool_calls.is_empty()) {
				MutexLock lock(_stream_mutex);
				_stream_has_tool_calls = true;
				for (int ti = 0; ti < delta_tool_calls.size(); ti++) {
					if (delta_tool_calls[ti].get_type() != Variant::DICTIONARY) {
						continue;
					}
					const Dictionary tc_delta = delta_tool_calls[ti];
					int tc_idx = tc_delta.get("index", 0);
					while (_stream_tool_call_accumulator.size() <= tc_idx) {
						_stream_tool_call_accumulator.append(Dictionary());
					}
					Dictionary accumulated = _stream_tool_call_accumulator[tc_idx];
					if (tc_delta.has("id")) {
						accumulated["id"] = tc_delta["id"];
					}
					if (tc_delta.has("type")) {
						accumulated["type"] = tc_delta["type"];
					}
					if (tc_delta.has("function")) {
						const Dictionary fn_delta = tc_delta["function"];
						Dictionary fn = accumulated.get("function", Dictionary());
						if (fn_delta.has("name")) {
							fn["name"] = fn_delta["name"];
						}
						if (fn_delta.has("arguments")) {
							String args = fn.get("arguments", "");
							args += String(fn_delta["arguments"]);
							fn["arguments"] = args;
						}
						accumulated["function"] = fn;
					}
					_stream_tool_call_accumulator[tc_idx] = accumulated;
				}
			}

			// ── Stream text content ──────────────────────────────────────────
			String content_chunk = delta.get("content", "");
			if (!content_chunk.is_empty()) {
				// Strip non-ASCII prefix artifacts (e.g., UTF-8 keep-alive bytes like 0xC4 0x81 = "ā").
				{
					int start = 0;
					while (start < content_chunk.length()) {
						const char32_t c = content_chunk[start];
						if (c < 32 || c > 126) {
							start++;
						} else {
							break;
						}
					}
					content_chunk = content_chunk.substr(start);
				}
				// Strip trailing null-byte artifacts and other non-printing suffixes.
				{
					int end = content_chunk.length();
					while (end > 0) {
						const char32_t c = content_chunk[end - 1];
						if (c < 32 || c > 126) {
							end--;
						} else {
							break;
						}
					}
					content_chunk = content_chunk.substr(0, end);
				}
				if (!content_chunk.is_empty()) {
					MutexLock lock(_stream_mutex);
					_pending_chunks.push_back(content_chunk);
				}
			}

			// (If the chunk had neither content nor tool calls, we simply fall through
			// to the next SSE line — no action needed.)
		}
	}

	memdelete(client);
	MutexLock lock(_stream_mutex);
	_stream_done_flag = true;
}

void YeetAIDock::_drain_stream_queue() {
	if (!_stream_active) {
		return;
	}

	// Collect pending data under lock
	Vector<String> chunks;
	bool done = false;
	bool had_error = false;
	String error_msg;
	{
		MutexLock lock(_stream_mutex);
		chunks = _pending_chunks;
		_pending_chunks.clear();
		done = _stream_done_flag;
		had_error = _stream_error_flag;
		error_msg = _stream_error_msg;
	}

	bool updated = !chunks.is_empty();
	for (const String &chunk : chunks) {
		_stream_accumulated += chunk;
	}
	if (updated) {
		_update_stream_label();
	}

	if (done) {
		if (_stream_thread.is_started()) {
			_stream_thread.wait_to_finish();
		}
		_stream_active = false;

		stream_label->set_visible(false);
		stream_label->clear();

		if (had_error) {
			_set_waiting(false, TTR("Error"));
			_append_message("assistant", TTR("Streaming error: ") + error_msg);
		} else {
			_finalize_stream();
		}
	}
}

void YeetAIDock::_finalize_stream() {
	String accumulated = _stream_accumulated;

	// Check for accumulated streaming tool calls first.
	if (_stream_has_tool_calls && !_stream_tool_call_accumulator.is_empty()) {
		Dictionary message;
		message["role"] = "assistant";
		message["content"] = accumulated;

		Array tool_calls;
		bool all_valid = true;
		for (int i = 0; i < _stream_tool_call_accumulator.size(); i++) {
			Dictionary tc = _stream_tool_call_accumulator[i];
			// Ensure required fields exist.
			if (!tc.has("id")) {
				tc["id"] = "call_stream_" + itos(i);
			}
			if (!tc.has("type")) {
				tc["type"] = "function";
			}
			// Validate that function name and arguments are present.
			if (!tc.has("function")) {
				all_valid = false;
				break;
			}
			Dictionary fn = tc["function"];
			if (!fn.has("name") || String(fn["name"]).is_empty()) {
				all_valid = false;
				break;
			}
			// Validate arguments JSON if present.
			if (fn.has("arguments")) {
				String args_str = fn["arguments"];
				if (!args_str.is_empty()) {
					Ref<JSON> j;
					j.instantiate();
					if (j->parse(args_str) != OK) {
						all_valid = false;
						break;
					}
				}
			}
			tool_calls.append(tc);
		}

		if (all_valid) {
			message["tool_calls"] = tool_calls;
			_handle_native_tool_calls(message);
			return;
		}
		// If validation failed, append a note and fall through to text handling.
		accumulated += "\n\n[Note: model attempted tool calls but they were incomplete or malformed.]";
	}

	// Clean artifacts and check for empty response before processing.
	if (accumulated.strip_edges().is_empty()) {
		_set_waiting(false, TTR("Ready"));
		_append_message("assistant", TTR("The model returned an empty response. Try again or adjust your prompt."));
		return;
	}

	// Non-SSE response: parse full OpenAI JSON response object.
	if (!_stream_expects_sse) {
		Ref<JSON> json;
		json.instantiate();
		if (json->parse(accumulated) == OK) {
			const Variant parsed_data = json->get_data();
			if (parsed_data.get_type() == Variant::DICTIONARY) {
				const Dictionary response = parsed_data;
				const Array choices = response.get("choices", Array());
				if (!choices.is_empty() && choices[0].get_type() == Variant::DICTIONARY) {
					const Dictionary choice = choices[0];
					const Dictionary message = choice.get("message", Dictionary());
					const Array tool_calls = message.get("tool_calls", Array());
					if (!tool_calls.is_empty()) {
						// Native tool calls detected — handle them.
						_handle_native_tool_calls(message);
						return;
					}
					const String content = message.get("content", "");
					if (!content.strip_edges().is_empty()) {
						_handle_model_response(content);
						return;
					}
				}
			}
		}
		// If JSON parsing failed or no recognizable structure, fall through to text handling.
	}

	_handle_model_response(accumulated);
}
