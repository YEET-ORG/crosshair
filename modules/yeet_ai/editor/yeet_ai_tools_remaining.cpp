/**************************************************************************/
/*  yeet_ai_tools_remaining.cpp                                           */
/**************************************************************************/
/*  Remaining 46 tools: networking, input, animation, navigation,         */
/*  particles, 3D/2D advanced, audio spatial, UI widgets, physics,        */
/*  skeleton, viewport, render settings, locale, multiplayer.             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "main/main.h"
#include "servers/display/display_server.h"

// ═══════════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════════

static ScriptEditorDebugger *_rem_get_dbg() {
	EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton();
	if (!edn) return nullptr;
	ScriptEditorDebugger *dbg = edn->get_default_debugger();
	if (!dbg || !dbg->is_session_active()) return nullptr;
	return dbg;
}

static Dictionary _rem_err(const char *msg) {
	Dictionary r; r["error"] = msg; r["requires_running_game"] = true; return r;
}

static void _rem_eval(ScriptEditorDebugger *dbg, const String &expr, int frames = 10) {
	dbg->request_remote_evaluate(expr, 0);
	for (int i = 0; i < frames; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}
}

// ═══════════════════════════════════════════════════════════════════════════════
// HIGH PRIORITY — Networking
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_http_request(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String url = p_args.get("url", "");
	const String method = p_args.get("method", "GET");
	if (url.is_empty()) return _make_error("url is required.");
	String expr = vformat(
		"var http = HTTPRequest.new(); get_tree().current_scene.add_child(http); "
		"http.request(\"%s\", [], HTTPClient.METHOD_%s); "
		"var res = await http.request_completed; "
		"print('HTTP[%s] status=', res[1], ' body_size=', res[3].size()); http.queue_free()",
		url, method.to_upper(), method);
	_rem_eval(dbg, expr, 30);
	Dictionary result; result["ok"] = true; result["url"] = url; result["method"] = method;
	result["note"] = "HTTP response printed to console. Use get_console_output to read.";
	return result;
}

Dictionary YeetAIDock::_tool_runtime_websocket(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String action = p_args.get("action", "connect");
	const String url = p_args.get("url", "");
	const String message = p_args.get("message", "");
	String expr;
	if (action == "connect") {
		if (url.is_empty()) return _make_error("url required for connect.");
		expr = vformat("var ws = WebSocketPeer.new(); ws.connect_to_url(\"%s\"); print('WS connecting to %s')", url, url);
	} else if (action == "send") {
		expr = vformat("print('WS send not implemented in eval mode — use a dedicated autoload')");
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// HIGH PRIORITY — Navigation, TileMap, Timer, Particles
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_navigate_path(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String mode = p_args.get("mode", "3d");
	String expr;
	if (mode == "3d") {
		expr = vformat(
			"var map = NavigationServer3D.get_maps()[0]; "
			"var path = NavigationServer3D.map_get_path(map, Vector3(%f,%f,%f), Vector3(%f,%f,%f), true); "
			"print('NAV_PATH_3D points=', path.size(), ' path=', path)",
			float(p_args.get("from_x", 0)), float(p_args.get("from_y", 0)), float(p_args.get("from_z", 0)),
			float(p_args.get("to_x", 0)), float(p_args.get("to_y", 0)), float(p_args.get("to_z", 0)));
	} else {
		expr = vformat(
			"var map = NavigationServer2D.get_maps()[0]; "
			"var path = NavigationServer2D.map_get_path(map, Vector2(%f,%f), Vector2(%f,%f), true); "
			"print('NAV_PATH_2D points=', path.size(), ' path=', path)",
			float(p_args.get("from_x", 0)), float(p_args.get("from_y", 0)),
			float(p_args.get("to_x", 0)), float(p_args.get("to_y", 0)));
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["mode"] = mode; return result;
}

Dictionary YeetAIDock::_tool_runtime_tilemap_cells(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "get");
	if (node_path.is_empty()) return _make_error("node_path to TileMapLayer required.");
	String expr;
	if (action == "set") {
		expr = vformat("get_node(\"%s\").set_cell(Vector2i(%d,%d), %d, Vector2i(%d,%d))",
			node_path, int(p_args.get("x", 0)), int(p_args.get("y", 0)),
			int(p_args.get("source_id", 0)), int(p_args.get("atlas_x", 0)), int(p_args.get("atlas_y", 0)));
	} else if (action == "get") {
		expr = vformat("print('TILE[%d,%d]=', get_node(\"%s\").get_cell_source_id(Vector2i(%d,%d)))",
			int(p_args.get("x", 0)), int(p_args.get("y", 0)), node_path, int(p_args.get("x", 0)), int(p_args.get("y", 0)));
	} else if (action == "erase") {
		expr = vformat("get_node(\"%s\").erase_cell(Vector2i(%d,%d))", node_path, int(p_args.get("x", 0)), int(p_args.get("y", 0)));
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_create_timer(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String parent = p_args.get("parent_path", "/root");
	const float wait_time = float(p_args.get("wait_time", 1.0));
	const bool one_shot = bool(p_args.get("one_shot", true));
	const bool autostart = bool(p_args.get("autostart", true));
	String expr = vformat(
		"var t = Timer.new(); t.name = 'RuntimeTimer'; t.wait_time = %f; t.one_shot = %s; "
		"get_node(\"%s\").add_child(t); t.owner = get_tree().current_scene; "
		"if %s: t.start(); print('TIMER created wait=%f')",
		wait_time, one_shot ? "true" : "false", parent, autostart ? "true" : "false", wait_time);
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["wait_time"] = wait_time; return result;
}

Dictionary YeetAIDock::_tool_runtime_particles(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty()) return _make_error("node_path to GPUParticles node required.");
	String expr = vformat("var p = get_node(\"%s\")", node_path);
	if (p_args.has("emitting")) expr += vformat("; p.emitting = %s", bool(p_args["emitting"]) ? "true" : "false");
	if (p_args.has("amount")) expr += vformat("; p.amount = %d", int(p_args["amount"]));
	if (p_args.has("lifetime")) expr += vformat("; p.lifetime = %f", float(p_args["lifetime"]));
	if (p_args.has("speed_scale")) expr += vformat("; p.speed_scale = %f", float(p_args["speed_scale"]));
	if (p_args.has("explosiveness")) expr += vformat("; p.explosiveness = %f", float(p_args["explosiveness"]));
	if (p_args.has("restart")) expr += "; p.restart()";
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// HIGH PRIORITY — Animation Advanced
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_animation_tree(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "travel");
	if (node_path.is_empty()) return _make_error("node_path to AnimationTree required.");
	String expr;
	if (action == "travel") {
		const String state = p_args.get("state", "");
		expr = vformat("get_node(\"%s\").get(\"parameters/playback\").travel(\"%s\")", node_path, state);
	} else if (action == "set_param") {
		const String param = p_args.get("param", "");
		const String value = p_args.get("value", "");
		expr = vformat("get_node(\"%s\").set(\"%s\", %s)", node_path, param, value);
	} else if (action == "get_state") {
		expr = vformat("print('ANIM_TREE state=', get_node(\"%s\").get(\"parameters/playback\").get_current_node())", node_path);
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_animation_control(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "seek");
	if (node_path.is_empty()) return _make_error("node_path to AnimationPlayer required.");
	String expr;
	if (action == "seek") {
		expr = vformat("get_node(\"%s\").seek(%f)", node_path, float(p_args.get("time", 0)));
	} else if (action == "set_speed") {
		expr = vformat("get_node(\"%s\").speed_scale = %f", node_path, float(p_args.get("speed", 1.0)));
	} else if (action == "queue") {
		expr = vformat("get_node(\"%s\").queue(\"%s\")", node_path, String(p_args.get("animation", "")));
	} else if (action == "get_info") {
		expr = vformat("var ap = get_node(\"%s\"); print('ANIM_CTRL current=', ap.current_animation, ' pos=', ap.current_animation_position, ' length=', ap.current_animation_length, ' playing=', ap.is_playing())", node_path);
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// HIGH PRIORITY — System/Process
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_process_mode(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const int mode = int(p_args.get("mode", 0));
	if (node_path.is_empty()) return _make_error("node_path required.");
	String expr = vformat("get_node(\"%s\").process_mode = %d; print('PROCESS_MODE set to %d')", node_path, mode, mode);
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["mode"] = mode; return result;
}

Dictionary YeetAIDock::_tool_runtime_world_settings(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String action = p_args.get("action", "get");
	String expr;
	if (action == "get") {
		expr = "print('WORLD gravity=', ProjectSettings.get_setting('physics/3d/default_gravity'), ' fps=', Engine.physics_ticks_per_second)";
	} else if (action == "set") {
		if (p_args.has("gravity")) expr = vformat("PhysicsServer3D.area_set_param(get_viewport().find_world_3d().space, PhysicsServer3D.AREA_PARAM_GRAVITY, %f)", float(p_args["gravity"]));
		if (p_args.has("physics_fps")) expr += vformat("; Engine.physics_ticks_per_second = %d", int(p_args["physics_fps"]));
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_os_info(const Dictionary &p_args) const {
	(void)p_args;
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	_rem_eval(dbg,
		"print('OS_INFO platform=', OS.get_name(), ' locale=', OS.get_locale(), "
		"' screen_size=', DisplayServer.screen_get_size(), "
		"' video_adapter=', RenderingServer.get_video_adapter_name(), "
		"' memory_static=', Performance.get_monitor(Performance.MEMORY_STATIC))");
	Dictionary result; result["ok"] = true; result["note"] = "OS info printed to console."; return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// HIGH PRIORITY — Input (remaining)
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_gamepad(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String action = p_args.get("action", "button");
	String expr;
	if (action == "button") {
		const int button = int(p_args.get("button", 0));
		expr = vformat("var ev = InputEventJoypadButton.new(); ev.button_index = %d; ev.pressed = true; Input.parse_input_event(ev); "
			"await get_tree().create_timer(0.1).timeout; ev.pressed = false; Input.parse_input_event(ev)", button);
	} else if (action == "axis") {
		const int axis = int(p_args.get("axis", 0));
		const float value = float(p_args.get("value", 0));
		expr = vformat("var ev = InputEventJoypadMotion.new(); ev.axis = %d; ev.axis_value = %f; Input.parse_input_event(ev)", axis, value);
	}
	_rem_eval(dbg, expr, 15);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_mouse_drag(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const float fx = float(p_args.get("from_x", 0)), fy = float(p_args.get("from_y", 0));
	const float tx = float(p_args.get("to_x", 100)), ty = float(p_args.get("to_y", 100));
	const int steps = int(p_args.get("steps", 10));
	String expr = vformat(
		"var from = Vector2(%f,%f); var to = Vector2(%f,%f); "
		"var ev_d = InputEventMouseButton.new(); ev_d.position = from; ev_d.button_index = 1; ev_d.pressed = true; Input.parse_input_event(ev_d); "
		"for i in range(%d): "
		"  var t = float(i) / %d; var pos = from.lerp(to, t); "
		"  var mv = InputEventMouseMotion.new(); mv.position = pos; mv.relative = (to - from) / %d; Input.parse_input_event(mv); "
		"  await get_tree().create_timer(0.02).timeout; "
		"ev_d.position = to; ev_d.pressed = false; Input.parse_input_event(ev_d)",
		fx, fy, tx, ty, steps, steps, steps);
	_rem_eval(dbg, expr, 30);
	Dictionary result; result["ok"] = true; return result;
}

Dictionary YeetAIDock::_tool_runtime_scroll(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const float x = float(p_args.get("x", 0)), y = float(p_args.get("y", 0));
	const bool up = bool(p_args.get("up", true));
	String expr = vformat(
		"var ev = InputEventMouseButton.new(); ev.position = Vector2(%f,%f); "
		"ev.button_index = %d; ev.pressed = true; Input.parse_input_event(ev); "
		"ev.pressed = false; Input.parse_input_event(ev)",
		x, y, up ? 4 : 5);
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["up"] = up; return result;
}

Dictionary YeetAIDock::_tool_runtime_touch(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String action = p_args.get("action", "press");
	const float x = float(p_args.get("x", 0)), y = float(p_args.get("y", 0));
	const int index = int(p_args.get("index", 0));
	String expr;
	if (action == "press" || action == "release") {
		expr = vformat("var ev = InputEventScreenTouch.new(); ev.index = %d; ev.position = Vector2(%f,%f); ev.pressed = %s; Input.parse_input_event(ev)",
			index, x, y, action == "press" ? "true" : "false");
	} else if (action == "drag") {
		expr = vformat("var ev = InputEventScreenDrag.new(); ev.index = %d; ev.position = Vector2(%f,%f); ev.relative = Vector2(%f,%f); Input.parse_input_event(ev)",
			index, x, y, float(p_args.get("relative_x", 0)), float(p_args.get("relative_y", 0)));
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_input_state(const Dictionary &p_args) const {
	(void)p_args;
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	_rem_eval(dbg,
		"print('INPUT_STATE mouse_pos=', get_viewport().get_mouse_position(), "
		"' mouse_pressed=', Input.is_mouse_button_pressed(1), "
		"' joy_connected=', Input.get_connected_joypads().size(), "
		"' actions_pressed=['); "
		"for a in InputMap.get_actions(): "
		"  if Input.is_action_pressed(a): print('  ', a); "
		"print(']')");
	Dictionary result; result["ok"] = true; result["note"] = "Input state printed to console."; return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MEDIUM PRIORITY — 3D Advanced
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_csg(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String parent = p_args.get("parent_path", "/root");
	const String name = p_args.get("name", "CSG");
	const String type = p_args.get("csg_type", "box");
	String class_name = "CSGBox3D";
	if (type == "sphere") class_name = "CSGSphere3D";
	else if (type == "cylinder") class_name = "CSGCylinder3D";
	else if (type == "torus") class_name = "CSGTorus3D";
	else if (type == "polygon") class_name = "CSGPolygon3D";
	else if (type == "combiner") class_name = "CSGCombiner3D";
	String expr = vformat("var c = %s.new(); c.name = \"%s\"; get_node(\"%s\").add_child(c); c.owner = get_tree().current_scene", class_name, name, parent);
	if (p_args.has("operation")) expr += vformat("; c.operation = %d", int(p_args["operation"]));
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["csg_type"] = type; return result;
}

Dictionary YeetAIDock::_tool_runtime_multimesh(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "create");
	if (action == "create") {
		const String parent = p_args.get("parent_path", "/root");
		const int count = int(p_args.get("instance_count", 100));
		String expr = vformat(
			"var mmi = MultiMeshInstance3D.new(); mmi.name = 'MultiMesh'; "
			"var mm = MultiMesh.new(); mm.transform_format = MultiMesh.TRANSFORM_3D; "
			"mm.instance_count = %d; mm.mesh = BoxMesh.new(); mmi.multimesh = mm; "
			"get_node(\"%s\").add_child(mmi); mmi.owner = get_tree().current_scene", count, parent);
		_rem_eval(dbg, expr);
	} else if (action == "set_transform" && !node_path.is_empty()) {
		const int idx = int(p_args.get("index", 0));
		String expr = vformat("get_node(\"%s\").multimesh.set_instance_transform(%d, Transform3D(Basis(), Vector3(%f,%f,%f)))",
			node_path, idx, float(p_args.get("x", 0)), float(p_args.get("y", 0)), float(p_args.get("z", 0)));
		_rem_eval(dbg, expr);
	}
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_procedural_mesh(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String parent = p_args.get("parent_path", "/root");
	const String name = p_args.get("name", "ProceduralMesh");
	// Create a simple triangle as an example; user provides vertices as GDScript literal
	const String vertices = p_args.get("vertices", "PackedVector3Array([Vector3(0,0,0), Vector3(1,0,0), Vector3(0,1,0)])");
	String expr = vformat(
		"var mi = MeshInstance3D.new(); mi.name = \"%s\"; var am = ArrayMesh.new(); "
		"var arrays = []; arrays.resize(Mesh.ARRAY_MAX); arrays[Mesh.ARRAY_VERTEX] = %s; "
		"am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays); mi.mesh = am; "
		"get_node(\"%s\").add_child(mi); mi.owner = get_tree().current_scene", name, vertices, parent);
	_rem_eval(dbg, expr, 15);
	Dictionary result; result["ok"] = true; return result;
}

Dictionary YeetAIDock::_tool_runtime_3d_effects(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String effect_type = p_args.get("effect_type", "reflection_probe");
	const String parent = p_args.get("parent_path", "/root");
	const String name = p_args.get("name", "Effect3D");
	String class_name = "ReflectionProbe";
	if (effect_type == "decal") class_name = "Decal";
	else if (effect_type == "fog_volume") class_name = "FogVolume";
	String expr = vformat("var e = %s.new(); e.name = \"%s\"; get_node(\"%s\").add_child(e); e.owner = get_tree().current_scene", class_name, name, parent);
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["effect_type"] = effect_type; return result;
}

Dictionary YeetAIDock::_tool_runtime_gi(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String gi_type = p_args.get("gi_type", "voxel");
	const String parent = p_args.get("parent_path", "/root");
	String class_name = gi_type == "lightmap" ? "LightmapGI" : "VoxelGI";
	String expr = vformat("var g = %s.new(); g.name = '%s'; get_node(\"%s\").add_child(g); g.owner = get_tree().current_scene", class_name, class_name, parent);
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["gi_type"] = gi_type; return result;
}

Dictionary YeetAIDock::_tool_runtime_path_3d(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "create");
	if (action == "create") {
		const String parent = p_args.get("parent_path", "/root");
		String expr = vformat("var p = Path3D.new(); p.name = 'Path3D'; p.curve = Curve3D.new(); get_node(\"%s\").add_child(p); p.owner = get_tree().current_scene", parent);
		_rem_eval(dbg, expr);
	} else if (action == "add_point" && !node_path.is_empty()) {
		String expr = vformat("get_node(\"%s\").curve.add_point(Vector3(%f,%f,%f))", node_path,
			float(p_args.get("x", 0)), float(p_args.get("y", 0)), float(p_args.get("z", 0)));
		_rem_eval(dbg, expr);
	}
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_camera_attributes(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	String expr = "var cam = get_viewport().get_camera_3d(); if not cam.attributes: cam.attributes = CameraAttributesPractical.new()";
	if (p_args.has("dof_blur_far_enabled")) expr += vformat("; cam.attributes.dof_blur_far_enabled = %s", bool(p_args["dof_blur_far_enabled"]) ? "true" : "false");
	if (p_args.has("dof_blur_far_distance")) expr += vformat("; cam.attributes.dof_blur_far_distance = %f", float(p_args["dof_blur_far_distance"]));
	if (p_args.has("exposure_multiplier")) expr += vformat("; cam.attributes.exposure_multiplier = %f", float(p_args["exposure_multiplier"]));
	if (p_args.has("auto_exposure_enabled")) expr += vformat("; cam.attributes.auto_exposure_enabled = %s", bool(p_args["auto_exposure_enabled"]) ? "true" : "false");
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; return result;
}

Dictionary YeetAIDock::_tool_runtime_navigation_3d(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String action = p_args.get("action", "create");
	const String parent = p_args.get("parent_path", "/root");
	if (action == "create") {
		String expr = vformat("var nr = NavigationRegion3D.new(); nr.name = 'NavRegion3D'; nr.navigation_mesh = NavigationMesh.new(); get_node(\"%s\").add_child(nr); nr.owner = get_tree().current_scene", parent);
		_rem_eval(dbg, expr);
	} else if (action == "bake") {
		const String node_path = p_args.get("node_path", "");
		String expr = vformat("get_node(\"%s\").bake_navigation_mesh()", node_path);
		_rem_eval(dbg, expr, 20);
	}
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_physics_3d_query(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String action = p_args.get("action", "point");
	String expr;
	if (action == "point") {
		expr = vformat(
			"var space = get_viewport().get_world_3d().direct_space_state; "
			"var params = PhysicsPointQueryParameters3D.new(); params.position = Vector3(%f,%f,%f); "
			"var results = space.intersect_point(params); print('PHYSICS3D_POINT hits=', results.size())",
			float(p_args.get("x", 0)), float(p_args.get("y", 0)), float(p_args.get("z", 0)));
	} else if (action == "shape") {
		expr = "print('PHYSICS3D_SHAPE: use runtime_raycast for ray queries')";
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MEDIUM PRIORITY — 2D Advanced
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_light_2d(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String parent = p_args.get("parent_path", "/root");
	const String name = p_args.get("name", "PointLight2D");
	String expr = vformat("var l = PointLight2D.new(); l.name = \"%s\"; l.texture = PlaceholderTexture2D.new(); get_node(\"%s\").add_child(l); l.owner = get_tree().current_scene", name, parent);
	if (p_args.has("energy")) expr += vformat("; l.energy = %f", float(p_args["energy"]));
	if (p_args.has("color")) expr += vformat("; l.color = Color(\"%s\")", String(p_args["color"]));
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; return result;
}

Dictionary YeetAIDock::_tool_runtime_shape_2d(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "add_point");
	if (node_path.is_empty()) return _make_error("node_path to Line2D/Polygon2D required.");
	String expr;
	if (action == "add_point") {
		expr = vformat("get_node(\"%s\").add_point(Vector2(%f,%f))", node_path, float(p_args.get("x", 0)), float(p_args.get("y", 0)));
	} else if (action == "clear_points") {
		expr = vformat("get_node(\"%s\").clear_points()", node_path);
	} else if (action == "set_point") {
		expr = vformat("get_node(\"%s\").set_point_position(%d, Vector2(%f,%f))", node_path, int(p_args.get("index", 0)), float(p_args.get("x", 0)), float(p_args.get("y", 0)));
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_physics_2d_query(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String action = p_args.get("action", "point");
	String expr;
	if (action == "point") {
		expr = vformat(
			"var space = get_viewport().get_world_2d().direct_space_state; "
			"var params = PhysicsPointQueryParameters2D.new(); params.position = Vector2(%f,%f); "
			"var results = space.intersect_point(params); print('PHYSICS2D_POINT hits=', results.size())",
			float(p_args.get("x", 0)), float(p_args.get("y", 0)));
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// MEDIUM PRIORITY — Audio/Skeleton/Viewport/Render/Locale/UI
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_runtime_audio_spatial(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty()) return _make_error("node_path to AudioStreamPlayer3D required.");
	String expr = vformat("var a = get_node(\"%s\")", node_path);
	if (p_args.has("max_distance")) expr += vformat("; a.max_distance = %f", float(p_args["max_distance"]));
	if (p_args.has("unit_size")) expr += vformat("; a.unit_size = %f", float(p_args["unit_size"]));
	if (p_args.has("attenuation_model")) expr += vformat("; a.attenuation_model = %d", int(p_args["attenuation_model"]));
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; return result;
}

Dictionary YeetAIDock::_tool_runtime_audio_bus_layout(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String action = p_args.get("action", "list");
	String expr;
	if (action == "list") {
		expr = "var buses = []; for i in range(AudioServer.bus_count): buses.append(AudioServer.get_bus_name(i)); print('AUDIO_BUSES:', buses)";
	} else if (action == "add") {
		const String name = p_args.get("name", "NewBus");
		expr = vformat("AudioServer.add_bus(); AudioServer.set_bus_name(AudioServer.bus_count - 1, \"%s\")", name);
	} else if (action == "remove") {
		const int idx = int(p_args.get("index", -1));
		if (idx >= 0) expr = vformat("AudioServer.remove_bus(%d)", idx);
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_skeleton_ik(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "start");
	if (node_path.is_empty()) return _make_error("node_path to SkeletonIK3D required.");
	String expr;
	if (action == "start") { expr = vformat("get_node(\"%s\").start()", node_path); }
	else if (action == "stop") { expr = vformat("get_node(\"%s\").stop()", node_path); }
	else if (action == "set_target") {
		expr = vformat("get_node(\"%s\").target = Transform3D(Basis(), Vector3(%f,%f,%f))", node_path,
			float(p_args.get("x", 0)), float(p_args.get("y", 0)), float(p_args.get("z", 0)));
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_create_joint(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String joint_type = p_args.get("joint_type", "pin");
	const String parent = p_args.get("parent_path", "/root");
	const String node_a = p_args.get("node_a", "");
	const String node_b = p_args.get("node_b", "");
	String class_name = "PinJoint3D";
	if (joint_type == "hinge") class_name = "HingeJoint3D";
	else if (joint_type == "slider") class_name = "SliderJoint3D";
	else if (joint_type == "cone_twist") class_name = "ConeTwistJoint3D";
	else if (joint_type == "generic_6dof") class_name = "Generic6DOFJoint3D";
	String expr = vformat("var j = %s.new(); j.name = '%s'; get_node(\"%s\").add_child(j); j.owner = get_tree().current_scene", class_name, class_name, parent);
	if (!node_a.is_empty()) expr += vformat("; j.node_a = j.get_path_to(get_node(\"%s\"))", node_a);
	if (!node_b.is_empty()) expr += vformat("; j.node_b = j.get_path_to(get_node(\"%s\"))", node_b);
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["joint_type"] = joint_type; return result;
}

Dictionary YeetAIDock::_tool_runtime_bone_pose(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "get");
	const int bone_idx = int(p_args.get("bone_index", 0));
	if (node_path.is_empty()) return _make_error("node_path to Skeleton3D required.");
	String expr;
	if (action == "get") {
		expr = vformat("var s = get_node(\"%s\"); print('BONE[%d] pos=', s.get_bone_pose_position(%d), ' rot=', s.get_bone_pose_rotation(%d))", node_path, bone_idx, bone_idx, bone_idx);
	} else if (action == "set_position") {
		expr = vformat("get_node(\"%s\").set_bone_pose_position(%d, Vector3(%f,%f,%f))", node_path, bone_idx,
			float(p_args.get("x", 0)), float(p_args.get("y", 0)), float(p_args.get("z", 0)));
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; return result;
}

Dictionary YeetAIDock::_tool_runtime_viewport(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String action = p_args.get("action", "create");
	if (action == "create") {
		const String parent = p_args.get("parent_path", "/root");
		const int w = int(p_args.get("width", 512)), h = int(p_args.get("height", 512));
		String expr = vformat("var sv = SubViewport.new(); sv.name = 'SubViewport'; sv.size = Vector2i(%d,%d); get_node(\"%s\").add_child(sv); sv.owner = get_tree().current_scene", w, h, parent);
		_rem_eval(dbg, expr);
	} else if (action == "configure") {
		const String node_path = p_args.get("node_path", "");
		String expr = vformat("var sv = get_node(\"%s\")", node_path);
		if (p_args.has("width") && p_args.has("height")) expr += vformat("; sv.size = Vector2i(%d,%d)", int(p_args["width"]), int(p_args["height"]));
		if (p_args.has("transparent_bg")) expr += vformat("; sv.transparent_bg = %s", bool(p_args["transparent_bg"]) ? "true" : "false");
		_rem_eval(dbg, expr);
	}
	Dictionary result; result["ok"] = true; return result;
}

Dictionary YeetAIDock::_tool_runtime_render_settings(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String action = p_args.get("action", "get");
	String expr;
	if (action == "get") {
		expr = "var vp = get_viewport(); print('RENDER msaa_2d=', vp.msaa_2d, ' msaa_3d=', vp.msaa_3d, ' screen_space_aa=', vp.screen_space_aa, ' scaling_3d_mode=', vp.scaling_3d_mode, ' scaling_3d_scale=', vp.scaling_3d_scale)";
	} else if (action == "set") {
		expr = "var vp = get_viewport()";
		if (p_args.has("msaa_3d")) expr += vformat("; vp.msaa_3d = %d", int(p_args["msaa_3d"]));
		if (p_args.has("screen_space_aa")) expr += vformat("; vp.screen_space_aa = %d", int(p_args["screen_space_aa"]));
		if (p_args.has("scaling_3d_scale")) expr += vformat("; vp.scaling_3d_scale = %f", float(p_args["scaling_3d_scale"]));
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_resource_load(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String path = p_args.get("path", "");
	const String action = p_args.get("action", "load");
	if (path.is_empty()) return _make_error("path required.");
	String expr;
	if (action == "load") {
		expr = vformat("var r = load(\"%s\"); print('RESOURCE_LOAD type=', r.get_class() if r else 'null')", path);
	} else if (action == "preload") {
		expr = vformat("ResourceLoader.load_threaded_request(\"%s\"); print('PRELOAD requested: %s')", path, path);
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; return result;
}

Dictionary YeetAIDock::_tool_runtime_locale(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String action = p_args.get("action", "get");
	String expr;
	if (action == "get") {
		expr = "print('LOCALE current=', TranslationServer.get_locale(), ' available=', TranslationServer.get_loaded_locales())";
	} else if (action == "set") {
		expr = vformat("TranslationServer.set_locale(\"%s\"); print('LOCALE set to %s')", String(p_args.get("locale", "en")), String(p_args.get("locale", "en")));
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_ui_tree(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "get");
	if (node_path.is_empty()) return _make_error("node_path to Tree widget required.");
	String expr;
	if (action == "get") {
		expr = vformat("var t = get_node(\"%s\"); var root = t.get_root(); print('TREE columns=', t.columns, ' root=', root.get_text(0) if root else 'null')", node_path);
	} else if (action == "add_item") {
		expr = vformat("var t = get_node(\"%s\"); var item = t.get_root().create_child(); item.set_text(0, \"%s\")", node_path, String(p_args.get("text", "New Item")));
	} else if (action == "select") {
		expr = vformat("get_node(\"%s\").get_root().get_first_child().select(0)", node_path);
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_ui_item_list(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "get");
	if (node_path.is_empty()) return _make_error("node_path required.");
	String expr;
	if (action == "get") { expr = vformat("var l = get_node(\"%s\"); print('ITEMLIST count=', l.item_count, ' selected=', l.get_selected_items())", node_path); }
	else if (action == "add") { expr = vformat("get_node(\"%s\").add_item(\"%s\")", node_path, String(p_args.get("text", "Item"))); }
	else if (action == "remove") { expr = vformat("get_node(\"%s\").remove_item(%d)", node_path, int(p_args.get("index", 0))); }
	else if (action == "select") { expr = vformat("get_node(\"%s\").select(%d)", node_path, int(p_args.get("index", 0))); }
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; return result;
}

Dictionary YeetAIDock::_tool_runtime_ui_tabs(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "get");
	if (node_path.is_empty()) return _make_error("node_path required.");
	String expr;
	if (action == "get") { expr = vformat("var tc = get_node(\"%s\"); print('TABS current=', tc.current_tab, ' count=', tc.get_tab_count())", node_path); }
	else if (action == "set") { expr = vformat("get_node(\"%s\").current_tab = %d", node_path, int(p_args.get("tab", 0))); }
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; return result;
}

Dictionary YeetAIDock::_tool_runtime_ui_menu(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "get");
	if (node_path.is_empty()) return _make_error("node_path to PopupMenu required.");
	String expr;
	if (action == "get") { expr = vformat("var m = get_node(\"%s\"); var items = []; for i in range(m.item_count): items.append(m.get_item_text(i)); print('MENU items=', items)", node_path); }
	else if (action == "add") { expr = vformat("get_node(\"%s\").add_item(\"%s\")", node_path, String(p_args.get("text", "MenuItem"))); }
	else if (action == "remove") { expr = vformat("get_node(\"%s\").remove_item(%d)", node_path, int(p_args.get("index", 0))); }
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; return result;
}

Dictionary YeetAIDock::_tool_runtime_script_attach(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String script_path = p_args.get("script_path", "");
	const String action = p_args.get("action", "attach");
	if (node_path.is_empty()) return _make_error("node_path required.");
	String expr;
	if (action == "attach" && !script_path.is_empty()) {
		expr = vformat("get_node(\"%s\").set_script(load(\"%s\"))", node_path, script_path);
	} else if (action == "detach") {
		expr = vformat("get_node(\"%s\").set_script(null)", node_path);
	} else if (action == "get") {
		expr = vformat("var s = get_node(\"%s\").get_script(); print('SCRIPT=', s.resource_path if s else 'none')", node_path);
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_manage_layers(const Dictionary &p_args) const {
	const String action = p_args.get("action", "list");
	const String layer_type = p_args.get("layer_type", "2d_physics");
	Dictionary result;
	if (action == "list") {
		Array layers;
		for (int i = 1; i <= 32; i++) {
			String key = vformat("layer_names/%s/layer_%d", layer_type, i);
			String name = GLOBAL_GET(key);
			if (!name.is_empty()) {
				Dictionary l; l["index"] = i; l["name"] = name;
				layers.push_back(l);
			}
		}
		result["layers"] = layers;
		result["layer_type"] = layer_type;
	} else if (action == "set") {
		const int index = int(p_args.get("index", 1));
		const String name = p_args.get("name", "");
		String key = vformat("layer_names/%s/layer_%d", layer_type, index);
		ProjectSettings::get_singleton()->set_setting(key, name);
		ProjectSettings::get_singleton()->save();
		result["ok"] = true;
	}
	return result;
}

Dictionary YeetAIDock::_tool_manage_translations(const Dictionary &p_args) const {
	const String action = p_args.get("action", "list");
	Dictionary result;
	if (action == "list") {
		PackedStringArray translations = GLOBAL_GET("internationalization/locale/translations");
		Array arr;
		for (int i = 0; i < translations.size(); i++) { arr.push_back(translations[i]); }
		result["translations"] = arr;
		result["count"] = arr.size();
	} else if (action == "add") {
		const String path = p_args.get("path", "");
		PackedStringArray translations = GLOBAL_GET("internationalization/locale/translations");
		translations.push_back(path);
		ProjectSettings::get_singleton()->set_setting("internationalization/locale/translations", translations);
		ProjectSettings::get_singleton()->save();
		result["ok"] = true;
	} else if (action == "remove") {
		const String path = p_args.get("path", "");
		PackedStringArray translations = GLOBAL_GET("internationalization/locale/translations");
		int idx = translations.find(path);
		if (idx >= 0) { translations.remove_at(idx); }
		ProjectSettings::get_singleton()->set_setting("internationalization/locale/translations", translations);
		ProjectSettings::get_singleton()->save();
		result["ok"] = true;
	}
	return result;
}

Dictionary YeetAIDock::_tool_runtime_multiplayer(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String action = p_args.get("action", "create_server");
	String expr;
	if (action == "create_server") {
		const int port = int(p_args.get("port", 9999));
		expr = vformat("var peer = ENetMultiplayerPeer.new(); peer.create_server(%d); multiplayer.multiplayer_peer = peer; print('SERVER started on port %d')", port, port);
	} else if (action == "create_client") {
		const String address = p_args.get("address", "127.0.0.1");
		const int port = int(p_args.get("port", 9999));
		expr = vformat("var peer = ENetMultiplayerPeer.new(); peer.create_client(\"%s\", %d); multiplayer.multiplayer_peer = peer; print('CLIENT connecting to %s:%d')", address, port, address, port);
	} else if (action == "disconnect") {
		expr = "multiplayer.multiplayer_peer = null; print('MULTIPLAYER disconnected')";
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["action"] = action; return result;
}

Dictionary YeetAIDock::_tool_runtime_rpc(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _rem_get_dbg();
	if (!dbg) return _rem_err("No active debug session.");
	const String node_path = p_args.get("node_path", "");
	const String method = p_args.get("method", "");
	const String args_str = p_args.get("args", "");
	if (node_path.is_empty() || method.is_empty()) return _make_error("node_path and method required.");
	String expr;
	if (args_str.is_empty()) {
		expr = vformat("get_node(\"%s\").rpc(\"%s\")", node_path, method);
	} else {
		expr = vformat("get_node(\"%s\").rpc(\"%s\", %s)", node_path, method, args_str);
	}
	_rem_eval(dbg, expr);
	Dictionary result; result["ok"] = true; result["method"] = method; return result;
}
