/**************************************************************************/
/*  yeet_ai_tools_advanced_runtime.cpp                                    */
/**************************************************************************/
/*  Phase 4: Advanced runtime operations — 3D, 2D, audio, UI, shaders,   */
/*  physics, and environment manipulation on the running game.            */
/*  All tools use request_remote_evaluate() to execute GDScript.          */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/json.h"
#include "core/os/os.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "main/main.h"
#include "servers/display/display_server.h"

// ═══════════════════════════════════════════════════════════════════════════════
// HELPERS (reusing patterns from yeet_ai_tools_runtime.cpp)
// ═══════════════════════════════════════════════════════════════════════════════

static ScriptEditorDebugger *_adv_get_debugger() {
	EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton();
	if (!edn) return nullptr;
	ScriptEditorDebugger *dbg = edn->get_default_debugger();
	if (!dbg || !dbg->is_session_active()) return nullptr;
	return dbg;
}

static Dictionary _adv_error(const char *msg) {
	Dictionary r;
	r["error"] = msg;
	r["requires_running_game"] = true;
	return r;
}

static void _adv_eval(ScriptEditorDebugger *dbg, const String &expr, int frames = 10) {
	dbg->request_remote_evaluate(expr, 0);
	for (int i = 0; i < frames; i++) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}
}

// ═══════════════════════════════════════════════════════════════════════════════
// 3D RENDERING
// ═══════════════════════════════════════════════════════════════════════════════

// runtime_mesh_instance — Create a MeshInstance3D with a primitive mesh
Dictionary YeetAIDock::_tool_runtime_mesh_instance(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String parent = p_args.get("parent_path", "/root");
	const String name = p_args.get("name", "MeshInstance3D");
	const String mesh_type = p_args.get("mesh_type", "BoxMesh");

	String expr = vformat(
		"var mi = MeshInstance3D.new(); mi.name = \"%s\"; mi.mesh = %s.new(); "
		"get_node(\"%s\").add_child(mi); mi.owner = get_tree().current_scene; "
		"print('MESH created: ', mi.get_path())", name, mesh_type, parent);
	_adv_eval(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["name"] = name;
	result["mesh_type"] = mesh_type;
	result["parent"] = parent;
	return result;
}

// runtime_light_3d — Create/configure a 3D light at runtime
Dictionary YeetAIDock::_tool_runtime_light_3d(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String action = p_args.get("action", "create");
	const String parent = p_args.get("parent_path", "/root");
	const String name = p_args.get("name", "Light3D");
	const String light_type = p_args.get("light_type", "omni");

	if (action == "create") {
		String class_name = "OmniLight3D";
		if (light_type == "directional") class_name = "DirectionalLight3D";
		else if (light_type == "spot") class_name = "SpotLight3D";

		String expr = vformat(
			"var l = %s.new(); l.name = \"%s\"; "
			"get_node(\"%s\").add_child(l); l.owner = get_tree().current_scene",
			class_name, name, parent);

		if (p_args.has("energy")) {
			expr += vformat("; l.light_energy = %f", float(p_args["energy"]));
		}
		if (p_args.has("color")) {
			expr += vformat("; l.light_color = Color(\"%s\")", String(p_args["color"]));
		}
		_adv_eval(dbg, expr);
	} else if (action == "configure") {
		const String node_path = p_args.get("node_path", "");
		if (node_path.is_empty()) return _make_error("node_path required for configure.");
		String expr = vformat("var l = get_node(\"%s\")", node_path);
		if (p_args.has("energy")) expr += vformat("; l.light_energy = %f", float(p_args["energy"]));
		if (p_args.has("color")) expr += vformat("; l.light_color = Color(\"%s\")", String(p_args["color"]));
		if (p_args.has("shadow")) expr += vformat("; l.shadow_enabled = %s", bool(p_args["shadow"]) ? "true" : "false");
		_adv_eval(dbg, expr);
	}

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	return result;
}

// runtime_gridmap — Set/get/clear GridMap cells at runtime
Dictionary YeetAIDock::_tool_runtime_gridmap(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "set");

	if (node_path.is_empty()) return _make_error("node_path to GridMap is required.");

	if (action == "set") {
		const int x = int(p_args.get("x", 0));
		const int y = int(p_args.get("y", 0));
		const int z = int(p_args.get("z", 0));
		const int item = int(p_args.get("item", 0));
		String expr = vformat("get_node(\"%s\").set_cell_item(Vector3i(%d,%d,%d), %d)", node_path, x, y, z, item);
		_adv_eval(dbg, expr);
	} else if (action == "clear") {
		String expr = vformat("get_node(\"%s\").clear()", node_path);
		_adv_eval(dbg, expr);
	} else if (action == "get") {
		const int x = int(p_args.get("x", 0));
		const int y = int(p_args.get("y", 0));
		const int z = int(p_args.get("z", 0));
		String expr = vformat("print('GRIDMAP_CELL[%d,%d,%d]=', get_node(\"%s\").get_cell_item(Vector3i(%d,%d,%d)))", x, y, z, node_path, x, y, z);
		_adv_eval(dbg, expr);
	}

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	return result;
}

// runtime_environment — Configure post-processing at runtime
Dictionary YeetAIDock::_tool_runtime_environment(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String node_path = p_args.get("node_path", "");
	String target = node_path.is_empty() ? "get_viewport().get_camera_3d().get_world_3d().environment" : vformat("get_node(\"%s\").environment", node_path);

	String expr = vformat("var env = %s", target);
	if (p_args.has("fog_enabled")) expr += vformat("; env.fog_enabled = %s", bool(p_args["fog_enabled"]) ? "true" : "false");
	if (p_args.has("fog_density")) expr += vformat("; env.fog_density = %f", float(p_args["fog_density"]));
	if (p_args.has("glow_enabled")) expr += vformat("; env.glow_enabled = %s", bool(p_args["glow_enabled"]) ? "true" : "false");
	if (p_args.has("glow_intensity")) expr += vformat("; env.glow_intensity = %f", float(p_args["glow_intensity"]));
	if (p_args.has("ssao_enabled")) expr += vformat("; env.ssao_enabled = %s", bool(p_args["ssao_enabled"]) ? "true" : "false");
	if (p_args.has("tonemap_mode")) expr += vformat("; env.tonemap_mode = %d", int(p_args["tonemap_mode"]));
	if (p_args.has("tonemap_exposure")) expr += vformat("; env.tonemap_exposure = %f", float(p_args["tonemap_exposure"]));
	expr += "; print('ENV configured')";
	_adv_eval(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	return result;
}

// runtime_sky — Configure sky/environment background at runtime
Dictionary YeetAIDock::_tool_runtime_sky(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String sky_type = p_args.get("sky_type", "procedural");

	String expr;
	if (sky_type == "procedural") {
		expr = "var env = get_viewport().get_camera_3d().get_world_3d().environment; "
			   "if not env: env = Environment.new(); get_viewport().get_camera_3d().get_world_3d().environment = env; "
			   "var sky = Sky.new(); sky.sky_material = ProceduralSkyMaterial.new(); env.sky = sky; env.background_mode = Environment.BG_SKY";
	} else if (sky_type == "color") {
		const String color = p_args.get("color", "#87CEEB");
		expr = vformat("var env = get_viewport().get_camera_3d().get_world_3d().environment; "
			   "env.background_mode = Environment.BG_COLOR; env.background_color = Color(\"%s\")", color);
	}
	_adv_eval(dbg, expr, 15);

	Dictionary result;
	result["ok"] = true;
	result["sky_type"] = sky_type;
	return result;
}

// runtime_debug_draw — Draw debug geometry in 3D
Dictionary YeetAIDock::_tool_runtime_debug_draw(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String shape = p_args.get("shape", "line");

	String expr;
	if (shape == "line") {
		expr = vformat(
			"var im = ImmediateMesh.new(); var mi = MeshInstance3D.new(); mi.mesh = im; mi.name = 'DebugDraw'; "
			"get_tree().current_scene.add_child(mi); mi.owner = get_tree().current_scene; "
			"im.surface_begin(Mesh.PRIMITIVE_LINES); "
			"im.surface_add_vertex(Vector3(%f,%f,%f)); im.surface_add_vertex(Vector3(%f,%f,%f)); "
			"im.surface_end()",
			float(p_args.get("from_x", 0)), float(p_args.get("from_y", 0)), float(p_args.get("from_z", 0)),
			float(p_args.get("to_x", 0)), float(p_args.get("to_y", 1)), float(p_args.get("to_z", 0)));
	} else if (shape == "sphere") {
		expr = vformat(
			"var mi = MeshInstance3D.new(); mi.name = 'DebugSphere'; "
			"mi.mesh = SphereMesh.new(); mi.mesh.radius = %f; mi.mesh.height = %f; "
			"mi.global_position = Vector3(%f,%f,%f); "
			"get_tree().current_scene.add_child(mi); mi.owner = get_tree().current_scene",
			float(p_args.get("radius", 0.5)), float(p_args.get("radius", 0.5)) * 2.0,
			float(p_args.get("x", 0)), float(p_args.get("y", 0)), float(p_args.get("z", 0)));
	} else if (shape == "box") {
		expr = vformat(
			"var mi = MeshInstance3D.new(); mi.name = 'DebugBox'; "
			"mi.mesh = BoxMesh.new(); mi.mesh.size = Vector3(%f,%f,%f); "
			"mi.global_position = Vector3(%f,%f,%f); "
			"get_tree().current_scene.add_child(mi); mi.owner = get_tree().current_scene",
			float(p_args.get("size_x", 1)), float(p_args.get("size_y", 1)), float(p_args.get("size_z", 1)),
			float(p_args.get("x", 0)), float(p_args.get("y", 0)), float(p_args.get("z", 0)));
	}
	_adv_eval(dbg, expr, 15);

	Dictionary result;
	result["ok"] = true;
	result["shape"] = shape;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 2D SYSTEMS
// ═══════════════════════════════════════════════════════════════════════════════

// runtime_canvas_draw — 2D drawing operations at runtime
Dictionary YeetAIDock::_tool_runtime_canvas_draw(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "line");

	if (node_path.is_empty()) return _make_error("node_path to a CanvasItem/Node2D is required.");

	String expr;
	if (action == "line") {
		expr = vformat("get_node(\"%s\").draw_line(Vector2(%f,%f), Vector2(%f,%f), Color(\"%s\"), %f)",
			node_path,
			float(p_args.get("from_x", 0)), float(p_args.get("from_y", 0)),
			float(p_args.get("to_x", 100)), float(p_args.get("to_y", 100)),
			String(p_args.get("color", "#FFFFFF")), float(p_args.get("width", 2.0)));
	} else if (action == "rect") {
		expr = vformat("get_node(\"%s\").draw_rect(Rect2(%f,%f,%f,%f), Color(\"%s\"), %s)",
			node_path,
			float(p_args.get("x", 0)), float(p_args.get("y", 0)),
			float(p_args.get("width", 50)), float(p_args.get("height", 50)),
			String(p_args.get("color", "#FFFFFF")),
			bool(p_args.get("filled", true)) ? "true" : "false");
	} else if (action == "circle") {
		expr = vformat("get_node(\"%s\").draw_circle(Vector2(%f,%f), %f, Color(\"%s\"))",
			node_path,
			float(p_args.get("x", 0)), float(p_args.get("y", 0)),
			float(p_args.get("radius", 25)),
			String(p_args.get("color", "#FFFFFF")));
	} else if (action == "clear") {
		expr = vformat("get_node(\"%s\").queue_redraw()", node_path);
	}
	_adv_eval(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	return result;
}

// runtime_parallax — Configure ParallaxBackground/layers at runtime
Dictionary YeetAIDock::_tool_runtime_parallax(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty()) return _make_error("node_path required.");

	String expr = vformat("var p = get_node(\"%s\")", node_path);
	if (p_args.has("scroll_offset_x")) expr += vformat("; p.scroll_offset.x = %f", float(p_args["scroll_offset_x"]));
	if (p_args.has("scroll_offset_y")) expr += vformat("; p.scroll_offset.y = %f", float(p_args["scroll_offset_y"]));
	if (p_args.has("motion_scale_x")) expr += vformat("; p.motion_scale.x = %f", float(p_args["motion_scale_x"]));
	if (p_args.has("motion_scale_y")) expr += vformat("; p.motion_scale.y = %f", float(p_args["motion_scale_y"]));
	if (p_args.has("motion_mirroring_x")) expr += vformat("; p.motion_mirroring.x = %f", float(p_args["motion_mirroring_x"]));
	_adv_eval(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// AUDIO
// ═══════════════════════════════════════════════════════════════════════════════

// runtime_audio_play — Play/stop/pause audio at runtime
Dictionary YeetAIDock::_tool_runtime_audio_play(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "play");

	if (node_path.is_empty()) return _make_error("node_path to AudioStreamPlayer is required.");

	String expr;
	if (action == "play") {
		expr = vformat("get_node(\"%s\").play()", node_path);
	} else if (action == "stop") {
		expr = vformat("get_node(\"%s\").stop()", node_path);
	} else if (action == "pause") {
		expr = vformat("get_node(\"%s\").stream_paused = true", node_path);
	} else if (action == "resume") {
		expr = vformat("get_node(\"%s\").stream_paused = false", node_path);
	}
	_adv_eval(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	return result;
}

// runtime_audio_bus — Configure audio bus properties at runtime
Dictionary YeetAIDock::_tool_runtime_audio_bus(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String bus_name = p_args.get("bus_name", "Master");
	const String action = p_args.get("action", "get");

	if (action == "get") {
		String expr = vformat(
			"var idx = AudioServer.get_bus_index(\"%s\"); "
			"print('AUDIO_BUS[%s] vol_db=', AudioServer.get_bus_volume_db(idx), "
			"' mute=', AudioServer.is_bus_mute(idx), ' solo=', AudioServer.is_bus_solo(idx), "
			"' effects=', AudioServer.get_bus_effect_count(idx))", bus_name, bus_name);
		_adv_eval(dbg, expr);
	} else if (action == "set_volume") {
		const float db = float(p_args.get("volume_db", 0.0));
		String expr = vformat("AudioServer.set_bus_volume_db(AudioServer.get_bus_index(\"%s\"), %f)", bus_name, db);
		_adv_eval(dbg, expr);
	} else if (action == "mute") {
		const bool mute = bool(p_args.get("mute", true));
		String expr = vformat("AudioServer.set_bus_mute(AudioServer.get_bus_index(\"%s\"), %s)", bus_name, mute ? "true" : "false");
		_adv_eval(dbg, expr);
	} else if (action == "solo") {
		const bool solo = bool(p_args.get("solo", true));
		String expr = vformat("AudioServer.set_bus_solo(AudioServer.get_bus_index(\"%s\"), %s)", bus_name, solo ? "true" : "false");
		_adv_eval(dbg, expr);
	}

	Dictionary result;
	result["ok"] = true;
	result["bus"] = bus_name;
	result["action"] = action;
	return result;
}

// runtime_audio_effect — Add/remove audio bus effects at runtime
Dictionary YeetAIDock::_tool_runtime_audio_effect(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String bus_name = p_args.get("bus_name", "Master");
	const String action = p_args.get("action", "add");
	const String effect_type = p_args.get("effect_type", "AudioEffectReverb");

	if (action == "add") {
		String expr = vformat(
			"var idx = AudioServer.get_bus_index(\"%s\"); "
			"AudioServer.add_bus_effect(idx, %s.new()); "
			"print('AUDIO_EFFECT added %s to bus %s')", bus_name, effect_type, effect_type, bus_name);
		_adv_eval(dbg, expr);
	} else if (action == "remove") {
		const int effect_idx = int(p_args.get("effect_index", 0));
		String expr = vformat(
			"AudioServer.remove_bus_effect(AudioServer.get_bus_index(\"%s\"), %d)", bus_name, effect_idx);
		_adv_eval(dbg, expr);
	}

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// UI CONTROLS
// ═══════════════════════════════════════════════════════════════════════════════

// runtime_ui_control — Configure Control node properties at runtime
Dictionary YeetAIDock::_tool_runtime_ui_control(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty()) return _make_error("node_path to a Control node is required.");

	String expr = vformat("var c = get_node(\"%s\")", node_path);
	if (p_args.has("visible")) expr += vformat("; c.visible = %s", bool(p_args["visible"]) ? "true" : "false");
	if (p_args.has("focus")) expr += "; c.grab_focus()";
	if (p_args.has("tooltip")) expr += vformat("; c.tooltip_text = \"%s\"", String(p_args["tooltip"]));
	if (p_args.has("mouse_filter")) expr += vformat("; c.mouse_filter = %d", int(p_args["mouse_filter"]));
	if (p_args.has("modulate")) expr += vformat("; c.modulate = Color(\"%s\")", String(p_args["modulate"]));
	if (p_args.has("size_x") && p_args.has("size_y")) {
		expr += vformat("; c.custom_minimum_size = Vector2(%f,%f)", float(p_args["size_x"]), float(p_args["size_y"]));
	}
	_adv_eval(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["node_path"] = node_path;
	return result;
}

// runtime_ui_text — Text operations on LineEdit/TextEdit/RichTextLabel/Label
Dictionary YeetAIDock::_tool_runtime_ui_text(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "set");

	if (node_path.is_empty()) return _make_error("node_path required.");

	String expr;
	if (action == "set") {
		const String text = p_args.get("text", "");
		expr = vformat("var n = get_node(\"%s\"); if n.has_method('set_text'): n.text = \"%s\"; elif 'bbcode_enabled' in n: n.text = \"%s\"", node_path, text, text);
	} else if (action == "get") {
		expr = vformat("print('UI_TEXT[%s]=', get_node(\"%s\").text)", node_path, node_path);
	} else if (action == "append") {
		const String text = p_args.get("text", "");
		expr = vformat("get_node(\"%s\").text += \"%s\"", node_path, text);
	} else if (action == "clear") {
		expr = vformat("get_node(\"%s\").text = ''", node_path);
	}
	_adv_eval(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	return result;
}

// runtime_ui_popup — Show/hide popup/dialog/window nodes
Dictionary YeetAIDock::_tool_runtime_ui_popup(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "show");

	if (node_path.is_empty()) return _make_error("node_path required.");

	String expr;
	if (action == "show" || action == "popup") {
		expr = vformat("var p = get_node(\"%s\"); if p.has_method('popup'): p.popup(); elif p.has_method('popup_centered'): p.popup_centered(); else: p.show()", node_path);
	} else if (action == "hide") {
		expr = vformat("get_node(\"%s\").hide()", node_path);
	}
	_adv_eval(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	return result;
}

// runtime_ui_range — Get/set value on ProgressBar/Slider/SpinBox
Dictionary YeetAIDock::_tool_runtime_ui_range(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String node_path = p_args.get("node_path", "");
	const String action = p_args.get("action", "get");

	if (node_path.is_empty()) return _make_error("node_path required.");

	String expr;
	if (action == "get") {
		expr = vformat("var r = get_node(\"%s\"); print('UI_RANGE[%s] value=', r.value, ' min=', r.min_value, ' max=', r.max_value)", node_path, node_path);
	} else if (action == "set") {
		const float value = float(p_args.get("value", 0));
		expr = vformat("get_node(\"%s\").value = %f", node_path, value);
	} else if (action == "set_range") {
		expr = vformat("var r = get_node(\"%s\")", node_path);
		if (p_args.has("min_value")) expr += vformat("; r.min_value = %f", float(p_args["min_value"]));
		if (p_args.has("max_value")) expr += vformat("; r.max_value = %f", float(p_args["max_value"]));
		if (p_args.has("step")) expr += vformat("; r.step = %f", float(p_args["step"]));
	}
	_adv_eval(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["action"] = action;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// SHADER / THEME / PHYSICS
// ═══════════════════════════════════════════════════════════════════════════════

// runtime_shader_param — Set shader parameters on a material at runtime
Dictionary YeetAIDock::_tool_runtime_shader_param(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String node_path = p_args.get("node_path", "");
	const String param_name = p_args.get("param_name", "");
	const String value = p_args.get("value", "");

	if (node_path.is_empty() || param_name.is_empty()) {
		return _make_error("node_path and param_name are required.");
	}

	String expr = vformat(
		"var mat = get_node(\"%s\").material; "
		"if mat == null: mat = get_node(\"%s\").get_surface_override_material(0); "
		"if mat: mat.set_shader_parameter(\"%s\", %s); print('SHADER_PARAM set %s')",
		node_path, node_path, param_name, value, param_name);
	_adv_eval(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["param_name"] = param_name;
	return result;
}

// runtime_theme_override — Apply theme overrides to a Control at runtime
Dictionary YeetAIDock::_tool_runtime_theme_override(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String node_path = p_args.get("node_path", "");
	const String override_type = p_args.get("override_type", "color");
	const String name = p_args.get("name", "");
	const String value = p_args.get("value", "");

	if (node_path.is_empty() || name.is_empty()) {
		return _make_error("node_path and name are required.");
	}

	String expr;
	if (override_type == "color") {
		expr = vformat("get_node(\"%s\").add_theme_color_override(\"%s\", Color(\"%s\"))", node_path, name, value);
	} else if (override_type == "constant") {
		expr = vformat("get_node(\"%s\").add_theme_constant_override(\"%s\", %s)", node_path, name, value);
	} else if (override_type == "font_size") {
		expr = vformat("get_node(\"%s\").add_theme_font_size_override(\"%s\", %s)", node_path, name, value);
	}
	_adv_eval(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["override_type"] = override_type;
	result["name"] = name;
	return result;
}

// runtime_physics_body — Configure physics body properties at runtime
Dictionary YeetAIDock::_tool_runtime_physics_body(const Dictionary &p_args) const {
	ScriptEditorDebugger *dbg = _adv_get_debugger();
	if (!dbg) return _adv_error("No active debug session.");

	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty()) return _make_error("node_path to physics body required.");

	String expr = vformat("var b = get_node(\"%s\")", node_path);
	if (p_args.has("mass")) expr += vformat("; b.mass = %f", float(p_args["mass"]));
	if (p_args.has("gravity_scale")) expr += vformat("; b.gravity_scale = %f", float(p_args["gravity_scale"]));
	if (p_args.has("linear_velocity_x") && p_args.has("linear_velocity_y")) {
		if (p_args.has("linear_velocity_z")) {
			expr += vformat("; b.linear_velocity = Vector3(%f,%f,%f)", float(p_args["linear_velocity_x"]), float(p_args["linear_velocity_y"]), float(p_args["linear_velocity_z"]));
		} else {
			expr += vformat("; b.linear_velocity = Vector2(%f,%f)", float(p_args["linear_velocity_x"]), float(p_args["linear_velocity_y"]));
		}
	}
	if (p_args.has("angular_velocity")) expr += vformat("; b.angular_velocity = %f", float(p_args["angular_velocity"]));
	if (p_args.has("freeze")) expr += vformat("; b.freeze = %s", bool(p_args["freeze"]) ? "true" : "false");
	_adv_eval(dbg, expr);

	Dictionary result;
	result["ok"] = true;
	result["node_path"] = node_path;
	return result;
}
