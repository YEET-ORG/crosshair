/**************************************************************************/
/*  yeet_ai_tools_capture.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/core_bind.h"
#include "core/io/image.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/editor_interface.h"
#include "editor/run/editor_run.h"
#include "main/main.h"
#include "scene/main/viewport.h"
#include "servers/display/display_server.h"

Dictionary YeetAIDock::_tool_capture_editor_viewport(const Dictionary &p_args) const {
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	const String target = String(p_args.get("target", "editor_3d")).to_lower();
	SubViewport *vp = nullptr;
	if (target == "editor_2d" || target == "2d") {
		vp = editor->get_editor_viewport_2d();
	} else {
		const int idx = CLAMP(int(p_args.get("viewport_sub_index", 0)), 0, 7);
		vp = editor->get_editor_viewport_3d(idx);
	}

	if (vp == nullptr) {
		result["error"] = "Editor viewport is not available (wrong target or index).";
		return result;
	}

	const Ref<ViewportTexture> tex = vp->get_texture();
	if (tex.is_null()) {
		result["error"] = "Viewport has no texture.";
		return result;
	}

	Ref<Image> img = tex->get_image();
	if (img.is_null() || img->is_empty()) {
		result["error"] = "Viewport image is empty; try switching to the 2D/3D editor tab and retry.";
		return result;
	}

	const int max_w = CLAMP(int(p_args.get("max_width", 640)), 64, 4096);
	if (img->get_width() > max_w) {
		const int nh = MAX(1, int(img->get_height() * (float(max_w) / float(img->get_width()))));
		img->resize(max_w, nh, Image::INTERPOLATE_BILINEAR);
	}

	CoreBind::Marshalls *marshalls = CoreBind::Marshalls::get_singleton();
	if (marshalls == nullptr) {
		result["error"] = "Marshalls singleton is unavailable.";
		return result;
	}

	const Vector<uint8_t> png = img->save_png_to_buffer();
	String b64 = marshalls->raw_to_base64(png);
	const int max_b64 = _get_editor_setting_int("yeet_ai/chat/max_base64_chars", 2000000);
	if (b64.length() > max_b64) {
		result["warning"] = vformat("png_base64 was truncated from %d to %d characters (yeet_ai/chat/max_base64_chars).", b64.length(), max_b64);
		b64 = b64.substr(0, max_b64);
	}
	result["width"] = img->get_width();
	result["height"] = img->get_height();
	result["format"] = "png";
	result["png_base64"] = b64;
	result["target"] = target;
	result["source"] = "editor_viewport";
	return result;
}

Dictionary YeetAIDock::_tool_capture_game_viewport(const Dictionary &p_args) const {
	Dictionary result;
	EditorInterface *ei = EditorInterface::get_singleton();
	if (ei == nullptr || !ei->is_playing_scene()) {
		result["error"] = "No game is running (start play mode with embedded game first).";
		return result;
	}

	game_screenshot_done = false;
	game_screenshot_path = String();
	if (!EditorRun::request_screenshot(callable_mp(const_cast<YeetAIDock *>(this), &YeetAIDock::_on_game_screenshot_cb))) {
		result["error"] = "Could not request a game screenshot (embedded game view may be unavailable).";
		return result;
	}

	const int timeout_ms = CLAMP(_get_editor_setting_int("yeet_ai/chat/game_screenshot_timeout_ms", 8000), 500, 60000);
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + uint64_t(timeout_ms);
	while (!game_screenshot_done && OS::get_singleton()->get_ticks_msec() < deadline) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	if (!game_screenshot_done) {
		result["error"] = "Timed out waiting for game screenshot.";
		return result;
	}

	Ref<Image> img = Image::load_from_file(game_screenshot_path);
	if (img.is_null() || img->is_empty()) {
		result["error"] = "Failed to load screenshot image from temporary path.";
		return result;
	}

	const int max_w = CLAMP(int(p_args.get("max_width", 640)), 64, 4096);
	if (img->get_width() > max_w) {
		const int nh = MAX(1, int(img->get_height() * (float(max_w) / float(img->get_width()))));
		img->resize(max_w, nh, Image::INTERPOLATE_BILINEAR);
	}

	CoreBind::Marshalls *marshalls = CoreBind::Marshalls::get_singleton();
	if (marshalls == nullptr) {
		result["error"] = "Marshalls singleton is unavailable.";
		return result;
	}

	const Vector<uint8_t> png = img->save_png_to_buffer();
	String b64 = marshalls->raw_to_base64(png);
	const int max_b64 = _get_editor_setting_int("yeet_ai/chat/max_base64_chars", 2000000);
	if (b64.length() > max_b64) {
		result["warning"] = vformat("png_base64 was truncated from %d to %d characters (yeet_ai/chat/max_base64_chars).", b64.length(), max_b64);
		b64 = b64.substr(0, max_b64);
	}

	result["width"] = img->get_width();
	result["height"] = img->get_height();
	result["format"] = "png";
	result["png_base64"] = b64;
	result["source"] = "embedded_game";
	result["raw_width"] = int(game_screenshot_w);
	result["raw_height"] = int(game_screenshot_h);
	return result;
}

Dictionary YeetAIDock::_tool_get_runtime_debugger_state(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton();
	if (edn == nullptr) {
		result["error"] = "EditorDebuggerNode is unavailable.";
		return result;
	}

	ScriptEditorDebugger *dbg = edn->get_default_debugger();
	if (dbg == nullptr) {
		result["error"] = "No script debugger instance (start a debug session by running the project).";
		return result;
	}

	result["session_active"] = dbg->is_session_active();
	result["error_count"] = dbg->get_error_count();
	result["warning_count"] = dbg->get_warning_count();
	result["is_breaked"] = dbg->is_breaked();
	result["stack_script_file"] = dbg->get_stack_script_file();
	result["stack_script_line"] = dbg->get_stack_script_line();
	result["stack_script_frame"] = dbg->get_stack_script_frame();
	result["remote_pid"] = dbg->get_remote_pid();
	return result;
}

Dictionary YeetAIDock::_tool_get_debug_snapshot(const Dictionary &p_args) const {
	Dictionary result;
	const int max_log = CLAMP(int(p_args.get("max_log_lines", 80)), 1, 500);
	Dictionary log_args;
	log_args["max_lines"] = max_log;
	result["editor_log"] = _tool_get_editor_log(log_args);
	const Dictionary empty_args;
	result["current_scene"] = _tool_get_current_scene(empty_args);
	result["selected_nodes"] = _tool_get_selected_nodes(empty_args);
	result["unsaved_scenes"] = _tool_get_unsaved_scenes(empty_args);
	result["open_scenes"] = _tool_get_open_scenes(empty_args);
	EditorInterface *ei = EditorInterface::get_singleton();
	if (ei != nullptr) {
		result["is_playing"] = ei->is_playing_scene();
		result["playing_scene"] = ei->get_playing_scene();
	}
	return result;
}
