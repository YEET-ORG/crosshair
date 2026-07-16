/**************************************************************************/
/*  yeet_ai_codex_provider.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/
/*  Codex CLI provider.                                                   */
/*                                                                        */
/*  Shells out to the `codex` CLI as a sibling of the existing HTTP-based  */
/*  providers. When YeetAISettingsPanel reports provider id 5 (Codex CLI), */
/*  _request_model_response short-circuits into _run_codex_turn, which     */
/*  spawns `codex exec --json --dangerously-bypass-approvals-and-sandbox    */
/*  --cd <project>` in the open project dir via a worker thread (the OS     */
/*  sandbox is bypassed because it does not exist on Windows and would      */
/*  otherwise block all file edits). The stdout JSONL stream is parsed and  */
/*  the assistant text plus an action transcript are pushed                */
/*  back through the existing _stream_accumulated / _pending_chunks       */
/*  machinery so _finalize_stream ends up calling _handle_model_response. */
/*  After the worker finishes we request EditorFileSystem::scan() so any   */
/*  files Codex created/modified appear in the FileSystem dock.           */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/string/translation.h"
#include "editor/editor_interface.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/gui/rich_text_label.h"

namespace {

// Cross-platform binary candidates. `codex` is what `npm i -g @openai/codex`
// installs on Unix; Windows npm shims may require cmd.exe /C, handled as a
// fallback in _detect_codex_binary().
Array _codex_probe_candidates() {
	Array out;
#ifdef WINDOWS_ENABLED
	out.push_back(String("codex.cmd"));
	out.push_back(String("codex.exe"));
	out.push_back(String("codex"));
#else
	out.push_back(String("codex"));
#endif
	return out;
}

// Codex emits some `error`-type items that are benign infrastructure chatter
// (transport fallback, reconnect attempts, skill-budget notices). These are not
// real failures and should not be surfaced to the user as errors.
bool _codex_is_benign_notice(const String &p_msg) {
	const String m = p_msg.to_lower();
	return m.contains("falling back") || m.contains("reconnect") ||
			m.contains("websocket") || m.contains("skill descriptions") ||
			m.contains("context budget") || m.contains("disconnected before completion");
}

String _pull_string_field(const Dictionary &p_dict, const String &p_key) {
	if (!p_dict.has(p_key)) {
		return String();
	}
	const Variant v = p_dict[p_key];
	if (v.get_type() != Variant::STRING && v.get_type() != Variant::STRING_NAME) {
		return String();
	}
	return String(v);
}

void _codex_append_unique_line(Vector<String> &r_lines, const String &p_line) {
	const String line = p_line.strip_edges();
	if (line.is_empty()) {
		return;
	}
	for (int i = 0; i < r_lines.size(); i++) {
		if (r_lines[i] == line) {
			return;
		}
	}
	// Cap noisy transcripts (Cursor-style short action list).
	if (r_lines.size() >= 12) {
		return;
	}
	r_lines.push_back(line);
}

// Shorten absolute paths and PowerShell wrappers for readable action lines.
String _codex_humanize_path(const String &p_path) {
	String p = p_path.replace("\\", "/").strip_edges();
	if (p.is_empty()) {
		return p;
	}
	// Prefer res://-style relative if path contains a project-ish materials/scenes segment.
	const int mat = p.findn("/materials/");
	if (mat >= 0) {
		return "res:/" + p.substr(mat);
	}
	const int scenes = p.findn("/scenes/");
	if (scenes >= 0) {
		return "res:/" + p.substr(scenes);
	}
	const int scripts = p.findn("/scripts/");
	if (scripts >= 0) {
		return "res:/" + p.substr(scripts);
	}
	// Drop drive prefix noise: C:/Users/.../file.tres → …/file.tres
	const int last_slash = p.rfind("/");
	if (last_slash > 0 && p.length() - last_slash < 48) {
		// keep last two segments when long
		int prev = p.rfind("/", last_slash - 1);
		if (prev >= 0 && p.length() > 60) {
			return "…/" + p.substr(prev + 1);
		}
	}
	if (p.length() > 72) {
		return "…" + p.substr(p.length() - 60);
	}
	return p;
}

String _codex_humanize_command(const String &p_cmd) {
	String cmd = p_cmd.strip_edges();
	if (cmd.is_empty()) {
		return "(command)";
	}
	// Unwrap: powershell.exe -Command '...' or -Command "..."
	const String lower = cmd.to_lower();
	int cmd_flag = lower.find("-command");
	if ((lower.contains("powershell") || lower.contains("pwsh")) && cmd_flag >= 0) {
		String rest = cmd.substr(cmd_flag + String("-command").length()).strip_edges();
		if ((rest.begins_with("'") && rest.ends_with("'")) || (rest.begins_with("\"") && rest.ends_with("\""))) {
			rest = rest.substr(1, rest.length() - 2);
		}
		rest = rest.strip_edges();
		// Map common inspection commands to short verbs.
		if (rest.findn("Get-Content") >= 0) {
			// Try to extract -Path value.
			int path_i = rest.findn("-Path");
			if (path_i >= 0) {
				String path_part = rest.substr(path_i + 5).strip_edges();
				// strip leading separators / quotes
				while (!path_part.is_empty() && (path_part[0] == ' ' || path_part[0] == '=')) {
					path_part = path_part.substr(1);
				}
				if ((path_part.begins_with("'") || path_part.begins_with("\"")) && path_part.length() > 1) {
					const char32_t q = path_part[0];
					const int end = path_part.find_char(q, 1);
					if (end > 1) {
						path_part = path_part.substr(1, end - 1);
					}
				} else {
					const int sp = path_part.find(" ");
					if (sp > 0) {
						path_part = path_part.substr(0, sp);
					}
				}
				return "read " + _codex_humanize_path(path_part.replace(".\\", "").replace("./", ""));
			}
			return "read file";
		}
		if (rest.findn("Get-ChildItem") >= 0) {
			return "list files";
		}
		if (rest.findn("rg ") >= 0 || rest.findn("Select-String") >= 0 || rest.begins_with("rg")) {
			return "search project";
		}
		if (rest.length() > 100) {
			return rest.substr(0, 100) + "…";
		}
		return rest;
	}
	if (cmd.length() > 120) {
		return cmd.substr(0, 120) + "…";
	}
	return cmd;
}

// Codex exec --json emits one JSONL object per line. Extract assistant text
// and a compact completed-only action trail (skip item.started / in_progress noise).
String _codex_extract_assistant_text(const Dictionary &p_event, Vector<String> &r_action_lines, String &r_latest_agent_message) {
	const String type = _pull_string_field(p_event, "type");
	Dictionary event = p_event;
	const Variant item_v = p_event.get("item", Variant());
	if (item_v.get_type() == Variant::DICTIONARY) {
		event = item_v;
	}

	const String item_type = _pull_string_field(event, "type");
	// Only record side-effects on completed items. item.started floods the UI with
	// [in_progress] duplicates of the same command.
	const bool is_completed_item = (type == "item.completed");
	const bool is_started_item = (type == "item.started");

	// ── Current Codex CLI event schema ────────────────────────────────────────
	if (item_type == "agent_message") {
		const String text = _pull_string_field(event, "text");
		// Keep only the latest agent message as the user-facing reply (not every
		// intermediate "I'll inspect…" status line).
		if (!text.is_empty() && (is_completed_item || type.is_empty() || type == "item.completed")) {
			r_latest_agent_message = text;
		}
		// Return empty so the outer loop does not concatenate every intermediate.
		return String();
	}
	if (item_type == "reasoning") {
		return String();
	}
	if (item_type == "command_execution") {
		if (is_started_item) {
			return String(); // ignore in_progress
		}
		const String status = _pull_string_field(event, "status").to_lower();
		if (status == "in_progress" || status == "running") {
			return String();
		}
		if (!is_completed_item && !status.is_empty() && status != "completed" && status != "failed" && status != "success") {
			return String();
		}
		const String human = _codex_humanize_command(_pull_string_field(event, "command"));
		_codex_append_unique_line(r_action_lines, "• " + human);
		return String();
	}
	if (item_type == "file_change" || item_type == "patch_apply" || item_type == "file_update") {
		if (is_started_item) {
			return String();
		}
		Vector<String> files;
		const Variant changes_v = event.get("changes", Variant());
		if (changes_v.get_type() == Variant::ARRAY) {
			const Array ch = changes_v;
			for (int i = 0; i < ch.size(); i++) {
				if (ch[i].get_type() != Variant::DICTIONARY) {
					continue;
				}
				const String p = _pull_string_field((Dictionary)ch[i], "path");
				if (!p.is_empty()) {
					files.push_back(_codex_humanize_path(p));
				}
			}
		}
		const String single = _pull_string_field(event, "path");
		if (files.is_empty() && !single.is_empty()) {
			files.push_back(_codex_humanize_path(single));
		}
		for (int i = 0; i < files.size(); i++) {
			_codex_append_unique_line(r_action_lines, "• edited " + files[i]);
		}
		return String();
	}
	if (item_type == "mcp_tool_call" || item_type == "web_search") {
		if (is_started_item) {
			return String();
		}
		const String tool = _pull_string_field(event, "tool");
		const String server = _pull_string_field(event, "server");
		const String query = _pull_string_field(event, "query");
		if (item_type == "web_search") {
			_codex_append_unique_line(r_action_lines, "• web search: " + (query.length() > 80 ? query.substr(0, 80) + "…" : query));
		} else {
			_codex_append_unique_line(r_action_lines, "• tool " + (server.is_empty() ? String() : server + "/") + (tool.is_empty() ? String("call") : tool));
		}
		return String();
	}
	if (item_type == "error") {
		const String msg = _pull_string_field(event, "message");
		if (!msg.is_empty() && !_codex_is_benign_notice(msg)) {
			_codex_append_unique_line(r_action_lines, "• error: " + msg.substr(0, 160));
		}
		return String();
	}

	// ── Legacy Responses-API format (older codex versions) ────────────────────
	if (item_type == "message") {
		const String role = _pull_string_field(event, "role");
		if (role != "assistant") {
			return String();
		}
		String assembled;
		const Variant content_v = event.get("content", Variant());
		if (content_v.get_type() == Variant::ARRAY) {
			const Array parts = content_v;
			for (int i = 0; i < parts.size(); i++) {
				if (parts[i].get_type() != Variant::DICTIONARY) {
					continue;
				}
				const Dictionary part = parts[i];
				const String part_type = _pull_string_field(part, "type");
				if (part_type == "output_text" || part_type == "text" || part_type == "input_text") {
					const String t = _pull_string_field(part, "text");
					if (!t.is_empty()) {
						if (!assembled.is_empty()) {
							assembled += "\n";
						}
						assembled += t;
					}
				}
			}
		} else if (content_v.get_type() == Variant::STRING) {
			assembled = String(content_v);
		}
		if (!assembled.is_empty()) {
			r_latest_agent_message = assembled;
		}
		return String();
	}

	if (item_type == "function_call") {
		if (is_started_item) {
			return String();
		}
		const String name = _pull_string_field(event, "name");
		const String status = _pull_string_field(event, "status").to_lower();
		if (status == "in_progress" || status == "running") {
			return String();
		}
		_codex_append_unique_line(r_action_lines, "• tool " + (name.is_empty() ? String("call") : name));
		return String();
	}

	if (item_type == "function_call_output") {
		// Omit raw tool dumps from the user-facing action list (too noisy).
		return String();
	}

	if (type == "error") {
		const String msg = _pull_string_field(p_event, "message");
		if (!msg.is_empty() && !_codex_is_benign_notice(msg)) {
			_codex_append_unique_line(r_action_lines, "• error: " + msg.substr(0, 160));
		}
		return String();
	}

	return String();
}

} // namespace

String YeetAIDock::_detect_codex_binary() {
	if (!_codex_binary_cache.is_empty()) {
		return _codex_binary_cache;
	}

	const Array candidates = _codex_probe_candidates();
	for (int i = 0; i < candidates.size(); i++) {
		const String candidate = candidates[i];
		List<String> args;
		args.push_back("--version");
		String output;
		int exit_code = -1;
		const Error err = OS::get_singleton()->execute(candidate, args, &output, &exit_code, false, nullptr, false);
		// A clean exit means the named `codex` binary exists and ran. Do NOT also
		// require the version string to contain "codex": some installs print the
		// version to stderr (uncaptured) or as a bare number, which previously made
		// detection fail even though Codex was installed.
		if (err == OK && exit_code == 0) {
			_codex_binary_cache = candidate;
			if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
				WARN_PRINT(vformat("[YeetAI Codex] found binary: %s (version: %s)", candidate, output.strip_edges()));
			}
			return _codex_binary_cache;
		}
	}

#ifdef WINDOWS_ENABLED
	{
		List<String> args;
		args.push_back("/C");
		args.push_back("codex");
		args.push_back("--version");
		String output;
		int exit_code = -1;
		const Error err = OS::get_singleton()->execute("cmd.exe", args, &output, &exit_code, false, nullptr, false);
		// npm installs `codex` as a .cmd shim that CreateProcess can't launch
		// directly, so on Windows this cmd.exe route is usually the one that works.
		// Accept on a clean exit; cmd.exe returns non-zero (e.g. 9009) when the
		// command is not recognized, which correctly fails detection.
		if (err == OK && exit_code == 0) {
			_codex_binary_cache = "cmd.exe";
			if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
				WARN_PRINT(vformat("[YeetAI Codex] found binary via cmd.exe /C codex (version: %s)", output.strip_edges()));
			}
			return _codex_binary_cache;
		}
	}
#endif
	return String();
}

void YeetAIDock::_codex_thread_trampoline(void *p_user) {
	static_cast<YeetAIDock *>(p_user)->_codex_thread_body();
}

void YeetAIDock::_codex_thread_body() {
	const String binary = _detect_codex_binary();
	if (binary.is_empty()) {
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = TTR("Codex CLI binary was not found. Install it with `npm i -g @openai/codex` and run `codex login` once, then retry.");
		_stream_done_flag = true;
		_codex_active = false;
		return;
	}

	// Write prompt to a temp file and feed via stdin (`codex exec ... -`).
	// Putting multiline prompts (with embedded quotes) on argv breaks Windows
	// CreateProcess quoting and truncates the actual user task.
	const String prompt_file = _cli_write_temp_text("codex_prompt", _codex_active_prompt);
	const String last_msg_file = _cli_write_temp_text("codex_last", ""); // path reserved; codex overwrites
	if (prompt_file.is_empty()) {
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = TTR("Failed to write temporary Codex prompt file.");
		_stream_done_flag = true;
		_codex_active = false;
		return;
	}

	// Official headless: codex exec --json --dangerously-bypass-approvals-and-sandbox
	//   --skip-git-repo-check -C <project> -o <last> -   (prompt on stdin)
	// https://developers.openai.com/codex/noninteractive
	List<String> args;
	args.push_back("exec");
	args.push_back("--json");
	args.push_back("--color");
	args.push_back("never");
	// Bypass sandbox so non-interactive exec can edit project files on Windows
	// (Seatbelt/Landlock are not available there).
	args.push_back("--dangerously-bypass-approvals-and-sandbox");
	args.push_back("--skip-git-repo-check");
	// -C is also set via cmd cd, but keep it so Codex's workspace root is correct
	// even if the shell cwd differs.
	if (!_codex_workspace_path.is_empty()) {
		args.push_back("-C");
		args.push_back(_codex_workspace_path);
	}
	const String model = _get_editor_setting_string("yeet_ai/chat/model", "").strip_edges();
	if (!model.is_empty() && model != "codex-cli" && !model.begins_with("claude") && !model.begins_with("grok")) {
		args.push_back("-m");
		args.push_back(model);
	}
	if (!last_msg_file.is_empty()) {
		args.push_back("-o");
		args.push_back(last_msg_file);
	}
	args.push_back("-"); // read prompt from stdin (never argv — Windows quoting)

	if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
		WARN_PRINT(vformat("[YeetAI Codex] spawn: %s (project=%s) prompt_len=%d via stdin file", binary, _codex_workspace_path, _codex_active_prompt.length()));
	}

	String stdout_text;
	int exit_code = -1;
	const String program = _cli_program_token(binary, "codex");
	const Error err = _cli_run_in_project(program, args, _codex_workspace_path, prompt_file, stdout_text, exit_code);
	_cli_delete_temp(prompt_file);

	if (_codex_should_stop) {
		MutexLock lock(_stream_mutex);
		_stream_done_flag = true;
		_codex_active = false;
		return;
	}

	if (err != OK) {
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat(TTR("Failed to launch `codex` (OS error %d). Check Editor Settings → Crosshair → Codex CLI."), (int)err);
		_stream_done_flag = true;
		_codex_active = false;
		return;
	}

	// Parse JSONL events emitted by Codex exec --json.
	String latest_agent_message;
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

		// Token usage rides on the final turn.completed event; hand it to the
		// shared usage path (it understands Codex's input_tokens/output_tokens).
		if (String(event.get("type", "")) == "turn.completed") {
			const Variant usage_v = event.get("usage", Variant());
			if (usage_v.get_type() == Variant::DICTIONARY && !((Dictionary)usage_v).is_empty()) {
				MutexLock lock(_stream_mutex);
				_stream_usage = usage_v;
			}
		}

		(void)_codex_extract_assistant_text(event, action_lines, latest_agent_message);
	}

	// Prefer -o last-message file when present (final answer only).
	String last_msg_text;
	if (!last_msg_file.is_empty() && FileAccess::exists(last_msg_file)) {
		Ref<FileAccess> lf = FileAccess::open(last_msg_file, FileAccess::READ);
		if (lf.is_valid()) {
			last_msg_text = lf->get_as_utf8_string().strip_edges();
		}
	}
	_cli_delete_temp(last_msg_file);

	// Compose the final text Crosshair will surface as the assistant reply.
	String final_text = last_msg_text.is_empty() ? latest_agent_message : last_msg_text;
	if (!action_lines.is_empty()) {
		// Prefer file edits first in display order for readability.
		String edits;
		String other;
		for (int i = 0; i < action_lines.size(); i++) {
			const String &line = action_lines[i];
			if (line.findn("edited ") >= 0) {
				if (!edits.is_empty()) {
					edits += "\n";
				}
				edits += line;
			} else {
				if (!other.is_empty()) {
					other += "\n";
				}
				other += line;
			}
		}
		String action_summary = edits;
		if (!other.is_empty()) {
			if (!action_summary.is_empty()) {
				action_summary += "\n";
			}
			action_summary += other;
		}
		if (!final_text.is_empty()) {
			final_text += "\n\n";
		}
		final_text += "_Actions:_\n" + action_summary;
	}

	if (final_text.strip_edges().is_empty()) {
		if (exit_code != 0) {
			final_text = vformat(TTR("Codex exited with code %d and produced no output. Make sure you’ve run `codex login` and that the prompt is valid."), exit_code);
		} else {
			final_text = TTR("Codex returned an empty response. The agent may have had nothing to do for this prompt.");
		}
		const String snippet = stdout_text.strip_edges();
		if (!snippet.is_empty()) {
			final_text += "\n\n_Output:_\n" + snippet.substr(0, 1500);
		}
	}

	{
		MutexLock lock(_stream_mutex);
		_pending_chunks.push_back(final_text);
		_codex_pending_rescan = true;
		_stream_done_flag = true;
	}
	_codex_active = false;
}

Error YeetAIDock::_run_codex_turn(const String &p_task_prompt, const String &p_system_prompt) {
	if (_codex_active) {
		_append_message("assistant", TTR("Codex is already running. Wait for the current turn or click Stop."));
		return ERR_ALREADY_IN_USE;
	}

	const String binary = _detect_codex_binary();
	if (binary.is_empty()) {
		_append_message("assistant", TTR("Codex CLI was not found on PATH. Install it with `npm i -g @openai/codex`, then run `codex login` once. Set the Codex CLI provider in Editor Settings → Crosshair."));
		_set_waiting(false, TTR("Error"));
		return ERR_CANT_OPEN;
	}

	// `codex exec` takes a single prompt argument and has no clean per-call
	// system-prompt flag, so the operating instructions are prepended to the task.
	String combined = p_system_prompt.strip_edges();
	if (!combined.is_empty()) {
		combined += "\n\n";
	}
	combined += p_task_prompt.strip_edges();
	_codex_active_prompt = combined.strip_edges();
	if (_codex_active_prompt.is_empty()) {
		_append_message("assistant", TTR("Empty prompt; nothing to send to Codex."));
		_set_waiting(false, TTR("Ready"));
		return ERR_INVALID_PARAMETER;
	}

	_codex_workspace_path = _cli_project_path();

	// Reset the shared streaming state so the existing drain loop controls the turn.
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
		stream_label->set_text(TTR("Codex is working... (it edits files directly in your project)"));
		stream_label->set_visible(true);
	}

	_codex_active = true;
	_codex_should_stop = false;
	_codex_pending_rescan = false;

	_set_waiting(true, TTR("Codex"));

	if (_codex_thread.is_started()) {
		_codex_thread.wait_to_finish();
	}
	_codex_thread.start(&YeetAIDock::_codex_thread_trampoline, this);
	return OK;
}

void YeetAIDock::_codex_finalize_and_rescan() {
	if (!_codex_pending_rescan) {
		return;
	}
	_codex_pending_rescan = false;
	if (EditorInterface::get_singleton() && EditorInterface::get_singleton()->get_resource_filesystem()) {
		EditorInterface::get_singleton()->get_resource_filesystem()->scan();
		if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
			WARN_PRINT("[YeetAI Codex] triggered EditorFileSystem scan");
		}
	}
}

void yeet_ai_register_codex_provider_stub() {
	// Anchor symbol to keep SCsub's wildcard *.cpp glob including this translation unit.
	// The actual method bindings live on YeetAIDock and the AppServer probe is lazy.
}
