/**************************************************************************/
/*  yeet_ai_claude_provider.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/
/*  Claude Code CLI provider.                                             */
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

Array _claude_probe_candidates() {
	Array out;
#ifdef WINDOWS_ENABLED
	out.push_back(String("claude.exe"));
	out.push_back(String("claude.cmd"));
	out.push_back(String("claude"));
#else
	out.push_back(String("claude"));
#endif
	return out;
}

String _claude_pull_string_field(const Dictionary &p_dict, const String &p_key) {
	if (!p_dict.has(p_key)) {
		return String();
	}
	const Variant v = p_dict[p_key];
	if (v.get_type() != Variant::STRING && v.get_type() != Variant::STRING_NAME) {
		return String();
	}
	return String(v);
}

void _claude_append_unique_action(Vector<String> &r_lines, const String &p_line) {
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
	r_lines.push_back(line);
}

// Collect tool names only; final user text comes from the terminal `result` event.
void _claude_collect_actions(const Dictionary &p_event, Vector<String> &r_action_lines) {
	const String type = _claude_pull_string_field(p_event, "type");
	if (type == "error") {
		const String msg = _claude_pull_string_field(p_event, "message");
		if (!msg.is_empty()) {
			_claude_append_unique_action(r_action_lines, "• error: " + msg.substr(0, 160));
		}
		return;
	}

	Dictionary message;
	if (p_event.has("message") && p_event["message"].get_type() == Variant::DICTIONARY) {
		message = p_event["message"];
	} else {
		message = p_event;
	}

	const Variant content_v = message.get("content", Variant());
	if (content_v.get_type() != Variant::ARRAY) {
		return;
	}
	const Array parts = content_v;
	for (int i = 0; i < parts.size(); i++) {
		if (parts[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary part = parts[i];
		const String part_type = _claude_pull_string_field(part, "type");
		if (part_type == "tool_use") {
			const String name = _claude_pull_string_field(part, "name");
			_claude_append_unique_action(r_action_lines, "• " + (name.is_empty() ? String("tool") : name));
		}
	}
}

} // namespace

String YeetAIDock::_detect_claude_binary() {
	if (!_claude_binary_cache.is_empty()) {
		return _claude_binary_cache;
	}

	const Array candidates = _claude_probe_candidates();
	for (int i = 0; i < candidates.size(); i++) {
		const String candidate = candidates[i];
		List<String> args;
		args.push_back("--version");
		String output;
		int exit_code = -1;
		const Error err = OS::get_singleton()->execute(candidate, args, &output, &exit_code, false, nullptr, false);
		// A clean exit means the named `claude` binary exists and ran. Do NOT also
		// require the version string to contain "claude": some installs print the
		// version to stderr (uncaptured) or as a bare number, which previously made
		// detection fail even though Claude Code was installed.
		if (err == OK && exit_code == 0) {
			_claude_binary_cache = candidate;
			if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
				WARN_PRINT(vformat("[YeetAI Claude] found binary: %s (version: %s)", candidate, output.strip_edges()));
			}
			return _claude_binary_cache;
		}
	}

#ifdef WINDOWS_ENABLED
	{
		List<String> args;
		args.push_back("/C");
		args.push_back("claude");
		args.push_back("--version");
		String output;
		int exit_code = -1;
		const Error err = OS::get_singleton()->execute("cmd.exe", args, &output, &exit_code, false, nullptr, false);
		// npm installs `claude` as a .cmd shim that CreateProcess can't launch
		// directly, so on Windows this cmd.exe route is usually the one that works.
		// Accept on a clean exit; cmd.exe returns non-zero (e.g. 9009) when the
		// command is not recognized, which correctly fails detection.
		if (err == OK && exit_code == 0) {
			_claude_binary_cache = "cmd.exe";
			if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
				WARN_PRINT(vformat("[YeetAI Claude] found binary via cmd.exe /C claude (version: %s)", output.strip_edges()));
			}
			return _claude_binary_cache;
		}
	}
#endif
	return String();
}

void YeetAIDock::_claude_thread_trampoline(void *p_user) {
	static_cast<YeetAIDock *>(p_user)->_claude_thread_body();
}

void YeetAIDock::_claude_thread_body() {
	const String binary = _detect_claude_binary();
	if (binary.is_empty()) {
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = TTR("Claude Code CLI binary was not found. Install Claude Code and run `claude auth` or your normal login flow once, then retry.");
		_stream_done_flag = true;
		_claude_active = false;
		return;
	}

	// Never put multiline system/task prompts in argv — Windows quoting truncates
	// at embedded quotes and made Claude claim the task was "cut off".
	const String sys_file = _claude_system_prompt.strip_edges().is_empty()
			? String()
			: _cli_write_temp_text("claude_sys", _claude_system_prompt);
	const String prompt_file = _cli_write_temp_text("claude_prompt", _claude_active_prompt);
	if (prompt_file.is_empty()) {
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = TTR("Failed to write temporary Claude prompt file.");
		_stream_done_flag = true;
		_claude_active = false;
		return;
	}

	// Headless: claude -p --output-format stream-json --verbose
	//   --permission-mode bypassPermissions --add-dir <project>
	//   --append-system-prompt-file <sys>   + prompt on stdin
	List<String> args;
	args.push_back("-p");
	args.push_back("--output-format");
	args.push_back("stream-json");
	args.push_back("--verbose"); // required with stream-json + -p
	args.push_back("--permission-mode");
	args.push_back("bypassPermissions");
	// Also set the explicit skip flag for older Claude Code builds.
	args.push_back("--dangerously-skip-permissions");
	if (!_claude_workspace_path.is_empty()) {
		args.push_back("--add-dir");
		args.push_back(_claude_workspace_path);
	}
	if (!sys_file.is_empty()) {
		args.push_back("--append-system-prompt-file");
		args.push_back(sys_file);
	}
	const String model = _get_editor_setting_string("yeet_ai/chat/model", "").strip_edges();
	if (!model.is_empty() && model != "claude-code" && model != "claude" && !model.begins_with("gpt") && !model.begins_with("grok")) {
		args.push_back("--model");
		args.push_back(model);
	}
	// Prompt body via stdin (see _cli_run_in_project).

	if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
		WARN_PRINT(vformat("[YeetAI Claude] spawn: %s (project=%s) prompt_len=%d via stdin file", binary, _claude_workspace_path, _claude_active_prompt.length()));
	}

	String stdout_text;
	int exit_code = -1;
	const String program = _cli_program_token(binary, "claude");
	const Error err = _cli_run_in_project(program, args, _claude_workspace_path, prompt_file, stdout_text, exit_code);
	_cli_delete_temp(prompt_file);
	_cli_delete_temp(sys_file);

	if (_claude_should_stop) {
		MutexLock lock(_stream_mutex);
		_stream_done_flag = true;
		_claude_active = false;
		return;
	}

	if (err != OK) {
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat(TTR("Failed to launch `claude` (OS error %d). Check that Claude Code is installed and on PATH."), (int)err);
		_stream_done_flag = true;
		_claude_active = false;
		return;
	}

	String result_text;
	Vector<String> action_lines;
	const PackedStringArray lines = stdout_text.split("\n", false);
	for (int i = 0; i < lines.size(); i++) {
		const String raw = lines[i].strip_edges();
		if (raw.is_empty() || !raw.begins_with("{")) {
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

		// Terminal `result` event is authoritative final text (+ usage).
		if (String(event.get("type", "")) == "result") {
			const String r = _claude_pull_string_field(event, "result");
			if (!r.is_empty()) {
				result_text = r;
			}
			const Variant usage_v = event.get("usage", Variant());
			if (usage_v.get_type() == Variant::DICTIONARY && !((Dictionary)usage_v).is_empty()) {
				MutexLock lock(_stream_mutex);
				_stream_usage = usage_v;
			}
			continue;
		}

		_claude_collect_actions(event, action_lines);
	}

	String final_text = result_text;
	if (!action_lines.is_empty()) {
		String action_summary;
		for (int i = 0; i < action_lines.size(); i++) {
			if (!action_summary.is_empty()) {
				action_summary += "\n";
			}
			action_summary += action_lines[i];
		}
		if (!final_text.is_empty()) {
			final_text += "\n\n";
		}
		final_text += "_Actions:_\n" + action_summary;
	}

	if (final_text.strip_edges().is_empty()) {
		if (exit_code != 0) {
			final_text = vformat(TTR("Claude Code exited with code %d and produced no parsed response. Make sure Claude Code is authenticated and allowed to run non-interactively."), exit_code);
		} else {
			final_text = TTR("Claude Code returned an empty response.");
		}
		const String snippet = stdout_text.strip_edges();
		if (!snippet.is_empty()) {
			final_text += "\n\n_Output:_\n" + snippet.substr(0, 1500);
		}
	}

	{
		MutexLock lock(_stream_mutex);
		_pending_chunks.push_back(final_text);
		_claude_pending_rescan = true;
		_stream_done_flag = true;
	}
	_claude_active = false;
}

Error YeetAIDock::_run_claude_turn(const String &p_task_prompt, const String &p_system_prompt) {
	if (_claude_active) {
		_append_message("assistant", TTR("Claude Code is already running. Wait for the current turn or click Stop."));
		return ERR_ALREADY_IN_USE;
	}

	const String binary = _detect_claude_binary();
	if (binary.is_empty()) {
		_append_message("assistant", TTR("Claude Code CLI was not found on PATH. Install Claude Code and authenticate it in a terminal first."));
		_set_waiting(false, TTR("Error"));
		return ERR_CANT_OPEN;
	}

	_claude_system_prompt = p_system_prompt;
	_claude_active_prompt = p_task_prompt.strip_edges();
	if (_claude_active_prompt.is_empty()) {
		_append_message("assistant", TTR("Empty prompt; nothing to send to Claude Code."));
		_set_waiting(false, TTR("Ready"));
		return ERR_INVALID_PARAMETER;
	}

	_claude_workspace_path = _cli_project_path();

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
		stream_label->set_text(TTR("Claude Code is working... (it edits files directly in your project)"));
		stream_label->set_visible(true);
	}

	_claude_active = true;
	_claude_should_stop = false;
	_claude_pending_rescan = false;

	_set_waiting(true, TTR("Claude Code"));

	if (_claude_thread.is_started()) {
		_claude_thread.wait_to_finish();
	}
	_claude_thread.start(&YeetAIDock::_claude_thread_trampoline, this);
	return OK;
}

void YeetAIDock::_claude_finalize_and_rescan() {
	if (!_claude_pending_rescan) {
		return;
	}
	_claude_pending_rescan = false;
	if (EditorInterface::get_singleton() && EditorInterface::get_singleton()->get_resource_filesystem()) {
		EditorInterface::get_singleton()->get_resource_filesystem()->scan();
		if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
			WARN_PRINT("[YeetAI Claude] triggered EditorFileSystem scan");
		}
	}
}

void yeet_ai_register_claude_provider_stub() {
	// Anchor symbol to keep SCsub's wildcard *.cpp glob including this translation unit.
}
