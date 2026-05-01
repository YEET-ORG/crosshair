/**************************************************************************/
/*  yeet_ai_tools_signal_project.cpp                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/input/input_map.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "editor/editor_interface.h"
#include "scene/gui/check_box.h"
#include "scene/gui/item_list.h"
#include "scene/gui/option_button.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/slider.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/texture_progress_bar.h"
#include "scene/gui/tree.h"

// Forward declarations for helpers defined in yeet_ai_dock.cpp.
bool is_valid_input_action_name(const String &p_name);
Ref<InputEvent> create_input_event_from_definition(const Variant &p_definition, String &r_error);

// ═══════════════════════════════════════════════════════════════════════════
// Signal / Project helpers
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_assign_resource_to_property(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	const String property_name = p_args.get("property", "");
	const String resource_path = p_args.get("resource_path", "");
	if (node_path.is_empty() || property_name.is_empty() || resource_path.is_empty()) {
		result["error"] = "node_path, property, and resource_path are required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *target = _resolve_node_target(scene_root, node_path, error);
	if (target == nullptr) {
		result["error"] = error;
		return result;
	}

	PropertyInfo property_info;
	if (!_get_node_property_info(target, property_name, property_info)) {
		result["error"] = "The target node does not expose that property.";
		return result;
	}

	if (property_info.type != Variant::OBJECT) {
		result["error"] = "Property is not a resource/object property.";
		return result;
	}

	Ref<Resource> resource = _load_resource_for_property(resource_path, property_info.hint_string, error);
	if (resource.is_null()) {
		result["error"] = error;
		return result;
	}

	target->set(property_name, resource);
	_mark_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(target->get_path());
	result["property"] = property_name;
	result["resource_path"] = resource_path;
	return result;
}

Dictionary YeetAIDock::_tool_connect_signal(const Dictionary &p_args) const {
	Dictionary result;
	const String source_node_path = p_args.get("source_node_path", "");
	const String signal_name = p_args.get("signal_name", "");
	const String target_node_path = p_args.get("target_node_path", "");
	const String method_name = p_args.get("method_name", "");
	if (source_node_path.is_empty() || signal_name.is_empty() || target_node_path.is_empty() || method_name.is_empty()) {
		result["error"] = "source_node_path, signal_name, target_node_path, and method_name are required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *source = _resolve_node_target(scene_root, source_node_path, error);
	if (source == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *target = _resolve_node_target(scene_root, target_node_path, error);
	if (target == nullptr) {
		result["error"] = error;
		return result;
	}

	if (!source->has_signal(signal_name)) {
		result["error"] = "The source node does not expose that signal.";
		return result;
	}
	if (!target->has_method(method_name)) {
		result["error"] = "The target node does not expose that method.";
		return result;
	}

	const Callable callable(target, method_name);
	if (source->is_connected(signal_name, callable)) {
		result["scene_path"] = scene_root->get_scene_file_path();
		result["source_node_path"] = String(source->get_path());
		result["target_node_path"] = String(target->get_path());
		result["signal_name"] = signal_name;
		result["method_name"] = method_name;
		result["already_connected"] = true;
		return result;
	}

	const uint32_t flags = uint32_t(int(p_args.get("flags", Object::CONNECT_PERSIST)));
	const Error connect_error = source->connect(signal_name, callable, flags);
	if (connect_error != OK) {
		result["error"] = vformat("Failed to connect signal: %d", connect_error);
		return result;
	}

	_mark_unsaved();
	result["scene_path"] = scene_root->get_scene_file_path();
	result["source_node_path"] = String(source->get_path());
	result["target_node_path"] = String(target->get_path());
	result["signal_name"] = signal_name;
	result["method_name"] = method_name;
	result["flags"] = int(flags);
	return result;
}

Dictionary YeetAIDock::_tool_create_input_action(const Dictionary &p_args) const {
	Dictionary result;
	const String action_name = String(p_args.get("action_name", "")).strip_edges();
	if (!is_valid_input_action_name(action_name)) {
		result["error"] = "action_name is invalid. It cannot be empty or contain / : = \\ or \".";
		return result;
	}

	const float deadzone = p_args.has("deadzone") ? float(p_args["deadzone"]) : InputMap::DEFAULT_DEADZONE;
	const Array event_definitions = p_args.get("events", Array());
	const bool replace_events = bool(p_args.get("replace_events", true));
	if (event_definitions.is_empty()) {
		result["error"] = "events is required and must contain at least one input event.";
		return result;
	}

	Array events;
	for (int i = 0; i < event_definitions.size(); i++) {
		String event_error;
		Ref<InputEvent> input_event = create_input_event_from_definition(event_definitions[i], event_error);
		if (input_event.is_null()) {
			result["error"] = "Failed to parse input event at index " + itos(i) + ": " + event_error;
			return result;
		}
		events.push_back(input_event);
	}

	InputMap *input_map = InputMap::get_singleton();
	if (input_map == nullptr) {
		result["error"] = "InputMap is unavailable.";
		return result;
	}
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	const String property_name = "input/" + action_name;
	Array saved_events;
	if (!replace_events && project_settings->has_setting(property_name)) {
		const Dictionary old_action = Dictionary(project_settings->get_setting_with_override(property_name));
		if (old_action.has("events")) {
			saved_events = old_action["events"];
		}
	}

	if (!input_map->has_action(action_name)) {
		input_map->add_action(action_name, deadzone);
	}
	input_map->action_set_deadzone(action_name, deadzone);
	if (replace_events) {
		input_map->action_erase_events(action_name);
	}
	for (const Variant &event_variant : events) {
		const Ref<InputEvent> input_event = Ref<InputEvent>(event_variant);
		input_map->action_add_event(action_name, input_event);
		saved_events.push_back(input_event);
	}

	Dictionary action;
	action["deadzone"] = deadzone;
	action["events"] = saved_events;
	project_settings->set_setting(property_name, action);
	const Error save_error = project_settings->save();
	if (save_error != OK) {
		result["error"] = vformat("Failed to save project settings: %d", save_error);
		return result;
	}

	result["action_name"] = action_name;
	result["deadzone"] = deadzone;
	result["event_count"] = events.size();
	result["total_event_count"] = saved_events.size();
	result["saved"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_set_main_scene(const Dictionary &p_args) const {
	Dictionary result;
	const String scene_path = p_args.get("scene_path", "");
	if (!scene_path.begins_with("res://")) {
		result["error"] = "scene_path must start with res://";
		return result;
	}
	if (!FileAccess::exists(scene_path)) {
		result["error"] = "scene_path does not exist.";
		return result;
	}

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	const String old_main_scene = String(project_settings->get_setting_with_override("application/run/main_scene"));
	project_settings->set_setting("application/run/main_scene", scene_path);
	const Error save_error = project_settings->save();
	if (save_error != OK) {
		result["error"] = vformat("Failed to save project settings: %d", save_error);
		return result;
	}

	result["main_scene"] = scene_path;
	result["previous_main_scene"] = old_main_scene;
	result["saved"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_play_current_scene(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	editor->play_current_scene();
	result["is_playing"] = editor->is_playing_scene();
	result["playing_scene"] = editor->get_playing_scene();
	return result;
}

Dictionary YeetAIDock::_tool_play_main_scene(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	editor->play_main_scene();
	result["is_playing"] = editor->is_playing_scene();
	result["playing_scene"] = editor->get_playing_scene();
	return result;
}

Dictionary YeetAIDock::_tool_stop_playing_scene(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	const String previous_scene = editor->get_playing_scene();
	editor->stop_playing_scene();
	result["stopped_scene"] = previous_scene;
	result["is_playing"] = editor->is_playing_scene();
	return result;
}

Dictionary YeetAIDock::_tool_set_editor_main_screen(const Dictionary &p_args) const {
	Dictionary result;
	const String screen = String(p_args.get("screen", "")).strip_edges();
	if (screen.is_empty()) {
		result["error"] = "screen is required (e.g. 2D, 3D, Script, Game, AssetLib).";
		return result;
	}
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}
	editor->set_main_screen_editor(screen);
	result["screen"] = screen;
	return result;
}

Dictionary YeetAIDock::_tool_edit_script(const Dictionary &p_args) const {
	Dictionary result;
	const String script_path = p_args.get("script_path", "");
	if (!script_path.begins_with("res://") || !script_path.ends_with(".gd")) {
		result["error"] = "script_path must be a res:// path to a .gd file.";
		return result;
	}
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	const Ref<Script> scr = ResourceLoader::load(script_path);
	if (scr.is_null()) {
		result["error"] = "Failed to load script (not found or not a Script resource).";
		return result;
	}

	const int line = int(p_args.get("line", -1));
	const int col = int(p_args.get("column", 0));
	const bool grab_focus = bool(p_args.get("grab_focus", true));
	editor->edit_script(scr, line, col, grab_focus);
	result["script_path"] = script_path;
	result["line"] = line;
	return result;
}

Dictionary YeetAIDock::_tool_connect_ui_signal(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	Node *source;
	if (!_resolve_scene_and_node(p_args, "source_node_path", &scene_root, &source, err)) {
		return _make_error(err);
	}

	const String ui_event = _arg_string(p_args, "ui_event", "").to_lower();
	if (ui_event.is_empty()) {
		return _make_error("ui_event is required (e.g., pressed, toggled, text_changed, value_changed).");
	}

	// Map ui_event to Godot signal name based on source node type.
	String signal_name;
	Button *btn = Object::cast_to<Button>(source);
	CheckBox *cb = Object::cast_to<CheckBox>(source);
	LineEdit *le = Object::cast_to<LineEdit>(source);
	TextEdit *te = Object::cast_to<TextEdit>(source);
	SpinBox *sb = Object::cast_to<SpinBox>(source);
	Slider *slider = Object::cast_to<Slider>(source);
	ProgressBar *pb = Object::cast_to<ProgressBar>(source);
	TextureProgressBar *tpb = Object::cast_to<TextureProgressBar>(source);
	ItemList *il = Object::cast_to<ItemList>(source);
	OptionButton *ob = Object::cast_to<OptionButton>(source);
	TabContainer *tc = Object::cast_to<TabContainer>(source);
	Tree *tree = Object::cast_to<Tree>(source);

	if (ui_event == "pressed") {
		if (btn || cb) {
			signal_name = "pressed";
		}
	} else if (ui_event == "toggled") {
		if (btn || cb) {
			signal_name = "toggled";
		}
	} else if (ui_event == "text_changed") {
		if (le) {
			signal_name = "text_changed";
		} else if (te) {
			signal_name = "text_changed";
		}
	} else if (ui_event == "text_submitted") {
		if (le) {
			signal_name = "text_submitted";
		}
	} else if (ui_event == "value_changed") {
		if (sb || slider || pb || tpb) {
			signal_name = "value_changed";
		}
	} else if (ui_event == "item_selected") {
		if (il || ob) {
			signal_name = "item_selected";
		} else if (tree) {
			signal_name = "item_selected";
		}
	} else if (ui_event == "tab_changed") {
		if (tc) {
			signal_name = "tab_changed";
		}
	} else if (ui_event == "gui_input") {
		signal_name = "gui_input";
	} else if (ui_event == "focus_entered") {
		signal_name = "focus_entered";
	} else if (ui_event == "focus_exited") {
		signal_name = "focus_exited";
	} else if (ui_event == "mouse_entered") {
		signal_name = "mouse_entered";
	} else if (ui_event == "mouse_exited") {
		signal_name = "mouse_exited";
	}

	if (signal_name.is_empty()) {
		return _make_error(vformat("ui_event '%s' is not supported for node type '%s'. Use connect_signal with an explicit signal_name instead.", ui_event, source->get_class()));
	}

	if (!source->has_signal(signal_name)) {
		return _make_error(vformat("Source node does not expose signal '%s'.", signal_name));
	}

	Node *target = nullptr;
	if (p_args.has("target_node_path")) {
		target = _resolve_node_target(scene_root, _arg_string(p_args, "target_node_path", ""), err);
		if (target == nullptr) {
			return _make_error(err);
		}
	}

	String method_name = _arg_string(p_args, "method_name", "");
	if (method_name.is_empty()) {
		if (target == nullptr) {
			return _make_error("method_name is required when target_node_path is omitted.");
		}
		// Auto-generate method name: _on_<source_name>_<signal_name>
		String src_name = String(source->get_name()).to_lower().replace(" ", "_").replace("-", "_");
		method_name = "_on_" + src_name + "_" + signal_name;
	}

	Callable callable;
	if (target != nullptr) {
		callable = Callable(target, method_name);
	} else {
		// If no target, assume method is on the source itself (self-connect)
		callable = Callable(source, method_name);
	}

	if (source->is_connected(signal_name, callable)) {
		result["already_connected"] = true;
		result["signal_name"] = signal_name;
		result["method_name"] = method_name;
		result["source_node_path"] = String(source->get_path());
		if (target) {
			result["target_node_path"] = String(target->get_path());
		}
		return result;
	}

	const uint32_t flags = uint32_t(_arg_int(p_args, "flags", Object::CONNECT_PERSIST));
	const Error connect_err = source->connect(signal_name, callable, flags);
	if (connect_err != OK) {
		return _make_error(vformat("Failed to connect signal: error %d", connect_err));
	}

	_mark_unsaved();
	result["ok"] = true;
	result["signal_name"] = signal_name;
	result["method_name"] = method_name;
	result["source_node_path"] = String(source->get_path());
	if (target) {
		result["target_node_path"] = String(target->get_path());
	}
	return result;
}
