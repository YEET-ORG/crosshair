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

static bool yeet_get_non_empty_string_field(const Dictionary &p_dict, const String &p_key, String &r_value) {
	if (!p_dict.has(p_key)) {
		return false;
	}
	const Variant value = p_dict.get(p_key, Variant());
	if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) {
		return false;
	}
	const String text = String(value).strip_edges();
	if (text.is_empty() || text == "<null>") {
		return false;
	}
	r_value = text;
	return true;
}

// Normalize a streamed tool-call slot into OpenAI chat.completions shape.
// Returns false for empty sparse holes (Responses API uses output_index which
// leaves gaps for reasoning/message items — those must be skipped, not rejected).
static bool yeet_normalize_streamed_tool_call(const Dictionary &p_tc, int p_index, Dictionary &r_out) {
	if (p_tc.is_empty()) {
		return false;
	}
	const Variant fn_variant = p_tc.get("function", Variant());
	if (fn_variant.get_type() != Variant::DICTIONARY) {
		return false;
	}
	Dictionary fn = fn_variant;
	String function_name;
	if (!yeet_get_non_empty_string_field(fn, "name", function_name)) {
		return false;
	}

	String args_str;
	const Variant args_variant = fn.get("arguments", Variant());
	if (args_variant.get_type() == Variant::STRING || args_variant.get_type() == Variant::STRING_NAME) {
		args_str = String(args_variant).strip_edges();
	} else if (args_variant.get_type() == Variant::DICTIONARY || args_variant.get_type() == Variant::ARRAY) {
		// Some providers send already-parsed JSON — re-stringify.
		args_str = JSON::stringify(args_variant, "", false);
	}
	if (args_str.is_empty() || args_str == "null") {
		args_str = "{}";
	}
	// Validate / lightly repair arguments JSON.
	{
		Ref<JSON> j;
		j.instantiate();
		if (j->parse(args_str) != OK) {
			// Common truncation: missing closing braces. Try one repair pass.
			String repaired = args_str;
			int open_braces = 0;
			int open_brackets = 0;
			bool in_string = false;
			bool escape = false;
			for (int i = 0; i < repaired.length(); i++) {
				const char32_t c = repaired[i];
				if (escape) {
					escape = false;
					continue;
				}
				if (c == '\\' && in_string) {
					escape = true;
					continue;
				}
				if (c == '"') {
					in_string = !in_string;
					continue;
				}
				if (in_string) {
					continue;
				}
				if (c == '{') {
					open_braces++;
				} else if (c == '}') {
					open_braces = MAX(0, open_braces - 1);
				} else if (c == '[') {
					open_brackets++;
				} else if (c == ']') {
					open_brackets = MAX(0, open_brackets - 1);
				}
			}
			if (in_string) {
				repaired += "\"";
			}
			while (open_brackets-- > 0) {
				repaired += "]";
			}
			while (open_braces-- > 0) {
				repaired += "}";
			}
			if (j->parse(repaired) != OK) {
				return false; // still unusable
			}
			args_str = repaired;
		}
	}

	Dictionary out_fn;
	out_fn["name"] = function_name;
	out_fn["arguments"] = args_str;

	Dictionary out;
	String id;
	if (!yeet_get_non_empty_string_field(p_tc, "id", id)) {
		id = "call_stream_" + itos(p_index);
	}
	out["id"] = id;
	out["type"] = "function";
	out["function"] = out_fn;
	r_out = out;
	return true;
}

static Dictionary yeet_make_function_tool_call(const Dictionary &p_item, int p_fallback_index) {
	Dictionary tc;
	tc["id"] = String(p_item.get("call_id", p_item.get("id", "call_" + itos(p_fallback_index))));
	tc["type"] = "function";
	Dictionary fn;
	fn["name"] = String(p_item.get("name", ""));
	String args = String(p_item.get("arguments", ""));
	if (args.is_empty()) {
		args = "{}";
	}
	fn["arguments"] = args;
	tc["function"] = fn;
	return tc;
}

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
	_stream_usage = Dictionary();
	// _stream_protocol is set by _request_model_response before start.

	stream_label->clear();
	stream_label->set_visible(true);

	_stream_thread.start(&YeetAIDock::_stream_thread_trampoline, this);
}

void YeetAIDock::_cancel_streaming() {
	if (!_stream_active && !_stream_thread.is_started() && !_codex_thread.is_started() && !_claude_thread.is_started() && !_grok_thread.is_started()) {
		return;
	}
	_stream_should_stop = true;
	_codex_should_stop = true;
	_claude_should_stop = true;
	_grok_should_stop = true;
	if (_stream_thread.is_started()) {
		_stream_thread.wait_to_finish();
	}
	if (_codex_thread.is_started()) {
		_codex_thread.wait_to_finish();
	}
	if (_claude_thread.is_started()) {
		_claude_thread.wait_to_finish();
	}
	if (_grok_thread.is_started()) {
		_grok_thread.wait_to_finish();
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

	// ── Retry loop for transient errors ──────────────────────────────────────
	// Retry gateway failures. Treat 429 as terminal so quota errors do not replay
	// the same expensive request several times.
	static constexpr int MAX_RETRIES = 3;

	for (int attempt = 0; attempt <= MAX_RETRIES; attempt++) {
		if (_stream_should_stop) {
			MutexLock lock(_stream_mutex);
			_stream_done_flag = true;
			return;
		}

		if (attempt > 0) {
			// Exponential backoff: 1s, 2s, 4s
			const int delay_ms = 1000 << (attempt - 1);
			OS::get_singleton()->delay_usec(uint32_t(delay_ms) * 1000);
			if (_stream_should_stop) {
				MutexLock lock(_stream_mutex);
				_stream_done_flag = true;
				return;
			}
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

			// Decide whether to retry.
			if (resp_code == 429) {
				MutexLock lock(_stream_mutex);
				_stream_error_flag = true;
				_stream_error_msg = vformat("HTTP 429 rate limit/quota exceeded (not retried): %s", err_body.substr(0, 400));
				_stream_done_flag = true;
				return;
			}
			const bool is_retryable = (resp_code == 502 || resp_code == 503 || resp_code == 504);
			if (is_retryable && attempt < MAX_RETRIES) {
				continue; // Retry with backoff
			}

			MutexLock lock(_stream_mutex);
			_stream_error_flag = true;
			_stream_error_msg = vformat("HTTP %d: %s", resp_code, err_body.substr(0, 400));
			_stream_done_flag = true;
			return;
		}

		// ── Success: read response body ──────────────────────────────────────
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

		// ── Read SSE body ────────────────────────────────────────────────────
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

				// ── Responses API v1 SSE (Azure Foundry / OpenAI Responses) ──────
				if (_stream_protocol == 1) {
					const String ev_type = String(d.get("type", ""));
					// Usage on completed response. Prefer a *dense* rebuild of tool
					// calls from final output[] — output_index is sparse (reasoning /
					// message items leave holes that used to fail validation).
					if (ev_type == "response.completed" || ev_type == "response.incomplete") {
						const Dictionary resp = d.get("response", Dictionary());
						const Variant usage_v = resp.get("usage", d.get("usage", Variant()));
						if (usage_v.get_type() == Variant::DICTIONARY && !((Dictionary)usage_v).is_empty()) {
							MutexLock lock(_stream_mutex);
							_stream_usage = usage_v;
						}
						const Array output = resp.get("output", Array());
						Array dense_tool_calls;
						for (int oi = 0; oi < output.size(); oi++) {
							if (output[oi].get_type() != Variant::DICTIONARY) {
								continue;
							}
							const Dictionary item = output[oi];
							if (String(item.get("type", "")) != "function_call") {
								continue;
							}
							dense_tool_calls.append(yeet_make_function_tool_call(item, dense_tool_calls.size()));
						}
						if (!dense_tool_calls.is_empty()) {
							MutexLock lock(_stream_mutex);
							_stream_has_tool_calls = true;
							_stream_tool_call_accumulator = dense_tool_calls;
						}
						continue;
					}
					if (ev_type == "response.output_text.delta") {
						const String content_chunk = String(d.get("delta", ""));
						if (!content_chunk.is_empty()) {
							MutexLock lock(_stream_mutex);
							_pending_chunks.push_back(content_chunk);
						}
						continue;
					}
					// Full item (added or done) — store by output_index for arg deltas.
					if (ev_type == "response.output_item.added" || ev_type == "response.output_item.done") {
						const Dictionary item = d.get("item", Dictionary());
						if (String(item.get("type", "")) == "function_call") {
							MutexLock lock(_stream_mutex);
							_stream_has_tool_calls = true;
							const int tc_idx = int(d.get("output_index", _stream_tool_call_accumulator.size()));
							while (_stream_tool_call_accumulator.size() <= tc_idx) {
								_stream_tool_call_accumulator.append(Dictionary());
							}
							Dictionary tc = yeet_make_function_tool_call(item, tc_idx);
							// Preserve args already streamed if the item has empty arguments.
							Dictionary prev = _stream_tool_call_accumulator[tc_idx];
							if (!prev.is_empty()) {
								Dictionary prev_fn = prev.get("function", Dictionary());
								Dictionary new_fn = tc.get("function", Dictionary());
								const String prev_args = String(prev_fn.get("arguments", ""));
								const String new_args = String(new_fn.get("arguments", ""));
								if ((new_args.is_empty() || new_args == "{}") && !prev_args.is_empty() && prev_args != "{}") {
									new_fn["arguments"] = prev_args;
									tc["function"] = new_fn;
								}
								if (String(new_fn.get("name", "")).is_empty() && !String(prev_fn.get("name", "")).is_empty()) {
									new_fn["name"] = prev_fn.get("name", "");
									tc["function"] = new_fn;
								}
							}
							_stream_tool_call_accumulator[tc_idx] = tc;
						}
						continue;
					}
					if (ev_type == "response.function_call_arguments.delta") {
						MutexLock lock(_stream_mutex);
						_stream_has_tool_calls = true;
						const int tc_idx = int(d.get("output_index", 0));
						while (_stream_tool_call_accumulator.size() <= tc_idx) {
							_stream_tool_call_accumulator.append(Dictionary());
						}
						Dictionary tc = _stream_tool_call_accumulator[tc_idx];
						tc["type"] = "function";
						if (!tc.has("id") || String(tc.get("id", "")).is_empty()) {
							tc["id"] = String(d.get("item_id", d.get("call_id", "call_" + itos(tc_idx))));
						}
						Dictionary fn = tc.get("function", Dictionary());
						// Name sometimes only appears on the done/item event — keep prior.
						if (!fn.has("name") || String(fn.get("name", "")).is_empty()) {
							const String n = String(d.get("name", ""));
							if (!n.is_empty()) {
								fn["name"] = n;
							}
						}
						String args = String(fn.get("arguments", ""));
						args += String(d.get("delta", ""));
						fn["arguments"] = args;
						tc["function"] = fn;
						_stream_tool_call_accumulator[tc_idx] = tc;
						continue;
					}
					if (ev_type == "response.function_call_arguments.done") {
						MutexLock lock(_stream_mutex);
						_stream_has_tool_calls = true;
						const int tc_idx = int(d.get("output_index", 0));
						while (_stream_tool_call_accumulator.size() <= tc_idx) {
							_stream_tool_call_accumulator.append(Dictionary());
						}
						Dictionary tc = _stream_tool_call_accumulator[tc_idx];
						tc["type"] = "function";
						if (!tc.has("id") || String(tc.get("id", "")).is_empty()) {
							tc["id"] = String(d.get("item_id", d.get("call_id", "call_" + itos(tc_idx))));
						}
						Dictionary fn = tc.get("function", Dictionary());
						const String n = String(d.get("name", ""));
						if (!n.is_empty()) {
							fn["name"] = n;
						}
						const String full_args = String(d.get("arguments", ""));
						if (!full_args.is_empty()) {
							fn["arguments"] = full_args;
						} else if (!fn.has("arguments") || String(fn.get("arguments", "")).is_empty()) {
							fn["arguments"] = "{}";
						}
						tc["function"] = fn;
						_stream_tool_call_accumulator[tc_idx] = tc;
						continue;
					}
					// Ignore other response.* lifecycle events.
					continue;
				}

				// ── Chat Completions SSE (choices[].delta) ───────────────────────
				// Capture token usage — present on the final SSE chunk when
				// stream_options.include_usage=true. This chunk usually has an empty
				// `choices` array, so grab usage before the early-continue below.
				{
					const Variant usage_v = d.get("usage", Variant());
					if (usage_v.get_type() == Variant::DICTIONARY && !((Dictionary)usage_v).is_empty()) {
						MutexLock lock(_stream_mutex);
						_stream_usage = usage_v;
					}
				}
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
						String text_field;
						if (yeet_get_non_empty_string_field(tc_delta, "id", text_field)) {
							accumulated["id"] = text_field;
						}
						if (yeet_get_non_empty_string_field(tc_delta, "type", text_field)) {
							accumulated["type"] = text_field;
						}
						const Variant fn_delta_variant = tc_delta.get("function", Variant());
						if (fn_delta_variant.get_type() == Variant::DICTIONARY) {
							const Dictionary fn_delta = fn_delta_variant;
							Dictionary fn = accumulated.get("function", Dictionary());
							if (yeet_get_non_empty_string_field(fn_delta, "name", text_field)) {
								fn["name"] = text_field;
							}
							const Variant arguments_delta = fn_delta.get("arguments", Variant());
							if (arguments_delta.get_type() == Variant::STRING) {
								String args = String(fn.get("arguments", ""));
								args += String(arguments_delta);
								fn["arguments"] = args;
							}
							accumulated["function"] = fn;
						}
						_stream_tool_call_accumulator[tc_idx] = accumulated;
					}
				}

				// ── Stream text content ──────────────────────────────────────────
				String content_chunk;
				const Variant content_variant = delta.get("content", Variant());
				if (content_variant.get_type() == Variant::STRING) {
					content_chunk = String(content_variant);
				}
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
		return;
	}
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
		if (_codex_thread.is_started()) {
			_codex_thread.wait_to_finish();
		}
		if (_claude_thread.is_started()) {
			_claude_thread.wait_to_finish();
		}
		if (_grok_thread.is_started()) {
			_grok_thread.wait_to_finish();
		}
		_stream_active = false;

		stream_label->set_visible(false);
		stream_label->clear();

		if (had_error) {
			_set_waiting(false, TTR("Error"));
			_append_message("assistant", TTR("Streaming error: ") + error_msg);
		} else {
			_finalize_stream();
			_codex_finalize_and_rescan();
			_claude_finalize_and_rescan();
			_grok_finalize_and_rescan();
		}
	}
}

void YeetAIDock::_capture_usage_from_response(const Dictionary &p_usage) {
	if (p_usage.is_empty()) {
		return;
	}
	_last_usage = p_usage;

	int prompt_tokens = 0;
	if (p_usage.has("prompt_tokens")) {
		prompt_tokens = int(p_usage.get("prompt_tokens", 0));
	} else if (p_usage.has("input_tokens")) { // Anthropic-style naming
		prompt_tokens = int(p_usage.get("input_tokens", 0));
	}
	if (prompt_tokens > 0) {
		_last_prompt_tokens = prompt_tokens;
	}

	if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
		// Surface prompt-cache effectiveness when the provider reports it.
		int cached = 0;
		const Variant details_v = p_usage.get("prompt_tokens_details", Variant());
		if (details_v.get_type() == Variant::DICTIONARY) {
			cached = int(((Dictionary)details_v).get("cached_tokens", 0));
		}
		if (p_usage.has("cache_read_input_tokens")) { // Anthropic-style
			cached = int(p_usage.get("cache_read_input_tokens", 0));
		}
		const int completion = p_usage.has("completion_tokens")
				? int(p_usage.get("completion_tokens", 0))
				: int(p_usage.get("output_tokens", 0));
		WARN_PRINT(vformat("[YeetAI Usage] prompt=%d completion=%d cached=%d", prompt_tokens, completion, cached));
	}
}

void YeetAIDock::_finalize_stream() {
	String accumulated = _stream_accumulated;

	// Pull any usage captured from the SSE stream's final chunk (set on the worker
	// thread) and fold it in on the main thread before dispatching the response.
	{
		Dictionary usage_copy;
		{
			MutexLock lock(_stream_mutex);
			usage_copy = _stream_usage;
			_stream_usage = Dictionary();
		}
		if (!usage_copy.is_empty()) {
			_capture_usage_from_response(usage_copy);
			_update_token_counter();
		}
	}

	if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
		WARN_PRINT(vformat("[YeetAI Debug] Response (%s):\n%s", _stream_has_tool_calls ? "tool_calls" : "text", accumulated));
	}

	// Check for accumulated streaming tool calls first.
	// Skip sparse holes (empty slots left by Responses API output_index) instead
	// of rejecting the whole batch — that was the "incomplete or malformed" bug.
	if (_stream_has_tool_calls && !_stream_tool_call_accumulator.is_empty()) {
		Dictionary message;
		message["role"] = "assistant";
		message["content"] = accumulated;

		Array tool_calls;
		int skipped_slots = 0;
		int malformed_slots = 0;
		for (int i = 0; i < _stream_tool_call_accumulator.size(); i++) {
			const Dictionary raw = _stream_tool_call_accumulator[i];
			if (raw.is_empty()) {
				skipped_slots++;
				continue;
			}
			Dictionary normalized;
			if (yeet_normalize_streamed_tool_call(raw, i, normalized)) {
				tool_calls.append(normalized);
			} else {
				malformed_slots++;
				if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
					WARN_PRINT(vformat("[YeetAI Debug] Skipping malformed tool call slot %d: %s", i, JSON::stringify(raw, "", false)));
				}
			}
		}

		if (!tool_calls.is_empty()) {
			message["tool_calls"] = tool_calls;
			_handle_native_tool_calls(message);
			return;
		}
		// All slots empty/malformed — surface a useful note rather than silent drop.
		if (malformed_slots > 0) {
			accumulated += "\n\n[Note: model attempted tool calls but they were incomplete or malformed.]";
		} else if (skipped_slots > 0 && accumulated.strip_edges().is_empty()) {
			// Had sparse placeholders only — treat as empty model response.
		}
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
				const Variant usage_v = response.get("usage", Variant());
				if (usage_v.get_type() == Variant::DICTIONARY) {
					_capture_usage_from_response(usage_v);
					_update_token_counter();
				}

				// Responses API: output_text / output[] with function_call items.
				if (_stream_protocol == 1 || response.has("output") || response.has("output_text")) {
					Array tool_calls;
					const Array output = response.get("output", Array());
					for (int i = 0; i < output.size(); i++) {
						if (output[i].get_type() != Variant::DICTIONARY) {
							continue;
						}
						const Dictionary item = output[i];
						if (String(item.get("type", "")) != "function_call") {
							continue;
						}
						Dictionary tc;
						tc["id"] = String(item.get("call_id", item.get("id", "call_" + itos(i))));
						tc["type"] = "function";
						Dictionary fn;
						fn["name"] = String(item.get("name", ""));
						fn["arguments"] = String(item.get("arguments", "{}"));
						tc["function"] = fn;
						tool_calls.append(tc);
					}
					if (!tool_calls.is_empty()) {
						Dictionary message;
						message["role"] = "assistant";
						message["content"] = String(response.get("output_text", ""));
						message["tool_calls"] = tool_calls;
						_handle_native_tool_calls(message);
						return;
					}
					String content = String(response.get("output_text", ""));
					if (content.is_empty()) {
						// Flatten message items' output_text parts.
						for (int i = 0; i < output.size(); i++) {
							if (output[i].get_type() != Variant::DICTIONARY) {
								continue;
							}
							const Dictionary item = output[i];
							if (String(item.get("type", "")) != "message") {
								continue;
							}
							const Array content_parts = item.get("content", Array());
							for (int c = 0; c < content_parts.size(); c++) {
								if (content_parts[c].get_type() != Variant::DICTIONARY) {
									continue;
								}
								const Dictionary part = content_parts[c];
								if (String(part.get("type", "")) == "output_text" || part.has("text")) {
									if (!content.is_empty()) {
										content += "\n";
									}
									content += String(part.get("text", ""));
								}
							}
						}
					}
					if (!content.strip_edges().is_empty()) {
						_handle_model_response(content);
						return;
					}
				}

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
