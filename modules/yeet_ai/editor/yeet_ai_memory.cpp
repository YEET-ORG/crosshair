/**************************************************************************/
/*  yeet_ai_memory.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/
/*  Project Memory system: persistent entity tracking across sessions.    */
/*  Stores entity metadata in .crosshair/memory.json for context-aware   */
/*  AI assistance.                                                        */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/time.h"
#include "core/variant/variant.h"

// ═══════════════════════════════════════════════════════════════════════════
// Internal: memory file path
// ═══════════════════════════════════════════════════════════════════════════

String YeetAIDock::_get_memory_file_path() const {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps == nullptr) {
		return String();
	}
	String project_path = ps->get_resource_path();
	return project_path.path_join(".crosshair/memory.json");
}

// ═══════════════════════════════════════════════════════════════════════════
// Internal: load memory from disk
// ═══════════════════════════════════════════════════════════════════════════

void YeetAIDock::_load_project_memory() const {
	if (_project_memory_loaded) {
		return;
	}
	_project_memory_loaded = true;

	String path = _get_memory_file_path();
	if (path.is_empty()) {
		return;
	}

	if (!FileAccess::exists(path)) {
		// Initialize empty memory
		_project_memory["version"] = 1;
		_project_memory["entities"] = Array();
		_project_memory["sessions"] = Array();
		_project_memory["last_updated"] = Time::get_singleton()->get_unix_time_from_system();
		return;
	}

	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	if (f.is_null()) {
		return;
	}
	String contents = f->get_as_text();
	f->close();

	JSON json;
	Error err = json.parse(contents);
	if (err == OK) {
		Variant data = json.get_data();
		if (data.get_type() == Variant::DICTIONARY) {
			_project_memory = Dictionary(data);
			// Ensure required keys exist
			if (!_project_memory.has("version")) {
				_project_memory["version"] = 1;
			}
			if (!_project_memory.has("entities")) {
				_project_memory["entities"] = Array();
			}
			if (!_project_memory.has("sessions")) {
				_project_memory["sessions"] = Array();
			}
		}
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// Internal: save memory to disk
// ═══════════════════════════════════════════════════════════════════════════

void YeetAIDock::_save_project_memory() const {
	String path = _get_memory_file_path();
	if (path.is_empty()) {
		return;
	}

	_project_memory["last_updated"] = Time::get_singleton()->get_unix_time_from_system();

	// Ensure directory exists
	String dir = path.get_base_dir();
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da.is_valid()) {
		Error err = da->make_dir_recursive(dir);
		if (err != OK && err != ERR_ALREADY_EXISTS) {
			return;
		}
	}

	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_null()) {
		return;
	}
	String json_text = JSON::stringify(_project_memory, "\t", false, true);
	f->store_string(json_text);
	f->close();
}

// ═══════════════════════════════════════════════════════════════════════════
// Internal: auto-record tool result
// ═══════════════════════════════════════════════════════════════════════════

void YeetAIDock::_auto_record_tool_result(const String &p_tool_name, const Dictionary &p_args, const Dictionary &p_result) const {
	_load_project_memory();

	if (!p_result.get("ok", false)) {
		return;
	}

	Dictionary entity;
	bool should_record = false;

	if (p_tool_name == "create_gdscript_file" || p_tool_name == "update_gdscript_file") {
		String script_path = p_result.get("script_path", "");
		if (!script_path.is_empty()) {
			entity["type"] = "script";
			entity["name"] = script_path.get_file();
			entity["paths"] = Array::make(script_path);
			should_record = true;
		}
	} else if (p_tool_name == "create_scene_file" || p_tool_name == "save_current_scene") {
		String scene_path = p_result.get("scene_path", p_args.get("scene_path", ""));
		if (!scene_path.is_empty()) {
			entity["type"] = "scene";
			entity["name"] = scene_path.get_file();
			entity["paths"] = Array::make(scene_path);
			should_record = true;
		}
	} else if (p_tool_name.begins_with("scaffold_")) {
		String node_path = p_result.get("node_path", "");
		String name = p_result.get("type", "unknown");
		if (!node_path.is_empty()) {
			entity["type"] = "node";
			entity["name"] = node_path.get_file();
			entity["paths"] = Array::make(node_path);
			if (p_result.has("components")) {
				entity["components"] = p_result["components"];
			}
			should_record = true;
		}
	}

	if (!should_record) {
		return;
	}

	// Generate ID from first path
	Array paths = entity["paths"];
	String id = paths[0];
	entity["id"] = id;

	int64_t now = Time::get_singleton()->get_unix_time_from_system();
	entity["last_modified"] = now;

	// Upsert: find existing entity by id
	Array entities = _project_memory.get("entities", Array());
	bool found = false;
	for (int i = 0; i < entities.size(); i++) {
		Dictionary existing = entities[i];
		if (existing.get("id", "") == id) {
			existing.merge(entity);
			existing["last_modified"] = now;
			found = true;
			break;
		}
	}
	if (!found) {
		entity["created_at"] = now;
		entities.push_back(entity);
	}
	_project_memory["entities"] = entities;
	_save_project_memory();
}

// ═══════════════════════════════════════════════════════════════════════════
// Internal: query memory entities
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_query_memory_entities(const Dictionary &p_query) const {
	_load_project_memory();

	Array entities = _project_memory.get("entities", Array());
	Array results;

	String type_filter = p_query.get("type", "");
	String tag_filter = p_query.get("tag", "");
	String name_filter = p_query.get("name", "").to_lower();
	String path_filter = p_query.get("path", "").to_lower();
	int max_results = CLAMP(int(p_query.get("max_results", 50)), 1, 500);

	for (int i = 0; i < entities.size() && results.size() < max_results; i++) {
		Dictionary entity = entities[i];
		bool match = true;

		if (!type_filter.is_empty()) {
			if (entity.get("type", "") != type_filter) {
				match = false;
			}
		}
		if (match && !tag_filter.is_empty()) {
			Array tags = entity.get("tags", Array());
			bool has_tag = false;
			for (int t = 0; t < tags.size(); t++) {
				if (String(tags[t]) == tag_filter) {
					has_tag = true;
					break;
				}
			}
			if (!has_tag) {
				match = false;
			}
		}
		if (match && !name_filter.is_empty()) {
			String entity_name = entity.get("name", "").to_lower();
			if (!entity_name.contains(name_filter)) {
				match = false;
			}
		}
		if (match && !path_filter.is_empty()) {
			Array paths = entity.get("paths", Array());
			bool path_match = false;
			for (int p = 0; p < paths.size(); p++) {
				if (String(paths[p]).to_lower().contains(path_filter)) {
					path_match = true;
					break;
				}
			}
			if (!path_match) {
				match = false;
			}
		}

		if (match) {
			results.push_back(entity);
		}
	}

	Dictionary result;
	result["ok"] = true;
	result["count"] = results.size();
	result["total"] = entities.size();
	result["entities"] = results;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: memory_record_entity
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_memory_record_entity(const Dictionary &p_args) const {
	_load_project_memory();

	String id = p_args.get("id", "");
	String type = p_args.get("type", "entity");
	String name = p_args.get("name", "");
	if (name.is_empty()) {
		return _make_error("'name' is required to record an entity.");
	}
	if (id.is_empty()) {
		id = name + "_" + String::num_int64(Time::get_singleton()->get_unix_time_from_system());
	}

	Array paths = _arg_array(p_args, "paths");
	if (paths.is_empty()) {
		paths.push_back(name);
	}

	Dictionary entity;
	entity["id"] = id;
	entity["type"] = type;
	entity["name"] = name;
	entity["paths"] = paths;

	if (p_args.has("tags")) {
		entity["tags"] = _arg_array(p_args, "tags");
	}
	if (p_args.has("metadata")) {
		entity["metadata"] = _arg_dict(p_args, "metadata");
	}
	if (p_args.has("components")) {
		entity["components"] = _arg_array(p_args, "components");
	}

	int64_t now = Time::get_singleton()->get_unix_time_from_system();
	entity["last_modified"] = now;

	Array entities = _project_memory.get("entities", Array());
	bool found = false;
	for (int i = 0; i < entities.size(); i++) {
		Dictionary existing = entities[i];
		if (existing.get("id", "") == id) {
			existing.merge(entity);
			existing["last_modified"] = now;
			found = true;
			break;
		}
	}
	if (!found) {
		entity["created_at"] = now;
		entities.push_back(entity);
	}
	_project_memory["entities"] = entities;
	_save_project_memory();

	Dictionary result;
	result["ok"] = true;
	result["id"] = id;
	result["recorded"] = true;
	result["is_new"] = !found;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: memory_query_entities
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_memory_query_entities(const Dictionary &p_args) const {
	return _query_memory_entities(p_args);
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: memory_get_entity
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_memory_get_entity(const Dictionary &p_args) const {
	_load_project_memory();

	String id = p_args.get("id", "");
	if (id.is_empty()) {
		return _make_error("'id' is required to get an entity.");
	}

	Array entities = _project_memory.get("entities", Array());
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		if (entity.get("id", "") == id) {
			Dictionary result;
			result["ok"] = true;
			result["found"] = true;
			result["entity"] = entity;
			return result;
		}
	}

	Dictionary result;
	result["ok"] = true;
	result["found"] = false;
	result["id"] = id;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: memory_update_entity
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_memory_update_entity(const Dictionary &p_args) const {
	_load_project_memory();

	String id = p_args.get("id", "");
	if (id.is_empty()) {
		return _make_error("'id' is required to update an entity.");
	}

	Array entities = _project_memory.get("entities", Array());
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		if (entity.get("id", "") == id) {
			if (p_args.has("name")) {
				entity["name"] = p_args["name"];
			}
			if (p_args.has("type")) {
				entity["type"] = p_args["type"];
			}
			if (p_args.has("paths")) {
				entity["paths"] = _arg_array(p_args, "paths");
			}
			if (p_args.has("tags")) {
				entity["tags"] = _arg_array(p_args, "tags");
			}
			if (p_args.has("metadata")) {
				Dictionary meta = entity.get("metadata", Dictionary());
				Dictionary new_meta = _arg_dict(p_args, "metadata");
				for (const Variant *key = new_meta.next(nullptr); key != nullptr; key = new_meta.next(key)) {
					meta[*key] = new_meta[*key];
				}
				entity["metadata"] = meta;
			}
			if (p_args.has("components")) {
				entity["components"] = _arg_array(p_args, "components");
			}
			entity["last_modified"] = Time::get_singleton()->get_unix_time_from_system();
			_project_memory["entities"] = entities;
			_save_project_memory();

			Dictionary result;
			result["ok"] = true;
			result["updated"] = true;
			result["id"] = id;
			return result;
		}
	}

	return _make_error(vformat("Entity with id '%s' not found.", id));
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: memory_delete_entity
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_memory_delete_entity(const Dictionary &p_args) const {
	_load_project_memory();

	String id = p_args.get("id", "");
	if (id.is_empty()) {
		return _make_error("'id' is required to delete an entity.");
	}

	Array entities = _project_memory.get("entities", Array());
	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		if (entity.get("id", "") == id) {
			entities.remove_at(i);
			_project_memory["entities"] = entities;
			_save_project_memory();

			Dictionary result;
			result["ok"] = true;
			result["deleted"] = true;
			result["id"] = id;
			return result;
		}
	}

	return _make_error(vformat("Entity with id '%s' not found.", id));
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: memory_get_summary
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_memory_get_summary(const Dictionary &p_args) const {
	_load_project_memory();

	Array entities = _project_memory.get("entities", Array());

	Dictionary type_counts;
	Dictionary tag_counts;
	int64_t now = Time::get_singleton()->get_unix_time_from_system();
	int recent_count = 0; // Modified in last 24 hours

	for (int i = 0; i < entities.size(); i++) {
		Dictionary entity = entities[i];
		String type = entity.get("type", "unknown");
		type_counts[type] = int(type_counts.get(type, 0)) + 1;

		Array tags = entity.get("tags", Array());
		for (int t = 0; t < tags.size(); t++) {
			String tag = tags[t];
			tag_counts[tag] = int(tag_counts.get(tag, 0)) + 1;
		}

		int64_t last_mod = entity.get("last_modified", 0);
		if (now - last_mod < 86400) {
			recent_count++;
		}
	}

	Dictionary result;
	result["ok"] = true;
	result["total_entities"] = entities.size();
	result["recent_entities_24h"] = recent_count;
	result["type_counts"] = type_counts;
	result["tag_counts"] = tag_counts;
	result["memory_version"] = _project_memory.get("version", 1);
	result["last_updated"] = _project_memory.get("last_updated", 0);
	return result;
}
