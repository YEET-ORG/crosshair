/**************************************************************************/
/*  yeet_ai_tools_inspect.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "editor/editor_interface.h"
#include "scene/2d/navigation/navigation_region_2d.h"
#include "scene/2d/physics/collision_object_2d.h"
#include "scene/2d/tile_map.h"
#include "scene/3d/navigation/navigation_region_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/animation/animation_player.h"

bool contains_string(const Vector<String> &p_values, const String &p_value);

Dictionary YeetAIDock::_tool_find_project_files(const Dictionary &p_args) const {
	Dictionary result;
	String root = p_args.get("root", "res://");
	if (!root.begins_with("res://")) {
		root = "res://";
	}

	const String query = String(p_args.get("query", "")).to_lower().strip_edges();
	if (query.is_empty()) {
		result["error"] = "query is required.";
		return result;
	}

	const int max_depth = CLAMP(int(p_args.get("max_depth", 6)), 0, 12);
	const int max_results = CLAMP(int(p_args.get("max_results", 40)), 1, 200);
	const Vector<String> include_extensions = _variant_array_to_string_vector(p_args.get("include_extensions", Array()));

	Array entries;
	int entry_count = 0;
	_find_project_entries(root, query, 0, max_depth, include_extensions, entries, entry_count, max_results);

	result["root"] = root;
	result["query"] = query;
	result["count"] = entries.size();
	result["entries"] = entries;
	return result;
}

Dictionary YeetAIDock::_tool_file_exists(const Dictionary &p_args) const {
	Dictionary result;
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		result["error"] = "path must start with res://";
		return result;
	}
	const bool is_file = FileAccess::exists(path);
	const bool is_dir = DirAccess::exists(path);
	result["path"] = path;
	result["exists_as_file"] = is_file;
	result["exists_as_directory"] = is_dir;
	result["exists"] = is_file || is_dir;
	if (is_file) {
		result["resource_type"] = ResourceLoader::get_resource_type(path);
		Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
		if (f.is_valid()) {
			result["size_bytes"] = f->get_length();
		}
	}
	return result;
}

Dictionary YeetAIDock::_tool_list_directory(const Dictionary &p_args) const {
	Dictionary result;
	String dir_path = p_args.get("path", "res://");
	if (!dir_path.begins_with("res://")) {
		dir_path = "res://";
	}
	if (!dir_path.ends_with("/")) {
		dir_path += "/";
	}
	const int max_entries = CLAMP(int(p_args.get("max_entries", 200)), 1, 2000);
	const Array ext_filter = p_args.get("include_extensions", Array());

	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(dir_path, &open_error);
	if (dir.is_null() || open_error != OK) {
		result["error"] = vformat("Cannot open directory: %d", open_error);
		return result;
	}

	dir->set_include_hidden(false);
	dir->set_include_navigational(false);
	if (dir->list_dir_begin() != OK) {
		result["error"] = "list_dir_begin failed.";
		return result;
	}

	Array entries;
	int count = 0;
	while (count < max_entries) {
		const String name = dir->get_next();
		if (name.is_empty()) {
			break;
		}
		const String full_path = dir_path.path_join(name);
		if (!ext_filter.is_empty() && !dir->current_is_dir()) {
			const String ext = "." + full_path.get_extension().to_lower();
			if (!contains_string(_variant_array_to_string_vector(ext_filter), ext)) {
				continue;
			}
		}
		Dictionary entry;
		entry["name"] = name;
		entry["path"] = full_path;
		entry["kind"] = dir->current_is_dir() ? "dir" : "file";
		if (!dir->current_is_dir()) {
			entry["resource_type"] = ResourceLoader::get_resource_type(full_path);
		}
		entries.push_back(entry);
		count++;
	}
	dir->list_dir_end();

	result["path"] = dir_path;
	result["entries"] = entries;
	result["count"] = entries.size();
	return result;
}

Dictionary YeetAIDock::_tool_grep_project_files(const Dictionary &p_args) const {
	Dictionary result;
	const String query = String(p_args.get("query", "")).strip_edges();
	if (query.is_empty()) {
		result["error"] = "query is required.";
		return result;
	}

	String root = String(p_args.get("path", "res://"));
	if (!root.begins_with("res://")) {
		root = "res://";
	}
	if (!root.ends_with("/")) {
		root += "/";
	}

	const int max_matches = CLAMP(int(p_args.get("max_results", 40)), 1, 200);
	const int max_bytes = CLAMP(int(p_args.get("max_file_bytes", 256 * 1024)), 1024, 2 * 1024 * 1024);
	const bool case_sensitive = bool(p_args.get("case_sensitive", false));

	Vector<String> exts = _variant_array_to_string_vector(p_args.get("include_extensions", Array()));
	if (exts.is_empty()) {
		static const char *defaults[] = {
			".gd", ".tscn", ".godot", ".tres", ".cfg", ".gdshader", ".shader", ".md", ".txt", ".json", nullptr
		};
		for (int i = 0; defaults[i] != nullptr; i++) {
			exts.push_back(String(defaults[i]));
		}
	}

	Array matches;
	int match_count = 0;
	_grep_project_files_recursive(root, query, case_sensitive, exts, max_bytes, max_matches, match_count, matches);

	result["matches"] = matches;
	result["match_count"] = matches.size();
	result["root"] = root;
	return result;
}

Dictionary YeetAIDock::_tool_get_node_groups(const Dictionary &p_args) const {
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

	List<Node::GroupInfo> groups;
	node->get_groups(&groups);
	Array group_names;
	for (const Node::GroupInfo &gi : groups) {
		group_names.push_back(String(gi.name));
	}

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(node->get_path());
	result["groups"] = group_names;
	return result;
}

Dictionary YeetAIDock::_tool_get_node_collision_layers(const Dictionary &p_args) const {
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

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(node->get_path());

	if (CollisionObject3D *co3 = Object::cast_to<CollisionObject3D>(node)) {
		result["dimension"] = "3d";
		result["collision_layer"] = co3->get_collision_layer();
		result["collision_mask"] = co3->get_collision_mask();
		return result;
	}
	if (CollisionObject2D *co2 = Object::cast_to<CollisionObject2D>(node)) {
		result["dimension"] = "2d";
		result["collision_layer"] = co2->get_collision_layer();
		result["collision_mask"] = co2->get_collision_mask();
		return result;
	}

	result["error"] = "Node is not a CollisionObject2D or CollisionObject3D.";
	return result;
}

Dictionary YeetAIDock::_tool_get_animation_player_state(const Dictionary &p_args) const {
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

	AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(node);
	if (ap == nullptr) {
		result["error"] = "Node is not an AnimationPlayer.";
		return result;
	}

	const LocalVector<StringName> sorted = ap->get_sorted_animation_list();
	Array animations;
	for (const StringName &n : sorted) {
		animations.push_back(String(n));
	}

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(ap->get_path());
	result["animations"] = animations;
	result["current_animation"] = String(ap->get_current_animation());
	result["assigned_animation"] = String(ap->get_assigned_animation());
	result["current_position"] = ap->get_current_animation_position();
	result["current_length"] = ap->get_current_animation_length();
	result["active"] = ap->is_active();
	return result;
}

Dictionary YeetAIDock::_tool_get_tilemap_info(const Dictionary &p_args) const {
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

	TileMap *tm = Object::cast_to<TileMap>(node);
	if (tm == nullptr) {
		result["error"] = "Node is not a TileMap.";
		return result;
	}

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(tm->get_path());
	result["layers_count"] = tm->get_layers_count();

	const Ref<TileSet> ts = tm->get_tileset();
	if (ts.is_valid()) {
		result["tileset_resource_path"] = ts->get_path();
	} else {
		result["tileset_resource_path"] = String();
	}

	const Rect2i ur = tm->get_used_rect();
	Dictionary urd;
	urd["x"] = ur.position.x;
	urd["y"] = ur.position.y;
	urd["w"] = ur.size.x;
	urd["h"] = ur.size.y;
	result["used_rect"] = urd;

	Array layer_names;
	const int lc = tm->get_layers_count();
	for (int i = 0; i < lc; i++) {
		layer_names.push_back(tm->get_layer_name(i));
	}
	result["layer_names"] = layer_names;

	return result;
}

Dictionary YeetAIDock::_tool_get_navigation_region_info(const Dictionary &p_args) const {
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

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(node->get_path());

	if (NavigationRegion3D *r3 = Object::cast_to<NavigationRegion3D>(node)) {
		result["dimension"] = "3d";
		result["enabled"] = r3->is_enabled();
		result["navigation_layers"] = r3->get_navigation_layers();
		result["is_baking"] = r3->is_baking();
		const AABB b = r3->get_bounds();
		Dictionary bd;
		bd["position"] = _json_safe_variant(b.position);
		bd["size"] = _json_safe_variant(b.size);
		result["bounds"] = bd;
		const Ref<NavigationMesh> nm = r3->get_navigation_mesh();
		result["has_navigation_mesh"] = nm.is_valid();
		return result;
	}

	if (NavigationRegion2D *r2 = Object::cast_to<NavigationRegion2D>(node)) {
		result["dimension"] = "2d";
		result["enabled"] = r2->is_enabled();
		result["navigation_layers"] = r2->get_navigation_layers();
		result["is_baking"] = r2->is_baking();
		const Rect2 b = r2->get_bounds();
		Dictionary bd;
		bd["x"] = b.position.x;
		bd["y"] = b.position.y;
		bd["w"] = b.size.x;
		bd["h"] = b.size.y;
		result["bounds"] = bd;
		const Ref<NavigationPolygon> np = r2->get_navigation_polygon();
		result["has_navigation_polygon"] = np.is_valid();
		return result;
	}

	result["error"] = "Node is not a NavigationRegion2D or NavigationRegion3D.";
	return result;
}

Dictionary YeetAIDock::_tool_open_scene(const Dictionary &p_args) const {
	Dictionary result;
	const String scene_path = p_args.get("scene_path", "");
	if (scene_path.is_empty()) {
		result["error"] = "scene_path is required.";
		return result;
	}
	if (!scene_path.begins_with("res://")) {
		result["error"] = "scene_path must start with res:// (e.g., res://scenes/main.tscn)";
		return result;
	}
	if (!FileAccess::exists(scene_path)) {
		result["error"] = vformat("Scene file does not exist: %s", scene_path);
		return result;
	}

	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	// Check if scene is already open and return early
	TypedArray<Node> open_roots = editor->get_open_scene_roots();
	for (int i = 0; i < open_roots.size(); i++) {
		Node *open_root = Object::cast_to<Node>(open_roots[i]);
		if (open_root != nullptr && open_root->get_scene_file_path() == scene_path) {
			result["ok"] = true;
			result["scene_path"] = open_root->get_scene_file_path();
			result["root_name"] = open_root->get_name();
			result["root_type"] = open_root->get_class();
			result["already_open"] = true;
			return result;
		}
	}

	// Open the scene
	editor->open_scene_from_path(scene_path);

	// Retry finding the scene root with a few attempts
	// (scene loading may not be immediate)
	Node *scene_root = nullptr;
	for (int attempt = 0; attempt < 5; attempt++) {
		open_roots = editor->get_open_scene_roots();
		for (int i = 0; i < open_roots.size(); i++) {
			Node *open_root = Object::cast_to<Node>(open_roots[i]);
			if (open_root != nullptr && open_root->get_scene_file_path() == scene_path) {
				scene_root = open_root;
				break;
			}
		}
		if (scene_root != nullptr) {
			break;
		}
		OS::get_singleton()->delay_usec(10000); // 10ms delay between attempts
	}

	if (scene_root == nullptr) {
		result["error"] = vformat("Failed to open scene: %s. The file may be corrupted or not a valid scene.", scene_path);
		return result;
	}

	result["ok"] = true;
	result["scene_path"] = scene_root->get_scene_file_path();
	result["root_name"] = scene_root->get_name();
	result["root_type"] = scene_root->get_class();
	return result;
}

Dictionary YeetAIDock::_tool_save_current_scene(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	Node *scene_root = editor->get_edited_scene_root();
	if (scene_root == nullptr) {
		result["error"] = "There is no current scene to save.";
		return result;
	}

	const Error err = editor->save_scene();
	result["scene_path"] = scene_root->get_scene_file_path();
	result["error_code"] = err;
	if (err != OK) {
		result["error"] = vformat("save_scene failed with error %d", err);
	} else {
		result["ok"] = true;
	}
	return result;
}

Dictionary YeetAIDock::_tool_save_all_scenes(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}
	editor->save_all_scenes();
	result["ok"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_reload_scene(const Dictionary &p_args) const {
	Dictionary result;
	const String scene_path = p_args.get("scene_path", "");
	if (scene_path.is_empty()) {
		result["error"] = "scene_path is required.";
		return result;
	}
	if (!scene_path.begins_with("res://")) {
		result["error"] = "scene_path must start with res://";
		return result;
	}
	if (!FileAccess::exists(scene_path)) {
		result["error"] = vformat("Scene file does not exist: %s", scene_path);
		return result;
	}
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}
	editor->reload_scene_from_path(scene_path);
	result["ok"] = true;
	result["scene_path"] = scene_path;
	result["reloaded"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_select_file(const Dictionary &p_args) const {
	Dictionary result;
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		result["error"] = "path must start with res://";
		return result;
	}
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}
	editor->select_file(path);
	result["ok"] = true;
	result["path"] = path;
	return result;
}

Dictionary YeetAIDock::_tool_get_unsaved_scenes(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}
	result["ok"] = true;
	result["unsaved_scenes"] = editor->get_unsaved_scenes();
	return result;
}
