/**************************************************************************/
/*  yeet_ai_tools_node_ops.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "editor/editor_interface.h"
#include "scene/3d/node_3d.h"
#include "scene/main/node.h"

Dictionary YeetAIDock::_tool_remove_node(const Dictionary &p_args) const {
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

	Node *target = _resolve_node_target(scene_root, node_path, error);
	if (target == nullptr) {
		result["error"] = error;
		return result;
	}

	if (target == scene_root) {
		result["error"] = "Refusing to remove the scene root.";
		return result;
	}

	const String removed_path = String(target->get_path());
	target->queue_free();
	_mark_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["removed_node_path"] = removed_path;
	return result;
}

Dictionary YeetAIDock::_tool_set_node_property(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	const String property_name = p_args.get("property", "");
	if (node_path.is_empty() || property_name.is_empty() || !p_args.has("value")) {
		result["error"] = "node_path, property, and value are required.";
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

	Variant converted_value;
	if (property_info.type == Variant::OBJECT && p_args["value"].get_type() == Variant::STRING && String(p_args["value"]).begins_with("res://")) {
		Ref<Resource> resource = _load_resource_for_property(p_args["value"], property_info.hint_string, error);
		if (resource.is_null()) {
			result["error"] = error;
			return result;
		}
		converted_value = resource;
	} else {
		const Variant current_value = target->get(property_name);
		const Variant::Type hint_type = current_value.get_type() != Variant::NIL ? current_value.get_type() : property_info.type;
		converted_value = _variant_from_json(p_args["value"], hint_type);
	}

	target->set(property_name, converted_value);
	_mark_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(target->get_path());
	result["property"] = property_name;
	result["value"] = _json_safe_variant(target->get(property_name));
	return result;
}

Dictionary YeetAIDock::_tool_reparent_node(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	const String new_parent_path = p_args.get("new_parent_path", "");
	if (node_path.is_empty() || new_parent_path.is_empty()) {
		result["error"] = "node_path and new_parent_path are required (paths relative to scene root).";
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
	if (target == scene_root) {
		result["error"] = "Cannot reparent the scene root.";
		return result;
	}

	Node *new_parent = _resolve_node_target(scene_root, new_parent_path, error);
	if (new_parent == nullptr) {
		result["error"] = error;
		return result;
	}
	if (new_parent == target) {
		result["error"] = "Invalid reparent: new_parent cannot be the node itself.";
		return result;
	}
	if (target->is_ancestor_of(new_parent)) {
		result["error"] = "Invalid reparent: cannot move a node under one of its descendants.";
		return result;
	}

	const bool keep_global = bool(p_args.get("keep_global_transform", true));
	target->reparent(new_parent, keep_global);
	_mark_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(target->get_path());
	result["new_parent_path"] = String(new_parent->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_rename_node(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	const String new_name = String(p_args.get("new_name", "")).strip_edges();
	if (node_path.is_empty() || new_name.is_empty()) {
		result["error"] = "node_path and new_name are required.";
		return result;
	}
	if (new_name.contains("/") || new_name.contains("\\")) {
		result["error"] = "new_name must be a single segment (no path separators).";
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
	if (target == scene_root) {
		result["error"] = "Cannot rename the scene root via this tool (use save_scene_file or edit scene).";
		return result;
	}

	Node *parent = target->get_parent();
	if (parent == nullptr) {
		result["error"] = "Node has no parent.";
		return result;
	}

	const String validated = parent->prevalidate_child_name(target, StringName(new_name));
	target->set_name(validated);
	_mark_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(target->get_path());
	result["new_name"] = target->get_name();
	return result;
}

Dictionary YeetAIDock::_tool_duplicate_node(const Dictionary &p_args) const {
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

	Node *target = _resolve_node_target(scene_root, node_path, error);
	if (target == nullptr) {
		result["error"] = error;
		return result;
	}
	if (target == scene_root) {
		result["error"] = "Cannot duplicate the scene root.";
		return result;
	}

	Node *dup_parent = target->get_parent();
	const String parent_path_arg = String(p_args.get("parent_path", "")).strip_edges();
	if (!parent_path_arg.is_empty()) {
		dup_parent = _resolve_node_target(scene_root, parent_path_arg, error);
		if (dup_parent == nullptr) {
			result["error"] = error;
			return result;
		}
	}
	if (dup_parent == nullptr) {
		result["error"] = "Could not resolve parent for duplicate.";
		return result;
	}
	if (target->is_ancestor_of(dup_parent)) {
		result["error"] = "Invalid parent_path: cannot parent duplicate under a descendant of the source.";
		return result;
	}

	Node *dup = target->duplicate(Node::DUPLICATE_SIGNALS | Node::DUPLICATE_GROUPS | Node::DUPLICATE_SCRIPTS);
	dup_parent->add_child(dup, true);
	const String base_name = String(p_args.get("new_name", String(target->get_name()) + "Copy")).strip_edges();
	dup->set_name(dup_parent->prevalidate_child_name(dup, StringName(base_name)));
	_set_owner_recursive(dup, scene_root);
	_mark_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(dup->get_path());
	result["duplicated_from"] = node_path;
	return result;
}

Dictionary YeetAIDock::_tool_move_child(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty() || !p_args.has("new_index")) {
		result["error"] = "node_path and new_index are required.";
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
	Node *parent = target->get_parent();
	if (parent == nullptr) {
		result["error"] = "Node has no parent.";
		return result;
	}

	int new_index = int(p_args["new_index"]);
	new_index = CLAMP(new_index, 0, parent->get_child_count() - 1);
	parent->move_child(target, new_index);
	_mark_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(target->get_path());
	result["new_index"] = new_index;
	return result;
}
