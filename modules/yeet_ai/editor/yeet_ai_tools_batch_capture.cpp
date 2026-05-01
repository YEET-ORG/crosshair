/**************************************************************************/
/*  yeet_ai_tools_batch_capture.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/templates/hash_set.h"
#include "editor/editor_interface.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"

// Forward declarations for helpers defined in yeet_ai_dock.cpp.
Array coerce_json_array_from_variant(const Variant &p_v);
Dictionary coerce_json_dictionary_from_variant(const Variant &p_v);

// ═══════════════════════════════════════════════════════════════════════════
// Batch execution & editor capture
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_batch_tool_calls(const Dictionary &p_args) const {
	Dictionary result;
	Array calls;
	if (p_args.has("calls")) {
		calls = coerce_json_array_from_variant(p_args["calls"]);
	} else if (p_args.has("tool_calls")) {
		calls = coerce_json_array_from_variant(p_args["tool_calls"]);
	}
	Dictionary shared_arguments;
	if (p_args.has("shared_arguments")) {
		shared_arguments = coerce_json_dictionary_from_variant(p_args["shared_arguments"]);
	}
	// Default false so that a failed read (e.g. file not yet created) does not
	// abort subsequent write steps in the same batch.  Pass stop_on_error:true
	// explicitly when each call depends strictly on the previous one succeeding.
	const bool stop_on_error = bool(p_args.get("stop_on_error", false));
	const bool dry_run = bool(p_args.get("dry_run", false));
	if (calls.is_empty()) {
		result["error"] = "calls is required and must contain at least one tool call.";
		return result;
	}

	// GDScript file tools blow up JSON size; never mix them with scene/editor tools in one batch.
	{
		bool any_gdscript_tool = false;
		bool any_non_gdscript_tool = false;
		for (int i = 0; i < calls.size(); i++) {
			const Dictionary parsed = _parse_one_batch_call(calls[i], i, shared_arguments);
			if (parsed.has("error")) {
				result["error"] = parsed["error"];
				result["failed_index"] = parsed["failed_index"];
				return result;
			}
			const String tn = String(parsed["tool"]).strip_edges();
			const bool is_gd = (tn == "create_gdscript_file" || tn == "update_gdscript_file");
			if (is_gd) {
				any_gdscript_tool = true;
			} else {
				any_non_gdscript_tool = true;
			}
		}
		if (any_gdscript_tool && any_non_gdscript_tool) {
			result["error"] =
					"batch_tool_calls cannot mix `create_gdscript_file` or `update_gdscript_file` with any other tool in one batch. "
					"Use one tool_call round with only scene/editor tools (inputs, nodes, meshes, attach_script, etc.), then a separate tool_call round whose batch contains only `create_gdscript_file` and/or `update_gdscript_file` calls.";
			return result;
		}
	}

	if (dry_run) {
		Array planned;
		for (int i = 0; i < calls.size(); i++) {
			const Dictionary parsed = _parse_one_batch_call(calls[i], i, shared_arguments);
			if (parsed.has("error")) {
				result["error"] = parsed["error"];
				result["failed_index"] = parsed["failed_index"];
				return result;
			}
			Dictionary entry;
			entry["tool"] = parsed["tool"];
			entry["arguments"] = parsed["arguments"];
			planned.push_back(entry);
		}
		result["dry_run"] = true;
		result["planned_calls"] = planned;
		result["planned_count"] = planned.size();
		result["ok"] = true;
		return result;
	}

	Array results;
	int executed_count = 0;
	bool had_failure = false;
	for (int i = 0; i < calls.size(); i++) {
		const Dictionary parsed = _parse_one_batch_call(calls[i], i, shared_arguments);
		if (parsed.has("error")) {
			result["error"] = parsed["error"];
			result["failed_index"] = parsed["failed_index"];
			return result;
		}
		const String tool_name = String(parsed["tool"]);
		const Dictionary call_args = parsed["arguments"];

		// Update UI to show which tool is currently running in the batch.
		const_cast<YeetAIDock *>(this)->_update_batch_progress(i + 1, calls.size(), tool_name);

		ToolExecutionResult call_result = const_cast<YeetAIDock *>(this)->_execute_tool(tool_name, call_args);
		Dictionary call_entry;
		call_entry["tool"] = tool_name;
		call_entry["arguments"] = call_args;
		call_entry["ok"] = call_result.ok && !call_result.payload.has("error");
		call_entry["result"] = call_result.payload;
		results.push_back(call_entry);
		executed_count++;
		had_failure = had_failure || !bool(call_entry["ok"]);

		if ((!call_result.ok || call_result.payload.has("error")) && stop_on_error) {
			result["ok"] = false;
			result["executed_count"] = executed_count;
			result["failed_index"] = i;
			result["results"] = results;
			return result;
		}
	}

	result["ok"] = !had_failure;
	result["executed_count"] = executed_count;
	result["results"] = results;

	// Auto-save every scene that was modified by a write tool in this batch.
	// This makes each batch self-contained: the model does not need to issue a
	// separate save_current_scene call for changes to persist on disk.
	static const char *scene_write_tools[] = {
		"add_node", "remove_node", "set_node_property",
		"add_primitive_mesh", "add_collision_shape",
		"instantiate_scene", "connect_signal",
		"create_standard_material", "assign_resource_to_property",
		"attach_script", "reparent_node", "rename_node",
		"duplicate_node", "move_child", nullptr
	};

	HashSet<String> scenes_to_save;
	for (int i = 0; i < results.size(); i++) {
		const Dictionary entry = results[i];
		const String entry_tool = String(entry.get("tool", ""));
		bool is_write = false;
		for (int k = 0; scene_write_tools[k] != nullptr; k++) {
			if (entry_tool == scene_write_tools[k]) {
				is_write = true;
				break;
			}
		}
		if (!is_write) {
			continue;
		}
		if (!bool(entry.get("ok", false))) {
			continue;
		}
		const Dictionary entry_result = entry.get("result", Dictionary());
		if (entry_result.has("scene_path")) {
			scenes_to_save.insert(String(entry_result["scene_path"]));
		}
	}

	if (!scenes_to_save.is_empty()) {
		EditorNode *editor_node = EditorNode::get_singleton();
		EditorInterface *editor = EditorInterface::get_singleton();
		if (editor_node != nullptr && editor != nullptr) {
			// Build the actually-savable subset: only scenes that are currently
			// open in the editor. EditorNode::save_scene_list looks them up by
			// scene index — no async scene switching, no clobbering whatever the
			// user is editing right now.
			HashSet<String> savable;
			Array saved_paths;
			const TypedArray<Node> open_roots = editor->get_open_scene_roots();
			for (const String &scene_path : scenes_to_save) {
				for (int r = 0; r < open_roots.size(); r++) {
					Node *root = Object::cast_to<Node>(open_roots[r]);
					if (root != nullptr && root->get_scene_file_path() == scene_path) {
						savable.insert(scene_path);
						saved_paths.push_back(scene_path);
						break;
					}
				}
			}
			if (!savable.is_empty()) {
				editor_node->save_scene_list(savable);
			}
			result["auto_saved_scenes"] = saved_paths;
		}
	}

	return result;
}

Dictionary YeetAIDock::_tool_get_editor_log(const Dictionary &p_args) const {
	Dictionary result;
	EditorLog *log = EditorNode::get_log();
	if (log == nullptr) {
		result["error"] = "Editor log is unavailable.";
		return result;
	}
	const int max_lines = CLAMP(int(p_args.get("max_lines", 200)), 1, 2000);
	const Array messages = log->get_recent_messages(max_lines);
	result["messages"] = messages;
	result["count"] = messages.size();
	return result;
}

Dictionary YeetAIDock::_tool_editor_undo(const Dictionary &p_args) const {
	Dictionary result;
	EditorInterface *ei = EditorInterface::get_singleton();
	if (ei == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	EditorUndoRedoManager *urm = ei->get_editor_undo_redo();
	if (urm == nullptr) {
		result["error"] = "EditorUndoRedoManager is unavailable.";
		return result;
	}

	const int steps = CLAMP(int(p_args.get("steps", 1)), 1, 50);
	int undone = 0;
	for (int i = 0; i < steps; i++) {
		if (!urm->undo()) {
			break;
		}
		undone++;
	}

	result["undone_steps"] = undone;
	result["requested_steps"] = steps;
	return result;
}
