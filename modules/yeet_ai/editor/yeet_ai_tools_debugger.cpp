/**************************************************************************/
/*  yeet_ai_tools_debugger.cpp                                            */
/**************************************************************************/
/*  Phase 2: Deep debugger tools.                                         */
/*  Provides stack inspection, variable queries, profiler control,        */
/*  condition awaiting, and session management.                           */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/debugger/debugger_marshalls.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/editor_interface.h"
#include "main/main.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/tree.h"
#include "servers/display/display_server.h"

// ═══════════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════════

static ScriptEditorDebugger *_dbg_get_active() {
	EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton();
	if (!edn) return nullptr;
	ScriptEditorDebugger *dbg = edn->get_default_debugger();
	if (!dbg || !dbg->is_session_active()) return nullptr;
	return dbg;
}

static Dictionary _dbg_error(const char *msg) {
	Dictionary r;
	r["error"] = msg;
	return r;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_get_sessions — List all debugger sessions and their state
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_get_sessions(const Dictionary &p_args) const {
	(void)p_args;
	EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton();
	if (!edn) {
		return _dbg_error("EditorDebuggerNode unavailable.");
	}

	Array sessions;
	for (int i = 0; ; i++) {
		ScriptEditorDebugger *dbg = edn->get_debugger(i);
		if (!dbg) break;

		Dictionary s;
		s["session_id"] = i;
		s["active"] = dbg->is_session_active();
		s["breaked"] = dbg->is_breaked();
		s["debuggable"] = dbg->is_debuggable();
		s["remote_pid"] = dbg->get_remote_pid();
		s["error_count"] = dbg->get_error_count();
		s["warning_count"] = dbg->get_warning_count();
		sessions.push_back(s);
	}

	Dictionary result;
	result["sessions"] = sessions;
	result["count"] = sessions.size();
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_get_state — Detailed debugger state (enhanced get_runtime_debugger_state)
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_get_state(const Dictionary &p_args) const {
	(void)p_args;
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session. Run the game first.");
	}

	Dictionary result;
	result["session_active"] = dbg->is_session_active();
	result["breaked"] = dbg->is_breaked();
	result["debuggable"] = dbg->is_debuggable();
	result["remote_pid"] = dbg->get_remote_pid();
	result["error_count"] = dbg->get_error_count();
	result["warning_count"] = dbg->get_warning_count();

	// Stack info (only meaningful when breaked)
	if (dbg->is_breaked()) {
		result["stack_file"] = dbg->get_stack_script_file();
		result["stack_line"] = dbg->get_stack_script_line();
		result["stack_frame"] = dbg->get_stack_script_frame();
	}

	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_get_stack — Get stack frames when debugger is breaked
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_get_stack(const Dictionary &p_args) const {
	(void)p_args;
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session.");
	}

	if (!dbg->is_breaked()) {
		return _dbg_error("Debugger is not breaked. Call runtime_break or set a breakpoint first.");
	}

	// Request a fresh stack dump for the current frame.
	// The response populates the stack_dump Tree widget.
	// We'll give it a moment to process.
	dbg->send_message("get_stack_dump", Array());

	for (int i = 0; i < 15; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	// Read the current stack info (top frame).
	Dictionary result;
	result["breaked"] = true;
	result["current_file"] = dbg->get_stack_script_file();
	result["current_line"] = dbg->get_stack_script_line();
	result["current_frame"] = dbg->get_stack_script_frame();
	result["note"] = "Stack dump requested. Full frames visible in Debugger panel. Use current_file:current_line to read the code at the break point.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_get_variables — Request variable inspection for a stack frame
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_get_variables(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session.");
	}

	if (!dbg->is_breaked()) {
		return _dbg_error("Debugger is not breaked. Break first to inspect variables.");
	}

	const int frame = int(p_args.get("frame", 0));
	dbg->request_stack_dump(frame);

	for (int i = 0; i < 20; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	Dictionary result;
	result["ok"] = true;
	result["frame"] = frame;
	result["note"] = "Variables requested for frame. Inspect them in the Debugger > Variables panel, or use runtime_eval/runtime_get_node_property to query specific values.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_step_out — Step out of the current function
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_step_out(const Dictionary &p_args) const {
	(void)p_args;
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session.");
	}

	if (!dbg->is_breaked()) {
		return _dbg_error("Cannot step out — debugger is not breaked.");
	}

	dbg->debug_out();

	Dictionary result;
	result["ok"] = true;
	result["action"] = "step_out";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_toggle_profiler — Enable/disable a profiler
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_toggle_profiler(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session.");
	}

	const String profiler = p_args.get("profiler", "");
	const bool enable = bool(p_args.get("enable", true));

	if (profiler.is_empty()) {
		Dictionary r;
		r["error"] = "profiler name is required. Valid: servers, visual, performance, scripts, multiplayer:bandwidth, multiplayer:rpc, multiplayer:replication";
		return r;
	}

	dbg->toggle_profiler(profiler, enable, Array());

	Dictionary result;
	result["ok"] = true;
	result["profiler"] = profiler;
	result["enabled"] = enable;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_evaluate — Evaluate an expression in debugger context (when breaked)
// Unlike runtime_eval which is fire-and-forget, this is meant for break-state inspection.
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_evaluate(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session.");
	}

	const String expression = p_args.get("expression", "");
	const int frame = int(p_args.get("frame", 0));

	if (expression.is_empty()) {
		return _make_error("expression is required.");
	}

	dbg->request_remote_evaluate(expression, frame);

	// Give it time to process.
	for (int i = 0; i < 15; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	Dictionary result;
	result["ok"] = true;
	result["expression"] = expression;
	result["frame"] = frame;
	result["note"] = "Expression evaluated in debugger context. Result appears in the Debugger Inspector or console output.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_await_condition — Poll an expression until truthy or timeout
// Useful for waiting for game state to reach a condition.
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_await_condition(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session. Run the game first.");
	}

	const String expression = p_args.get("expression", "");
	const int timeout_ms = int(p_args.get("timeout_ms", 5000));
	const int poll_interval_ms = int(p_args.get("poll_interval_ms", 200));

	if (expression.is_empty()) {
		return _make_error("expression is required (e.g. 'get_node(\"/root/Main/Player\").health <= 0').");
	}

	// We use print-based polling: evaluate "print('AWAIT_CHECK:', <expr>)"
	// and then we'd need to read console output. Since we can't easily capture
	// the return value synchronously, we'll use a simple loop that evaluates
	// the expression repeatedly and gives the game time to process.
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + uint64_t(timeout_ms);
	int polls = 0;

	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		// Evaluate the condition
		String check_expr = vformat("print('AWAIT_RESULT:', %s)", expression);
		dbg->request_remote_evaluate(check_expr, 0);

		// Wait for poll interval
		uint64_t poll_end = OS::get_singleton()->get_ticks_msec() + uint64_t(poll_interval_ms);
		while (OS::get_singleton()->get_ticks_msec() < poll_end) {
			DisplayServer::get_singleton()->process_events();
			Main::iteration();
		}
		polls++;
	}

	Dictionary result;
	result["ok"] = true;
	result["expression"] = expression;
	result["polls"] = polls;
	result["timeout_ms"] = timeout_ms;
	result["note"] = "Condition polled for " + String::num_int64(timeout_ms) + "ms (" + String::num_int64(polls) + " polls). Check get_console_output for AWAIT_RESULT lines to see if condition became true.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_assert_condition — Assert an expression is truthy (check once)
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_assert_condition(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session. Run the game first.");
	}

	const String expression = p_args.get("expression", "");
	const String message = p_args.get("message", "Assertion");

	if (expression.is_empty()) {
		return _make_error("expression is required.");
	}

	// Evaluate and print assertion result.
	String expr = vformat(
		"var _val = %s; "
		"if _val: print('ASSERT_PASS: %s — ', _val) "
		"else: push_error('ASSERT_FAIL: %s — got: ' + str(_val))",
		expression, message, message);
	dbg->request_remote_evaluate(expr, 0);

	for (int i = 0; i < 15; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	Dictionary result;
	result["ok"] = true;
	result["expression"] = expression;
	result["message"] = message;
	result["note"] = "Assertion evaluated. Check get_console_output for ASSERT_PASS/ASSERT_FAIL, and get_runtime_debugger_state for any push_error increase.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_get_errors — Get error and warning counts + request error details
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_get_errors(const Dictionary &p_args) const {
	(void)p_args;
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session.");
	}

	Dictionary result;
	result["error_count"] = dbg->get_error_count();
	result["warning_count"] = dbg->get_warning_count();
	result["session_active"] = dbg->is_session_active();
	result["breaked"] = dbg->is_breaked();

	if (dbg->is_breaked()) {
		result["break_file"] = dbg->get_stack_script_file();
		result["break_line"] = dbg->get_stack_script_line();
	}

	if (dbg->get_error_count() > 0 || dbg->get_warning_count() > 0) {
		result["tip"] = "Errors/warnings are visible in the Debugger > Errors panel. Use get_console_output for print-level output, or read_project_file on the error's script file to inspect the code.";
	}
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_send_custom_message — Send a custom EngineDebugger message
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_send_custom_message(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session.");
	}

	const String message = p_args.get("message", "");
	const Array data = p_args.get("data", Array());

	if (message.is_empty()) {
		return _make_error("message is required (e.g. 'mcp:ping').");
	}

	dbg->send_message(message, data);

	Dictionary result;
	result["ok"] = true;
	result["message"] = message;
	result["data_size"] = data.size();
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_get_performance_snapshot — Detailed performance snapshot
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_get_performance_snapshot(const Dictionary &p_args) const {
	(void)p_args;
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session.");
	}

	// Enable performance profiler briefly to get data, then read.
	dbg->toggle_profiler("performance", true, Array());

	for (int i = 0; i < 10; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	// Request performance data via eval.
	String expr =
		"var p = Performance; print('PERF_SNAPSHOT: {"
		"\"fps\":', p.get_monitor(p.TIME_FPS), ',',"
		"'\"frame_time\":', p.get_monitor(p.TIME_PROCESS), ',',"
		"'\"physics_time\":', p.get_monitor(p.TIME_PHYSICS_PROCESS), ',',"
		"'\"navigation_time\":', p.get_monitor(p.TIME_NAVIGATION_PROCESS), ',',"
		"'\"objects\":', p.get_monitor(p.OBJECT_COUNT), ',',"
		"'\"resources\":', p.get_monitor(p.OBJECT_RESOURCE_COUNT), ',',"
		"'\"nodes\":', p.get_monitor(p.OBJECT_NODE_COUNT), ',',"
		"'\"orphan_nodes\":', p.get_monitor(p.OBJECT_ORPHAN_NODE_COUNT), ',',"
		"'\"memory_static\":', p.get_monitor(p.MEMORY_STATIC), ',',"
		"'\"memory_static_max\":', p.get_monitor(p.MEMORY_STATIC_MAX), ',',"
		"'\"memory_message_buffer\":', p.get_monitor(p.MEMORY_MESSAGE_BUFFER_MAX), ',',"
		"'\"render_objects\":', p.get_monitor(p.RENDER_TOTAL_OBJECTS_IN_FRAME), ',',"
		"'\"render_primitives\":', p.get_monitor(p.RENDER_TOTAL_PRIMITIVES_IN_FRAME), ',',"
		"'\"render_draw_calls\":', p.get_monitor(p.RENDER_TOTAL_DRAW_CALLS_IN_FRAME), ',',"
		"'\"physics_2d_active_objects\":', p.get_monitor(p.PHYSICS_2D_ACTIVE_OBJECTS), ',',"
		"'\"physics_3d_active_objects\":', p.get_monitor(p.PHYSICS_3D_ACTIVE_OBJECTS),"
		"'}')";
	dbg->request_remote_evaluate(expr, 0);

	for (int i = 0; i < 10; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	Dictionary result;
	result["ok"] = true;
	result["note"] = "Performance snapshot printed to game console as PERF_SNAPSHOT JSON. Use get_console_output to read.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_get_memory_info — Get memory usage details from running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_get_memory_info(const Dictionary &p_args) const {
	(void)p_args;
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session.");
	}

	String expr =
		"print('MEMORY_INFO: {"
		"\"static\":', Performance.get_monitor(Performance.MEMORY_STATIC), ',',"
		"'\"static_max\":', Performance.get_monitor(Performance.MEMORY_STATIC_MAX), ',',"
		"'\"msg_buffer_max\":', Performance.get_monitor(Performance.MEMORY_MESSAGE_BUFFER_MAX), ',',"
		"'\"objects\":', Performance.get_monitor(Performance.OBJECT_COUNT), ',',"
		"'\"resources\":', Performance.get_monitor(Performance.OBJECT_RESOURCE_COUNT), ',',"
		"'\"nodes\":', Performance.get_monitor(Performance.OBJECT_NODE_COUNT), ',',"
		"'\"orphan_nodes\":', Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT),"
		"'}')";
	dbg->request_remote_evaluate(expr, 0);

	for (int i = 0; i < 10; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	Dictionary result;
	result["ok"] = true;
	result["note"] = "Memory info printed as MEMORY_INFO JSON to game console. Use get_console_output to read.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// debugger_reload_scripts — Reload all scripts in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_debugger_reload_scripts(const Dictionary &p_args) const {
	(void)p_args;
	ScriptEditorDebugger *dbg = _dbg_get_active();
	if (!dbg) {
		return _dbg_error("No active debug session.");
	}

	dbg->reload_all_scripts();

	Dictionary result;
	result["ok"] = true;
	result["note"] = "All scripts reloaded in the running game. Check get_console_output for any reload errors.";
	return result;
}
