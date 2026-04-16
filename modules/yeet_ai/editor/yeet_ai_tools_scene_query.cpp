/**************************************************************************/
/*  yeet_ai_tools_scene_query.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/config/project_settings.h"
#include "core/input/input_map.h"
#include "core/io/dir_access.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"

Dictionary YeetAIDock::_tool_get_project_tree(const Dictionary &p_args) const {
	String root = p_args.get("root", "res://");
	if (!root.begins_with("res://")) {
		root = "res://";
	}

	int max_depth = CLAMP(int(p_args.get("max_depth", 3)), 0, 8);
	Vector<String> include_extensions = _variant_array_to_string_vector(p_args.get("include_extensions", Array()));

	Array entries;
	int entry_count = 0;
	_collect_project_entries(root, 0, max_depth, include_extensions, entries, entry_count);

	Dictionary result;
	result["root"] = root;
	result["entry_count"] = entries.size();
	result["entries"] = entries;
	return result;
}

Dictionary YeetAIDock::_tool_get_open_scenes(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	result["open_scenes"] = editor->get_open_scenes();
	result["unsaved_scenes"] = editor->get_unsaved_scenes();
	return result;
}

Dictionary YeetAIDock::_tool_get_current_scene(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	String error;
	Node *scene_root = _resolve_scene_root(String(), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	result["scene_path"] = scene_root->get_scene_file_path();
	result["root_name"] = scene_root->get_name();
	result["root_type"] = scene_root->get_class();
	result["root_node_path"] = String(scene_root->get_path());
	result["child_count"] = scene_root->get_child_count();
	return result;
}

Dictionary YeetAIDock::_tool_get_selected_nodes(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr || editor->get_selection() == nullptr) {
		result["error"] = "Editor selection is unavailable.";
		return result;
	}

	Array selected_nodes;
	TypedArray<Node> selection = editor->get_selection()->get_selected_nodes();
	for (int i = 0; i < selection.size(); i++) {
		Node *node = Object::cast_to<Node>(selection[i]);
		if (node == nullptr) {
			continue;
		}

		Dictionary entry;
		entry["name"] = node->get_name();
		entry["type"] = node->get_class();
		entry["node_path"] = String(node->get_path());
		entry["owner_scene_path"] = node->get_owner() != nullptr ? node->get_owner()->get_scene_file_path() : String();
		selected_nodes.push_back(entry);
	}

	result["selected_nodes"] = selected_nodes;
	result["count"] = selected_nodes.size();
	return result;
}

Dictionary YeetAIDock::_tool_get_project_settings(const Dictionary &p_args) const {
	Dictionary result;
	Array keys = p_args.get("keys", Array());
	if (keys.is_empty()) {
		keys.push_back("application/config/name");
		keys.push_back("application/run/main_scene");
		keys.push_back("display/window/size/viewport_width");
		keys.push_back("display/window/size/viewport_height");
		keys.push_back("rendering/renderer/rendering_method");
	}

	Dictionary values;
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	for (int i = 0; i < keys.size(); i++) {
		const String key = String(keys[i]).strip_edges();
		if (key.is_empty()) {
			continue;
		}
		if (project_settings->has_setting(key)) {
			values[key] = _json_safe_variant(project_settings->get_setting_with_override(key));
		}
	}

	result["values"] = values;
	result["project_data_dir"] = project_settings->get_project_data_path();
	result["resource_path"] = project_settings->get_resource_path();
	return result;
}

Dictionary YeetAIDock::_tool_get_input_actions(const Dictionary &p_args) const {
	Dictionary result;
	InputMap *input_map = InputMap::get_singleton();
	if (input_map == nullptr) {
		result["error"] = "InputMap is unavailable.";
		return result;
	}

	const bool include_events = bool(p_args.get("include_events", true));
	const int max_actions = CLAMP(int(p_args.get("max_actions", 64)), 1, 256);
	const String query = String(p_args.get("query", "")).to_lower();

	Array actions_array;
	const TypedArray<StringName> actions = input_map->get_actions();
	for (int i = 0; i < actions.size() && actions_array.size() < max_actions; i++) {
		const String action_name = String(actions[i]);
		if (!query.is_empty() && !action_name.to_lower().contains(query)) {
			continue;
		}

		Dictionary action_entry;
		action_entry["name"] = action_name;
		action_entry["deadzone"] = input_map->action_get_deadzone(action_name);

		if (include_events) {
			Array events_array;
			const List<Ref<InputEvent>> *events = input_map->action_get_events(action_name);
			if (events != nullptr) {
				for (const Ref<InputEvent> &event : *events) {
					if (event.is_null()) {
						continue;
					}
					Dictionary event_entry;
					event_entry["class"] = event->get_class();
					event_entry["text"] = event->as_text();
					events_array.push_back(event_entry);
				}
			}
			action_entry["events"] = events_array;
		}

		actions_array.push_back(action_entry);
	}

	result["actions"] = actions_array;
	result["count"] = actions_array.size();
	return result;
}

Dictionary YeetAIDock::_tool_get_scene_tree(const Dictionary &p_args) const {
	Dictionary result;
	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	const int max_depth = CLAMP(int(p_args.get("max_depth", 4)), 0, 8);
	const Vector<String> include_properties = _variant_array_to_string_vector(p_args.get("include_properties", Array()));
	int node_count = 0;

	result["scene_path"] = scene_root->get_scene_file_path();
	result["tree"] = _serialize_node(scene_root, 0, max_depth, include_properties, node_count);
	result["node_count"] = node_count;
	result["truncated"] = node_count >= 300;
	return result;
}

Dictionary YeetAIDock::_tool_get_node_details(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty()) {
		result["error"] = "node_path is required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *node = _resolve_node_target(scene_root, node_path, error);
	if (node == nullptr) {
		result["error"] = error;
		return result;
	}

	const Vector<String> include_properties = _variant_array_to_string_vector(p_args.get("include_properties", Array()));
	result["scene_path"] = scene_root->get_scene_file_path();
	result["name"] = node->get_name();
	result["type"] = node->get_class();
	result["node_path"] = String(node->get_path());
	result["parent_path"] = node->get_parent() != nullptr ? String(node->get_parent()->get_path()) : String();
	result["child_count"] = node->get_child_count();
	result["properties"] = _serialize_node_properties(node, include_properties);
	return result;
}

Dictionary YeetAIDock::_tool_get_node_api(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty()) {
		result["error"] = "node_path is required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *node = _resolve_node_target(scene_root, node_path, error);
	if (node == nullptr) {
		result["error"] = error;
		return result;
	}

	const bool include_private = bool(p_args.get("include_private", false));
	const int max_methods = CLAMP(int(p_args.get("max_methods", 80)), 1, 400);
	const int max_signals = CLAMP(int(p_args.get("max_signals", 60)), 1, 200);
	const int max_properties = CLAMP(int(p_args.get("max_properties", 80)), 1, 400);

	Array methods_array;
	List<MethodInfo> methods;
	node->get_method_list(&methods);
	for (const MethodInfo &method_info : methods) {
		const String method_name = String(method_info.name);
		if (!include_private && method_name.begins_with("_")) {
			continue;
		}
		Dictionary entry;
		entry["name"] = method_name;
		entry["arg_count"] = method_info.arguments.size();
		methods_array.push_back(entry);
		if (methods_array.size() >= max_methods) {
			break;
		}
	}

	Array signals_array;
	List<MethodInfo> signals;
	node->get_signal_list(&signals);
	for (const MethodInfo &signal_info : signals) {
		const String signal_name = String(signal_info.name);
		if (!include_private && signal_name.begins_with("_")) {
			continue;
		}
		Dictionary entry;
		entry["name"] = signal_name;
		entry["arg_count"] = signal_info.arguments.size();
		signals_array.push_back(entry);
		if (signals_array.size() >= max_signals) {
			break;
		}
	}

	Array properties_array;
	List<PropertyInfo> properties;
	node->get_property_list(&properties);
	for (const PropertyInfo &property_info : properties) {
		const String property_name = String(property_info.name);
		if (!include_private && property_name.begins_with("_")) {
			continue;
		}
		Dictionary entry;
		entry["name"] = property_name;
		entry["type"] = Variant::get_type_name(property_info.type);
		properties_array.push_back(entry);
		if (properties_array.size() >= max_properties) {
			break;
		}
	}

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(node->get_path());
	result["type"] = node->get_class();
	result["methods"] = methods_array;
	result["signals"] = signals_array;
	result["properties"] = properties_array;
	if (_node_has_property(node, "script")) {
		const Variant script_variant = node->get("script");
		if (script_variant.get_type() == Variant::OBJECT) {
			Object *script_object = script_variant;
			if (const Resource *script_resource = Object::cast_to<Resource>(script_object)) {
				result["script_path"] = script_resource->get_path();
			}
		}
	}
	return result;
}

Dictionary YeetAIDock::_tool_get_autoloads(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps == nullptr) {
		result["error"] = "ProjectSettings is unavailable.";
		return result;
	}

	Array autoloads;
	const HashMap<StringName, ProjectSettings::AutoloadInfo> &map = ps->get_autoload_list();
	for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &E : map) {
		Dictionary entry;
		entry["name"] = String(E.key);
		entry["path"] = E.value.path;
		entry["singleton"] = E.value.is_singleton;
		autoloads.push_back(entry);
	}

	result["autoloads"] = autoloads;
	result["count"] = autoloads.size();
	return result;
}
