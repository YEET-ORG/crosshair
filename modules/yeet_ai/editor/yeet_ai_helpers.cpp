/**************************************************************************/
/*  yeet_ai_helpers.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/json.h"
#include "core/io/file_access.h"
#include "core/io/dir_access.h"
#include "core/io/resource_loader.h"
#include "core/string/string_name.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/settings/editor_settings.h"

bool contains_string(const Vector<String> &p_values, const String &p_value) {
	for (const String &value : p_values) {
		if (value == p_value) {
			return true;
		}
	}
	return false;
}

Array coerce_json_array_from_variant(const Variant &p_v) {
	if (p_v.get_type() == Variant::ARRAY) {
		return p_v;
	}
	if (p_v.get_type() == Variant::DICTIONARY) {
		Array single;
		single.push_back(p_v);
		return single;
	}
	if (p_v.get_type() == Variant::STRING) {
		Ref<JSON> json;
		json.instantiate();
		if (json->parse(String(p_v).strip_edges()) == OK) {
			const Variant data = json->get_data();
			if (data.get_type() == Variant::ARRAY) {
				return data;
			}
			if (data.get_type() == Variant::DICTIONARY) {
				Array single;
				single.push_back(data);
				return single;
			}
		}
	}
	return Array();
}

Dictionary coerce_json_dictionary_from_variant(const Variant &p_v) {
	if (p_v.get_type() == Variant::DICTIONARY) {
		return p_v;
	}
	if (p_v.get_type() == Variant::STRING) {
		Ref<JSON> json;
		json.instantiate();
		if (json->parse(String(p_v).strip_edges()) == OK) {
			const Variant data = json->get_data();
			if (data.get_type() == Variant::DICTIONARY) {
				return data;
			}
		}
	}
	return Dictionary();
}

static String _clean_tool_string_field(const Variant &p_value) {
	if (p_value.get_type() != Variant::STRING && p_value.get_type() != Variant::STRING_NAME) {
		return String();
	}
	const String value = String(p_value).strip_edges();
	const String lower = value.to_lower();
	if (value.is_empty() || lower == "<null>" || lower == "null") {
		return String();
	}
	return value;
}

static String _clean_tool_string_field(const Dictionary &p_dict, const StringName &p_key) {
	if (!p_dict.has(p_key)) {
		return String();
	}
	return _clean_tool_string_field(p_dict[p_key]);
}

static constexpr int MAX_PROJECT_TREE_ENTRIES = 200;
static constexpr int MAX_FILE_READ_BYTES = 24 * 1024;
static constexpr int MAX_FILE_WRITE_BYTES = 512 * 1024;
static constexpr int MAX_SCENE_TREE_NODES = 300;
static constexpr int MAX_PROPERTY_COLLECTION_DEPTH = 4;

int YeetAIDock::_get_editor_setting_int(const String &p_setting, int p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	bool valid = false;
	const Variant v = settings->get(StringName(p_setting), &valid);
	if (!valid) {
		return p_default;
	}
	return int(v);
}

bool YeetAIDock::_get_editor_setting_bool(const String &p_setting, bool p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	bool valid = false;
	const Variant v = settings->get(StringName(p_setting), &valid);
	if (!valid) {
		return p_default;
	}
	return bool(v);
}

Dictionary YeetAIDock::_serialize_node(Node *p_node, int p_depth, int p_max_depth, const Vector<String> &p_include_properties, int &r_node_count) const {
	Dictionary result;
	if (p_node == nullptr || r_node_count >= MAX_SCENE_TREE_NODES) {
		return result;
	}

	r_node_count++;
	result["name"] = p_node->get_name();
	result["type"] = p_node->get_class();
	result["node_path"] = String(p_node->get_path());
	result["child_count"] = p_node->get_child_count();

	if (!p_include_properties.is_empty()) {
		result["properties"] = _serialize_node_properties(p_node, p_include_properties);
	}

	Array children;
	if (p_depth < p_max_depth) {
		const int child_count = p_node->get_child_count();
		for (int i = 0; i < child_count && r_node_count < MAX_SCENE_TREE_NODES; i++) {
			children.push_back(_serialize_node(p_node->get_child(i), p_depth + 1, p_max_depth, p_include_properties, r_node_count));
		}
	}
	result["children"] = children;
	return result;
}

Dictionary YeetAIDock::_serialize_node_properties(Node *p_node, const Vector<String> &p_include_properties) const {
	Dictionary properties;
	for (const String &property_name : p_include_properties) {
		if (!_node_has_property(p_node, property_name)) {
			continue;
		}
		properties[property_name] = _json_safe_variant(p_node->get(property_name));
	}
	return properties;
}

bool YeetAIDock::_node_has_property(Node *p_node, const StringName &p_property) const {
	PropertyInfo property_info;
	return _get_node_property_info(p_node, p_property, property_info);
}

bool YeetAIDock::_get_node_property_info(Object *p_object, const StringName &p_property, PropertyInfo &r_info) const {
	if (p_object == nullptr) {
		return false;
	}

	List<PropertyInfo> property_list;
	p_object->get_property_list(&property_list);
	for (const PropertyInfo &property_info : property_list) {
		if (property_info.name == p_property) {
			r_info = property_info;
			return true;
		}
	}
	return false;
}

Ref<Resource> YeetAIDock::_load_resource_for_property(const String &p_resource_path, const String &p_expected_type, String &r_error) const {
	if (!p_resource_path.begins_with("res://")) {
		r_error = "resource_path must start with res://";
		return Ref<Resource>();
	}

	String type_hint = p_expected_type.get_slice(",", 0).strip_edges();
	Error load_error = OK;
	Ref<Resource> resource = ResourceLoader::load(p_resource_path, type_hint, ResourceFormatLoader::CACHE_MODE_REPLACE, &load_error);
	if (resource.is_null() || load_error != OK) {
		r_error = vformat("Failed to load resource: %d", load_error);
		return Ref<Resource>();
	}
	return resource;
}

Variant YeetAIDock::_json_safe_variant(const Variant &p_value, int p_depth) const {
	if (p_depth >= MAX_PROPERTY_COLLECTION_DEPTH) {
		return String("<max-depth>");
	}

	switch (p_value.get_type()) {
		case Variant::NIL:
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::STRING:
			return p_value;
		case Variant::STRING_NAME:
			return String(p_value);
		case Variant::NODE_PATH:
			return String(p_value);
		case Variant::VECTOR2: {
			const Vector2 value = p_value;
			Dictionary dict;
			dict["x"] = value.x;
			dict["y"] = value.y;
			return dict;
		}
		case Variant::VECTOR2I: {
			const Vector2i value = p_value;
			Dictionary dict;
			dict["x"] = value.x;
			dict["y"] = value.y;
			return dict;
		}
		case Variant::VECTOR3: {
			const Vector3 value = p_value;
			Dictionary dict;
			dict["x"] = value.x;
			dict["y"] = value.y;
			dict["z"] = value.z;
			return dict;
		}
		case Variant::VECTOR3I: {
			const Vector3i value = p_value;
			Dictionary dict;
			dict["x"] = value.x;
			dict["y"] = value.y;
			dict["z"] = value.z;
			return dict;
		}
		case Variant::VECTOR4: {
			const Vector4 value = p_value;
			Dictionary dict;
			dict["x"] = value.x;
			dict["y"] = value.y;
			dict["z"] = value.z;
			dict["w"] = value.w;
			return dict;
		}
		case Variant::COLOR: {
			const Color value = p_value;
			Dictionary dict;
			dict["r"] = value.r;
			dict["g"] = value.g;
			dict["b"] = value.b;
			dict["a"] = value.a;
			return dict;
		}
		case Variant::ARRAY: {
			Array array = p_value;
			Array json_array;
			const int count = MIN(array.size(), 32);
			for (int i = 0; i < count; i++) {
				json_array.push_back(_json_safe_variant(array[i], p_depth + 1));
			}
			if (array.size() > count) {
				json_array.push_back("...[truncated]");
			}
			return json_array;
		}
		case Variant::DICTIONARY: {
			Dictionary dictionary = p_value;
			Dictionary json_dict;
			Array keys = dictionary.keys();
			const int count = MIN(keys.size(), 32);
			for (int i = 0; i < count; i++) {
				const Variant key = keys[i];
				json_dict[String(key)] = _json_safe_variant(dictionary[key], p_depth + 1);
			}
			if (keys.size() > count) {
				json_dict["__truncated__"] = true;
			}
			return json_dict;
		}
		case Variant::OBJECT: {
			Object *object = p_value;
			if (object == nullptr) {
				return Variant();
			}

			Dictionary dict;
			dict["class"] = object->get_class();
			if (const Resource *resource = Object::cast_to<Resource>(object)) {
				dict["resource_path"] = resource->get_path();
			}
			return dict;
		}
		default:
			return String(p_value);
	}
}

Variant YeetAIDock::_variant_from_json(const Variant &p_input, Variant::Type p_hint_type) const {
	if (p_input.get_type() == Variant::DICTIONARY) {
		const Dictionary dict = p_input;
		if ((p_hint_type == Variant::VECTOR2 || p_hint_type == Variant::NIL) && dict.has("x") && dict.has("y") && !dict.has("z")) {
			return Vector2(dict.get("x", 0.0), dict.get("y", 0.0));
		}
		// Model may pass a 3-axis dict {x,y,z} for a 2D parameter (e.g. PlaneMesh
		// size). Interpret x as width and z as depth, dropping the irrelevant y.
		if (p_hint_type == Variant::VECTOR2 && dict.has("x") && dict.has("z")) {
			return Vector2(dict.get("x", 0.0), dict.get("z", 0.0));
		}
		if ((p_hint_type == Variant::VECTOR2I) && dict.has("x") && dict.has("y") && !dict.has("z")) {
			return Vector2i(int(dict.get("x", 0)), int(dict.get("y", 0)));
		}
		if ((p_hint_type == Variant::VECTOR3 || p_hint_type == Variant::NIL) && dict.has("x") && dict.has("y") && dict.has("z") && !dict.has("w")) {
			return Vector3(dict.get("x", 0.0), dict.get("y", 0.0), dict.get("z", 0.0));
		}
		if (p_hint_type == Variant::VECTOR3I && dict.has("x") && dict.has("y") && dict.has("z") && !dict.has("w")) {
			return Vector3i(int(dict.get("x", 0)), int(dict.get("y", 0)), int(dict.get("z", 0)));
		}
		if ((p_hint_type == Variant::VECTOR4 || p_hint_type == Variant::NIL) && dict.has("x") && dict.has("y") && dict.has("z") && dict.has("w")) {
			return Vector4(dict.get("x", 0.0), dict.get("y", 0.0), dict.get("z", 0.0), dict.get("w", 0.0));
		}
		if ((p_hint_type == Variant::COLOR || p_hint_type == Variant::NIL) && dict.has("r") && dict.has("g") && dict.has("b")) {
			return Color(dict.get("r", 0.0), dict.get("g", 0.0), dict.get("b", 0.0), dict.get("a", 1.0));
		}
		if ((p_hint_type == Variant::COLOR || p_hint_type == Variant::NIL) && dict.has("red") && dict.has("green") && dict.has("blue")) {
			return Color(dict.get("red", 0.0), dict.get("green", 0.0), dict.get("blue", 0.0), dict.get("alpha", 1.0));
		}
	}

	if (p_input.get_type() == Variant::ARRAY) {
		if (p_hint_type == Variant::COLOR || p_hint_type == Variant::NIL) {
			const Array a = p_input;
			if (a.size() >= 3) {
				return Color(double(a[0]), double(a[1]), double(a[2]), a.size() >= 4 ? double(a[3]) : 1.0);
			}
		}
		if (p_hint_type == Variant::VECTOR2 || p_hint_type == Variant::NIL) {
			const Array a = p_input;
			if (a.size() >= 2) {
				return Vector2(double(a[0]), double(a[1]));
			}
		}
		if (p_hint_type == Variant::VECTOR3 || p_hint_type == Variant::NIL) {
			const Array a = p_input;
			if (a.size() >= 3) {
				return Vector3(double(a[0]), double(a[1]), double(a[2]));
			}
		}
	}

	if (p_input.get_type() == Variant::STRING) {
		const String s = String(p_input).strip_edges();
		if (p_hint_type == Variant::COLOR || p_hint_type == Variant::NIL) {
			if (s.begins_with("#")) {
				return Color::html(s);
			}
			const PackedStringArray parts = s.split(",");
			if (parts.size() >= 3) {
				return Color(parts[0].to_float(), parts[1].to_float(), parts[2].to_float(), parts.size() >= 4 ? parts[3].to_float() : 1.0);
			}
		}
		if (p_hint_type == Variant::VECTOR3 || p_hint_type == Variant::NIL) {
			const PackedStringArray parts = s.split(",");
			if (parts.size() >= 3) {
				return Vector3(parts[0].to_float(), parts[1].to_float(), parts[2].to_float());
			}
		}
		if (p_hint_type == Variant::VECTOR2 || p_hint_type == Variant::NIL) {
			const PackedStringArray parts = s.split(",");
			if (parts.size() >= 2) {
				return Vector2(parts[0].to_float(), parts[1].to_float());
			}
		}
	}

	switch (p_hint_type) {
		case Variant::BOOL:
			return bool(p_input);
		case Variant::INT:
			return int64_t(p_input);
		case Variant::FLOAT:
			return double(p_input);
		case Variant::STRING:
			return String(p_input);
		case Variant::STRING_NAME:
			return StringName(String(p_input));
		case Variant::NODE_PATH:
			return NodePath(String(p_input));
		default:
			return p_input;
	}
}

bool YeetAIDock::_parse_json_dictionary_quiet(const String &p_text, Dictionary &r_result) const {
	Ref<JSON> json;
	json.instantiate();
	const Error error = json->parse(p_text);
	if (error != OK) {
		return false;
	}

	const Variant json_root = json->get_data();
	if (json_root.get_type() != Variant::DICTIONARY) {
		return false;
	}

	r_result = json_root;
	return true;
}

Vector<String> YeetAIDock::_variant_array_to_string_vector(const Array &p_values) const {
	Vector<String> values;
	values.reserve(p_values.size());
	for (const Variant &value : p_values) {
		values.push_back(String(value).to_lower());
	}
	return values;
}

bool YeetAIDock::_is_allowed_text_file(const String &p_path) const {
	static const char *allowed_extensions[] = {
		"godot",
		"gd",
		"tscn",
		"tres",
		"cfg",
		"ini",
		"txt",
		"json",
		"md",
		"shader",
		"gdshader",
	};

	const String ext = p_path.get_extension().to_lower();
	for (const char *allowed_extension : allowed_extensions) {
		if (ext == allowed_extension) {
			return true;
		}
	}
	if (_get_editor_setting_bool("yeet_ai/tools/extended_read_extensions", false)) {
		static const char *extra_extensions[] = {
			"import", "po", "pot", "csv", "xml", "yaml", "yml", "toml", "glsl", "cs", "rs", "proto", "html", "css", "js", "ts", nullptr
		};
		for (int i = 0; extra_extensions[i] != nullptr; i++) {
			if (ext == String(extra_extensions[i])) {
				return true;
			}
		}
	}
	return false;
}

void YeetAIDock::_collect_project_entries(const String &p_dir_path, int p_depth, int p_max_depth, const Vector<String> &p_include_extensions, Array &r_entries, int &r_entry_count) const {
	if (r_entry_count >= MAX_PROJECT_TREE_ENTRIES) {
		return;
	}

	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(p_dir_path, &open_error);
	if (dir.is_null() || open_error != OK) {
		return;
	}

	dir->set_include_hidden(false);
	dir->set_include_navigational(false);
	if (dir->list_dir_begin() != OK) {
		return;
	}

	while (r_entry_count < MAX_PROJECT_TREE_ENTRIES) {
		const String name = dir->get_next();
		if (name.is_empty()) {
			break;
		}

		const String full_path = p_dir_path.path_join(name);
		Dictionary entry;
		entry["path"] = full_path;
		if (dir->current_is_dir()) {
			entry["kind"] = "dir";
			r_entries.push_back(entry);
			r_entry_count++;
			if (p_depth < p_max_depth) {
				_collect_project_entries(full_path, p_depth + 1, p_max_depth, p_include_extensions, r_entries, r_entry_count);
			}
			continue;
		}

		if (!p_include_extensions.is_empty()) {
			const String ext = "." + full_path.get_extension().to_lower();
			if (!contains_string(p_include_extensions, ext)) {
				continue;
			}
		}

		entry["kind"] = "file";
		entry["resource_type"] = ResourceLoader::get_resource_type(full_path);
		r_entries.push_back(entry);
		r_entry_count++;
	}

	dir->list_dir_end();
}

void YeetAIDock::_find_project_entries(const String &p_dir_path, const String &p_query, int p_depth, int p_max_depth, const Vector<String> &p_include_extensions, Array &r_entries, int &r_entry_count, int p_max_results) const {
	if (r_entry_count >= p_max_results) {
		return;
	}

	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(p_dir_path, &open_error);
	if (dir.is_null() || open_error != OK) {
		return;
	}

	dir->set_include_hidden(false);
	dir->set_include_navigational(false);
	if (dir->list_dir_begin() != OK) {
		return;
	}

	while (r_entry_count < p_max_results) {
		const String name = dir->get_next();
		if (name.is_empty()) {
			break;
		}

		const String full_path = p_dir_path.path_join(name);
		if (dir->current_is_dir()) {
			if (p_depth < p_max_depth) {
				_find_project_entries(full_path, p_query, p_depth + 1, p_max_depth, p_include_extensions, r_entries, r_entry_count, p_max_results);
			}
			continue;
		}

		if (!p_include_extensions.is_empty()) {
			const String ext = "." + full_path.get_extension().to_lower();
			if (!contains_string(p_include_extensions, ext)) {
				continue;
			}
		}

		const String lowered_path = full_path.to_lower();
		if (!lowered_path.contains(p_query)) {
			continue;
		}

		Dictionary entry;
		entry["path"] = full_path;
		entry["kind"] = "file";
		entry["resource_type"] = ResourceLoader::get_resource_type(full_path);
		r_entries.push_back(entry);
		r_entry_count++;
	}

	dir->list_dir_end();
}

void YeetAIDock::_grep_project_files_recursive(const String &p_dir, const String &p_query, bool p_case_sensitive, const Vector<String> &p_extensions, int p_max_bytes, int p_max_matches, int &r_match_count, Array &r_matches) const {
	if (r_match_count >= p_max_matches) {
		return;
	}

	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(p_dir, &open_error);
	if (dir.is_null() || open_error != OK) {
		return;
	}

	dir->set_include_hidden(false);
	dir->set_include_navigational(false);
	if (dir->list_dir_begin() != OK) {
		return;
	}

	while (r_match_count < p_max_matches) {
		const String name = dir->get_next();
		if (name.is_empty()) {
			break;
		}

		const String full_path = p_dir.path_join(name);
		if (dir->current_is_dir()) {
			if (name == ".godot" || name == ".git") {
				continue;
			}
			_grep_project_files_recursive(full_path, p_query, p_case_sensitive, p_extensions, p_max_bytes, p_max_matches, r_match_count, r_matches);
			continue;
		}

		if (!p_extensions.is_empty()) {
			const String ext = "." + full_path.get_extension().to_lower();
			if (!contains_string(p_extensions, ext)) {
				continue;
			}
		}

		Error ferr = OK;
		const String content = FileAccess::get_file_as_string(full_path, &ferr);
		if (ferr != OK) {
			continue;
		}

		String scan = content;
		if (scan.length() > p_max_bytes) {
			scan = scan.substr(0, p_max_bytes);
		}

		const String hay = p_case_sensitive ? scan : scan.to_lower();
		const String needle = p_case_sensitive ? p_query : p_query.to_lower();
		if (!hay.contains(needle)) {
			continue;
		}

		int first_line = 1;
		const PackedStringArray lines = scan.split("\n");
		for (int i = 0; i < lines.size(); i++) {
			const String line_text = p_case_sensitive ? lines[i] : lines[i].to_lower();
			if (line_text.contains(needle)) {
				first_line = i + 1;
				break;
			}
		}

		Dictionary hit;
		hit["path"] = full_path;
		hit["first_line"] = first_line;
		r_matches.push_back(hit);
		r_match_count++;
	}

	dir->list_dir_end();
}

Dictionary YeetAIDock::_parse_one_batch_call(const Variant &p_call_var, int p_index, const Dictionary &p_shared_arguments) const {
	Dictionary out;
	Dictionary call;
	if (p_call_var.get_type() == Variant::DICTIONARY) {
		call = p_call_var;
	} else if (p_call_var.get_type() == Variant::STRING) {
		if (!_parse_json_dictionary_quiet(String(p_call_var).strip_edges(), call)) {
			out["error"] = "Each batch call must be a JSON object (or a JSON object encoded as a string).";
			out["failed_index"] = p_index;
			return out;
		}
	} else {
		out["error"] = "Each batch call must be a dictionary.";
		out["failed_index"] = p_index;
		return out;
	}

	String tool_name = _clean_tool_string_field(call, SNAME("tool"));
	if (tool_name.is_empty()) {
		tool_name = _clean_tool_string_field(call, SNAME("name"));
	}
	if (tool_name.is_empty()) {
		tool_name = _clean_tool_string_field(call, SNAME("method"));
	}
	if (tool_name.is_empty()) {
		const Dictionary fn = call.get("function", Dictionary());
		tool_name = _clean_tool_string_field(fn, SNAME("name"));
	}
	if (tool_name.is_empty()) {
		out["error"] = "Each batch call needs a tool name (`tool`, `name`, `method`, or `function.name`).";
		out["failed_index"] = p_index;
		return out;
	}
	if (tool_name == "batch_tool_calls") {
		out["error"] = "batch_tool_calls cannot invoke itself recursively.";
		out["failed_index"] = p_index;
		return out;
	}

	Dictionary call_args;
	if (call.has("arguments")) {
		const Variant av = call["arguments"];
		if (av.get_type() == Variant::STRING) {
			Dictionary parsed;
			if (_parse_json_dictionary_quiet(String(av).strip_edges(), parsed)) {
				call_args = parsed;
			}
		} else if (av.get_type() == Variant::DICTIONARY) {
			call_args = av;
		}
	} else if (call.has("parameters") && call["parameters"].get_type() == Variant::DICTIONARY) {
		call_args = call["parameters"];
	}

	const Array shared_keys = p_shared_arguments.keys();
	for (int key_index = 0; key_index < shared_keys.size(); key_index++) {
		const Variant shared_key_variant = shared_keys[key_index];
		const String shared_key = String(shared_key_variant);
		if (!call_args.has(shared_key)) {
			call_args[shared_key] = p_shared_arguments[shared_key_variant];
		}
	}

	out["tool"] = tool_name;
	out["arguments"] = call_args;
	return out;
}

void YeetAIDock::_mark_unsaved() const {
	EditorInterface::get_singleton()->mark_scene_as_unsaved();
}

void YeetAIDock::_add_to_scene(Node *p_parent, Node *p_child, Node *p_owner) const {
	if (p_parent == nullptr || p_child == nullptr) {
		return;
	}

	// If called from a tool execution, wrap in EditorUndoRedoManager.
	if (!_current_tool_name_for_undo.is_empty()) {
		EditorInterface *ei = EditorInterface::get_singleton();
		if (ei != nullptr) {
			EditorUndoRedoManager *urm = ei->get_editor_undo_redo();
			if (urm != nullptr) {
				String action_name = "AI: " + _current_tool_name_for_undo;
				urm->create_action(action_name);
				urm->add_do_method(p_parent, "add_child", p_child, true);
				urm->add_undo_method(p_parent, "remove_child", p_child);
				if (p_owner != nullptr) {
					urm->add_do_method(p_child, "set_owner", p_owner);
				}
				urm->add_do_reference(p_child);
				urm->commit_action();
				_mark_unsaved();
				return;
			}
		}
	}

	// Fallback: direct addition without undo.
	p_parent->add_child(p_child, true);
	p_child->set_owner(p_owner);
	_set_owner_recursive(p_child, p_owner);
	_mark_unsaved();
}

// ═══════════════════════════════════════════════════════════════════════════
// Editor refresh after tool execution
// ═══════════════════════════════════════════════════════════════════════════

void YeetAIDock::_refresh_editor_after_tool(const String &p_tool_name, const Dictionary &p_result) const {
	EditorInterface *ei = EditorInterface::get_singleton();
	if (ei == nullptr) {
		return;
	}

	const bool is_create = p_tool_name.begins_with("create_") || p_tool_name.begins_with("add_") || p_tool_name.begins_with("instantiate_");
	const bool is_remove = p_tool_name.begins_with("remove_") || p_tool_name.begins_with("delete_");
	const bool is_modify = p_tool_name.begins_with("set_") || p_tool_name.begins_with("update_") || p_tool_name.begins_with("write_") || p_tool_name.begins_with("attach_") || p_tool_name.begins_with("assign_");
	const bool is_file = p_tool_name.begins_with("create_gdscript_file") || p_tool_name.begins_with("update_gdscript_file") || p_tool_name.begins_with("write_project_file") || p_tool_name.begins_with("create_scene_file") || p_tool_name.begins_with("copy_project_file") || p_tool_name.begins_with("move_project_file") || p_tool_name.begins_with("delete_project_file");

	// ── Scene tree refresh ─────────────────────────────────────────────────
	if (is_create || is_remove) {
		// Force scene tree dock to refresh by triggering a selection update.
		if (p_result.has("node_path")) {
			const String node_path = p_result["node_path"];
			Node *scene_root = ei->get_edited_scene_root();
			if (scene_root != nullptr) {
				Node *node = scene_root->get_node_or_null(NodePath(node_path));
				if (node != nullptr) {
					ei->get_selection()->clear();
					ei->get_selection()->add_node(node);
					ei->edit_node(node);
				}
			}
		} else {
			ei->get_selection()->clear();
		}
	}

	// ── Inspector refresh ──────────────────────────────────────────────────
	if (is_modify && p_result.has("node_path")) {
		const String node_path = p_result["node_path"];
		Node *scene_root = ei->get_edited_scene_root();
		if (scene_root != nullptr) {
			Node *node = scene_root->get_node_or_null(NodePath(node_path));
			if (node != nullptr) {
				node->notify_property_list_changed();
			}
		}
	}

	// ── FileSystem refresh ─────────────────────────────────────────────────
	if (is_file) {
		EditorFileSystem *efs = EditorFileSystem::get_singleton();
		if (efs != nullptr) {
			efs->scan_changes();
		}
	}
}
