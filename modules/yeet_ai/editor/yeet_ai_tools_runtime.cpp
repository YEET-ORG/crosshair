/**************************************************************************/
/*  yeet_ai_tools_runtime.cpp                                             */
/**************************************************************************/
/*  Phase 1: Runtime game manipulation tools.                             */
/*  These tools communicate with the running game via the editor          */
/*  debugger's bidirectional message protocol.                            */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/json.h"
#include "core/os/os.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/editor_interface.h"
#include "main/main.h"
#include "scene/debugger/scene_debugger_object.h"
#include "servers/display/display_server.h"

// ═══════════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════════

static ScriptEditorDebugger *_get_active_debugger() {
	EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton();
	if (edn == nullptr) {
		return nullptr;
	}
	ScriptEditorDebugger *dbg = edn->get_default_debugger();
	if (dbg == nullptr || !dbg->is_session_active()) {
		return nullptr;
	}
	return dbg;
}

static Dictionary _runtime_error(const char *msg) {
	Dictionary r;
	r["error"] = msg;
	r["requires_running_game"] = true;
	return r;
}

// Busy-wait for an async response with timeout (same pattern as capture_game_viewport).
static bool _wait_for_flag(const bool &flag, int timeout_ms = 3000) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + uint64_t(timeout_ms);
	while (!flag && OS::get_singleton()->get_ticks_msec() < deadline) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}
	return flag;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_get_scene_tree — Get the live scene tree from the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_get_scene_tree(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first with play_current_scene or play_main_scene.");
	}

	// Remember the serial before requesting to detect when a fresh tree arrives.
	const uint64_t old_serial = dbg->get_remote_scene_tree_serial();
	dbg->request_remote_tree();

	// Wait for the tree to update.
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 5000;
	while (dbg->get_remote_scene_tree_serial() == old_serial && OS::get_singleton()->get_ticks_msec() < deadline) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	if (dbg->get_remote_scene_tree_serial() == old_serial) {
		return _runtime_error("Timed out waiting for remote scene tree.");
	}

	const SceneDebuggerTree *tree = dbg->get_remote_tree();
	if (tree == nullptr) {
		return _runtime_error("Remote tree is null after request.");
	}

	// Serialize the tree to a Dictionary.
	Dictionary result;
	Array nodes_arr;
	for (const SceneDebuggerTree::RemoteNode &rn : tree->nodes) {
		Dictionary nd;
		nd["name"] = rn.name;
		nd["type_name"] = rn.type_name;
		nd["id"] = (int64_t)rn.id;
		nd["scene_file_path"] = rn.scene_file_path;
		nd["child_count"] = rn.child_count;
		nodes_arr.push_back(nd);
	}
	result["nodes"] = nodes_arr;
	result["count"] = tree->nodes.size();
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_eval — Evaluate a GDScript expression in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_eval(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String expression = p_args.get("expression", "");
	if (expression.is_empty()) {
		Dictionary r;
		r["error"] = "expression is required.";
		return r;
	}

	const int frame = int(p_args.get("frame", 0));
	dbg->request_remote_evaluate(expression, frame);

	// The evaluation result comes back asynchronously via _msg_evaluation_return.
	// We use a short wait — if the game processes it quickly, we get it back.
	// Unfortunately, the evaluation return doesn't have a simple signal we can wait on
	// in all Godot versions, so we'll give it a brief iteration window.
	for (int i = 0; i < 30; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	Dictionary result;
	result["ok"] = true;
	result["expression"] = expression;
	result["note"] = "Expression sent to running game. Results appear in the debugger output/inspector. Use get_runtime_debugger_state or get_console_output to read them.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_create_node — Create a node in the running game via live debug
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_create_node(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String parent_path = p_args.get("parent_path", "/root");
	const String node_type = p_args.get("node_type", "");
	const String node_name = p_args.get("node_name", "");

	if (node_type.is_empty()) {
		Dictionary r;
		r["error"] = "node_type is required.";
		return r;
	}
	if (node_name.is_empty()) {
		Dictionary r;
		r["error"] = "node_name is required.";
		return r;
	}

	dbg->live_debug_create_node(NodePath(parent_path), node_type, node_name);

	Dictionary result;
	result["ok"] = true;
	result["parent_path"] = parent_path;
	result["node_type"] = node_type;
	result["node_name"] = node_name;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_remove_node — Remove a node from the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_remove_node(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty()) {
		Dictionary r;
		r["error"] = "node_path is required.";
		return r;
	}

	dbg->live_debug_remove_node(NodePath(node_path));

	Dictionary result;
	result["ok"] = true;
	result["removed"] = node_path;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_instantiate_scene — Instantiate a PackedScene in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_instantiate_scene(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String parent_path = p_args.get("parent_path", "/root");
	const String scene_path = p_args.get("scene_path", "");
	const String node_name = p_args.get("node_name", "");

	if (scene_path.is_empty()) {
		Dictionary r;
		r["error"] = "scene_path is required (e.g. res://scenes/enemy.tscn).";
		return r;
	}

	dbg->live_debug_instantiate_node(NodePath(parent_path), scene_path, node_name);

	Dictionary result;
	result["ok"] = true;
	result["parent_path"] = parent_path;
	result["scene_path"] = scene_path;
	result["node_name"] = node_name;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_duplicate_node — Duplicate a node in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_duplicate_node(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String node_path = p_args.get("node_path", "");
	const String new_name = p_args.get("new_name", "");
	if (node_path.is_empty()) {
		Dictionary r;
		r["error"] = "node_path is required.";
		return r;
	}

	dbg->live_debug_duplicate_node(NodePath(node_path), new_name);

	Dictionary result;
	result["ok"] = true;
	result["duplicated"] = node_path;
	result["new_name"] = new_name;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_reparent_node — Reparent a node in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_reparent_node(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String node_path = p_args.get("node_path", "");
	const String new_parent = p_args.get("new_parent_path", "");
	const String new_name = p_args.get("new_name", "");
	const int at_pos = int(p_args.get("at_position", -1));

	if (node_path.is_empty() || new_parent.is_empty()) {
		Dictionary r;
		r["error"] = "node_path and new_parent_path are required.";
		return r;
	}

	dbg->live_debug_reparent_node(NodePath(node_path), NodePath(new_parent), new_name, at_pos);

	Dictionary result;
	result["ok"] = true;
	result["node_path"] = node_path;
	result["new_parent_path"] = new_parent;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_set_property — Set a property on a remote object
// Uses send_message with the scene:set_object_property protocol.
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_set_property(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const int64_t object_id = int64_t(p_args.get("object_id", 0));
	const String property = p_args.get("property", "");
	const Variant value = p_args.get("value", Variant());

	if (object_id == 0) {
		Dictionary r;
		r["error"] = "object_id is required. Use runtime_get_scene_tree to find object IDs.";
		return r;
	}
	if (property.is_empty()) {
		Dictionary r;
		r["error"] = "property name is required.";
		return r;
	}

	dbg->update_remote_object(ObjectID(uint64_t(object_id)), property, value);

	Dictionary result;
	result["ok"] = true;
	result["object_id"] = object_id;
	result["property"] = property;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_send_message — Send a generic debugger message to the running game
// This is the most flexible tool — can invoke any debugger protocol command.
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_send_message(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String message = p_args.get("message", "");
	const Array data = p_args.get("data", Array());

	if (message.is_empty()) {
		Dictionary r;
		r["error"] = "message is required (e.g. 'scene:request_scene_tree').";
		return r;
	}

	dbg->send_message(message, data);

	Dictionary result;
	result["ok"] = true;
	result["message"] = message;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_pause — Pause or unpause the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_pause(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const bool pause = bool(p_args.get("pause", true));

	if (pause) {
		dbg->send_message("scene:request_pause", Array());
	} else {
		dbg->send_message("scene:request_unpause", Array());
	}

	Dictionary result;
	result["ok"] = true;
	result["paused"] = pause;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_inspect_object — Request inspection of a remote object by ID
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_inspect_object(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const int64_t object_id = int64_t(p_args.get("object_id", 0));
	if (object_id == 0) {
		Dictionary r;
		r["error"] = "object_id is required. Use runtime_get_scene_tree to find IDs.";
		return r;
	}

	TypedArray<uint64_t> ids;
	ids.push_back(uint64_t(object_id));
	dbg->request_remote_objects(ids, false);

	// Give the response a few frames to arrive.
	for (int i = 0; i < 20; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	Dictionary result;
	result["ok"] = true;
	result["object_id"] = object_id;
	result["note"] = "Object inspection requested. Properties will appear in the Remote Inspector panel. Use runtime_get_scene_tree to find specific node properties.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_break — Request the debugger to break execution
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_break(const Dictionary &p_args) const {
	(void)p_args;
	EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton();
	if (edn == nullptr) {
		return _runtime_error("EditorDebuggerNode unavailable.");
	}

	edn->debug_break();

	Dictionary result;
	result["ok"] = true;
	result["action"] = "break";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_step — Step execution (into/over/out/continue)
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_step(const Dictionary &p_args) const {
	EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton();
	if (edn == nullptr) {
		return _runtime_error("EditorDebuggerNode unavailable.");
	}

	const String action = p_args.get("action", "continue");

	if (action == "continue") {
		edn->debug_continue();
	} else if (action == "step_over" || action == "next") {
		edn->debug_next();
	} else if (action == "step_into" || action == "step") {
		edn->debug_step();
	} else if (action == "break") {
		edn->debug_break();
	} else {
		Dictionary r;
		r["error"] = "Invalid action. Use: continue, step_over, step_into, break.";
		return r;
	}

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_time_scale — Get/set Engine.time_scale in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_time_scale(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	if (p_args.has("value")) {
		const float scale = float(p_args["value"]);
		// Send via eval since there's no direct protocol for this.
		String expr = vformat("Engine.time_scale = %f", scale);
		dbg->request_remote_evaluate(expr, 0);
		for (int i = 0; i < 10; i++) {
			DisplayServer::get_singleton()->process_events();
			Main::iteration();
		}
		Dictionary result;
		result["ok"] = true;
		result["time_scale"] = scale;
		return result;
	}

	// Get current time_scale.
	dbg->request_remote_evaluate("Engine.time_scale", 0);
	for (int i = 0; i < 10; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	Dictionary result;
	result["ok"] = true;
	result["note"] = "time_scale value requested. Check debugger output.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// PHASE 1 REMAINING: Signals, Input, Camera, Animation, Groups, State, Window
// These use request_remote_evaluate() to execute GDScript in the running game.
// ═══════════════════════════════════════════════════════════════════════════════

// Helper: evaluate a GDScript expression in the running game and pump frames.
static void _eval_in_game(ScriptEditorDebugger *dbg, const String &expr, int wait_frames = 10) {
	dbg->request_remote_evaluate(expr, 0);
	for (int i = 0; i < wait_frames; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_connect_signal — Connect a signal between nodes in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_connect_signal(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String source = p_args.get("source_path", "");
	const String signal_name = p_args.get("signal_name", "");
	const String target = p_args.get("target_path", "");
	const String method = p_args.get("method_name", "");

	if (source.is_empty() || signal_name.is_empty() || target.is_empty() || method.is_empty()) {
		return _make_error("source_path, signal_name, target_path, and method_name are all required.");
	}

	String expr = vformat("get_node(\"%s\").connect(\"%s\", Callable(get_node(\"%s\"), \"%s\"))", source, signal_name, target, method);
	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["source"] = source;
	result["signal"] = signal_name;
	result["target"] = target;
	result["method"] = method;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_disconnect_signal
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_disconnect_signal(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String source = p_args.get("source_path", "");
	const String signal_name = p_args.get("signal_name", "");
	const String target = p_args.get("target_path", "");
	const String method = p_args.get("method_name", "");

	if (source.is_empty() || signal_name.is_empty() || target.is_empty() || method.is_empty()) {
		return _make_error("source_path, signal_name, target_path, and method_name are all required.");
	}

	String expr = vformat("get_node(\"%s\").disconnect(\"%s\", Callable(get_node(\"%s\"), \"%s\"))", source, signal_name, target, method);
	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["disconnected"] = true;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_emit_signal — Emit a signal on a node in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_emit_signal(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String node_path = p_args.get("node_path", "");
	const String signal_name = p_args.get("signal_name", "");

	if (node_path.is_empty() || signal_name.is_empty()) {
		return _make_error("node_path and signal_name are required.");
	}

	String expr = vformat("get_node(\"%s\").emit_signal(\"%s\")", node_path, signal_name);
	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["node_path"] = node_path;
	result["signal"] = signal_name;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_play_animation — Play/stop animation on an AnimationPlayer
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_play_animation(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String node_path = p_args.get("node_path", "");
	const String animation = p_args.get("animation_name", "");
	const String action = p_args.get("action", "play");

	if (node_path.is_empty()) {
		return _make_error("node_path to AnimationPlayer is required.");
	}

	String expr;
	if (action == "play") {
		if (animation.is_empty()) {
			expr = vformat("get_node(\"%s\").play()", node_path);
		} else {
			expr = vformat("get_node(\"%s\").play(\"%s\")", node_path, animation);
		}
	} else if (action == "stop") {
		expr = vformat("get_node(\"%s\").stop()", node_path);
	} else if (action == "pause") {
		expr = vformat("get_node(\"%s\").pause()", node_path);
	} else {
		return _make_error("action must be: play, stop, or pause.");
	}

	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	result["animation"] = animation;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_tween_property — Tween a property with easing in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_tween_property(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String node_path = p_args.get("node_path", "");
	const String property = p_args.get("property", "");
	const String final_value = p_args.get("final_value", "");
	const float duration = float(p_args.get("duration", 1.0));

	if (node_path.is_empty() || property.is_empty() || final_value.is_empty()) {
		return _make_error("node_path, property, and final_value are required.");
	}

	// Build a tween expression. final_value should be a GDScript literal (e.g. Vector2(100,200), 0.5, Color.RED)
	String expr = vformat(
		"var t = get_node(\"%s\").create_tween(); t.tween_property(get_node(\"%s\"), \"%s\", %s, %f)",
		node_path, node_path, property, final_value, duration);
	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["node_path"] = node_path;
	result["property"] = property;
	result["final_value"] = final_value;
	result["duration"] = duration;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_key_press — Simulate a key press in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_key_press(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String key = p_args.get("key", "");
	const String action = p_args.get("action", "");

	if (key.is_empty() && action.is_empty()) {
		return _make_error("Either 'key' (e.g. 'KEY_SPACE') or 'action' (e.g. 'ui_jump') is required.");
	}

	String expr;
	if (!action.is_empty()) {
		// Use Input.action_press/release for input actions
		expr = vformat("Input.action_press(\"%s\"); await get_tree().create_timer(0.1).timeout; Input.action_release(\"%s\")", action, action);
	} else {
		// Use InputEventKey for raw keys
		expr = vformat(
			"var ev = InputEventKey.new(); ev.keycode = %s; ev.pressed = true; Input.parse_input_event(ev); "
			"await get_tree().create_timer(0.1).timeout; "
			"ev.pressed = false; Input.parse_input_event(ev)", key);
	}

	_eval_in_game(dbg, expr, 15);

	Dictionary result;
	result["ok"] = true;
	if (!action.is_empty()) {
		result["action"] = action;
	} else {
		result["key"] = key;
	}
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_key_hold — Hold a key/action down (no auto-release)
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_key_hold(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String action = p_args.get("action", "");
	if (action.is_empty()) {
		return _make_error("action is required (e.g. 'ui_right', 'move_forward').");
	}

	String expr = vformat("Input.action_press(\"%s\")", action);
	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["held"] = action;
	result["note"] = "Action held. Call runtime_key_release to release.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_key_release — Release a held key/action
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_key_release(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String action = p_args.get("action", "");
	if (action.is_empty()) {
		return _make_error("action is required.");
	}

	String expr = vformat("Input.action_release(\"%s\")", action);
	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["released"] = action;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_mouse_click — Click at a position in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_mouse_click(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const float x = float(p_args.get("x", 0));
	const float y = float(p_args.get("y", 0));
	const int button = int(p_args.get("button", 1)); // 1=left, 2=right, 3=middle

	String expr = vformat(
		"var ev = InputEventMouseButton.new(); ev.position = Vector2(%f, %f); ev.button_index = %d; "
		"ev.pressed = true; Input.parse_input_event(ev); "
		"await get_tree().create_timer(0.05).timeout; "
		"ev.pressed = false; Input.parse_input_event(ev)", x, y, button);

	_eval_in_game(dbg, expr, 15);

	Dictionary result;
	result["ok"] = true;
	result["x"] = x;
	result["y"] = y;
	result["button"] = button;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_mouse_move — Move the mouse to a position
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_mouse_move(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const float x = float(p_args.get("x", 0));
	const float y = float(p_args.get("y", 0));

	String expr = vformat(
		"var ev = InputEventMouseMotion.new(); ev.position = Vector2(%f, %f); "
		"ev.relative = Vector2(0, 0); Input.parse_input_event(ev)", x, y);

	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["x"] = x;
	result["y"] = y;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_get_camera — Get the active camera transform
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_get_camera(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String mode = p_args.get("mode", "3d");

	if (mode == "2d") {
		_eval_in_game(dbg, "var c = get_viewport().get_camera_2d(); print('CAM2D pos=', c.global_position, ' zoom=', c.zoom)");
	} else {
		_eval_in_game(dbg, "var c = get_viewport().get_camera_3d(); print('CAM3D pos=', c.global_position, ' rot=', c.global_rotation_degrees)");
	}

	Dictionary result;
	result["ok"] = true;
	result["mode"] = mode;
	result["note"] = "Camera info printed to game console. Use get_console_output to read it.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_set_camera — Set the active camera position/rotation
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_set_camera(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String mode = p_args.get("mode", "3d");

	if (mode == "2d") {
		if (p_args.has("x") && p_args.has("y")) {
			String expr = vformat("get_viewport().get_camera_2d().global_position = Vector2(%f, %f)",
				float(p_args["x"]), float(p_args["y"]));
			_eval_in_game(dbg, expr);
		}
		if (p_args.has("zoom")) {
			String expr = vformat("get_viewport().get_camera_2d().zoom = Vector2(%f, %f)",
				float(p_args["zoom"]), float(p_args["zoom"]));
			_eval_in_game(dbg, expr);
		}
	} else {
		if (p_args.has("x") && p_args.has("y") && p_args.has("z")) {
			String expr = vformat("get_viewport().get_camera_3d().global_position = Vector3(%f, %f, %f)",
				float(p_args["x"]), float(p_args["y"]), float(p_args["z"]));
			_eval_in_game(dbg, expr);
		}
		if (p_args.has("rotation_x") && p_args.has("rotation_y") && p_args.has("rotation_z")) {
			String expr = vformat("get_viewport().get_camera_3d().rotation_degrees = Vector3(%f, %f, %f)",
				float(p_args["rotation_x"]), float(p_args["rotation_y"]), float(p_args["rotation_z"]));
			_eval_in_game(dbg, expr);
		}
	}

	Dictionary result;
	result["ok"] = true;
	result["mode"] = mode;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_change_scene — Change the current scene in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_change_scene(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String scene_path = p_args.get("scene_path", "");
	if (scene_path.is_empty()) {
		return _make_error("scene_path is required (e.g. res://scenes/level2.tscn).");
	}

	String expr = vformat("get_tree().change_scene_to_file(\"%s\")", scene_path);
	_eval_in_game(dbg, expr, 20);

	Dictionary result;
	result["ok"] = true;
	result["scene_path"] = scene_path;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_get_nodes_in_group — Query nodes by group name
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_get_nodes_in_group(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String group = p_args.get("group", "");
	if (group.is_empty()) {
		return _make_error("group name is required.");
	}

	String expr = vformat(
		"var nodes = get_tree().get_nodes_in_group(\"%s\"); "
		"var names = []; for n in nodes: names.append(str(n.get_path())); "
		"print('GROUP[%s]:', names)", group, group);
	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["group"] = group;
	result["note"] = "Group nodes printed to game console. Use get_console_output to read.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_manage_group — Add/remove a node from a group at runtime
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_manage_group(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String node_path = p_args.get("node_path", "");
	const String group = p_args.get("group", "");
	const String action = p_args.get("action", "add");

	if (node_path.is_empty() || group.is_empty()) {
		return _make_error("node_path and group are required.");
	}

	String expr;
	if (action == "add") {
		expr = vformat("get_node(\"%s\").add_to_group(\"%s\")", node_path, group);
	} else {
		expr = vformat("get_node(\"%s\").remove_from_group(\"%s\")", node_path, group);
	}
	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	result["node_path"] = node_path;
	result["group"] = group;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_get_node_property — Get a property value from a running game node
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_get_node_property(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String node_path = p_args.get("node_path", "");
	const String property = p_args.get("property", "");

	if (node_path.is_empty() || property.is_empty()) {
		return _make_error("node_path and property are required.");
	}

	String expr = vformat("print('PROP[%s.%s]=', get_node(\"%s\").get(\"%s\"))", node_path, property, node_path, property);
	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["node_path"] = node_path;
	result["property"] = property;
	result["note"] = "Property value printed to game console. Use get_console_output to read.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_set_node_property — Set a property by node path (eval-based)
// More flexible than runtime_set_property (which needs object_id)
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_set_node_property(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String node_path = p_args.get("node_path", "");
	const String property = p_args.get("property", "");
	const String value_str = p_args.get("value", "");

	if (node_path.is_empty() || property.is_empty()) {
		return _make_error("node_path and property are required.");
	}

	// value_str should be a GDScript literal (e.g. Vector2(10,20), true, 5.0, "hello")
	String expr = vformat("get_node(\"%s\").set(\"%s\", %s)", node_path, property, value_str);
	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["node_path"] = node_path;
	result["property"] = property;
	result["value"] = value_str;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_call_method — Call a method on a node in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_call_method(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String node_path = p_args.get("node_path", "");
	const String method = p_args.get("method", "");
	const String args_str = p_args.get("args", "");

	if (node_path.is_empty() || method.is_empty()) {
		return _make_error("node_path and method are required.");
	}

	String expr;
	if (args_str.is_empty()) {
		expr = vformat("var r = get_node(\"%s\").%s(); print('CALL[%s.%s]=', r)", node_path, method, node_path, method);
	} else {
		expr = vformat("var r = get_node(\"%s\").%s(%s); print('CALL[%s.%s]=', r)", node_path, method, args_str, node_path, method);
	}
	_eval_in_game(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["node_path"] = node_path;
	result["method"] = method;
	result["note"] = "Method called. Return value printed to game console. Use get_console_output to read.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_window — Get/set window properties in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_window(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String action = p_args.get("action", "get");

	if (action == "get") {
		_eval_in_game(dbg, "var w = get_window(); print('WINDOW size=', w.size, ' pos=', w.position, ' title=', w.title, ' mode=', w.mode)");
	} else if (action == "set_size") {
		const int w = int(p_args.get("width", 1280));
		const int h = int(p_args.get("height", 720));
		String expr = vformat("get_window().size = Vector2i(%d, %d)", w, h);
		_eval_in_game(dbg, expr);
	} else if (action == "set_title") {
		const String title = p_args.get("title", "");
		String expr = vformat("get_window().title = \"%s\"", title);
		_eval_in_game(dbg, expr);
	} else if (action == "set_fullscreen") {
		const bool fs = bool(p_args.get("fullscreen", true));
		String expr = vformat("get_window().mode = %d", fs ? 3 : 0); // 3 = MODE_FULLSCREEN
		_eval_in_game(dbg, expr);
	}

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_get_performance — Get FPS/memory/object counts from running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_get_performance(const Dictionary &p_args) const {
	(void)p_args;
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	_eval_in_game(dbg,
		"print('PERF fps=', Performance.get_monitor(Performance.TIME_FPS), "
		"' frame_time=', Performance.get_monitor(Performance.TIME_PROCESS), "
		"' physics_time=', Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS), "
		"' objects=', Performance.get_monitor(Performance.OBJECT_COUNT), "
		"' resources=', Performance.get_monitor(Performance.OBJECT_RESOURCE_COUNT), "
		"' nodes=', Performance.get_monitor(Performance.OBJECT_NODE_COUNT), "
		"' memory=', Performance.get_monitor(Performance.MEMORY_STATIC))");

	Dictionary result;
	result["ok"] = true;
	result["note"] = "Performance data printed to game console. Use get_console_output to read.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_raycast — Cast a physics ray in the running game
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_raycast(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String mode = p_args.get("mode", "3d");

	if (mode == "3d") {
		const float fx = float(p_args.get("from_x", 0));
		const float fy = float(p_args.get("from_y", 0));
		const float fz = float(p_args.get("from_z", 0));
		const float tx = float(p_args.get("to_x", 0));
		const float ty = float(p_args.get("to_y", -100));
		const float tz = float(p_args.get("to_z", 0));

		String expr = vformat(
			"var space = get_viewport().get_world_3d().direct_space_state; "
			"var q = PhysicsRayQueryParameters3D.create(Vector3(%f,%f,%f), Vector3(%f,%f,%f)); "
			"var hit = space.intersect_ray(q); "
			"if hit: print('RAYCAST3D hit=', hit.collider.name, ' pos=', hit.position, ' normal=', hit.normal) "
			"else: print('RAYCAST3D no hit')", fx, fy, fz, tx, ty, tz);
		_eval_in_game(dbg, expr);
	} else {
		const float fx = float(p_args.get("from_x", 0));
		const float fy = float(p_args.get("from_y", 0));
		const float tx = float(p_args.get("to_x", 0));
		const float ty = float(p_args.get("to_y", 100));

		String expr = vformat(
			"var space = get_viewport().get_world_2d().direct_space_state; "
			"var q = PhysicsRayQueryParameters2D.create(Vector2(%f,%f), Vector2(%f,%f)); "
			"var hit = space.intersect_ray(q); "
			"if hit: print('RAYCAST2D hit=', hit.collider.name, ' pos=', hit.position, ' normal=', hit.normal) "
			"else: print('RAYCAST2D no hit')", fx, fy, tx, ty);
		_eval_in_game(dbg, expr);
	}

	Dictionary result;
	result["ok"] = true;
	result["mode"] = mode;
	result["note"] = "Raycast results printed to game console. Use get_console_output to read.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// runtime_serialize_state — Serialize node tree state as JSON (for save/load)
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_serialize_state(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _get_active_debugger();
	if (dbg == nullptr) {
		return _runtime_error("No active debug session. Run the game first.");
	}

	const String node_path = p_args.get("node_path", "/root");
	const String action = p_args.get("action", "save");
	const String file_path = p_args.get("file_path", "user://save_state.json");

	if (action == "save") {
		String expr = vformat(
			"var root = get_node(\"%s\"); "
			"var data = {}; "
			"for child in root.get_children(): "
			"  var d = {'name': child.name, 'type': child.get_class()}; "
			"  if child.has_method('get_path'): d['path'] = str(child.get_path()); "
			"  if 'position' in child: d['position'] = {'x': child.position.x, 'y': child.position.y}; "
			"  data[child.name] = d; "
			"var f = FileAccess.open(\"%s\", FileAccess.WRITE); "
			"f.store_string(JSON.stringify(data)); f.close(); "
			"print('STATE_SAVED to %s, nodes=', data.size())", node_path, file_path, file_path);
		_eval_in_game(dbg, expr, 20);
	} else if (action == "print") {
		String expr = vformat(
			"var root = get_node(\"%s\"); "
			"for child in root.get_children(): "
			"  var info = child.name + ' [' + child.get_class() + ']'; "
			"  if 'position' in child: info += ' pos=' + str(child.position); "
			"  if 'health' in child: info += ' hp=' + str(child.health); "
			"  print('STATE: ', info)", node_path);
		_eval_in_game(dbg, expr, 15);
	}

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	result["node_path"] = node_path;
	result["note"] = "State operation executed. Check get_console_output for results.";
	return result;
}
