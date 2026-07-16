/**************************************************************************/
/*  yeet_ai_grok_provider.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/
/*  Grok Build CLI provider (xAI).                                        */
/*                                                                        */
/*  Shells out to the official `grok` CLI in headless mode:               */
/*    grok --no-auto-update -p <prompt> --cwd <project>                   */
/*         --output-format streaming-json --always-approve                */
/*  Auth: `grok login` or XAI_API_KEY. Install:                           */
/*    curl -fsSL https://x.ai/cli/install.sh | bash                       */
/*    irm https://x.ai/cli/install.ps1 | iex   (Windows)                  */
/*  Docs: https://docs.x.ai/build/overview                                */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/string/translation.h"
#include "editor/editor_interface.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/gui/rich_text_label.h"

namespace {

Array _grok_probe_candidates() {
	Array out;
#ifdef WINDOWS_ENABLED
	out.push_back(String("grok.exe"));
	out.push_back(String("grok.cmd"));
	out.push_back(String("grok"));
#else
	out.push_back(String("grok"));
#endif
	return out;
}

String _grok_pull_string_field(const Dictionary &p_dict, const String &p_key) {
	if (!p_dict.has(p_key)) {
		return String();
	}
	const Variant v = p_dict[p_key];
	if (v.get_type() != Variant::STRING && v.get_type() != Variant::STRING_NAME) {
		return String();
	}
	return String(v);
}

void _grok_append_unique_action(Vector<String> &r_lines, const String &p_line) {
	const String line = p_line.strip_edges();
	if (line.is_empty()) {
		return;
	}
	for (int i = 0; i < r_lines.size(); i++) {
		if (r_lines[i] == line) {
			return;
		}
	}
	if (r_lines.size() >= 12) {
		return;
	}
	r_lines.push_back(line.substr(0, 200));
}

void _grok_append_action(String &r_action_summary, const String &p_line) {
	// Kept for extract path; prefer vector collection at call sites when possible.
	if (p_line.is_empty()) {
		return;
	}
	if (!r_action_summary.is_empty()) {
		r_action_summary += "\n";
	}
	r_action_summary += p_line.substr(0, 200);
}

// Best-effort parse of Grok Build headless streaming-json / json events.
// Official formats evolve; tolerate common shapes without failing the turn.
String _grok_extract_text(const Dictionary &p_event, String &r_action_summary) {
	const String type = _grok_pull_string_field(p_event, "type");
	const String event_type = _grok_pull_string_field(p_event, "event");

	// Final result objects
	if (type == "result" || type == "final" || type == "turn.completed" || event_type == "result") {
		String r = _grok_pull_string_field(p_event, "result");
		if (r.is_empty()) {
			r = _grok_pull_string_field(p_event, "text");
		}
		if (r.is_empty()) {
			r = _grok_pull_string_field(p_event, "content");
		}
		if (r.is_empty()) {
			r = _grok_pull_string_field(p_event, "message");
		}
		return r;
	}

	// Nested message / item
	Dictionary message = p_event;
	if (p_event.has("message") && p_event["message"].get_type() == Variant::DICTIONARY) {
		message = p_event["message"];
	} else if (p_event.has("item") && p_event["item"].get_type() == Variant::DICTIONARY) {
		message = p_event["item"];
	} else if (p_event.has("data") && p_event["data"].get_type() == Variant::DICTIONARY) {
		message = p_event["data"];
	}

	const String role = _grok_pull_string_field(message, "role");
	const String item_type = _grok_pull_string_field(message, "type");

	// Tool / shell activity
	if (type == "tool_use" || type == "tool_call" || item_type == "tool_use" || item_type == "command_execution" ||
			item_type == "function_call" || type == "tool" || event_type.contains("tool")) {
		const String name = _grok_pull_string_field(message, "name");
		const String cmd = _grok_pull_string_field(message, "command");
		const String tool = name.is_empty() ? _grok_pull_string_field(message, "tool") : name;
		if (!cmd.is_empty()) {
			_grok_append_action(r_action_summary, "- $ " + cmd);
		} else if (!tool.is_empty()) {
			_grok_append_action(r_action_summary, "- tool " + tool);
		} else {
			_grok_append_action(r_action_summary, "- tool call");
		}
		return String();
	}

	if (type == "error" || item_type == "error") {
		const String msg = _grok_pull_string_field(p_event, "message");
		if (!msg.is_empty()) {
			_grok_append_action(r_action_summary, "- error: " + msg);
		}
		return String();
	}

	// Assistant text chunks
	if (type == "assistant" || type == "agent_message" || type == "message" || item_type == "agent_message" ||
			item_type == "message" || role == "assistant" || type == "content_block_delta" ||
			event_type == "agent_message_chunk" || type == "agent_message_chunk") {
		// Direct text
		String direct = _grok_pull_string_field(message, "text");
		if (direct.is_empty()) {
			direct = _grok_pull_string_field(p_event, "text");
		}
		if (direct.is_empty() && p_event.has("content") && p_event["content"].get_type() == Variant::DICTIONARY) {
			direct = _grok_pull_string_field((Dictionary)p_event["content"], "text");
		}
		if (!direct.is_empty()) {
			return direct;
		}

		// OpenAI-style content array
		String assembled;
		const Variant content_v = message.get("content", p_event.get("content", Variant()));
		if (content_v.get_type() == Variant::ARRAY) {
			const Array parts = content_v;
			for (int i = 0; i < parts.size(); i++) {
				if (parts[i].get_type() == Variant::STRING) {
					if (!assembled.is_empty()) {
						assembled += "\n";
					}
					assembled += String(parts[i]);
					continue;
				}
				if (parts[i].get_type() != Variant::DICTIONARY) {
					continue;
				}
				const Dictionary part = parts[i];
				const String part_type = _grok_pull_string_field(part, "type");
				if (part_type == "text" || part_type == "output_text" || part_type.is_empty()) {
					const String text = _grok_pull_string_field(part, "text");
					if (!text.is_empty()) {
						if (!assembled.is_empty()) {
							assembled += "\n";
						}
						assembled += text;
					}
				} else if (part_type == "tool_use") {
					const String name = _grok_pull_string_field(part, "name");
					_grok_append_action(r_action_summary, "- tool " + (name.is_empty() ? String("call") : name));
				}
			}
		} else if (content_v.get_type() == Variant::STRING) {
			assembled = String(content_v);
		} else if (content_v.get_type() == Variant::DICTIONARY) {
			assembled = _grok_pull_string_field((Dictionary)content_v, "text");
		}
		return assembled;
	}

	// Delta-style streaming
	if (p_event.has("delta")) {
		const Variant delta_v = p_event["delta"];
		if (delta_v.get_type() == Variant::STRING) {
			return String(delta_v);
		}
		if (delta_v.get_type() == Variant::DICTIONARY) {
			const Dictionary d = delta_v;
			String t = _grok_pull_string_field(d, "text");
			if (t.is_empty()) {
				t = _grok_pull_string_field(d, "content");
			}
			return t;
		}
	}

	return String();
}

} // namespace

String YeetAIDock::_detect_grok_binary() {
	if (!_grok_binary_cache.is_empty()) {
		return _grok_binary_cache;
	}

	const Array candidates = _grok_probe_candidates();
	for (int i = 0; i < candidates.size(); i++) {
		const String candidate = candidates[i];
		List<String> args;
		args.push_back("--version");
		String output;
		int exit_code = -1;
		const Error err = OS::get_singleton()->execute(candidate, args, &output, &exit_code, false, nullptr, false);
		if (err == OK && exit_code == 0) {
			_grok_binary_cache = candidate;
			if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
				WARN_PRINT(vformat("[YeetAI Grok] found binary: %s (version: %s)", candidate, output.strip_edges()));
			}
			return _grok_binary_cache;
		}
	}

#ifdef WINDOWS_ENABLED
	{
		List<String> args;
		args.push_back("/C");
		args.push_back("grok");
		args.push_back("--version");
		String output;
		int exit_code = -1;
		const Error err = OS::get_singleton()->execute("cmd.exe", args, &output, &exit_code, false, nullptr, false);
		if (err == OK && exit_code == 0) {
			_grok_binary_cache = "cmd.exe";
			if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
				WARN_PRINT(vformat("[YeetAI Grok] found binary via cmd.exe /C grok (version: %s)", output.strip_edges()));
			}
			return _grok_binary_cache;
		}
	}
#endif
	return String();
}

void YeetAIDock::_grok_thread_trampoline(void *p_user) {
	static_cast<YeetAIDock *>(p_user)->_grok_thread_body();
}

void YeetAIDock::_grok_thread_body() {
	const String binary = _detect_grok_binary();
	if (binary.is_empty()) {
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = TTR("Grok Build CLI was not found. Install with `curl -fsSL https://x.ai/cli/install.sh | bash` (or PowerShell: `irm https://x.ai/cli/install.ps1 | iex`), then run `grok login` or set XAI_API_KEY.");
		_stream_done_flag = true;
		_grok_active = false;
		return;
	}

	// Official headless (https://docs.x.ai/build/cli/headless-scripting):
	//   grok --prompt-file <file> --cwd <project> --output-format streaming-json --always-approve
	// Never pass both -p/--single AND a positional [PROMPT] (clap conflict).
	// Never put the prompt body in argv (Windows quoting).
	const String prompt_file = _cli_write_temp_text("grok_prompt", _grok_active_prompt);
	if (prompt_file.is_empty()) {
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = TTR("Failed to write temporary Grok prompt file.");
		_stream_done_flag = true;
		_grok_active = false;
		return;
	}

	List<String> args;
	args.push_back("--prompt-file");
	args.push_back(prompt_file);
	if (!_grok_workspace_path.is_empty()) {
		args.push_back("--cwd");
		args.push_back(_grok_workspace_path);
	}
	args.push_back("--output-format");
	args.push_back("streaming-json");
	args.push_back("--always-approve");
	args.push_back("--verbatim");
	const String model = _get_editor_setting_string("yeet_ai/chat/model", "").strip_edges();
	if (!model.is_empty() && model != "grok-cli" && model != "grok-build" && !model.begins_with("claude") && !model.begins_with("gpt-5") && model != "codex-cli") {
		args.push_back("-m");
		args.push_back(model);
	}

	if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
		WARN_PRINT(vformat("[YeetAI Grok] spawn: %s (project=%s) prompt_file=%s prompt_len=%d", binary, _grok_workspace_path, prompt_file, _grok_active_prompt.length()));
	}

	String stdout_text;
	int exit_code = -1;
	// No stdin redirect — prompt is already in --prompt-file.
	const String program = _cli_program_token(binary, "grok");
	const Error err = _cli_run_in_project(program, args, _grok_workspace_path, String(), stdout_text, exit_code);
	_cli_delete_temp(prompt_file);

	if (_grok_should_stop) {
		MutexLock lock(_stream_mutex);
		_stream_done_flag = true;
		_grok_active = false;
		return;
	}

	if (err != OK) {
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat(TTR("Failed to launch `grok` (OS error %d). Check that Grok Build CLI is installed and on PATH."), (int)err);
		_stream_done_flag = true;
		_grok_active = false;
		return;
	}

	// Surface CLI arg errors immediately (e.g. conflicting flags).
	if (exit_code != 0 && stdout_text.to_lower().contains("cannot be used with")) {
		MutexLock lock(_stream_mutex);
		_pending_chunks.push_back(stdout_text.strip_edges().substr(0, 800));
		_stream_done_flag = true;
		_grok_active = false;
		return;
	}

	String assistant_accumulated;
	String result_text;
	String action_summary;
	String plain_fallback;
	const PackedStringArray lines = stdout_text.split("\n", false);
	for (int i = 0; i < lines.size(); i++) {
		const String raw = lines[i].strip_edges();
		if (raw.is_empty()) {
			continue;
		}
		if (!raw.begins_with("{")) {
			if (!plain_fallback.is_empty()) {
				plain_fallback += "\n";
			}
			plain_fallback += raw;
			continue;
		}
		Ref<JSON> j;
		j.instantiate();
		if (j->parse(raw) != OK) {
			continue;
		}
		const Variant parsed = j->get_data();
		if (parsed.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary event = parsed;

		const Variant usage_v = event.get("usage", Variant());
		if (usage_v.get_type() == Variant::DICTIONARY && !((Dictionary)usage_v).is_empty()) {
			MutexLock lock(_stream_mutex);
			_stream_usage = usage_v;
		}

		const String type = _grok_pull_string_field(event, "type");
		if (type == "result" || type == "final" || type == "turn.completed") {
			const String r = _grok_extract_text(event, action_summary);
			if (!r.is_empty()) {
				result_text = r;
			}
			continue;
		}

		// Only keep the last agent message as the visible reply; tools go into action_summary.
		const String text_chunk = _grok_extract_text(event, action_summary);
		if (!text_chunk.is_empty()) {
			assistant_accumulated = text_chunk;
		}
	}

	String final_text = result_text.is_empty() ? assistant_accumulated : result_text;
	if (final_text.strip_edges().is_empty() && !plain_fallback.is_empty()) {
		// Prefer last non-empty plain line when JSON parse produced nothing.
		final_text = plain_fallback;
	}
	// Dedupe action_summary lines.
	if (!action_summary.is_empty()) {
		const PackedStringArray alines = action_summary.split("\n", false);
		Vector<String> unique;
		for (int i = 0; i < alines.size(); i++) {
			_grok_append_unique_action(unique, alines[i].strip_edges());
		}
		String compact;
		for (int i = 0; i < unique.size(); i++) {
			if (!compact.is_empty()) {
				compact += "\n";
			}
			// Normalize bullet prefix.
			String u = unique[i];
			if (!u.begins_with("•") && !u.begins_with("-")) {
				u = "• " + u;
			} else if (u.begins_with("-")) {
				u = "•" + u.substr(1);
			}
			compact += u;
		}
		if (!compact.is_empty()) {
			if (!final_text.is_empty()) {
				final_text += "\n\n";
			}
			final_text += "_Actions:_\n" + compact;
		}
	}

	if (final_text.strip_edges().is_empty()) {
		if (exit_code != 0) {
			final_text = vformat(TTR("Grok Build exited with code %d and produced no parsed response. Run `grok login` or set XAI_API_KEY, then retry."), exit_code);
		} else {
			final_text = TTR("Grok Build returned an empty response.");
		}
		const String snippet = stdout_text.strip_edges();
		if (!snippet.is_empty()) {
			final_text += "\n\n_Output:_\n" + snippet.substr(0, 1500);
		}
	}

	{
		MutexLock lock(_stream_mutex);
		_pending_chunks.push_back(final_text);
		_grok_pending_rescan = true;
		_stream_done_flag = true;
	}
	_grok_active = false;
}

Error YeetAIDock::_run_grok_turn(const String &p_task_prompt, const String &p_system_prompt) {
	if (_grok_active) {
		_append_message("assistant", TTR("Grok Build is already running. Wait for the current turn or click Stop."));
		return ERR_ALREADY_IN_USE;
	}

	const String binary = _detect_grok_binary();
	if (binary.is_empty()) {
		_append_message("assistant", TTR("Grok Build CLI was not found on PATH. Install from https://x.ai/cli (curl install.sh or PowerShell install.ps1), authenticate with `grok login` or XAI_API_KEY, then select the Grok CLI provider."));
		_set_waiting(false, TTR("Error"));
		return ERR_CANT_OPEN;
	}

	// Headless uses --prompt-file (not -p + positional). Prepend system steering into that file.
	String combined = p_system_prompt.strip_edges();
	if (!combined.is_empty()) {
		combined += "\n\n";
	}
	combined += p_task_prompt.strip_edges();
	_grok_active_prompt = combined.strip_edges();
	if (_grok_active_prompt.is_empty()) {
		_append_message("assistant", TTR("Empty prompt; nothing to send to Grok Build."));
		_set_waiting(false, TTR("Ready"));
		return ERR_INVALID_PARAMETER;
	}

	_grok_workspace_path = _cli_project_path();

	_stream_accumulated = "";
	_stream_done_flag = false;
	_stream_error_flag = false;
	_stream_error_msg = "";
	_stream_should_stop = false;
	_stream_active = true;
	_stream_expects_sse = true;
	_stream_tool_call_accumulator.clear();
	_stream_has_tool_calls = false;
	{
		MutexLock lock(_stream_mutex);
		_pending_chunks.clear();
	}

	if (stream_label) {
		stream_label->clear();
		stream_label->set_text(TTR("Grok Build is working... (it edits files directly in your project)"));
		stream_label->set_visible(true);
	}

	_grok_active = true;
	_grok_should_stop = false;
	_grok_pending_rescan = false;

	_set_waiting(true, TTR("Grok Build"));

	if (_grok_thread.is_started()) {
		_grok_thread.wait_to_finish();
	}
	_grok_thread.start(&YeetAIDock::_grok_thread_trampoline, this);
	return OK;
}

void YeetAIDock::_grok_finalize_and_rescan() {
	if (!_grok_pending_rescan) {
		return;
	}
	_grok_pending_rescan = false;
	if (EditorInterface::get_singleton() && EditorInterface::get_singleton()->get_resource_filesystem()) {
		EditorInterface::get_singleton()->get_resource_filesystem()->scan();
		if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
			WARN_PRINT("[YeetAI Grok] triggered EditorFileSystem scan");
		}
	}
}

void yeet_ai_register_grok_provider_stub() {
	// Anchor symbol so SCsub's wildcard editor/*.cpp glob keeps this translation unit.
}
