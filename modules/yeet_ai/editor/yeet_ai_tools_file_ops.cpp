/**************************************************************************/
/*  yeet_ai_tools_file_ops.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/editor_interface.h"
#include "modules/gdscript/gdscript.h"
#include "scene/resources/packed_scene.h"

constexpr int MAX_FILE_WRITE_BYTES = 512 * 1024;

// Helper: validate a GDScript file by attempting to parse it.
// Returns an empty string if valid, otherwise returns the first parse error.
static String _validate_gdscript_file(const String &p_path) {
	Ref<GDScript> script;
	script.instantiate();
	Error load_err = script->load_source_code(p_path);
	if (load_err != OK) {
		return vformat("Failed to load source code for validation: %d", load_err);
	}
	script->set_path(p_path, true);
	script->reload(true);
	if (!script->is_valid()) {
		String err = script->get_script_path_invalid_error();
		if (!err.is_empty()) {
			return err;
		}
		return "Script has parse errors (unknown)";
	}
	return String();
}

Dictionary YeetAIDock::_tool_create_scene_file(const Dictionary &p_args) const {
	Dictionary result;
	const String scene_path = p_args.get("scene_path", "");
	const String root_type = p_args.get("root_type", "Node3D");
	String root_name = p_args.get("root_name", scene_path.get_file().get_basename());

	if (!scene_path.begins_with("res://") || scene_path.get_extension().is_empty()) {
		result["error"] = "scene_path must be a res:// path with a scene extension.";
		return result;
	}
	if (!ClassDB::class_exists(root_type) || !ClassDB::can_instantiate(root_type) || ClassDB::is_virtual(root_type) || !ClassDB::is_parent_class(root_type, "Node")) {
		result["error"] = "root_type must be an instantiable Node type.";
		return result;
	}
	if (FileAccess::exists(scene_path) && !bool(p_args.get("overwrite", false))) {
		result["error"] = "scene_path already exists. Pass overwrite=true to replace it.";
		return result;
	}

	const Error dir_error = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(scene_path.get_base_dir()));
	if (dir_error != OK) {
		result["error"] = vformat("Failed to create scene directory: %d", dir_error);
		return result;
	}

	Node *root = Object::cast_to<Node>(ClassDB::instantiate(root_type));
	if (root == nullptr) {
		result["error"] = "Failed to instantiate root_type.";
		return result;
	}
	if (root_name.is_empty()) {
		root_name = "Root";
	}
	root->set_name(root_name);

	Ref<PackedScene> packed_scene;
	packed_scene.instantiate();
	const Error pack_error = packed_scene->pack(root);
	if (pack_error != OK) {
		memdelete(root);
		root = nullptr;
		result["error"] = vformat("Failed to pack scene: %d", pack_error);
		return result;
	}

	const Error save_error = ResourceSaver::save(packed_scene, scene_path, ResourceSaver::FLAG_CHANGE_PATH);
	if (root != nullptr) {
		memdelete(root);
	}
	if (save_error != OK) {
		result["error"] = vformat("Failed to save scene: %d", save_error);
		return result;
	}

	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->update_file(scene_path);
	}

	if (bool(p_args.get("open", true))) {
		if (editor != nullptr) {
			editor->open_scene_from_path(scene_path);
		}
	}

	result["scene_path"] = scene_path;
	result["root_type"] = root_type;
	result["root_name"] = root_name;
	return result;
}

Dictionary YeetAIDock::_tool_create_gdscript_file(const Dictionary &p_args) const {
	Dictionary result;
	const String script_path = p_args.get("script_path", "");
	const String contents = p_args.get("contents", "");
	if (!script_path.begins_with("res://") || script_path.get_extension().to_lower() != "gd") {
		result["error"] = "script_path must be a res:// path ending in .gd";
		return result;
	}
	if (FileAccess::exists(script_path) && !bool(p_args.get("overwrite", false))) {
		result["error"] = "script_path already exists. Pass overwrite=true to replace it.";
		return result;
	}

	const Error dir_error = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(script_path.get_base_dir()));
	if (dir_error != OK) {
		result["error"] = vformat("Failed to create script directory: %d", dir_error);
		return result;
	}

	Ref<FileAccess> file = FileAccess::open(script_path, FileAccess::WRITE);
	if (file.is_null()) {
		result["error"] = "Failed to open script_path for writing.";
		return result;
	}
	file->store_string(contents);
	file.unref();

	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->update_file(script_path);
	}

	// Validate the written script for parse errors.
	String parse_error = _validate_gdscript_file(script_path);
	if (!parse_error.is_empty()) {
		// Attempt auto-fix up to 3 times.
		static constexpr int MAX_AUTO_FIX_ATTEMPTS = 3;
		bool fixed = false;
		String fixed_contents;
		for (int attempt = 0; attempt < MAX_AUTO_FIX_ATTEMPTS; attempt++) {
			if (!_attempt_gdscript_auto_fix(script_path, parse_error, fixed_contents)) {
				break; // No heuristic fix applied
			}
			String new_error = _validate_gdscript_file(script_path);
			if (new_error.is_empty()) {
				fixed = true;
				break;
			}
			parse_error = new_error;
		}

		if (fixed) {
			result["script_path"] = script_path;
			result["bytes_written"] = fixed_contents.to_utf8_buffer().size();
			result["auto_fixed"] = true;
			result["auto_fix_attempts"] = MAX_AUTO_FIX_ATTEMPTS;
			result["ok"] = true;
			return result;
		}

		result["script_path"] = script_path;
		result["bytes_written"] = contents.to_utf8_buffer().size();
		result["parse_error"] = parse_error;
		result["warning"] = "Script was written but has parse errors. Auto-fix was attempted but could not resolve all issues. Fix the error and call update_gdscript_file again.";
		return result;
	}

	result["script_path"] = script_path;
	result["bytes_written"] = contents.to_utf8_buffer().size();
	result["ok"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_update_gdscript_file(const Dictionary &p_args) const {
	Dictionary result;
	const String script_path = p_args.get("script_path", "");
	const String contents = p_args.get("contents", "");
	const String operation = String(p_args.get("operation", "replace")).to_lower();
	if (!script_path.begins_with("res://") || script_path.get_extension().to_lower() != "gd") {
		result["error"] = "script_path must be a res:// path ending in .gd";
		return result;
	}
	if (contents.is_empty() && operation != "replace") {
		result["error"] = "contents is required for non-empty script updates.";
		return result;
	}

	const bool exists = FileAccess::exists(script_path);
	if (!exists && !bool(p_args.get("create_if_missing", false))) {
		result["error"] = "script_path does not exist. Pass create_if_missing=true to create it.";
		return result;
	}

	String existing_contents;
	if (exists) {
		Error read_error = OK;
		existing_contents = FileAccess::get_file_as_string(script_path, &read_error);
		if (read_error != OK) {
			result["error"] = vformat("Failed to read existing script: %d", read_error);
			return result;
		}
	}

	String final_contents;
	if (operation == "replace") {
		final_contents = contents;
	} else if (operation == "append") {
		final_contents = existing_contents + contents;
	} else if (operation == "prepend") {
		final_contents = contents + existing_contents;
	} else {
		result["error"] = "operation must be one of: replace, append, prepend.";
		return result;
	}

	const Error dir_error = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(script_path.get_base_dir()));
	if (dir_error != OK) {
		result["error"] = vformat("Failed to create script directory: %d", dir_error);
		return result;
	}

	Ref<FileAccess> file = FileAccess::open(script_path, FileAccess::WRITE);
	if (file.is_null()) {
		result["error"] = "Failed to open script_path for writing.";
		return result;
	}
	file->store_string(final_contents);
	file.unref();

	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->update_file(script_path);
	}

	// Validate the updated script for parse errors.
	String parse_error = _validate_gdscript_file(script_path);
	if (!parse_error.is_empty()) {
		// Attempt auto-fix up to 3 times.
		static constexpr int MAX_AUTO_FIX_ATTEMPTS = 3;
		bool fixed = false;
		String fixed_contents;
		for (int attempt = 0; attempt < MAX_AUTO_FIX_ATTEMPTS; attempt++) {
			if (!_attempt_gdscript_auto_fix(script_path, parse_error, fixed_contents)) {
				break; // No heuristic fix applied
			}
			String new_error = _validate_gdscript_file(script_path);
			if (new_error.is_empty()) {
				fixed = true;
				break;
			}
			parse_error = new_error;
		}

		if (fixed) {
			result["script_path"] = script_path;
			result["operation"] = operation;
			result["created"] = !exists;
			result["bytes_written"] = fixed_contents.to_utf8_buffer().size();
			result["auto_fixed"] = true;
			result["auto_fix_attempts"] = MAX_AUTO_FIX_ATTEMPTS;
			result["ok"] = true;
			return result;
		}

		result["script_path"] = script_path;
		result["operation"] = operation;
		result["created"] = !exists;
		result["bytes_written"] = final_contents.to_utf8_buffer().size();
		result["parse_error"] = parse_error;
		result["warning"] = "Script was updated but has parse errors. Auto-fix was attempted but could not resolve all issues. Fix the error and call update_gdscript_file again.";
		return result;
	}

	result["script_path"] = script_path;
	result["operation"] = operation;
	result["created"] = !exists;
	result["bytes_written"] = final_contents.to_utf8_buffer().size();
	result["ok"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_write_project_file(const Dictionary &p_args) const {
	Dictionary result;
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		result["error"] = "Path must start with res://";
		return result;
	}
	if (!_is_allowed_text_file(path)) {
		result["error"] = "Only safe text project extensions are allowed (same allowlist as read_project_file).";
		return result;
	}
	if (!p_args.has("contents")) {
		result["error"] = "contents is required.";
		return result;
	}
	const String contents = String(p_args["contents"]);
	if (contents.length() > MAX_FILE_WRITE_BYTES) {
		result["error"] = vformat("contents exceeds max length (%d bytes).", MAX_FILE_WRITE_BYTES);
		return result;
	}
	if (FileAccess::exists(path) && !bool(p_args.get("overwrite", false))) {
		result["error"] = "File exists. Pass overwrite=true to replace it.";
		return result;
	}

	const Error dir_error = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(path.get_base_dir()));
	if (dir_error != OK) {
		result["error"] = vformat("Failed to create parent directory: %d", dir_error);
		return result;
	}

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		result["error"] = "Failed to open file for writing.";
		return result;
	}
	file->store_string(contents);
	file->flush();

	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->update_file(path);
	}

	result["path"] = path;
	result["bytes_written"] = contents.length();
	return result;
}

Dictionary YeetAIDock::_tool_create_project_folder(const Dictionary &p_args) const {
	Dictionary result;
	const String folder_path = p_args.get("path", "");
	if (!folder_path.begins_with("res://")) {
		result["error"] = "path must start with res://";
		return result;
	}
	const Error err = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(folder_path));
	if (err != OK) {
		result["error"] = vformat("make_dir_recursive failed: %d", err);
		return result;
	}
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->scan();
	}
	result["path"] = folder_path;
	return result;
}

Dictionary YeetAIDock::_tool_delete_project_file(const Dictionary &p_args) const {
	Dictionary result;
	if (!bool(p_args.get("confirm", false))) {
		result["error"] = "Refusing to delete without confirm:true.";
		return result;
	}
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		result["error"] = "path must start with res://";
		return result;
	}
	if (!_is_allowed_text_file(path)) {
		result["error"] = "Only the same safe extensions as write_project_file are allowed.";
		return result;
	}
	if (!FileAccess::exists(path)) {
		result["error"] = "File does not exist.";
		return result;
	}

	Ref<DirAccess> da = DirAccess::open(path.get_base_dir());
	if (da.is_null()) {
		result["error"] = "Cannot open parent directory.";
		return result;
	}
	const Error err = da->remove(path.get_file());
	if (err != OK) {
		result["error"] = vformat("remove failed: %d", err);
		return result;
	}

	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->update_file(path);
	}

	result["path"] = path;
	result["deleted"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_move_project_file(const Dictionary &p_args) const {
	Dictionary result;
	const String from_path = p_args.get("from_path", "");
	const String to_path = p_args.get("to_path", "");
	if (!from_path.begins_with("res://") || !to_path.begins_with("res://")) {
		result["error"] = "from_path and to_path must start with res://";
		return result;
	}
	if (from_path == to_path) {
		result["error"] = "from_path and to_path must differ.";
		return result;
	}
	if (!_is_allowed_text_file(from_path) || !_is_allowed_text_file(to_path)) {
		result["error"] = "Only safe text/resource extensions are allowed (same allowlist as write_project_file).";
		return result;
	}
	if (!FileAccess::exists(from_path)) {
		result["error"] = "Source file does not exist.";
		return result;
	}
	if (FileAccess::exists(to_path) || DirAccess::exists(to_path)) {
		result["error"] = "Target path already exists.";
		return result;
	}

	const Error mkdir_err = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(to_path.get_base_dir()));
	if (mkdir_err != OK) {
		result["error"] = vformat("Failed to create target directory: %d", mkdir_err);
		return result;
	}

	const String from_abs = ProjectSettings::get_singleton()->globalize_path(from_path);
	const String to_abs = ProjectSettings::get_singleton()->globalize_path(to_path);
	const Error err = DirAccess::rename_absolute(from_abs, to_abs);
	if (err != OK) {
		result["error"] = vformat("rename_absolute failed: %d", err);
		return result;
	}

	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->scan();
	}

	result["from_path"] = from_path;
	result["to_path"] = to_path;
	result["moved"] = true;
	return result;
}
