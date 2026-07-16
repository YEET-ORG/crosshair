/**************************************************************************/
/*  yeet_ai_tool_schema.cpp                                               */
/**************************************************************************/
/*  Tool schema implementation with validation, auto-resolution, and       */
/*  context management for improved efficiency and robustness.             */
/**************************************************************************/

#include "yeet_ai_tool_schema.h"
#include "yeet_ai_dock.h"

// Helper to check if a path looks like a proper node path
bool YeetAIToolSchemaRegistry::is_valid_node_path(const String &p_path) {
	if (p_path.is_empty()) return false;

	// Paths starting with "/" or "." are already proper node paths
	if (p_path[0] == '/' || p_path[0] == '.') {
		return true;
	}

	// Paths with ".." or multiple slashes need validation
	if (p_path.find("..") != -1 || p_path.find("//") != -1) {
		return false;
	}

	// Simple names could be node names that need auto-resolution
	// Return false to indicate they need resolution
	return false;
}

// Auto-resolve a bare node name to a full path
String YeetAIToolSchemaRegistry::auto_resolve_node_path(
	const String &p_node_path,
	Node *p_scene_root,
	String &r_error
) {
	if (p_scene_root == nullptr) {
		r_error = "Scene root is null, cannot auto-resolve node paths";
		return "";
	}

	if (p_node_path.is_empty()) {
		r_error = "Empty node path";
		return "";
	}

	// If already a proper path, return as-is
	if (is_valid_node_path(p_node_path)) {
		return p_node_path;
	}

	String candidate_path = p_node_path;
	const String root_name = String(p_scene_root->get_name());
	if (!root_name.is_empty()) {
		if (candidate_path == root_name) {
			return String(p_scene_root->get_path());
		}
		if (candidate_path.begins_with(root_name + "/")) {
			candidate_path = candidate_path.substr(root_name.length() + 1);
		} else {
			const int root_pos = candidate_path.find("/" + root_name + "/");
			if (root_pos >= 0) {
				candidate_path = candidate_path.substr(root_pos + root_name.length() + 2);
			}
		}
	}

	// Try exact name match first
	Node *found = p_scene_root->get_node_or_null(candidate_path);
	if (found != nullptr) {
		return String(found->get_path());
	}

	// Try finding by name anywhere in tree
	String found_path;
	Node *search_node = p_scene_root;

	// Recursive search helper (inline lambda won't work in static context,
	// so we use a simple breadth-first search)
	List<Node *> queue;
	queue.push_back(search_node);

	HashSet<Node *> visited;
	visited.insert(search_node);

	while (!queue.is_empty()) {
		Node *current = queue.front()->get();
		queue.pop_front();

		if (current == p_scene_root) continue;

		if (String(current->get_name()) == p_node_path || String(current->get_name()) == candidate_path) {
			return String(current->get_path());
		}

		for (int i = 0; i < current->get_child_count(); i++) {
			Node *child = current->get_child(i);
			if (!visited.has(child)) {
				visited.insert(child);
				queue.push_back(child);
			}
		}
	}

	r_error = vformat("Node '%s' not found in scene tree", p_node_path);
	return "";
}

// Variant types are interchangeable for schema purposes when JSON erases the distinction.
// JSON has only one numeric type, so an INT-typed schema field may receive a FLOAT
// (and vice versa) through the parser; both should validate.
static bool _types_compatible(Variant::Type p_expected, Variant::Type p_actual) {
	if (p_expected == p_actual) {
		return true;
	}
	if (p_expected == Variant::NIL) {
		return true;
	}
	if ((p_expected == Variant::INT && p_actual == Variant::FLOAT) ||
			(p_expected == Variant::FLOAT && p_actual == Variant::INT)) {
		return true;
	}
	if ((p_expected == Variant::VECTOR2 || p_expected == Variant::VECTOR3) &&
			(p_actual == Variant::DICTIONARY || p_actual == Variant::ARRAY)) {
		return true;
	}
	if (p_expected == Variant::COLOR &&
			(p_actual == Variant::DICTIONARY || p_actual == Variant::ARRAY || p_actual == Variant::STRING)) {
		return true;
	}
	// Allow PackedStringArray ↔ Array, since coercers may produce either.
	if ((p_expected == Variant::ARRAY && p_actual == Variant::PACKED_STRING_ARRAY) ||
			(p_expected == Variant::PACKED_STRING_ARRAY && p_actual == Variant::ARRAY)) {
		return true;
	}
	return false;
}

// Validate arguments against schema. Both required AND present-optional args are
// type/range/enum checked; missing optional args are silently allowed.
bool YeetAIToolSchemaRegistry::validate_arguments(
	const ToolSchema &p_schema,
	const Dictionary &p_args,
	Array &r_errors
) {
	bool valid = true;

	for (int i = 0; i < p_schema.arguments.size(); i++) {
		const ToolArgSchema &arg = p_schema.arguments[i];

		const bool present = p_args.has(arg.name);
		if (!present) {
			if (arg.required && p_schema.category != "inferred") {
				Dictionary error;
				error["type"] = "missing_required_argument";
				error["tool"] = p_schema.tool_name;
				error["argument"] = arg.name;
				error["message"] = vformat("Required argument '%s' is missing", arg.name);
				r_errors.push_back(error);
				valid = false;
			}
			continue;
		}

		const Variant value = p_args[arg.name];

		// Type checking — applies to both required and present-optional args.
		if (arg.type_hint != Variant::NIL && !_types_compatible(arg.type_hint, value.get_type())) {
			Dictionary error;
			error["type"] = "type_mismatch";
			error["tool"] = p_schema.tool_name;
			error["argument"] = arg.name;
			error["expected"] = Variant::get_type_name(arg.type_hint);
			error["actual"] = Variant::get_type_name(value.get_type());
			error["message"] = vformat("Argument '%s' expected type %s, got %s",
				arg.name, Variant::get_type_name(arg.type_hint), Variant::get_type_name(value.get_type()));
			r_errors.push_back(error);
			valid = false;
			// Skip range/enum on type mismatch — the conversions below would be meaningless.
			continue;
		}

		// Value range checking for numeric types.
		if (arg.type_hint == Variant::INT || arg.type_hint == Variant::FLOAT) {
			const float val = static_cast<float>(value);
			if (val < arg.min_value || val > arg.max_value) {
				Dictionary error;
				error["type"] = "out_of_range";
				error["tool"] = p_schema.tool_name;
				error["argument"] = arg.name;
				error["value"] = val;
				error["min"] = arg.min_value;
				error["max"] = arg.max_value;
				error["message"] = vformat("Argument '%s' value %.2f is out of range [%.2f, %.2f]",
					arg.name, val, arg.min_value, arg.max_value);
				r_errors.push_back(error);
				valid = false;
			}
		}

		// Valid values checking — only meaningful for string-like fields.
		if (!arg.valid_values.is_empty()) {
			const String str_value = String(value);
			bool found_valid = false;
			for (int v = 0; v < arg.valid_values.size(); v++) {
				if (arg.valid_values[v] == str_value) {
					found_valid = true;
					break;
				}
			}
			if (!found_valid) {
				Dictionary error;
				error["type"] = "invalid_value";
				error["tool"] = p_schema.tool_name;
				error["argument"] = arg.name;
				error["value"] = str_value;
				error["valid_values"] = arg.valid_values;
				error["message"] = vformat("Argument '%s' value '%s' is not valid. Allowed values: %s",
					arg.name, str_value, String(", ").join(arg.valid_values));
				r_errors.push_back(error);
				valid = false;
			}
		}
	}

	return valid;
}

// Build a readable description from a tool name when no explicit one exists.
// Maps the leading verb to a phrase and normalizes _2d/_3d suffixes, so e.g.
// "create_rigid_body_2d" -> "Create a rigid body (2D) node." This makes the
// auto-generated tool definitions much clearer to the model than a bare
// title-cased name, across every tool that lacks a hand-written schema.
static String _infer_tool_description(const String &p_name) {
	const String s = p_name.to_lower();

	struct VerbPhrase {
		const char *prefix;
		const char *verb;
		bool node_creator;
	};
	static const VerbPhrase verbs[] = {
		{ "create_", "Create a", true },
		{ "scaffold_", "Scaffold a ready-made", false },
		{ "instantiate_", "Instantiate the", false },
		{ "duplicate_", "Duplicate the", false },
		{ "add_", "Add a", false },
		{ "set_", "Set the", false },
		{ "update_", "Update the", false },
		{ "patch_", "Patch the", false },
		{ "assign_", "Assign a", false },
		{ "attach_", "Attach a", false },
		{ "connect_", "Connect the", false },
		{ "disconnect_", "Disconnect the", false },
		{ "reparent_", "Reparent the", false },
		{ "rename_", "Rename the", false },
		{ "remove_", "Remove the", false },
		{ "delete_", "Delete the", false },
		{ "clear_", "Clear the", false },
		{ "get_", "Get / inspect the", false },
		{ "find_", "Find", false },
		{ "list_", "List the", false },
		{ "search_", "Search", false },
		{ "inspect_", "Inspect the", false },
		{ "validate_", "Validate the", false },
		{ "audit_", "Audit the", false },
		{ "repair_", "Repair the", false },
		{ "capture_", "Capture the", false },
		{ "read_", "Read the", false },
		{ "write_", "Write the", false },
		{ "copy_", "Copy the", false },
		{ "move_", "Move the", false },
		{ "save_", "Save the", false },
		{ "open_", "Open the", false },
		{ "reload_", "Reload the", false },
		{ "select_", "Select the", false },
		{ "focus_", "Focus the", false },
		{ "import_", "Import the", false },
		{ "bake_", "Bake the", false },
		{ "fill_", "Fill the", false },
		{ "paint_", "Paint the", false },
		{ "play_", "Play the", false },
		{ "stop_", "Stop the", false },
		{ "run_", "Run the", false },
		{ "batch_", "Batch", false },
		{ "manage_", "Manage", false },
		{ "resolve_", "Resolve the", false },
		{ "scan_", "Scan the", false },
		{ "lint_", "Lint the", false },
		{ "memory_", "Project memory:", false },
		{ "git_", "Git:", false },
		{ "runtime_", "On the running game, control the", false },
	};

	for (const VerbPhrase &v : verbs) {
		const String prefix = v.prefix;
		if (!s.begins_with(prefix)) {
			continue;
		}
		String rest = s.substr(prefix.length());
		String dim;
		if (rest.ends_with("_2d")) {
			rest = rest.substr(0, rest.length() - 3);
			dim = " (2D)";
		} else if (rest.ends_with("_3d")) {
			rest = rest.substr(0, rest.length() - 3);
			dim = " (3D)";
		}
		const String object = rest.replace("_", " ").strip_edges();
		String out = String(v.verb) + " " + object + dim;
		if (v.node_creator && !object.contains("file") && !object.contains("folder") &&
				!object.contains("material") && !object.contains("resource") &&
				!object.contains("texture") && !object.contains("theme") &&
				!object.contains("gradient") && !object.contains("curve") &&
				!object.contains("font") && !object.contains("tileset") &&
				!object.contains("animation") && !object.contains("shader")) {
			out += " node";
		}
		return out.strip_edges() + ".";
	}

	// Fallback: title-cased name.
	return p_name.replace("_", " ").capitalize() + ".";
}

// Generate a fallback schema for tools without explicit definitions.
// This keeps the native tools payload comprehensive even when we haven't
// hand-written every schema.
static ToolSchema _infer_schema(const String &p_tool_name) {
	ToolSchema schema;
	schema.tool_name = p_tool_name;
	schema.category = "inferred";
	schema.description = _infer_tool_description(p_tool_name);
	schema.requires_scene = false;
	schema.is_write_operation = false;
	schema.supports_batching = false;

	const String s = p_tool_name.to_lower();

	// Inspection tools (get_*)
	if (s.begins_with("get_")) {
		schema.requires_scene = s.contains("node") || s.contains("scene") || s.contains("collision") || s.contains("navigation") || s.contains("tilemap") || s.contains("world") || s.contains("environment");
		schema.is_write_operation = false;

		// Node-path based inspection
		if (s.contains("node") || s.contains("scene_tree") || s.contains("collision") || s.contains("navigation") || s.contains("tilemap") || s.contains("world") || s.contains("environment") || s.contains("animation") || s.contains("shader")) {
			ToolArgSchema arg_path;
			arg_path.name = "node_path";
			arg_path.type_hint = Variant::STRING;
			arg_path.required = !s.contains("scene_tree") && !s.contains("editor") && !s.contains("current") && !s.contains("open") && !s.contains("selected") && !s.contains("project_") && !s.contains("global") && !s.contains("input") && !s.contains("audio") && !s.contains("network") && !s.contains("console") && !s.contains("debug") && !s.contains("runtime");
			arg_path.auto_resolve_node_path = true;
			arg_path.description = "Node path or name (auto-resolved)";
			schema.arguments.push_back(arg_path);
		}

		// Optional query/path args
		if (s.contains("project_settings") || s.contains("settings_keys")) {
			ToolArgSchema arg_query;
			arg_query.name = "query";
			arg_query.type_hint = Variant::STRING;
			arg_query.required = false;
			schema.arguments.push_back(arg_query);
		}
		if (s.contains("project_tree") || s.contains("find_")) {
			ToolArgSchema arg_root;
			arg_root.name = "root";
			arg_root.type_hint = Variant::STRING;
			arg_root.required = false;
			schema.arguments.push_back(arg_root);
		}
		if (s.contains("editor_log") || s.contains("console_output") || s.contains("gdscript_errors") || s.contains("debug_snapshot")) {
			ToolArgSchema arg_max;
			arg_max.name = s.contains("debug_snapshot") ? "max_log_lines" : "max_lines";
			arg_max.type_hint = Variant::INT;
			arg_max.required = false;
			schema.arguments.push_back(arg_max);
		}
		if (s.contains("project_settings")) {
			ToolArgSchema arg_keys;
			arg_keys.name = "keys";
			arg_keys.type_hint = Variant::ARRAY;
			arg_keys.required = false;
			schema.arguments.push_back(arg_keys);
		}
		if (s.contains("global_classes")) {
			ToolArgSchema arg_query;
			arg_query.name = "query";
			arg_query.type_hint = Variant::STRING;
			arg_query.required = false;
			schema.arguments.push_back(arg_query);
			ToolArgSchema arg_max;
			arg_max.name = "max_results";
			arg_max.type_hint = Variant::INT;
			arg_max.required = false;
			schema.arguments.push_back(arg_max);
		}
		return schema;
	}

	// Creation tools (create_*)
	if (s.begins_with("create_")) {
		schema.requires_scene = false;
		schema.is_write_operation = true;

		if (!s.contains("gradient") && !s.contains("curve") && !s.contains("stylebox") && !s.contains("theme") && !s.contains("font") && !s.contains("texture") && !s.contains("tileset") && !s.contains("scene_file") && !s.contains("sky") && !s.contains("project_folder")) {
			ToolArgSchema arg_name;
			arg_name.name = "name";
			arg_name.type_hint = Variant::STRING;
			arg_name.required = true;
			schema.arguments.push_back(arg_name);

			if (!s.contains("control_") && !s.contains("container_") && !s.contains("ui_element") && !s.contains("scroll_") && !s.contains("progress_") && !s.contains("slider") && !s.contains("item_list") && !s.contains("option_button") && !s.contains("tab_container") && !s.contains("graph_node") && !s.contains("tree_widget")) {
				ToolArgSchema arg_parent;
				arg_parent.name = "parent_path";
				arg_parent.type_hint = Variant::STRING;
				arg_parent.required = false;
				arg_parent.auto_resolve_node_path = true;
				schema.arguments.push_back(arg_parent);
			}
		}

		if (s.contains("gradient") || s.contains("curve") || s.contains("stylebox") || s.contains("theme") || s.contains("font") || s.contains("texture") || s.contains("tileset") || s.contains("scene_file")) {
			ToolArgSchema arg_path;
			arg_path.name = s.contains("scene_file") ? "scene_path" : "save_path";
			arg_path.type_hint = Variant::STRING;
			arg_path.required = true;
			schema.arguments.push_back(arg_path);
		}

		if (s.contains("scene_file")) {
			ToolArgSchema arg_type;
			arg_type.name = "root_type";
			arg_type.type_hint = Variant::STRING;
			arg_type.required = false;
			arg_type.default_value = String("Node3D");
			schema.arguments.push_back(arg_type);
		}

		// 3D position
		if (s.contains("camera_3d") || s.contains("light") || s.contains("rigid") || s.contains("static") || s.contains("character") || s.contains("area_3d") || s.contains("ray_cast_3d") || s.contains("shape_cast_3d") || s.contains("path_3d") || s.contains("vehicle") || s.contains("fog_volume") || s.contains("reflection_probe") || s.contains("gi_probe") || s.contains("particle_emitter") || s.contains("marker_3d") || s.contains("remote_transform_3d") || s.contains("visible_on_screen_notifier_3d") || s.contains("navigation_agent_3d") || s.contains("navigation_region_3d")) {
			ToolArgSchema arg_pos;
			arg_pos.name = "position";
			arg_pos.type_hint = Variant::VECTOR3;
			arg_pos.required = false;
			schema.arguments.push_back(arg_pos);
		}

		// 2D position
		if (s.contains("sprite_2d") || s.contains("animated_sprite_2d") || s.contains("rigid_body_2d") || s.contains("static_body_2d") || s.contains("character_body_2d") || s.contains("area_2d") || s.contains("ray_cast_2d") || s.contains("shape_cast_2d") || s.contains("path_2d") || s.contains("marker_2d") || s.contains("remote_transform_2d") || s.contains("visible_on_screen_notifier_2d") || s.contains("navigation_agent_2d") || s.contains("navigation_region_2d") || s.contains("collision_polygon_2d") || s.contains("audio_stream_player_2d") || s.contains("animatable_body_2d") || s.contains("parallax_layer") || s.contains("polygon_2d") || s.contains("line_2d") || s.contains("light_2d") || s.contains("cpu_particles_2d") || s.contains("gpu_particles_2d")) {
			ToolArgSchema arg_pos;
			arg_pos.name = "position";
			arg_pos.type_hint = Variant::VECTOR2;
			arg_pos.required = false;
			schema.arguments.push_back(arg_pos);
		}

		if (s.contains("camera_3d")) {
			ToolArgSchema arg_rot;
			arg_rot.name = "rotation_degrees";
			arg_rot.type_hint = Variant::FLOAT;
			arg_rot.required = false;
			schema.arguments.push_back(arg_rot);
		}

		if (s.contains("camera_2d")) {
			ToolArgSchema arg_zoom;
			arg_zoom.name = "zoom";
			arg_zoom.type_hint = Variant::VECTOR2;
			arg_zoom.required = false;
			arg_zoom.default_value = Vector2(1, 1);
			schema.arguments.push_back(arg_zoom);

			ToolArgSchema arg_current;
			arg_current.name = "make_current";
			arg_current.type_hint = Variant::BOOL;
			arg_current.required = false;
			arg_current.default_value = false;
			schema.arguments.push_back(arg_current);
		}

		if (s.contains("light") && !s.contains("light_2d")) {
			ToolArgSchema arg_type;
			arg_type.name = "light_type";
			arg_type.type_hint = Variant::STRING;
			arg_type.required = true;
			arg_type.valid_values = Vector<String>{"omni", "directional", "spot"};
			schema.arguments.push_back(arg_type);

			ToolArgSchema arg_color;
			arg_color.name = "color";
			arg_color.type_hint = Variant::COLOR;
			arg_color.required = false;
			schema.arguments.push_back(arg_color);

			ToolArgSchema arg_energy;
			arg_energy.name = "energy";
			arg_energy.type_hint = Variant::FLOAT;
			arg_energy.required = false;
			schema.arguments.push_back(arg_energy);
		}

		return schema;
	}

	// Set/Update tools
	if (s.begins_with("set_") || s.begins_with("update_") || s.begins_with("add_") || s.begins_with("patch_")) {
		schema.requires_scene = true;
		schema.is_write_operation = true;

		if (!s.contains("project_settings") && !s.contains("editor_settings") && !s.contains("import_setting") && !s.contains("default_import_presets") && !s.contains("main_scene")) {
			ToolArgSchema arg_path;
			arg_path.name = "node_path";
			arg_path.type_hint = Variant::STRING;
			arg_path.required = true;
			arg_path.auto_resolve_node_path = true;
			schema.arguments.push_back(arg_path);
		}

		if (s.contains("project_settings") || s.contains("editor_settings")) {
			ToolArgSchema arg_settings;
			arg_settings.name = "settings";
			arg_settings.type_hint = Variant::DICTIONARY;
			arg_settings.required = true;
			schema.arguments.push_back(arg_settings);
		}

		return schema;
	}

	// Scene/script operations
	if (s == "open_scene" || s == "reload_scene" || s == "set_main_scene") {
		schema.requires_scene = false;
		schema.is_write_operation = true;
		ToolArgSchema arg_path;
		arg_path.name = "scene_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		schema.arguments.push_back(arg_path);
		return schema;
	}

	if (s == "attach_script" || s == "edit_script") {
		schema.requires_scene = true;
		schema.is_write_operation = true;
		ToolArgSchema arg_path;
		arg_path.name = "node_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_path);
		ToolArgSchema arg_script;
		arg_script.name = "script_path";
		arg_script.type_hint = Variant::STRING;
		arg_script.required = true;
		schema.arguments.push_back(arg_script);
		return schema;
	}

	if (s == "create_gdscript_file" || s == "update_gdscript_file") {
		schema.requires_scene = false;
		schema.is_write_operation = true;
		ToolArgSchema arg_path;
		arg_path.name = "script_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		schema.arguments.push_back(arg_path);
		ToolArgSchema arg_contents;
		arg_contents.name = "contents";
		arg_contents.type_hint = Variant::STRING;
		arg_contents.required = true;
		schema.arguments.push_back(arg_contents);
		return schema;
	}

	if (s == "read_project_file" || s == "write_project_file" || s == "delete_project_file") {
		schema.requires_scene = false;
		schema.is_write_operation = (s != "read_project_file");
		ToolArgSchema arg_path;
		arg_path.name = "path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		schema.arguments.push_back(arg_path);
		if (s == "write_project_file") {
			ToolArgSchema arg_contents;
			arg_contents.name = "contents";
			arg_contents.type_hint = Variant::STRING;
			arg_contents.required = true;
			schema.arguments.push_back(arg_contents);
		}
		if (s == "delete_project_file") {
			ToolArgSchema arg_confirm;
			arg_confirm.name = "confirm";
			arg_confirm.type_hint = Variant::BOOL;
			arg_confirm.required = true;
			schema.arguments.push_back(arg_confirm);
		}
		return schema;
	}

	if (s == "move_project_file" || s == "copy_project_file") {
		schema.requires_scene = false;
		schema.is_write_operation = true;
		ToolArgSchema arg_from;
		arg_from.name = "from_path";
		arg_from.type_hint = Variant::STRING;
		arg_from.required = true;
		schema.arguments.push_back(arg_from);
		ToolArgSchema arg_to;
		arg_to.name = "to_path";
		arg_to.type_hint = Variant::STRING;
		arg_to.required = true;
		schema.arguments.push_back(arg_to);
		return schema;
	}

	// Git tools
	if (s.begins_with("git_")) {
		schema.requires_scene = false;
		schema.is_write_operation = false;
		if (s == "git_diff_file") {
			ToolArgSchema arg_path;
			arg_path.name = "file_path";
			arg_path.type_hint = Variant::STRING;
			arg_path.required = true;
			schema.arguments.push_back(arg_path);
		}
		if (s == "git_log") {
			ToolArgSchema arg_max;
			arg_max.name = "max_count";
			arg_max.type_hint = Variant::INT;
			arg_max.required = false;
			schema.arguments.push_back(arg_max);
		}
		return schema;
	}

	// Validation/Run tools
	if (s == "validate_scene" || s == "run_scene_script" || s == "lint_gdscript" || s == "resolve_resource_uid") {
		schema.requires_scene = false;
		schema.is_write_operation = false;
		ToolArgSchema arg_path;
		arg_path.name = s.contains("uid") ? "uid" : "path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		schema.arguments.push_back(arg_path);
		return schema;
	}

	// Debug tools
	if (s.begins_with("inspect_") || s == "set_breakpoint" || s == "debugger_continue" || s == "profile_frame" || s == "monitor_runtime_performance") {
		schema.requires_scene = true;
		schema.is_write_operation = s != "inspect_runtime_node" && s != "inspect_runtime_variable" && s != "profile_frame" && s != "monitor_runtime_performance";
		if (s == "inspect_runtime_variable") {
			ToolArgSchema arg_name;
			arg_name.name = "variable_name";
			arg_name.type_hint = Variant::STRING;
			arg_name.required = true;
			schema.arguments.push_back(arg_name);
		}
		if (s == "set_breakpoint") {
			ToolArgSchema arg_path;
			arg_path.name = "script_path";
			arg_path.type_hint = Variant::STRING;
			arg_path.required = true;
			schema.arguments.push_back(arg_path);
			ToolArgSchema arg_line;
			arg_line.name = "line";
			arg_line.type_hint = Variant::INT;
			arg_line.required = true;
			schema.arguments.push_back(arg_line);
		}
		return schema;
	}

	// Capture/viewport tools
	if (s.begins_with("capture_") || s == "capture_dual_view") {
		schema.requires_scene = true;
		schema.is_write_operation = false;
		if (s == "capture_editor_viewport" || s == "capture_game_viewport") {
			ToolArgSchema arg_max;
			arg_max.name = "max_width";
			arg_max.type_hint = Variant::INT;
			arg_max.required = false;
			schema.arguments.push_back(arg_max);
		}
		if (s == "capture_texture_resource") {
			ToolArgSchema arg_path;
			arg_path.name = "path";
			arg_path.type_hint = Variant::STRING;
			arg_path.required = true;
			arg_path.description = "Texture resource path under res:// to capture for visual inspection.";
			schema.arguments.push_back(arg_path);

			ToolArgSchema arg_max;
			arg_max.name = "max_width";
			arg_max.type_hint = Variant::INT;
			arg_max.required = false;
			arg_max.default_value = 640;
			arg_max.description = "Maximum encoded PNG width. Use smaller values to reduce token/image size.";
			schema.arguments.push_back(arg_max);
		}
		if (s == "capture_subviewport") {
			ToolArgSchema arg_path;
			arg_path.name = "node_path";
			arg_path.type_hint = Variant::STRING;
			arg_path.required = true;
			arg_path.auto_resolve_node_path = true;
			schema.arguments.push_back(arg_path);

			ToolArgSchema arg_max;
			arg_max.name = "max_width";
			arg_max.type_hint = Variant::INT;
			arg_max.required = false;
			arg_max.default_value = 640;
			arg_max.description = "Maximum encoded PNG width. Use smaller values to reduce token/image size.";
			schema.arguments.push_back(arg_max);
		}
		return schema;
	}

	// Default: no args
	return schema;
}

// Get schema for a tool (returns inferred schema if no explicit schema defined)
const ToolSchema* YeetAIToolSchemaRegistry::get_schema(const String &p_tool_name) {
	const HashMap<String, ToolSchema> &all_schemas = get_all_schemas();
	const ToolSchema *schema = all_schemas.getptr(p_tool_name);
	if (schema != nullptr) {
		return schema;
	}

	// Return a thread-local inferred schema so callers can still get validation.
	static thread_local HashMap<String, ToolSchema> inferred_cache;
	ToolSchema *cached = inferred_cache.getptr(p_tool_name);
	if (cached != nullptr) {
		return cached;
	}

	inferred_cache[p_tool_name] = _infer_schema(p_tool_name);
	return inferred_cache.getptr(p_tool_name);
}

// Define all tool schemas here
const HashMap<String, ToolSchema> &YeetAIToolSchemaRegistry::get_all_schemas() {
	static bool initialized = false;
	static HashMap<String, ToolSchema> schemas;

	if (initialized) return schemas;

	// === CORE TOOLS ===

	{
		ToolSchema schema;
		schema.tool_name = "add_node";
		schema.description = "Add a new node to the scene tree";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "node_name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = true;
		arg_name.description = "Name for the new node";
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_type;
		arg_type.name = "node_type";
		arg_type.type_hint = Variant::STRING;
		arg_type.required = true;
		arg_type.description = "Godot class type (e.g., 'Node3D', 'Sprite2D')";
		schema.arguments.push_back(arg_type);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.description = "Parent node path or name (auto-resolved)";
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		schemas["add_node"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "set_node_property";
		schema.description = "Set a property on an existing node";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_path;
		arg_path.name = "node_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Node path or name (auto-resolved)";
		arg_path.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_path);

		ToolArgSchema arg_prop;
		arg_prop.name = "property";
		arg_prop.type_hint = Variant::STRING;
		arg_prop.required = true;
		arg_prop.description = "Property name to set";
		schema.arguments.push_back(arg_prop);

		ToolArgSchema arg_val;
		arg_val.name = "value";
		arg_val.type_hint = Variant::NIL; // Can be any type
		arg_val.required = true;
		arg_val.description = "Value to set";
		schema.arguments.push_back(arg_val);

		schemas["set_node_property"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "get_node_details";
		schema.description = "Get detailed information about a specific node";
		schema.requires_scene = true;
		schema.is_write_operation = false;

		ToolArgSchema arg_path;
		arg_path.name = "node_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Node path or name (auto-resolved)";
		arg_path.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_path);

		schemas["get_node_details"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "get_scene_tree";
		schema.description = "Get the full scene tree";
		schema.requires_scene = true;
		schema.is_write_operation = false;

		schemas["get_scene_tree"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "remove_node";
		schema.description = "Remove a node from the scene";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_path;
		arg_path.name = "node_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Node path or name (auto-resolved)";
		arg_path.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_path);

		schemas["remove_node"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "instantiate_scene";
		schema.description = "Instantiate a scene file and add it to the current scene";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_scene;
		arg_scene.name = "packed_scene_path";
		arg_scene.type_hint = Variant::STRING;
		arg_scene.required = true;
		arg_scene.description = "Path to the scene file (e.g., 'res://scenes/player.tscn')";
		schema.arguments.push_back(arg_scene);

		ToolArgSchema arg_name;
		arg_name.name = "node_name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String();
		arg_name.description = "Name for the instance (optional)";
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.description = "Parent node path or name (auto-resolved)";
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		schemas["instantiate_scene"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_scene_file";
		schema.description = "Create a new scene file with a root node";
		schema.requires_scene = false;
		schema.is_write_operation = true;

		ToolArgSchema arg_path;
		arg_path.name = "scene_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Path to save the scene file";
		schema.arguments.push_back(arg_path);

		ToolArgSchema arg_type;
		arg_type.name = "root_type";
		arg_type.type_hint = Variant::STRING;
		arg_type.required = false;
		arg_type.default_value = String("Node3D");
		arg_type.description = "Type of root node (default: 'Node3D')";
		schema.arguments.push_back(arg_type);

		schemas["create_scene_file"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "batch_tool_calls";
		schema.description = "Execute multiple tool calls in a single batch";
		schema.requires_scene = false;
		schema.is_write_operation = false;
		schema.supports_batching = false; // Special tool

		ToolArgSchema arg_calls;
		arg_calls.name = "calls";
		arg_calls.type_hint = Variant::ARRAY;
		arg_calls.required = true;
		arg_calls.description = "Array of tool call dictionaries: [{tool:string, arguments:object}]";
		schema.arguments.push_back(arg_calls);

		ToolArgSchema arg_shared;
		arg_shared.name = "shared_arguments";
		arg_shared.type_hint = Variant::DICTIONARY;
		arg_shared.required = false;
		arg_shared.description = "Arguments merged into every call; call-specific keys win";
		schema.arguments.push_back(arg_shared);

		ToolArgSchema arg_stop;
		arg_stop.name = "stop_on_error";
		arg_stop.type_hint = Variant::BOOL;
		arg_stop.required = false;
		arg_stop.description = "Stop executing later calls after the first failed call";
		schema.arguments.push_back(arg_stop);

		ToolArgSchema arg_dry;
		arg_dry.name = "dry_run";
		arg_dry.type_hint = Variant::BOOL;
		arg_dry.required = false;
		arg_dry.description = "Preview normalized calls without executing them";
		schema.arguments.push_back(arg_dry);

		schemas["batch_tool_calls"] = schema;
	}

	// === FILE OPERATIONS ===

	{
		ToolSchema schema;
		schema.tool_name = "create_gdscript_file";
		schema.description = "Create a new GDScript file";
		schema.requires_scene = false;
		schema.is_write_operation = true;

		ToolArgSchema arg_path;
		arg_path.name = "script_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Path to save the script";
		schema.arguments.push_back(arg_path);

		ToolArgSchema arg_contents;
		arg_contents.name = "contents";
		arg_contents.type_hint = Variant::STRING;
		arg_contents.required = true;
		arg_contents.description = "Script contents";
		schema.arguments.push_back(arg_contents);

		schemas["create_gdscript_file"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "update_gdscript_file";
		schema.description = "Update an existing GDScript file";
		schema.requires_scene = false;
		schema.is_write_operation = true;

		ToolArgSchema arg_path;
		arg_path.name = "script_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Path to the script file";
		schema.arguments.push_back(arg_path);

		ToolArgSchema arg_contents;
		arg_contents.name = "contents";
		arg_contents.type_hint = Variant::STRING;
		arg_contents.required = true;
		arg_contents.description = "New contents";
		schema.arguments.push_back(arg_contents);

		schemas["update_gdscript_file"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "read_project_file";
		schema.description = "Read a text file from the project";
		schema.requires_scene = false;
		schema.is_write_operation = false;

		ToolArgSchema arg_path;
		arg_path.name = "path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Path to the file";
		schema.arguments.push_back(arg_path);

		schemas["read_project_file"] = schema;
	}

	// === NODE OPERATIONS ===

	{
		ToolSchema schema;
		schema.tool_name = "rename_node";
		schema.description = "Rename a node in the scene";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_old;
		arg_old.name = "node_path";
		arg_old.type_hint = Variant::STRING;
		arg_old.required = true;
		arg_old.description = "Current node path or name (auto-resolved)";
		arg_old.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_old);

		ToolArgSchema arg_new;
		arg_new.name = "new_name";
		arg_new.type_hint = Variant::STRING;
		arg_new.required = true;
		arg_new.description = "New node name";
		schema.arguments.push_back(arg_new);

		schemas["rename_node"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "duplicate_node";
		schema.description = "Duplicate a node in the scene";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_path;
		arg_path.name = "node_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Node path or name to duplicate (auto-resolved)";
		arg_path.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_path);

		ToolArgSchema arg_name;
		arg_name.name = "new_name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String();
		arg_name.description = "Name for the duplicate (optional)";
		schema.arguments.push_back(arg_name);

		schemas["duplicate_node"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "reparent_node";
		schema.description = "Change the parent of a node";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_node;
		arg_node.name = "node_path";
		arg_node.type_hint = Variant::STRING;
		arg_node.required = true;
		arg_node.description = "Node to reparent (auto-resolved)";
		arg_node.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_node);

		ToolArgSchema arg_parent;
		arg_parent.name = "new_parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = true;
		arg_parent.description = "New parent path or name (auto-resolved)";
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		schemas["reparent_node"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "move_child";
		schema.description = "Move a node to a different index among its siblings";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_node;
		arg_node.name = "node_path";
		arg_node.type_hint = Variant::STRING;
		arg_node.required = true;
		arg_node.description = "Node to move (auto-resolved)";
		arg_node.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_node);

		ToolArgSchema arg_index;
		arg_index.name = "new_index";
		arg_index.type_hint = Variant::INT;
		arg_index.required = true;
		arg_index.description = "New index position";
		schema.arguments.push_back(arg_index);

		schemas["move_child"] = schema;
	}

	// === 3D TOOLS ===

	{
		ToolSchema schema;
		schema.tool_name = "add_primitive_mesh";
		schema.description = "Create a MeshInstance3D with a primitive mesh";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_name;
		arg_name.name = "node_name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = true;
		arg_name.description = "Name for the MeshInstance3D";
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_type;
		arg_type.name = "mesh_type";
		arg_type.type_hint = Variant::STRING;
		arg_type.required = true;
		arg_type.description = "Mesh type";
		arg_type.valid_values = Vector<String>{"box", "capsule", "cylinder", "plane", "sphere"};
		schema.arguments.push_back(arg_type);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.description = "Parent node path or name (auto-resolved)";
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_params;
		arg_params.name = "parameters";
		arg_params.type_hint = Variant::DICTIONARY;
		arg_params.required = false;
		arg_params.description = "Mesh parameters such as {size:{x,y,z}}, {radius:number}, or {height:number}";
		schema.arguments.push_back(arg_params);

		schemas["add_primitive_mesh"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "add_collision_shape";
		schema.description = "Create a CollisionShape3D under a physics body";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_name;
		arg_name.name = "node_name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = true;
		arg_name.description = "Name for the CollisionShape3D";
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_type;
		arg_type.name = "shape_type";
		arg_type.type_hint = Variant::STRING;
		arg_type.required = true;
		arg_type.description = "Collision shape type";
		arg_type.valid_values = Vector<String>{"box", "sphere", "capsule", "cylinder", "terrain"};
		schema.arguments.push_back(arg_type);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.description = "Parent physics body path or name (auto-resolved)";
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_params;
		arg_params.name = "parameters";
		arg_params.type_hint = Variant::DICTIONARY;
		arg_params.required = false;
		arg_params.description = "Shape parameters such as {size:{x,y,z}}, {radius:number}, or {height:number}";
		schema.arguments.push_back(arg_params);

		schemas["add_collision_shape"] = schema;
	}

	// === INPUT ACTIONS ===

	{
		ToolSchema schema;
		schema.tool_name = "create_input_action";
		schema.description = "Create a new input action";
		schema.requires_scene = false;
		schema.is_write_operation = true;

		ToolArgSchema arg_name;
		arg_name.name = "action_name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = true;
		arg_name.description = "Input action name (e.g., 'ui_jump')";
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_events;
		arg_events.name = "events";
		arg_events.type_hint = Variant::ARRAY;
		arg_events.required = true;
		arg_events.description = "Input event definitions, e.g. [{type:'key',key:'W'}]";
		schema.arguments.push_back(arg_events);

		schemas["create_input_action"] = schema;
	}

	// === CAMERA ===

	{
		ToolSchema schema;
		schema.tool_name = "create_camera_3d";
		schema.description = "Create a new Camera3D node";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.description = "Parent node (auto-resolved)";
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_position;
		arg_position.name = "position";
		arg_position.type_hint = Variant::VECTOR3;
		arg_position.required = false;
		arg_position.default_value = Vector3(0, 5, 10);
		arg_position.description = "Camera position";
		schema.arguments.push_back(arg_position);

		schemas["create_camera_3d"] = schema;
	}

	// === LIGHT ===

	{
		ToolSchema schema;
		schema.tool_name = "create_light";
		schema.description = "Create a new Light3D";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_type;
		arg_type.name = "light_type";
		arg_type.type_hint = Variant::STRING;
		arg_type.required = true;
		arg_type.description = "Light type";
		arg_type.valid_values = Vector<String>{"omni", "directional", "spot"};
		schema.arguments.push_back(arg_type);

		ToolArgSchema arg_color;
		arg_color.name = "color";
		arg_color.type_hint = Variant::COLOR;
		arg_color.required = false;
		arg_color.default_value = Color(1, 1, 1, 1);
		arg_color.description = "Light color";
		schema.arguments.push_back(arg_color);

		schemas["create_light"] = schema;
	}

	// === MATERIALS ===

	{
		ToolSchema schema;
		schema.tool_name = "create_standard_material";
		schema.description = "Create a StandardMaterial3D";
		schema.requires_scene = false;
		schema.is_write_operation = true;

		ToolArgSchema arg_path;
		arg_path.name = "material_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Resource path for the material, e.g. res://materials/player.tres";
		schema.arguments.push_back(arg_path);

		ToolArgSchema arg_color;
		arg_color.name = "albedo";
		arg_color.type_hint = Variant::COLOR;
		arg_color.required = false;
		arg_color.default_value = Color(1, 1, 1, 1);
		arg_color.description = "Base color";
		schema.arguments.push_back(arg_color);

		schemas["create_standard_material"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "assign_resource_to_property";
		schema.description = "Assign a resource to a node property";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_node;
		arg_node.name = "node_path";
		arg_node.type_hint = Variant::STRING;
		arg_node.required = true;
		arg_node.description = "Target node (auto-resolved)";
		arg_node.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_node);

		ToolArgSchema arg_prop;
		arg_prop.name = "property";
		arg_prop.type_hint = Variant::STRING;
		arg_prop.required = true;
		arg_prop.description = "Property to set";
		schema.arguments.push_back(arg_prop);

		ToolArgSchema arg_resource;
		arg_resource.name = "resource_path";
		arg_resource.type_hint = Variant::STRING;
		arg_resource.required = true;
		arg_resource.description = "Path to the resource";
		schema.arguments.push_back(arg_resource);

		schemas["assign_resource_to_property"] = schema;
	}

	// === ANIMATION ===

	{
		ToolSchema schema;
		schema.tool_name = "create_animation";
		schema.description = "Create a new animation track";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_player;
		arg_player.name = "anim_player_path";
		arg_player.type_hint = Variant::STRING;
		arg_player.required = true;
		arg_player.description = "AnimationPlayer path (auto-resolved)";
		arg_player.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_player);

		ToolArgSchema arg_track;
		arg_track.name = "track_name";
		arg_track.type_hint = Variant::STRING;
		arg_track.required = true;
		arg_track.description = "Name for the new track";
		schema.arguments.push_back(arg_track);

		schemas["create_animation"] = schema;
	}

	// === SAVE/LOAD ===

	{
		ToolSchema schema;
		schema.tool_name = "save_current_scene";
		schema.description = "Save the currently edited scene";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schemas["save_current_scene"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "save_all_scenes";
		schema.description = "Save all open scenes";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schemas["save_all_scenes"] = schema;
	}

	// === DEBUGGING ===

	{
		ToolSchema schema;
		schema.tool_name = "get_editor_log";
		schema.description = "Get recent messages from the editor log";
		schema.requires_scene = false;
		schema.is_write_operation = false;

		ToolArgSchema arg_lines;
		arg_lines.name = "max_lines";
		arg_lines.type_hint = Variant::INT;
		arg_lines.required = false;
		arg_lines.default_value = 200;
		arg_lines.min_value = 1;
		arg_lines.max_value = 2000;
		arg_lines.description = "Maximum number of log lines to return";
		schema.arguments.push_back(arg_lines);

		schemas["get_editor_log"] = schema;
	}

	// === SCENE MANAGEMENT ===

	{
		ToolSchema schema;
		schema.tool_name = "open_scene";
		schema.description = "Open a scene file in the editor";
		schema.requires_scene = false;
		schema.is_write_operation = false;

		ToolArgSchema arg_path;
		arg_path.name = "scene_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Path to the scene file";
		schema.arguments.push_back(arg_path);

		schemas["open_scene"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "get_current_scene";
		schema.description = "Get the currently edited scene path";
		schema.requires_scene = false;
		schema.is_write_operation = false;
		schemas["get_current_scene"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "get_open_scenes";
		schema.description = "Get list of all open scene paths";
		schema.requires_scene = false;
		schema.is_write_operation = false;
		schemas["get_open_scenes"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "get_selected_nodes";
		schema.description = "Get the currently selected nodes in the editor";
		schema.requires_scene = true;
		schema.is_write_operation = false;
		schemas["get_selected_nodes"] = schema;
	}

	// === FILE UTILITIES ===

	{
		ToolSchema schema;
		schema.tool_name = "file_exists";
		schema.description = "Check if a file exists in the project";
		schema.requires_scene = false;
		schema.is_write_operation = false;

		ToolArgSchema arg_path;
		arg_path.name = "path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Path to check";
		schema.arguments.push_back(arg_path);

		schemas["file_exists"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "list_directory";
		schema.description = "List all files and folders in a directory";
		schema.requires_scene = false;
		schema.is_write_operation = false;

		ToolArgSchema arg_path;
		arg_path.name = "path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Directory path";
		schema.arguments.push_back(arg_path);

		schemas["list_directory"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_project_folder";
		schema.description = "Create a new folder in the project";
		schema.requires_scene = false;
		schema.is_write_operation = true;

		ToolArgSchema arg_path;
		arg_path.name = "path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Folder path to create";
		schema.arguments.push_back(arg_path);

		schemas["create_project_folder"] = schema;
	}

	// === CONNECT SIGNAL ===

	{
		ToolSchema schema;
		schema.tool_name = "connect_signal";
		schema.description = "Connect a signal between two nodes";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_from;
		arg_from.name = "from_node_path";
		arg_from.type_hint = Variant::STRING;
		arg_from.required = true;
		arg_from.description = "Source node (auto-resolved)";
		arg_from.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_from);

		ToolArgSchema arg_signal;
		arg_signal.name = "signal_name";
		arg_signal.type_hint = Variant::STRING;
		arg_signal.required = true;
		arg_signal.description = "Signal name";
		schema.arguments.push_back(arg_signal);

		ToolArgSchema arg_to;
		arg_to.name = "to_node_path";
		arg_to.type_hint = Variant::STRING;
		arg_to.required = true;
		arg_to.description = "Target node (auto-resolved)";
		arg_to.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_to);

		ToolArgSchema arg_method;
		arg_method.name = "method_name";
		arg_method.type_hint = Variant::STRING;
		arg_method.required = true;
		arg_method.description = "Method to call";
		schema.arguments.push_back(arg_method);

		schemas["connect_signal"] = schema;
	}

	// === EDITOR ACTIONS ===

	{
		ToolSchema schema;
		schema.tool_name = "play_current_scene";
		schema.description = "Start playing the current scene";
		schema.requires_scene = true;
		schema.is_write_operation = false;
		schemas["play_current_scene"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "play_main_scene";
		schema.description = "Start playing the main scene";
		schema.requires_scene = true;
		schema.is_write_operation = false;
		schemas["play_main_scene"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "stop_playing_scene";
		schema.description = "Stop the running game";
		schema.requires_scene = true;
		schema.is_write_operation = false;
		schemas["stop_playing_scene"] = schema;
	}

	// === SET MAIN SCENE ===

	{
		ToolSchema schema;
		schema.tool_name = "set_main_scene";
		schema.description = "Set the main scene for the project";
		schema.requires_scene = false;
		schema.is_write_operation = true;

		ToolArgSchema arg_path;
		arg_path.name = "scene_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.description = "Path to set as main scene";
		schema.arguments.push_back(arg_path);

		schemas["set_main_scene"] = schema;
	}

	// === PROJECT SETTINGS ===

	{
		ToolSchema schema;
		schema.tool_name = "get_project_settings";
		schema.description = "Get project settings";
		schema.requires_scene = false;
		schema.is_write_operation = false;
		schemas["get_project_settings"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "patch_project_settings";
		schema.description = "Modify project settings";
		schema.requires_scene = false;
		schema.is_write_operation = true;

		ToolArgSchema arg_settings;
		arg_settings.name = "settings";
		arg_settings.type_hint = Variant::DICTIONARY;
		arg_settings.required = true;
		arg_settings.description = "Dictionary of setting key-value pairs";
		schema.arguments.push_back(arg_settings);

		schemas["patch_project_settings"] = schema;
	}

	// === PHYSICS TOOLS ===

	{
		ToolSchema schema;
		schema.tool_name = "raycast_query";
		schema.description = "Perform a raycast query";
		schema.requires_scene = true;
		schema.is_write_operation = false;

		ToolArgSchema arg_from;
		arg_from.name = "from";
		arg_from.type_hint = Variant::VECTOR3;
		arg_from.required = true;
		schema.arguments.push_back(arg_from);

		ToolArgSchema arg_to;
		arg_to.name = "to";
		arg_to.type_hint = Variant::VECTOR3;
		arg_to.required = true;
		schema.arguments.push_back(arg_to);

		schemas["raycast_query"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "shape_cast_query";
		schema.description = "Perform a shape cast query";
		schema.requires_scene = true;
		schema.is_write_operation = false;

		ToolArgSchema arg_shape;
		arg_shape.name = "shape_type";
		arg_shape.type_hint = Variant::STRING;
		arg_shape.required = true;
		arg_shape.valid_values = Vector<String>{"box", "sphere", "capsule"};
		schema.arguments.push_back(arg_shape);

		ToolArgSchema arg_from;
		arg_from.name = "from";
		arg_from.type_hint = Variant::VECTOR3;
		arg_from.required = true;
		schema.arguments.push_back(arg_from);

		schemas["shape_cast_query"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "query_physics";
		schema.description = "Query physics scene";
		schema.requires_scene = true;
		schema.is_write_operation = false;

		ToolArgSchema arg_from;
		arg_from.name = "from";
		arg_from.type_hint = Variant::VECTOR3;
		arg_from.required = false;
		schema.arguments.push_back(arg_from);

		ToolArgSchema arg_to;
		arg_to.name = "to";
		arg_to.type_hint = Variant::VECTOR3;
		arg_to.required = false;
		schema.arguments.push_back(arg_to);

		schemas["query_physics"] = schema;
	}

	// === ANIMATION TOOLS ===

	{
		ToolSchema schema;
		schema.tool_name = "add_animation_track";
		schema.description = "Add an animation track";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_player;
		arg_player.name = "node_path";
		arg_player.type_hint = Variant::STRING;
		arg_player.required = true;
		arg_player.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_player);

		ToolArgSchema arg_name;
		arg_name.name = "animation_name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = true;
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_type;
		arg_type.name = "track_type";
		arg_type.type_hint = Variant::STRING;
		arg_type.required = true;
		arg_type.valid_values = Vector<String>{"value", "position", "rotation", "scale", "bezier", "method", "audio", "animation", "blend_shape"};
		schema.arguments.push_back(arg_type);

		schemas["add_animation_track"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "set_animation_track_key";
		schema.description = "Set an animation track key";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_player;
		arg_player.name = "node_path";
		arg_player.type_hint = Variant::STRING;
		arg_player.required = true;
		arg_player.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_player);

		ToolArgSchema arg_anim;
		arg_anim.name = "animation_name";
		arg_anim.type_hint = Variant::STRING;
		arg_anim.required = true;
		schema.arguments.push_back(arg_anim);

		ToolArgSchema arg_track;
		arg_track.name = "track_index";
		arg_track.type_hint = Variant::INT;
		arg_track.required = true;
		schema.arguments.push_back(arg_track);

		ToolArgSchema arg_time;
		arg_time.name = "time";
		arg_time.type_hint = Variant::FLOAT;
		arg_time.required = true;
		schema.arguments.push_back(arg_time);

		schemas["set_animation_track_key"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "play_animation";
		schema.description = "Play an animation";
		schema.requires_scene = true;
		schema.is_write_operation = false;

		ToolArgSchema arg_player;
		arg_player.name = "node_path";
		arg_player.type_hint = Variant::STRING;
		arg_player.required = true;
		arg_player.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_player);

		ToolArgSchema arg_name;
		arg_name.name = "animation_name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = true;
		schema.arguments.push_back(arg_name);

		schemas["play_animation"] = schema;
	}

	// === SHADER TOOLS ===

	{
		ToolSchema schema;
		schema.tool_name = "create_shader_material";
		schema.description = "Create a shader material";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_node;
		arg_node.name = "node_path";
		arg_node.type_hint = Variant::STRING;
		arg_node.required = true;
		arg_node.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_node);

		ToolArgSchema arg_name;
		arg_name.name = "shader_name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = true;
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_vertex;
		arg_vertex.name = "vertex_code";
		arg_vertex.type_hint = Variant::STRING;
		arg_vertex.required = true;
		schema.arguments.push_back(arg_vertex);

		ToolArgSchema arg_fragment;
		arg_fragment.name = "fragment_code";
		arg_fragment.type_hint = Variant::STRING;
		arg_fragment.required = true;
		schema.arguments.push_back(arg_fragment);

		schemas["create_shader_material"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "set_shader_uniform";
		schema.description = "Set a shader uniform value";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_node;
		arg_node.name = "node_path";
		arg_node.type_hint = Variant::STRING;
		arg_node.required = true;
		arg_node.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_node);

		ToolArgSchema arg_name;
		arg_name.name = "uniform_name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = true;
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_value;
		arg_value.name = "value";
		arg_value.type_hint = Variant::NIL;
		arg_value.required = true;
		schema.arguments.push_back(arg_value);

		schemas["set_shader_uniform"] = schema;
	}

	// === NAVIGATION TOOLS ===

	{
		ToolSchema schema;
		schema.tool_name = "bake_navigation_mesh";
		schema.description = "Bake a navigation mesh";
		schema.requires_scene = true;
		schema.is_write_operation = true;

		ToolArgSchema arg_node;
		arg_node.name = "node_path";
		arg_node.type_hint = Variant::STRING;
		arg_node.required = true;
		arg_node.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_node);

		schemas["bake_navigation_mesh"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "get_navigation_path";
		schema.description = "Calculate navigation path between two points";
		schema.requires_scene = true;
		schema.is_write_operation = false;

		ToolArgSchema arg_from;
		arg_from.name = "from";
		arg_from.type_hint = Variant::VECTOR3;
		arg_from.required = true;
		schema.arguments.push_back(arg_from);

		ToolArgSchema arg_to;
		arg_to.name = "to";
		arg_to.type_hint = Variant::VECTOR3;
		arg_to.required = true;
		schema.arguments.push_back(arg_to);

		schemas["get_navigation_path"] = schema;
	}

	// === DEBUGGING TOOLS ===

	{
		ToolSchema schema;
		schema.tool_name = "set_breakpoint";
		schema.description = "Set a script breakpoint";
		schema.requires_scene = false;
		schema.is_write_operation = true;

		ToolArgSchema arg_path;
		arg_path.name = "script_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		schema.arguments.push_back(arg_path);

		ToolArgSchema arg_line;
		arg_line.name = "line";
		arg_line.type_hint = Variant::INT;
		arg_line.required = true;
		schema.arguments.push_back(arg_line);

		schemas["set_breakpoint"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "profile_frame";
		schema.description = "Profile current frame";
		schema.requires_scene = true;
		schema.is_write_operation = false;
		schemas["profile_frame"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "monitor_runtime_performance";
		schema.description = "Monitor runtime performance metrics";
		schema.requires_scene = true;
		schema.is_write_operation = false;
		schemas["monitor_runtime_performance"] = schema;
	}

	// === MULTIPLAYER TOOLS ===

	{
		ToolSchema schema;
		schema.tool_name = "get_network_state";
		schema.description = "Get network connection state";
		schema.requires_scene = true;
		schema.is_write_operation = false;
		schemas["get_network_state"] = schema;
	}

	// === 2D SCENE TOOLS ===

	{
		ToolSchema schema;
		schema.tool_name = "create_camera_2d";
		schema.description = "Create a Camera2D node for 2D camera control";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("Camera2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_pos;
		arg_pos.name = "position";
		arg_pos.type_hint = Variant::VECTOR2;
		arg_pos.required = false;
		schema.arguments.push_back(arg_pos);

		ToolArgSchema arg_zoom;
		arg_zoom.name = "zoom";
		arg_zoom.type_hint = Variant::VECTOR2;
		arg_zoom.required = false;
		arg_zoom.default_value = Vector2(1, 1);
		schema.arguments.push_back(arg_zoom);

		ToolArgSchema arg_current;
		arg_current.name = "make_current";
		arg_current.type_hint = Variant::BOOL;
		arg_current.required = false;
		arg_current.default_value = false;
		schema.arguments.push_back(arg_current);

		schemas["create_camera_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_character_body_2d";
		schema.description = "Create a CharacterBody2D node for 2D character physics";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("CharacterBody2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_pos;
		arg_pos.name = "position";
		arg_pos.type_hint = Variant::VECTOR2;
		arg_pos.required = false;
		schema.arguments.push_back(arg_pos);

		schemas["create_character_body_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_area_2d";
		schema.description = "Create an Area2D node for 2D collision detection without physics response";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("Area2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_pos;
		arg_pos.name = "position";
		arg_pos.type_hint = Variant::VECTOR2;
		arg_pos.required = false;
		schema.arguments.push_back(arg_pos);

		schemas["create_area_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "add_collision_shape_2d";
		schema.description = "Add a direct CollisionShape2D child to an Area2D, StaticBody2D, RigidBody2D, CharacterBody2D, or AnimatableBody2D. Prefer explicit size/radius so colliders match visuals.";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		arg_parent.description = "2D collision object path. Use parent_path for new calls; node_path is accepted as a legacy alias.";
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_node_path;
		arg_node_path.name = "node_path";
		arg_node_path.type_hint = Variant::STRING;
		arg_node_path.required = false;
		arg_node_path.auto_resolve_node_path = true;
		arg_node_path.description = "Legacy alias for parent_path.";
		schema.arguments.push_back(arg_node_path);

		ToolArgSchema arg_shape;
		arg_shape.name = "shape_type";
		arg_shape.type_hint = Variant::STRING;
		arg_shape.required = false;
		arg_shape.default_value = String("rectangle");
		arg_shape.valid_values = Vector<String>{"rectangle", "box", "circle", "capsule", "segment", "convex", "separation_ray", "world_boundary"};
		arg_shape.description = "Shape type. Use rectangle/box for platforms, capsule for characters, circle for collectibles/projectiles.";
		schema.arguments.push_back(arg_shape);

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("CollisionShape2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_size;
		arg_size.name = "size";
		arg_size.type_hint = Variant::VECTOR2;
		arg_size.required = false;
		arg_size.default_value = Vector2(32, 32);
		arg_size.description = "Explicit collider size for rectangle/box and default dimensions for other shapes.";
		schema.arguments.push_back(arg_size);

		ToolArgSchema arg_width;
		arg_width.name = "width";
		arg_width.type_hint = Variant::FLOAT;
		arg_width.required = false;
		arg_width.default_value = 32.0;
		arg_width.description = "Legacy width alias used when size is omitted.";
		schema.arguments.push_back(arg_width);

		ToolArgSchema arg_height;
		arg_height.name = "height";
		arg_height.type_hint = Variant::FLOAT;
		arg_height.required = false;
		arg_height.default_value = 32.0;
		arg_height.description = "Height for rectangle/capsule/cylinder-like 2D shapes.";
		schema.arguments.push_back(arg_height);

		ToolArgSchema arg_radius;
		arg_radius.name = "radius";
		arg_radius.type_hint = Variant::FLOAT;
		arg_radius.required = false;
		arg_radius.default_value = 16.0;
		schema.arguments.push_back(arg_radius);

		ToolArgSchema arg_pos;
		arg_pos.name = "position";
		arg_pos.type_hint = Variant::VECTOR2;
		arg_pos.required = false;
		arg_pos.description = "Local offset of the CollisionShape2D relative to its parent body.";
		schema.arguments.push_back(arg_pos);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		arg_x.description = "Legacy local x offset used when position is omitted.";
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		arg_y.description = "Legacy local y offset used when position is omitted.";
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_points;
		arg_points.name = "points";
		arg_points.type_hint = Variant::ARRAY;
		arg_points.required = false;
		arg_points.description = "Points for convex/segment shapes. Each point is {x,y}.";
		schema.arguments.push_back(arg_points);

		ToolArgSchema arg_disabled;
		arg_disabled.name = "disabled";
		arg_disabled.type_hint = Variant::BOOL;
		arg_disabled.required = false;
		arg_disabled.default_value = false;
		schema.arguments.push_back(arg_disabled);

		ToolArgSchema arg_one_way;
		arg_one_way.name = "one_way_collision";
		arg_one_way.type_hint = Variant::BOOL;
		arg_one_way.required = false;
		arg_one_way.default_value = false;
		schema.arguments.push_back(arg_one_way);

		ToolArgSchema arg_one_way_margin;
		arg_one_way_margin.name = "one_way_collision_margin";
		arg_one_way_margin.type_hint = Variant::FLOAT;
		arg_one_way_margin.required = false;
		arg_one_way_margin.default_value = 1.0;
		schema.arguments.push_back(arg_one_way_margin);

		schemas["add_collision_shape_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "add_2d_collision_shape";
		schema.description = "Legacy alias for add_collision_shape_2d. Accepts node_path or parent_path plus the same shape/dimension arguments.";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_node_path;
		arg_node_path.name = "node_path";
		arg_node_path.type_hint = Variant::STRING;
		arg_node_path.required = false;
		arg_node_path.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_node_path);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_shape;
		arg_shape.name = "shape_type";
		arg_shape.type_hint = Variant::STRING;
		arg_shape.required = false;
		arg_shape.default_value = String("rectangle");
		arg_shape.valid_values = Vector<String>{"rectangle", "box", "circle", "capsule", "segment", "convex", "separation_ray", "world_boundary"};
		schema.arguments.push_back(arg_shape);

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("CollisionShape2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_size;
		arg_size.name = "size";
		arg_size.type_hint = Variant::VECTOR2;
		arg_size.required = false;
		arg_size.default_value = Vector2(32, 32);
		schema.arguments.push_back(arg_size);

		ToolArgSchema arg_width;
		arg_width.name = "width";
		arg_width.type_hint = Variant::FLOAT;
		arg_width.required = false;
		arg_width.default_value = 32.0;
		schema.arguments.push_back(arg_width);

		ToolArgSchema arg_radius;
		arg_radius.name = "radius";
		arg_radius.type_hint = Variant::FLOAT;
		arg_radius.required = false;
		arg_radius.default_value = 16.0;
		schema.arguments.push_back(arg_radius);

		ToolArgSchema arg_height;
		arg_height.name = "height";
		arg_height.type_hint = Variant::FLOAT;
		arg_height.required = false;
		arg_height.default_value = 32.0;
		schema.arguments.push_back(arg_height);

		ToolArgSchema arg_pos;
		arg_pos.name = "position";
		arg_pos.type_hint = Variant::VECTOR2;
		arg_pos.required = false;
		schema.arguments.push_back(arg_pos);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_points;
		arg_points.name = "points";
		arg_points.type_hint = Variant::ARRAY;
		arg_points.required = false;
		schema.arguments.push_back(arg_points);

		ToolArgSchema arg_disabled;
		arg_disabled.name = "disabled";
		arg_disabled.type_hint = Variant::BOOL;
		arg_disabled.required = false;
		arg_disabled.default_value = false;
		schema.arguments.push_back(arg_disabled);

		ToolArgSchema arg_one_way;
		arg_one_way.name = "one_way_collision";
		arg_one_way.type_hint = Variant::BOOL;
		arg_one_way.required = false;
		arg_one_way.default_value = false;
		schema.arguments.push_back(arg_one_way);

		schemas["add_2d_collision_shape"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_game_actor_2d";
		schema.description = "Create a complete 2D gameplay actor with physics body, direct CollisionShape2D, sensible collider size, collision layer/mask, and optional matching visual.";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_role;
		arg_role.name = "role";
		arg_role.type_hint = Variant::STRING;
		arg_role.required = false;
		arg_role.default_value = String("generic");
		arg_role.valid_values = Vector<String>{"player", "enemy", "platform", "wall", "collectible", "hazard", "projectile", "trigger", "generic"};
		schema.arguments.push_back(arg_role);

		ToolArgSchema arg_body;
		arg_body.name = "body_type";
		arg_body.type_hint = Variant::STRING;
		arg_body.required = false;
		arg_body.description = "CharacterBody2D, StaticBody2D, RigidBody2D, or Area2D. Defaults from role.";
		schema.arguments.push_back(arg_body);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_position;
		arg_position.name = "position";
		arg_position.type_hint = Variant::VECTOR2;
		arg_position.required = false;
		schema.arguments.push_back(arg_position);

		ToolArgSchema arg_size;
		arg_size.name = "size";
		arg_size.type_hint = Variant::VECTOR2;
		arg_size.required = false;
		arg_size.description = "Collider/visual size in pixels. Defaults from role.";
		schema.arguments.push_back(arg_size);

		ToolArgSchema arg_shape;
		arg_shape.name = "shape_type";
		arg_shape.type_hint = Variant::STRING;
		arg_shape.required = false;
		arg_shape.valid_values = Vector<String>{"rectangle", "box", "circle", "capsule"};
		arg_shape.description = "rectangle, circle, or capsule. Defaults from role.";
		schema.arguments.push_back(arg_shape);

		ToolArgSchema arg_layer;
		arg_layer.name = "collision_layer";
		arg_layer.type_hint = Variant::INT;
		arg_layer.required = false;
		arg_layer.default_value = 1;
		schema.arguments.push_back(arg_layer);

		ToolArgSchema arg_mask;
		arg_mask.name = "collision_mask";
		arg_mask.type_hint = Variant::INT;
		arg_mask.required = false;
		arg_mask.default_value = 1;
		schema.arguments.push_back(arg_mask);

		ToolArgSchema arg_visual;
		arg_visual.name = "add_visual";
		arg_visual.type_hint = Variant::BOOL;
		arg_visual.required = false;
		arg_visual.default_value = true;
		schema.arguments.push_back(arg_visual);

		schemas["create_game_actor_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_game_actor_3d";
		schema.description = "Create a complete 3D gameplay actor with physics body, direct CollisionShape3D, sensible collider size, collision layer/mask, and optional matching visual.";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_role;
		arg_role.name = "role";
		arg_role.type_hint = Variant::STRING;
		arg_role.required = false;
		arg_role.default_value = String("generic");
		arg_role.valid_values = Vector<String>{"player", "enemy", "platform", "wall", "collectible", "hazard", "projectile", "trigger", "generic"};
		schema.arguments.push_back(arg_role);

		ToolArgSchema arg_body;
		arg_body.name = "body_type";
		arg_body.type_hint = Variant::STRING;
		arg_body.required = false;
		arg_body.description = "CharacterBody3D, StaticBody3D, RigidBody3D, or Area3D. Defaults from role.";
		schema.arguments.push_back(arg_body);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_position;
		arg_position.name = "position";
		arg_position.type_hint = Variant::VECTOR3;
		arg_position.required = false;
		schema.arguments.push_back(arg_position);

		ToolArgSchema arg_size;
		arg_size.name = "size";
		arg_size.type_hint = Variant::VECTOR3;
		arg_size.required = false;
		arg_size.description = "Collider/visual size in world units. Defaults from role.";
		schema.arguments.push_back(arg_size);

		ToolArgSchema arg_shape;
		arg_shape.name = "shape_type";
		arg_shape.type_hint = Variant::STRING;
		arg_shape.required = false;
		arg_shape.valid_values = Vector<String>{"box", "sphere", "capsule", "cylinder"};
		schema.arguments.push_back(arg_shape);

		ToolArgSchema arg_layer;
		arg_layer.name = "collision_layer";
		arg_layer.type_hint = Variant::INT;
		arg_layer.required = false;
		arg_layer.default_value = 1;
		schema.arguments.push_back(arg_layer);

		ToolArgSchema arg_mask;
		arg_mask.name = "collision_mask";
		arg_mask.type_hint = Variant::INT;
		arg_mask.required = false;
		arg_mask.default_value = 1;
		schema.arguments.push_back(arg_mask);

		ToolArgSchema arg_visual;
		arg_visual.name = "add_visual";
		arg_visual.type_hint = Variant::BOOL;
		arg_visual.required = false;
		arg_visual.default_value = true;
		schema.arguments.push_back(arg_visual);

		schemas["create_game_actor_3d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "audit_game_physics";
		schema.description = "Inspect the current scene for missing, disabled, tiny, or misconfigured colliders and return suggested repair tool calls.";
		schema.requires_scene = true;
		schema.is_write_operation = false;
		schema.supports_batching = false;

		ToolArgSchema arg_scene;
		arg_scene.name = "scene_path";
		arg_scene.type_hint = Variant::STRING;
		arg_scene.required = false;
		schema.arguments.push_back(arg_scene);

		ToolArgSchema arg_dimensions;
		arg_dimensions.name = "dimensions";
		arg_dimensions.type_hint = Variant::STRING;
		arg_dimensions.required = false;
		arg_dimensions.default_value = String("both");
		arg_dimensions.valid_values = Vector<String>{"2d", "3d", "both"};
		schema.arguments.push_back(arg_dimensions);

		ToolArgSchema arg_min2d;
		arg_min2d.name = "min_2d_size_px";
		arg_min2d.type_hint = Variant::FLOAT;
		arg_min2d.required = false;
		arg_min2d.default_value = 8.0;
		schema.arguments.push_back(arg_min2d);

		ToolArgSchema arg_min3d;
		arg_min3d.name = "min_3d_size";
		arg_min3d.type_hint = Variant::FLOAT;
		arg_min3d.required = false;
		arg_min3d.default_value = 0.05;
		schema.arguments.push_back(arg_min3d);

		ToolArgSchema arg_suggest;
		arg_suggest.name = "include_suggestions";
		arg_suggest.type_hint = Variant::BOOL;
		arg_suggest.required = false;
		arg_suggest.default_value = true;
		schema.arguments.push_back(arg_suggest);

		schemas["audit_game_physics"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "repair_game_physics";
		schema.description = "Repair missing and tiny collider issues reported by audit_game_physics. Dry-run by default; set apply:true to modify the scene.";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = false;

		ToolArgSchema arg_scene;
		arg_scene.name = "scene_path";
		arg_scene.type_hint = Variant::STRING;
		arg_scene.required = false;
		schema.arguments.push_back(arg_scene);

		ToolArgSchema arg_ids;
		arg_ids.name = "issue_ids";
		arg_ids.type_hint = Variant::ARRAY;
		arg_ids.required = false;
		arg_ids.description = "Optional issue id list from audit_game_physics. Omit to repair all supported issues.";
		schema.arguments.push_back(arg_ids);

		ToolArgSchema arg_apply;
		arg_apply.name = "apply";
		arg_apply.type_hint = Variant::BOOL;
		arg_apply.required = false;
		arg_apply.default_value = false;
		schema.arguments.push_back(arg_apply);

		schemas["repair_game_physics"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_tile_map";
		schema.description = "Create a TileMap node for 2D tile-based maps";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("TileMap");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_pos;
		arg_pos.name = "position";
		arg_pos.type_hint = Variant::VECTOR2;
		arg_pos.required = false;
		schema.arguments.push_back(arg_pos);

		ToolArgSchema arg_tileset;
		arg_tileset.name = "tileset_path";
		arg_tileset.type_hint = Variant::STRING;
		arg_tileset.required = false;
		schema.arguments.push_back(arg_tileset);

		schemas["create_tile_map"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_tilemap_layer";
		schema.description = "Create a TileMapLayer node (Godot 4 uses layers as children of TileMap)";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("Layer0");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_pos;
		arg_pos.name = "position";
		arg_pos.type_hint = Variant::VECTOR2;
		arg_pos.required = false;
		schema.arguments.push_back(arg_pos);

		ToolArgSchema arg_tileset;
		arg_tileset.name = "tileset_path";
		arg_tileset.type_hint = Variant::STRING;
		arg_tileset.required = false;
		schema.arguments.push_back(arg_tileset);

		schemas["create_tilemap_layer"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "fill_tilemap_rect";
		schema.description = "Fill a rectangular area with the same tile";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_path;
		arg_path.name = "node_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_path);

		ToolArgSchema arg_from_x;
		arg_from_x.name = "from_x";
		arg_from_x.type_hint = Variant::INT;
		arg_from_x.required = true;
		schema.arguments.push_back(arg_from_x);

		ToolArgSchema arg_from_y;
		arg_from_y.name = "from_y";
		arg_from_y.type_hint = Variant::INT;
		arg_from_y.required = true;
		schema.arguments.push_back(arg_from_y);

		ToolArgSchema arg_to_x;
		arg_to_x.name = "to_x";
		arg_to_x.type_hint = Variant::INT;
		arg_to_x.required = true;
		schema.arguments.push_back(arg_to_x);

		ToolArgSchema arg_to_y;
		arg_to_y.name = "to_y";
		arg_to_y.type_hint = Variant::INT;
		arg_to_y.required = true;
		schema.arguments.push_back(arg_to_y);

		ToolArgSchema arg_source;
		arg_source.name = "source_id";
		arg_source.type_hint = Variant::INT;
		arg_source.required = true;
		schema.arguments.push_back(arg_source);

		ToolArgSchema arg_atlas_x;
		arg_atlas_x.name = "atlas_coords_x";
		arg_atlas_x.type_hint = Variant::INT;
		arg_atlas_x.required = false;
		arg_atlas_x.default_value = -1;
		schema.arguments.push_back(arg_atlas_x);

		ToolArgSchema arg_atlas_y;
		arg_atlas_y.name = "atlas_coords_y";
		arg_atlas_y.type_hint = Variant::INT;
		arg_atlas_y.required = false;
		arg_atlas_y.default_value = -1;
		schema.arguments.push_back(arg_atlas_y);

		schemas["fill_tilemap_rect"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_marker_2d";
		schema.description = "Create a Marker2D node for positional markers and spawn points";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("Marker2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_pos;
		arg_pos.name = "position";
		arg_pos.type_hint = Variant::VECTOR2;
		arg_pos.required = false;
		schema.arguments.push_back(arg_pos);

		schemas["create_marker_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_cpu_particles_2d";
		schema.description = "Create a CPUParticles2D node for 2D particle effects";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("CPUParticles2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_pos;
		arg_pos.name = "position";
		arg_pos.type_hint = Variant::VECTOR2;
		arg_pos.required = false;
		schema.arguments.push_back(arg_pos);

		schemas["create_cpu_particles_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_gpu_particles_2d";
		schema.description = "Create a GPUParticles2D node for GPU-accelerated 2D particles";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("GPUParticles2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_pos;
		arg_pos.name = "position";
		arg_pos.type_hint = Variant::VECTOR2;
		arg_pos.required = false;
		schema.arguments.push_back(arg_pos);

		schemas["create_gpu_particles_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_canvas_layer";
		schema.description = "Create a CanvasLayer for UI overlay rendering";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("CanvasLayer");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_layer;
		arg_layer.name = "layer";
		arg_layer.type_hint = Variant::INT;
		arg_layer.required = false;
		arg_layer.default_value = 1;
		schema.arguments.push_back(arg_layer);

		ToolArgSchema arg_visible;
		arg_visible.name = "visible";
		arg_visible.type_hint = Variant::BOOL;
		arg_visible.required = false;
		arg_visible.default_value = true;
		schema.arguments.push_back(arg_visible);

		schemas["create_canvas_layer"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_parallax_background";
		schema.description = "Create a ParallaxBackground node for 2D parallax scrolling";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("ParallaxBackground");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_add_layer;
		arg_add_layer.name = "add_layer";
		arg_add_layer.type_hint = Variant::BOOL;
		arg_add_layer.required = false;
		arg_add_layer.default_value = true;
		schema.arguments.push_back(arg_add_layer);

		schemas["create_parallax_background"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_navigation_region_2d";
		schema.description = "Create a NavigationRegion2D for 2D pathfinding";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("NavigationRegion2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_pos;
		arg_pos.name = "position";
		arg_pos.type_hint = Variant::VECTOR2;
		arg_pos.required = false;
		schema.arguments.push_back(arg_pos);

		ToolArgSchema arg_bake;
		arg_bake.name = "bake";
		arg_bake.type_hint = Variant::BOOL;
		arg_bake.required = false;
		arg_bake.default_value = false;
		schema.arguments.push_back(arg_bake);

		schemas["create_navigation_region_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_animatable_body_2d";
		schema.description = "Create an AnimatableBody2D for kinematic 2D platforms";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("AnimatableBody2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_pos;
		arg_pos.name = "position";
		arg_pos.type_hint = Variant::VECTOR2;
		arg_pos.required = false;
		schema.arguments.push_back(arg_pos);

		schemas["create_animatable_body_2d"] = schema;
	}

	// === TIMER / TWEEN / PATHFOLLOW ===

	{
		ToolSchema schema;
		schema.tool_name = "create_timer";
		schema.description = "Create a Timer node for timed events, cooldowns, and delays";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("Timer");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_wait;
		arg_wait.name = "wait_time";
		arg_wait.type_hint = Variant::FLOAT;
		arg_wait.required = false;
		arg_wait.default_value = 1.0;
		schema.arguments.push_back(arg_wait);

		ToolArgSchema arg_oneshot;
		arg_oneshot.name = "one_shot";
		arg_oneshot.type_hint = Variant::BOOL;
		arg_oneshot.required = false;
		arg_oneshot.default_value = false;
		schema.arguments.push_back(arg_oneshot);

		ToolArgSchema arg_autostart;
		arg_autostart.name = "autostart";
		arg_autostart.type_hint = Variant::BOOL;
		arg_autostart.required = false;
		arg_autostart.default_value = false;
		schema.arguments.push_back(arg_autostart);

		ToolArgSchema arg_target;
		arg_target.name = "timeout_target_path";
		arg_target.type_hint = Variant::STRING;
		arg_target.required = false;
		arg_target.description = "Node path to connect timeout signal to";
		schema.arguments.push_back(arg_target);

		ToolArgSchema arg_method;
		arg_method.name = "timeout_method";
		arg_method.type_hint = Variant::STRING;
		arg_method.required = false;
		arg_method.description = "Method name on target node to call on timeout";
		schema.arguments.push_back(arg_method);

		schemas["create_timer"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_tween";
		schema.description = "Animate a property on a node using an AnimationPlayer (Tween equivalent)";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("TweenAnimation");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_target;
		arg_target.name = "target_path";
		arg_target.type_hint = Variant::STRING;
		arg_target.required = true;
		arg_target.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_target);

		ToolArgSchema arg_property;
		arg_property.name = "property";
		arg_property.type_hint = Variant::STRING;
		arg_property.required = false;
		arg_property.default_value = String("position");
		arg_property.description = "Property to animate: position, scale, rotation, modulate";
		schema.arguments.push_back(arg_property);

		ToolArgSchema arg_duration;
		arg_duration.name = "duration";
		arg_duration.type_hint = Variant::FLOAT;
		arg_duration.required = false;
		arg_duration.default_value = 1.0;
		schema.arguments.push_back(arg_duration);

		ToolArgSchema arg_from;
		arg_from.name = "from";
		arg_from.type_hint = Variant::DICTIONARY;
		arg_from.required = true;
		arg_from.description = "Start value: {x, y} for vectors or {value} for floats";
		schema.arguments.push_back(arg_from);

		ToolArgSchema arg_to;
		arg_to.name = "to";
		arg_to.type_hint = Variant::DICTIONARY;
		arg_to.required = true;
		arg_to.description = "End value: {x, y} for vectors or {value} for floats";
		schema.arguments.push_back(arg_to);

		ToolArgSchema arg_ease;
		arg_ease.name = "ease";
		arg_ease.type_hint = Variant::STRING;
		arg_ease.required = false;
		arg_ease.default_value = String("in_out");
		arg_ease.valid_values = Vector<String>{"linear", "ease_in", "ease_out", "ease_in_out"};
		schema.arguments.push_back(arg_ease);

		ToolArgSchema arg_loop;
		arg_loop.name = "loop";
		arg_loop.type_hint = Variant::BOOL;
		arg_loop.required = false;
		arg_loop.default_value = false;
		schema.arguments.push_back(arg_loop);

		ToolArgSchema arg_autoplay;
		arg_autoplay.name = "autoplay";
		arg_autoplay.type_hint = Variant::BOOL;
		arg_autoplay.required = false;
		arg_autoplay.default_value = false;
		schema.arguments.push_back(arg_autoplay);

		schemas["create_tween"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_sprite_2d";
		schema.description = "Create a Sprite2D node. For sprite sheets, set hframes+vframes+frame. For atlas sub-regions, set use_region+region_x/y/w/h.";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("Sprite2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_texture;
		arg_texture.name = "texture_path";
		arg_texture.type_hint = Variant::STRING;
		arg_texture.required = false;
		schema.arguments.push_back(arg_texture);

		ToolArgSchema arg_hframes;
		arg_hframes.name = "hframes";
		arg_hframes.type_hint = Variant::INT;
		arg_hframes.required = false;
		arg_hframes.default_value = 1;
		schema.arguments.push_back(arg_hframes);

		ToolArgSchema arg_vframes;
		arg_vframes.name = "vframes";
		arg_vframes.type_hint = Variant::INT;
		arg_vframes.required = false;
		arg_vframes.default_value = 1;
		schema.arguments.push_back(arg_vframes);

		ToolArgSchema arg_frame;
		arg_frame.name = "frame";
		arg_frame.type_hint = Variant::INT;
		arg_frame.required = false;
		arg_frame.default_value = 0;
		schema.arguments.push_back(arg_frame);

		ToolArgSchema arg_centered;
		arg_centered.name = "centered";
		arg_centered.type_hint = Variant::BOOL;
		arg_centered.required = false;
		arg_centered.default_value = true;
		schema.arguments.push_back(arg_centered);

		ToolArgSchema arg_use_region;
		arg_use_region.name = "use_region";
		arg_use_region.type_hint = Variant::BOOL;
		arg_use_region.required = false;
		arg_use_region.default_value = false;
		schema.arguments.push_back(arg_use_region);

		ToolArgSchema arg_region_x;
		arg_region_x.name = "region_x";
		arg_region_x.type_hint = Variant::INT;
		arg_region_x.required = false;
		arg_region_x.default_value = 0;
		schema.arguments.push_back(arg_region_x);

		ToolArgSchema arg_region_y;
		arg_region_y.name = "region_y";
		arg_region_y.type_hint = Variant::INT;
		arg_region_y.required = false;
		arg_region_y.default_value = 0;
		schema.arguments.push_back(arg_region_y);

		ToolArgSchema arg_region_w;
		arg_region_w.name = "region_w";
		arg_region_w.type_hint = Variant::INT;
		arg_region_w.required = false;
		arg_region_w.default_value = 0;
		schema.arguments.push_back(arg_region_w);

		ToolArgSchema arg_region_h;
		arg_region_h.name = "region_h";
		arg_region_h.type_hint = Variant::INT;
		arg_region_h.required = false;
		arg_region_h.default_value = 0;
		schema.arguments.push_back(arg_region_h);

		schemas["create_sprite_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_sprite_frames";
		schema.description = "Create a SpriteFrames resource from a sprite sheet grid for use with AnimatedSprite2D";
		schema.requires_scene = false;
		schema.is_write_operation = true;
		schema.supports_batching = false;

		ToolArgSchema arg_save;
		arg_save.name = "save_path";
		arg_save.type_hint = Variant::STRING;
		arg_save.required = true;
		schema.arguments.push_back(arg_save);

		ToolArgSchema arg_texture;
		arg_texture.name = "texture_path";
		arg_texture.type_hint = Variant::STRING;
		arg_texture.required = true;
		schema.arguments.push_back(arg_texture);

		ToolArgSchema arg_hframes;
		arg_hframes.name = "hframes";
		arg_hframes.type_hint = Variant::INT;
		arg_hframes.required = true;
		schema.arguments.push_back(arg_hframes);

		ToolArgSchema arg_vframes;
		arg_vframes.name = "vframes";
		arg_vframes.type_hint = Variant::INT;
		arg_vframes.required = true;
		schema.arguments.push_back(arg_vframes);

		ToolArgSchema arg_anim;
		arg_anim.name = "animation_name";
		arg_anim.type_hint = Variant::STRING;
		arg_anim.required = false;
		arg_anim.default_value = String("default");
		schema.arguments.push_back(arg_anim);

		ToolArgSchema arg_fps;
		arg_fps.name = "fps";
		arg_fps.type_hint = Variant::FLOAT;
		arg_fps.required = false;
		arg_fps.default_value = 5.0;
		schema.arguments.push_back(arg_fps);

		ToolArgSchema arg_loop;
		arg_loop.name = "loop";
		arg_loop.type_hint = Variant::BOOL;
		arg_loop.required = false;
		arg_loop.default_value = true;
		schema.arguments.push_back(arg_loop);

		schemas["create_sprite_frames"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_path_follow_2d";
		schema.description = "Create a PathFollow2D node for moving objects along a Path2D";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("PathFollow2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "path_2d_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = true;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_progress;
		arg_progress.name = "progress_ratio";
		arg_progress.type_hint = Variant::FLOAT;
		arg_progress.required = false;
		arg_progress.default_value = 0.0;
		schema.arguments.push_back(arg_progress);

		ToolArgSchema arg_rotates;
		arg_rotates.name = "rotates";
		arg_rotates.type_hint = Variant::BOOL;
		arg_rotates.required = false;
		arg_rotates.default_value = true;
		schema.arguments.push_back(arg_rotates);

		ToolArgSchema arg_loop;
		arg_loop.name = "loop";
		arg_loop.type_hint = Variant::BOOL;
		arg_loop.required = false;
		arg_loop.default_value = true;
		schema.arguments.push_back(arg_loop);

		ToolArgSchema arg_child_type;
		arg_child_type.name = "child_type";
		arg_child_type.type_hint = Variant::STRING;
		arg_child_type.required = false;
		arg_child_type.description = "Optional node type to add as child (e.g. Sprite2D)";
		schema.arguments.push_back(arg_child_type);

		schemas["create_path_follow_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_light_occluder_2d";
		schema.description = "Create a LightOccluder2D for 2D shadows. Requires polygon_points.";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("LightOccluder2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_points;
		arg_points.name = "polygon_points";
		arg_points.type_hint = Variant::ARRAY;
		arg_points.required = false;
		arg_points.description = "Array of {x,y} points defining the occluder polygon";
		schema.arguments.push_back(arg_points);

		ToolArgSchema arg_closed;
		arg_closed.name = "closed";
		arg_closed.type_hint = Variant::BOOL;
		arg_closed.required = false;
		arg_closed.default_value = true;
		schema.arguments.push_back(arg_closed);

		schemas["create_light_occluder_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_canvas_modulate";
		schema.description = "Create a CanvasModulate for global 2D color tint (day/night cycles)";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("CanvasModulate");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_color;
		arg_color.name = "color";
		arg_color.type_hint = Variant::COLOR;
		arg_color.required = false;
		arg_color.default_value = Color(1, 1, 1);
		schema.arguments.push_back(arg_color);

		schemas["create_canvas_modulate"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_skeleton_2d";
		schema.description = "Create a Skeleton2D for 2D skeletal animation";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("Skeleton2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		schemas["create_skeleton_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_bone_2d";
		schema.description = "Create a Bone2D under a Skeleton2D. Set auto_calculate_length:true (default) or specify length manually.";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("Bone2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_auto;
		arg_auto.name = "auto_calculate_length";
		arg_auto.type_hint = Variant::BOOL;
		arg_auto.required = false;
		arg_auto.default_value = true;
		schema.arguments.push_back(arg_auto);

		ToolArgSchema arg_length;
		arg_length.name = "length";
		arg_length.type_hint = Variant::FLOAT;
		arg_length.required = false;
		arg_length.default_value = 16.0;
		schema.arguments.push_back(arg_length);

		schemas["create_bone_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_pin_joint_2d";
		schema.description = "Create a PinJoint2D connecting two physics bodies";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("PinJoint2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_a;
		arg_a.name = "node_a";
		arg_a.type_hint = Variant::STRING;
		arg_a.required = false;
		arg_a.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_a);

		ToolArgSchema arg_b;
		arg_b.name = "node_b";
		arg_b.type_hint = Variant::STRING;
		arg_b.required = false;
		arg_b.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_b);

		ToolArgSchema arg_softness;
		arg_softness.name = "softness";
		arg_softness.type_hint = Variant::FLOAT;
		arg_softness.required = false;
		arg_softness.default_value = 0.0;
		schema.arguments.push_back(arg_softness);

		schemas["create_pin_joint_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_damped_spring_joint_2d";
		schema.description = "Create a DampedSpringJoint2D (spring/rope physics)";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("DampedSpringJoint2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_a;
		arg_a.name = "node_a";
		arg_a.type_hint = Variant::STRING;
		arg_a.required = false;
		arg_a.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_a);

		ToolArgSchema arg_b;
		arg_b.name = "node_b";
		arg_b.type_hint = Variant::STRING;
		arg_b.required = false;
		arg_b.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_b);

		ToolArgSchema arg_length;
		arg_length.name = "length";
		arg_length.type_hint = Variant::FLOAT;
		arg_length.required = false;
		arg_length.default_value = 16.0;
		schema.arguments.push_back(arg_length);

		ToolArgSchema arg_stiffness;
		arg_stiffness.name = "stiffness";
		arg_stiffness.type_hint = Variant::FLOAT;
		arg_stiffness.required = false;
		arg_stiffness.default_value = 20.0;
		schema.arguments.push_back(arg_stiffness);

		ToolArgSchema arg_damping;
		arg_damping.name = "damping";
		arg_damping.type_hint = Variant::FLOAT;
		arg_damping.required = false;
		arg_damping.default_value = 1.5;
		schema.arguments.push_back(arg_damping);

		schemas["create_damped_spring_joint_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_multimesh_instance_2d";
		schema.description = "Create a MultiMeshInstance2D for batch-rendering thousands of identical sprites (grass, particles, stars)";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("MultiMeshInstance2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_mesh;
		arg_mesh.name = "mesh_path";
		arg_mesh.type_hint = Variant::STRING;
		arg_mesh.required = false;
		schema.arguments.push_back(arg_mesh);

		ToolArgSchema arg_count;
		arg_count.name = "instance_count";
		arg_count.type_hint = Variant::INT;
		arg_count.required = false;
		arg_count.default_value = 0;
		schema.arguments.push_back(arg_count);

		schemas["create_multimesh_instance_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_touch_screen_button";
		schema.description = "Create a TouchScreenButton for mobile input";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("TouchScreenButton");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_texture;
		arg_texture.name = "texture_path";
		arg_texture.type_hint = Variant::STRING;
		arg_texture.required = false;
		schema.arguments.push_back(arg_texture);

		ToolArgSchema arg_pressed;
		arg_pressed.name = "pressed_texture_path";
		arg_pressed.type_hint = Variant::STRING;
		arg_pressed.required = false;
		schema.arguments.push_back(arg_pressed);

		ToolArgSchema arg_passby;
		arg_passby.name = "passby_press";
		arg_passby.type_hint = Variant::BOOL;
		arg_passby.required = false;
		arg_passby.default_value = false;
		schema.arguments.push_back(arg_passby);

		ToolArgSchema arg_visible;
		arg_visible.name = "always_visible";
		arg_visible.type_hint = Variant::BOOL;
		arg_visible.required = false;
		arg_visible.default_value = true;
		schema.arguments.push_back(arg_visible);

		schemas["create_touch_screen_button"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_subviewport";
		schema.description = "Create a SubViewport for minimaps, split-screen, or render-to-texture";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("SubViewport");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_w;
		arg_w.name = "width";
		arg_w.type_hint = Variant::INT;
		arg_w.required = false;
		arg_w.default_value = 512;
		schema.arguments.push_back(arg_w);

		ToolArgSchema arg_h;
		arg_h.name = "height";
		arg_h.type_hint = Variant::INT;
		arg_h.required = false;
		arg_h.default_value = 512;
		schema.arguments.push_back(arg_h);

		ToolArgSchema arg_disable3d;
		arg_disable3d.name = "disable_3d";
		arg_disable3d.type_hint = Variant::BOOL;
		arg_disable3d.required = false;
		arg_disable3d.default_value = true;
		schema.arguments.push_back(arg_disable3d);

		schemas["create_subviewport"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_mesh_instance_2d";
		schema.description = "Create a MeshInstance2D for custom 2D meshes";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("MeshInstance2D");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_mesh;
		arg_mesh.name = "mesh_path";
		arg_mesh.type_hint = Variant::STRING;
		arg_mesh.required = false;
		schema.arguments.push_back(arg_mesh);

		ToolArgSchema arg_tex;
		arg_tex.name = "texture_path";
		arg_tex.type_hint = Variant::STRING;
		arg_tex.required = false;
		schema.arguments.push_back(arg_tex);

		schemas["create_mesh_instance_2d"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "query_raycast_2d";
		schema.description = "Query an existing RayCast2D node for collision results";
		schema.requires_scene = true;
		schema.is_write_operation = false;
		schema.supports_batching = false;

		ToolArgSchema arg_path;
		arg_path.name = "node_path";
		arg_path.type_hint = Variant::STRING;
		arg_path.required = true;
		arg_path.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_path);

		schemas["query_raycast_2d"] = schema;
	}

	// === UI TOOLS ===

	{
		ToolSchema schema;
		schema.tool_name = "create_button";
		schema.description = "Create a Button UI node";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("Button");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_text;
		arg_text.name = "text";
		arg_text.type_hint = Variant::STRING;
		arg_text.required = false;
		arg_text.default_value = String("Button");
		schema.arguments.push_back(arg_text);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_w;
		arg_w.name = "width";
		arg_w.type_hint = Variant::FLOAT;
		arg_w.required = false;
		arg_w.default_value = 100.0;
		schema.arguments.push_back(arg_w);

		ToolArgSchema arg_h;
		arg_h.name = "height";
		arg_h.type_hint = Variant::FLOAT;
		arg_h.required = false;
		arg_h.default_value = 30.0;
		schema.arguments.push_back(arg_h);

		ToolArgSchema arg_disabled;
		arg_disabled.name = "disabled";
		arg_disabled.type_hint = Variant::BOOL;
		arg_disabled.required = false;
		arg_disabled.default_value = false;
		schema.arguments.push_back(arg_disabled);

		ToolArgSchema arg_toggle;
		arg_toggle.name = "toggle_mode";
		arg_toggle.type_hint = Variant::BOOL;
		arg_toggle.required = false;
		arg_toggle.default_value = false;
		schema.arguments.push_back(arg_toggle);

		schemas["create_button"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_label";
		schema.description = "Create a Label UI node for displaying text";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("Label");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_text;
		arg_text.name = "text";
		arg_text.type_hint = Variant::STRING;
		arg_text.required = false;
		arg_text.default_value = String("");
		schema.arguments.push_back(arg_text);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_w;
		arg_w.name = "width";
		arg_w.type_hint = Variant::FLOAT;
		arg_w.required = false;
		arg_w.default_value = 100.0;
		schema.arguments.push_back(arg_w);

		ToolArgSchema arg_h;
		arg_h.name = "height";
		arg_h.type_hint = Variant::FLOAT;
		arg_h.required = false;
		arg_h.default_value = 30.0;
		schema.arguments.push_back(arg_h);

		ToolArgSchema arg_align;
		arg_align.name = "align";
		arg_align.type_hint = Variant::INT;
		arg_align.required = false;
		arg_align.default_value = 0;
		arg_align.description = "0=left, 1=center, 2=right, 3=fill";
		schema.arguments.push_back(arg_align);

		ToolArgSchema arg_valign;
		arg_valign.name = "valign";
		arg_valign.type_hint = Variant::INT;
		arg_valign.required = false;
		arg_valign.default_value = 0;
		arg_valign.description = "0=top, 1=center, 2=bottom, 3=fill";
		schema.arguments.push_back(arg_valign);

		schemas["create_label"] = schema;
	}

	// === UI TOOLS ===

	{
		ToolSchema schema;
		schema.tool_name = "create_texture_rect";
		schema.description = "Create a TextureRect to display an image/texture in the UI";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("TextureRect");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_tex;
		arg_tex.name = "texture_path";
		arg_tex.type_hint = Variant::STRING;
		arg_tex.required = false;
		schema.arguments.push_back(arg_tex);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_w;
		arg_w.name = "width";
		arg_w.type_hint = Variant::FLOAT;
		arg_w.required = false;
		arg_w.default_value = 64.0;
		schema.arguments.push_back(arg_w);

		ToolArgSchema arg_h;
		arg_h.name = "height";
		arg_h.type_hint = Variant::FLOAT;
		arg_h.required = false;
		arg_h.default_value = 64.0;
		schema.arguments.push_back(arg_h);

		ToolArgSchema arg_stretch;
		arg_stretch.name = "stretch_mode";
		arg_stretch.type_hint = Variant::INT;
		arg_stretch.required = false;
		arg_stretch.default_value = 0;
		arg_stretch.description = "0=scale, 1=tile, 2=keep, 3=keep_centered, 4=keep_aspect, 5=keep_aspect_centered, 6=keep_aspect_covered";
		schema.arguments.push_back(arg_stretch);

		schemas["create_texture_rect"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_nine_patch_rect";
		schema.description = "Create a NinePatchRect for scalable UI frames using 9-slice scaling";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("NinePatchRect");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_tex;
		arg_tex.name = "texture_path";
		arg_tex.type_hint = Variant::STRING;
		arg_tex.required = false;
		schema.arguments.push_back(arg_tex);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_w;
		arg_w.name = "width";
		arg_w.type_hint = Variant::FLOAT;
		arg_w.required = false;
		arg_w.default_value = 64.0;
		schema.arguments.push_back(arg_w);

		ToolArgSchema arg_h;
		arg_h.name = "height";
		arg_h.type_hint = Variant::FLOAT;
		arg_h.required = false;
		arg_h.default_value = 64.0;
		schema.arguments.push_back(arg_h);

		ToolArgSchema arg_region;
		arg_region.name = "region_rect";
		arg_region.type_hint = Variant::DICTIONARY;
		arg_region.required = false;
		arg_region.description = "{x, y, w, h} region of the texture to use";
		schema.arguments.push_back(arg_region);

		ToolArgSchema arg_margins;
		arg_margins.name = "patch_margin";
		arg_margins.type_hint = Variant::DICTIONARY;
		arg_margins.required = false;
		arg_margins.description = "{left, top, right, bottom} margins for 9-slice scaling";
		schema.arguments.push_back(arg_margins);

		schemas["create_nine_patch_rect"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_color_rect";
		schema.description = "Create a ColorRect for solid color backgrounds and overlays";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("ColorRect");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_color;
		arg_color.name = "color";
		arg_color.type_hint = Variant::DICTIONARY;
		arg_color.required = false;
		arg_color.default_value = Dictionary();
		arg_color.description = "Color as {r, g, b} or hex string (e.g., '#FF5733')";
		schema.arguments.push_back(arg_color);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_w;
		arg_w.name = "width";
		arg_w.type_hint = Variant::FLOAT;
		arg_w.required = false;
		arg_w.default_value = 64.0;
		schema.arguments.push_back(arg_w);

		ToolArgSchema arg_h;
		arg_h.name = "height";
		arg_h.type_hint = Variant::FLOAT;
		arg_h.required = false;
		arg_h.default_value = 64.0;
		schema.arguments.push_back(arg_h);

		schemas["create_color_rect"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_rich_text_label";
		schema.description = "Create a RichTextLabel for formatted text with BBCode support";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("RichTextLabel");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_text;
		arg_text.name = "text";
		arg_text.type_hint = Variant::STRING;
		arg_text.required = false;
		arg_text.default_value = String("");
		schema.arguments.push_back(arg_text);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_w;
		arg_w.name = "width";
		arg_w.type_hint = Variant::FLOAT;
		arg_w.required = false;
		arg_w.default_value = 200.0;
		schema.arguments.push_back(arg_w);

		ToolArgSchema arg_h;
		arg_h.name = "height";
		arg_h.type_hint = Variant::FLOAT;
		arg_h.required = false;
		arg_h.default_value = 100.0;
		schema.arguments.push_back(arg_h);

		ToolArgSchema arg_fit;
		arg_fit.name = "fit_content";
		arg_fit.type_hint = Variant::BOOL;
		arg_fit.required = false;
		arg_fit.default_value = false;
		schema.arguments.push_back(arg_fit);

		schemas["create_rich_text_label"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_texture_progress_bar";
		schema.description = "Create a TextureProgressBar for stylized progress bars (health, mana, cooldown)";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("TextureProgressBar");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_under;
		arg_under.name = "texture_under";
		arg_under.type_hint = Variant::STRING;
		arg_under.required = false;
		schema.arguments.push_back(arg_under);

		ToolArgSchema arg_over;
		arg_over.name = "texture_over";
		arg_over.type_hint = Variant::STRING;
		arg_over.required = false;
		schema.arguments.push_back(arg_over);

		ToolArgSchema arg_progress;
		arg_progress.name = "texture_progress";
		arg_progress.type_hint = Variant::STRING;
		arg_progress.required = false;
		schema.arguments.push_back(arg_progress);

		ToolArgSchema arg_value;
		arg_value.name = "value";
		arg_value.type_hint = Variant::FLOAT;
		arg_value.required = false;
		arg_value.default_value = 0.0;
		schema.arguments.push_back(arg_value);

		ToolArgSchema arg_min;
		arg_min.name = "min";
		arg_min.type_hint = Variant::FLOAT;
		arg_min.required = false;
		arg_min.default_value = 0.0;
		schema.arguments.push_back(arg_min);

		ToolArgSchema arg_max;
		arg_max.name = "max";
		arg_max.type_hint = Variant::FLOAT;
		arg_max.required = false;
		arg_max.default_value = 100.0;
		schema.arguments.push_back(arg_max);

		ToolArgSchema arg_fill;
		arg_fill.name = "fill_mode";
		arg_fill.type_hint = Variant::INT;
		arg_fill.required = false;
		arg_fill.default_value = 0;
		arg_fill.description = "0=left-right, 1=right-left, 2=top-bottom, 3=bottom-top, 4=clockwise, 5=counter-clockwise";
		schema.arguments.push_back(arg_fill);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_w;
		arg_w.name = "width";
		arg_w.type_hint = Variant::FLOAT;
		arg_w.required = false;
		arg_w.default_value = 64.0;
		schema.arguments.push_back(arg_w);

		ToolArgSchema arg_h;
		arg_h.name = "height";
		arg_h.type_hint = Variant::FLOAT;
		arg_h.required = false;
		arg_h.default_value = 64.0;
		schema.arguments.push_back(arg_h);

		schemas["create_texture_progress_bar"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_line_edit";
		schema.description = "Create a LineEdit for single-line text input";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("LineEdit");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_text;
		arg_text.name = "text";
		arg_text.type_hint = Variant::STRING;
		arg_text.required = false;
		arg_text.default_value = String("");
		schema.arguments.push_back(arg_text);

		ToolArgSchema arg_placeholder;
		arg_placeholder.name = "placeholder_text";
		arg_placeholder.type_hint = Variant::STRING;
		arg_placeholder.required = false;
		arg_placeholder.default_value = String("");
		schema.arguments.push_back(arg_placeholder);

		ToolArgSchema arg_editable;
		arg_editable.name = "editable";
		arg_editable.type_hint = Variant::BOOL;
		arg_editable.required = false;
		arg_editable.default_value = true;
		schema.arguments.push_back(arg_editable);

		ToolArgSchema arg_secret;
		arg_secret.name = "secret";
		arg_secret.type_hint = Variant::BOOL;
		arg_secret.required = false;
		arg_secret.default_value = false;
		schema.arguments.push_back(arg_secret);

		ToolArgSchema arg_max;
		arg_max.name = "max_length";
		arg_max.type_hint = Variant::INT;
		arg_max.required = false;
		arg_max.default_value = 0;
		schema.arguments.push_back(arg_max);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_w;
		arg_w.name = "width";
		arg_w.type_hint = Variant::FLOAT;
		arg_w.required = false;
		arg_w.default_value = 150.0;
		schema.arguments.push_back(arg_w);

		ToolArgSchema arg_h;
		arg_h.name = "height";
		arg_h.type_hint = Variant::FLOAT;
		arg_h.required = false;
		arg_h.default_value = 30.0;
		schema.arguments.push_back(arg_h);

		schemas["create_line_edit"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_text_edit";
		schema.description = "Create a TextEdit for multi-line text editing";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("TextEdit");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_text;
		arg_text.name = "text";
		arg_text.type_hint = Variant::STRING;
		arg_text.required = false;
		arg_text.default_value = String("");
		schema.arguments.push_back(arg_text);

		ToolArgSchema arg_placeholder;
		arg_placeholder.name = "placeholder_text";
		arg_placeholder.type_hint = Variant::STRING;
		arg_placeholder.required = false;
		arg_placeholder.default_value = String("");
		schema.arguments.push_back(arg_placeholder);

		ToolArgSchema arg_editable;
		arg_editable.name = "editable";
		arg_editable.type_hint = Variant::BOOL;
		arg_editable.required = false;
		arg_editable.default_value = true;
		schema.arguments.push_back(arg_editable);

		ToolArgSchema arg_wrap;
		arg_wrap.name = "wrap_mode";
		arg_wrap.type_hint = Variant::INT;
		arg_wrap.required = false;
		arg_wrap.default_value = 0;
		arg_wrap.description = "0=off, 1=boundary, 2=char";
		schema.arguments.push_back(arg_wrap);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_w;
		arg_w.name = "width";
		arg_w.type_hint = Variant::FLOAT;
		arg_w.required = false;
		arg_w.default_value = 200.0;
		schema.arguments.push_back(arg_w);

		ToolArgSchema arg_h;
		arg_h.name = "height";
		arg_h.type_hint = Variant::FLOAT;
		arg_h.required = false;
		arg_h.default_value = 100.0;
		schema.arguments.push_back(arg_h);

		schemas["create_text_edit"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_check_box";
		schema.description = "Create a CheckBox for boolean toggles";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("CheckBox");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_text;
		arg_text.name = "text";
		arg_text.type_hint = Variant::STRING;
		arg_text.required = false;
		arg_text.default_value = String("");
		schema.arguments.push_back(arg_text);

		ToolArgSchema arg_pressed;
		arg_pressed.name = "pressed";
		arg_pressed.type_hint = Variant::BOOL;
		arg_pressed.required = false;
		arg_pressed.default_value = false;
		schema.arguments.push_back(arg_pressed);

		ToolArgSchema arg_disabled;
		arg_disabled.name = "disabled";
		arg_disabled.type_hint = Variant::BOOL;
		arg_disabled.required = false;
		arg_disabled.default_value = false;
		schema.arguments.push_back(arg_disabled);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		schemas["create_check_box"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_spin_box";
		schema.description = "Create a SpinBox for numeric input with min/max/step";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("SpinBox");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_min;
		arg_min.name = "min";
		arg_min.type_hint = Variant::FLOAT;
		arg_min.required = false;
		arg_min.default_value = 0.0;
		schema.arguments.push_back(arg_min);

		ToolArgSchema arg_max;
		arg_max.name = "max";
		arg_max.type_hint = Variant::FLOAT;
		arg_max.required = false;
		arg_max.default_value = 100.0;
		schema.arguments.push_back(arg_max);

		ToolArgSchema arg_step;
		arg_step.name = "step";
		arg_step.type_hint = Variant::FLOAT;
		arg_step.required = false;
		arg_step.default_value = 1.0;
		schema.arguments.push_back(arg_step);

		ToolArgSchema arg_value;
		arg_value.name = "value";
		arg_value.type_hint = Variant::FLOAT;
		arg_value.required = false;
		arg_value.default_value = 0.0;
		schema.arguments.push_back(arg_value);

		ToolArgSchema arg_prefix;
		arg_prefix.name = "prefix";
		arg_prefix.type_hint = Variant::STRING;
		arg_prefix.required = false;
		arg_prefix.default_value = String("");
		schema.arguments.push_back(arg_prefix);

		ToolArgSchema arg_suffix;
		arg_suffix.name = "suffix";
		arg_suffix.type_hint = Variant::STRING;
		arg_suffix.required = false;
		arg_suffix.default_value = String("");
		schema.arguments.push_back(arg_suffix);

		ToolArgSchema arg_editable;
		arg_editable.name = "editable";
		arg_editable.type_hint = Variant::BOOL;
		arg_editable.required = false;
		arg_editable.default_value = true;
		schema.arguments.push_back(arg_editable);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_w;
		arg_w.name = "width";
		arg_w.type_hint = Variant::FLOAT;
		arg_w.required = false;
		arg_w.default_value = 80.0;
		schema.arguments.push_back(arg_w);

		ToolArgSchema arg_h;
		arg_h.name = "height";
		arg_h.type_hint = Variant::FLOAT;
		arg_h.required = false;
		arg_h.default_value = 30.0;
		schema.arguments.push_back(arg_h);

		schemas["create_spin_box"] = schema;
	}

	{
		ToolSchema schema;
		schema.tool_name = "create_panel_container";
		schema.description = "Create a PanelContainer for grouped UI sections with optional custom stylebox";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		schema.supports_batching = true;

		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String("PanelContainer");
		schema.arguments.push_back(arg_name);

		ToolArgSchema arg_parent;
		arg_parent.name = "parent_path";
		arg_parent.type_hint = Variant::STRING;
		arg_parent.required = false;
		arg_parent.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_parent);

		ToolArgSchema arg_x;
		arg_x.name = "x";
		arg_x.type_hint = Variant::FLOAT;
		arg_x.required = false;
		arg_x.default_value = 0.0;
		schema.arguments.push_back(arg_x);

		ToolArgSchema arg_y;
		arg_y.name = "y";
		arg_y.type_hint = Variant::FLOAT;
		arg_y.required = false;
		arg_y.default_value = 0.0;
		schema.arguments.push_back(arg_y);

		ToolArgSchema arg_w;
		arg_w.name = "width";
		arg_w.type_hint = Variant::FLOAT;
		arg_w.required = false;
		arg_w.default_value = 200.0;
		schema.arguments.push_back(arg_w);

		ToolArgSchema arg_h;
		arg_h.name = "height";
		arg_h.type_hint = Variant::FLOAT;
		arg_h.required = false;
		arg_h.default_value = 150.0;
		schema.arguments.push_back(arg_h);

		ToolArgSchema arg_style;
		arg_style.name = "stylebox_path";
		arg_style.type_hint = Variant::STRING;
		arg_style.required = false;
		arg_style.description = "Optional path to a StyleBox resource for custom panel appearance";
		schema.arguments.push_back(arg_style);

		schemas["create_panel_container"] = schema;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// BULK SCHEMA BACKFILL — compact group-based registration
	// Uses helpers to register tools with shared argument patterns.
	// All additional args marked optional to avoid rejecting valid calls.
	// ═══════════════════════════════════════════════════════════════════════════

	// Helper: register a node creation tool with name + parent_path + optional position
	auto reg_create_node = [&](const char *tool, const char *desc, bool is_2d) {
		String t(tool);
		if (schemas.has(t)) return;
		ToolSchema s;
		s.tool_name = t;
		s.description = desc;
		s.requires_scene = true;
		s.is_write_operation = true;
		s.supports_batching = true;
		ToolArgSchema a_name; a_name.name = "name"; a_name.type_hint = Variant::STRING; a_name.required = true; a_name.description = "Node name"; s.arguments.push_back(a_name);
		ToolArgSchema a_parent; a_parent.name = "parent_path"; a_parent.type_hint = Variant::STRING; a_parent.required = false; a_parent.auto_resolve_node_path = true; a_parent.description = "Parent node path"; s.arguments.push_back(a_parent);
		if (is_2d) {
			ToolArgSchema ax; ax.name = "x"; ax.type_hint = Variant::FLOAT; ax.required = false; s.arguments.push_back(ax);
			ToolArgSchema ay; ay.name = "y"; ay.type_hint = Variant::FLOAT; ay.required = false; s.arguments.push_back(ay);
		} else {
			ToolArgSchema ap; ap.name = "position"; ap.type_hint = Variant::VECTOR3; ap.required = false; s.arguments.push_back(ap);
		}
		schemas[t] = s;
	};

	// Helper: register a read-only tool with optional node_path
	auto reg_read_node = [&](const char *tool, const char *desc, bool needs_path) {
		String t(tool);
		if (schemas.has(t)) return;
		ToolSchema s;
		s.tool_name = t;
		s.description = desc;
		s.requires_scene = true;
		s.is_write_operation = false;
		if (needs_path) {
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; ap.description = "Node path"; s.arguments.push_back(ap);
		}
		schemas[t] = s;
	};

	// Helper: register a write tool on a node path
	auto reg_write_node = [&](const char *tool, const char *desc) {
		String t(tool);
		if (schemas.has(t)) return;
		ToolSchema s;
		s.tool_name = t;
		s.description = desc;
		s.requires_scene = true;
		s.is_write_operation = true;
		s.supports_batching = true;
		ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; ap.description = "Node path"; s.arguments.push_back(ap);
		schemas[t] = s;
	};

	// Helper: register a file/project tool (no scene required)
	auto reg_file_tool = [&](const char *tool, const char *desc, bool is_write) {
		String t(tool);
		if (schemas.has(t)) return;
		ToolSchema s;
		s.tool_name = t;
		s.description = desc;
		s.requires_scene = false;
		s.is_write_operation = is_write;
		ToolArgSchema ap; ap.name = "path"; ap.type_hint = Variant::STRING; ap.required = true; ap.description = "File path"; s.arguments.push_back(ap);
		schemas[t] = s;
	};

	// Helper: register a no-arg read-only tool
	auto reg_noarg_read = [&](const char *tool, const char *desc, bool needs_scene) {
		String t(tool);
		if (schemas.has(t)) return;
		ToolSchema s;
		s.tool_name = t;
		s.description = desc;
		s.requires_scene = needs_scene;
		s.is_write_operation = false;
		schemas[t] = s;
	};

	// ── 2D BODY NODES ──
	reg_create_node("create_area_2d", "Create an Area2D node", true);
	reg_create_node("create_rigid_body_2d", "Create a RigidBody2D node", true);
	reg_create_node("create_static_body_2d", "Create a StaticBody2D node", true);
	reg_create_node("create_animatable_body_2d", "Create an AnimatableBody2D node", true);
	reg_create_node("create_ray_cast_2d", "Create a RayCast2D node", true);
	reg_create_node("create_shape_cast_2d", "Create a ShapeCast2D node", true);
	reg_create_node("create_line_2d", "Create a Line2D node", true);
	reg_create_node("create_path_2d", "Create a Path2D node", true);
	reg_create_node("create_polygon_2d", "Create a Polygon2D node", true);
	reg_create_node("create_marker_2d", "Create a Marker2D node", true);
	reg_create_node("create_light_2d", "Create a PointLight2D node", true);
	reg_create_node("create_cpu_particles_2d", "Create a CPUParticles2D node", true);
	reg_create_node("create_gpu_particles_2d", "Create a GPUParticles2D node", true);
	reg_create_node("create_canvas_layer", "Create a CanvasLayer node", true);
	reg_create_node("create_parallax_background", "Create a ParallaxBackground node", true);
	reg_create_node("create_parallax_layer", "Create a ParallaxLayer node", true);
	reg_create_node("create_navigation_region_2d", "Create a NavigationRegion2D node", true);
	reg_create_node("create_navigation_agent_2d", "Create a NavigationAgent2D node", true);
	reg_create_node("create_collision_polygon_2d", "Create a CollisionPolygon2D node", true);
	reg_create_node("create_remote_transform_2d", "Create a RemoteTransform2D node", true);
	reg_create_node("create_visible_on_screen_notifier_2d", "Create a VisibleOnScreenNotifier2D node", true);
	reg_create_node("create_audio_stream_player_2d", "Create an AudioStreamPlayer2D node", true);
	reg_create_node("create_mesh_instance_2d", "Create a MeshInstance2D node", true);
	reg_create_node("create_path_follow_2d", "Create a PathFollow2D node", true);
	reg_create_node("create_skeleton_2d", "Create a Skeleton2D node", true);
	reg_create_node("create_bone_2d", "Create a Bone2D node", true);
	reg_create_node("create_multimesh_instance_2d", "Create a MultiMeshInstance2D node", true);

	// ── 3D BODY NODES ──
	reg_create_node("create_area_3d", "Create an Area3D node", false);
	reg_create_node("create_rigid_body_3d", "Create a RigidBody3D node", false);
	reg_create_node("create_static_body_3d", "Create a StaticBody3D node", false);
	reg_create_node("create_character_body_3d", "Create a CharacterBody3D node", false);
	reg_create_node("create_ray_cast_3d", "Create a RayCast3D node", false);
	reg_create_node("create_shape_cast_3d", "Create a ShapeCast3D node", false);
	reg_create_node("create_vehicle_body_3d", "Create a VehicleBody3D node", false);
	reg_create_node("create_path_3d", "Create a Path3D node", false);
	reg_create_node("create_gi_probe", "Create a VoxelGI node", false);
	reg_create_node("create_reflection_probe", "Create a ReflectionProbe node", false);
	reg_create_node("create_fog_volume", "Create a FogVolume node", false);
	reg_create_node("create_particle_emitter", "Create a GPUParticles2D/3D node", false);

	// ── UI NODES ──
	reg_create_node("create_control_node", "Create a Control-derived UI node", true);
	reg_create_node("create_container_layout", "Create a container layout node", true);
	reg_create_node("create_scroll_container", "Create a ScrollContainer node", true);
	reg_create_node("create_progress_bar", "Create a ProgressBar node", true);
	reg_create_node("create_slider", "Create a Slider node", true);
	reg_create_node("create_item_list", "Create an ItemList node", true);
	reg_create_node("create_option_button", "Create an OptionButton node", true);
	reg_create_node("create_tab_container", "Create a TabContainer node", true);
	reg_create_node("create_graph_node", "Create a GraphNode node", true);
	reg_create_node("create_tree_widget", "Create a Tree widget node", true);
	reg_create_node("create_margin_container", "Create a MarginContainer node", true);
	reg_create_node("create_h_separator", "Create an HSeparator node", true);
	reg_create_node("create_v_separator", "Create a VSeparator node", true);
	reg_create_node("create_reference_rect", "Create a ReferenceRect node", true);
	reg_create_node("create_aspect_ratio_container", "Create an AspectRatioContainer node", true);
	reg_create_node("create_accept_dialog", "Create an AcceptDialog node", true);
	reg_create_node("create_confirmation_dialog", "Create a ConfirmationDialog node", true);
	reg_create_node("create_subviewport_container", "Create a SubViewportContainer node", true);
	reg_create_node("create_audio_player", "Create an AudioStreamPlayer node", true);

	// ── INSPECTION TOOLS ──
	reg_read_node("get_node_api", "Get signals, methods, properties of a node", true);
	reg_read_node("get_node_groups", "Get groups a node belongs to", true);
	reg_read_node("get_node_collision_layers", "Get collision layer/mask of a node", true);
	reg_read_node("get_signal_connections", "Get signal connections from a node", true);
	reg_read_node("get_animation_player_state", "Get animation player tracks/keys", true);
	reg_read_node("get_tilemap_info", "Get tilemap cells and sources", true);
	reg_read_node("get_tileset_sources", "Get tileset source atlas info", true);
	reg_read_node("get_navigation_region_info", "Get navigation region agents/obstacles/links", true);
	reg_read_node("get_world_environment", "Get environment settings", false);
	reg_read_node("get_shader_uniforms", "Get shader uniform names and values", true);
	reg_read_node("inspect_runtime_node", "Inspect a runtime node properties", true);

	reg_noarg_read("get_autoloads", "List project autoloads", false);
	reg_noarg_read("get_audio_buses", "Get audio bus configuration", false);
	reg_noarg_read("get_console_output", "Get console/output messages", false);
	reg_noarg_read("get_debug_snapshot", "Get debug variables, memory, scene tree", false);
	reg_noarg_read("get_runtime_debugger_state", "Get runtime errors, warnings, stack", false);
	reg_noarg_read("get_editor_3d_camera_transform", "Get editor 3D camera position/rotation", true);
	reg_noarg_read("get_editor_inspector_subject", "Get currently inspected node", true);
	reg_noarg_read("get_editor_version", "Get Godot editor version string", false);
	reg_noarg_read("get_export_presets", "List export presets", false);
	reg_noarg_read("get_global_classes", "List global script classes", false);
	reg_noarg_read("get_network_state", "Get multiplayer network state", false);
	reg_noarg_read("get_remote_scene_tree", "Get running game scene tree", false);
	reg_noarg_read("get_translation_overview", "Get translation keys/values", false);
	reg_noarg_read("get_unsaved_scenes", "List unsaved scene paths", false);
	reg_noarg_read("get_binary_file_metadata", "Get file size and format info", false);
	reg_noarg_read("get_collision_exceptions", "Get collision exceptions for a node", true);
	reg_noarg_read("get_resource_dependencies", "Get resource dependencies list", false);
	reg_noarg_read("get_scene_dependency_closure", "Get scene dependency closure", false);
	reg_noarg_read("profile_frame", "Get frame time, physics time, draw calls", false);
	reg_noarg_read("monitor_runtime_performance", "Get FPS, CPU, memory, vertices", false);

	// ── WRITE/SET TOOLS (node-path based) ──
	reg_write_node("set_collision_layer_mask", "Set collision layer and mask");
	reg_write_node("set_node_collision_layers", "Set node collision layers");
	reg_write_node("set_control_layout", "Set control layout anchors/offsets");
	reg_write_node("set_control_theme_override", "Set theme override on control");
	reg_write_node("set_navigation_agent_params", "Set navigation agent parameters");
	reg_write_node("set_physics_material", "Set physics material properties");
	reg_write_node("set_node_meta", "Set/get/remove node metadata");
	reg_write_node("set_environment_fog", "Set environment fog parameters");
	reg_write_node("set_environment_tonemap", "Set tonemap parameters");
	reg_write_node("set_environment_ss_effects", "Set SSAO/SSR/glow parameters");
	reg_write_node("set_world_environment", "Set world environment properties");
	reg_write_node("set_editor_3d_camera", "Set editor 3D camera transform");
	reg_write_node("add_collision_exception", "Add collision exception pair");
	reg_write_node("remove_collision_exception", "Remove collision exception pair");
	reg_write_node("add_csg_primitive", "Add CSG primitive to node");
	reg_write_node("disconnect_signal", "Disconnect a signal connection");
	reg_write_node("clear_tilemap_cells", "Clear tilemap cells in region");
	reg_write_node("paint_terrain", "Paint terrain cells");
	reg_write_node("focus_scene_tree_node", "Focus/select node in scene tree");
	reg_write_node("play_audio", "Play audio on AudioStreamPlayer");
	reg_write_node("stop_audio", "Stop audio on AudioStreamPlayer");

	// ── FILE/PROJECT TOOLS ──
	reg_file_tool("write_project_file", "Write text content to a project file", true);
	reg_file_tool("delete_project_file", "Delete a project file (requires confirm:true)", true);
	reg_file_tool("move_project_file", "Move/rename a project file", true);
	reg_file_tool("copy_project_file", "Copy a project file", true);
	reg_file_tool("import_asset", "Import an external asset into the project", true);
	reg_file_tool("select_file", "Select file in FileSystem dock", false);
	reg_file_tool("save_resource", "Save a resource to disk", true);
	reg_file_tool("resolve_resource_uid", "Resolve UID to file path", false);
	reg_file_tool("get_shader_code", "Read shader source code", false);
	reg_file_tool("update_shader_code", "Update shader file contents", true);
	reg_file_tool("lint_gdscript", "Lint a GDScript file for issues", false);
	reg_file_tool("get_gdscript_symbols", "Get script classes/functions/signals/properties", false);
	reg_file_tool("get_gdscript_docs", "Get script documentation comments", false);

	// ── NO-SCENE FILE TOOLS ──
	{
		if (!schemas.has("find_project_files")) {
			ToolSchema s; s.tool_name = "find_project_files"; s.description = "Search project files by name/extension"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "root"; a.type_hint = Variant::STRING; a.required = false; s.arguments.push_back(a);
			ToolArgSchema aq; aq.name = "query"; aq.type_hint = Variant::STRING; aq.required = false; s.arguments.push_back(aq);
			schemas["find_project_files"] = s;
		}
		if (!schemas.has("grep_project_files")) {
			ToolSchema s; s.tool_name = "grep_project_files"; s.description = "Search file contents with regex"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "query"; a.type_hint = Variant::STRING; a.required = true; a.description = "Search pattern"; s.arguments.push_back(a);
			ToolArgSchema ap; ap.name = "path"; ap.type_hint = Variant::STRING; ap.required = true; ap.description = "Root path to search"; s.arguments.push_back(ap);
			schemas["grep_project_files"] = s;
		}
		if (!schemas.has("replace_in_project_files")) {
			ToolSchema s; s.tool_name = "replace_in_project_files"; s.description = "Find and replace in project files"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema aq; aq.name = "query"; aq.type_hint = Variant::STRING; aq.required = true; s.arguments.push_back(aq);
			ToolArgSchema ar; ar.name = "replacement"; ar.type_hint = Variant::STRING; ar.required = true; s.arguments.push_back(ar);
			ToolArgSchema ap; ap.name = "path"; ap.type_hint = Variant::STRING; ap.required = true; s.arguments.push_back(ap);
			schemas["replace_in_project_files"] = s;
		}
		if (!schemas.has("reimport_project_files")) {
			ToolSchema s; s.tool_name = "reimport_project_files"; s.description = "Reimport files through Godot import pipeline"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "paths"; a.type_hint = Variant::ARRAY; a.required = true; s.arguments.push_back(a);
			schemas["reimport_project_files"] = s;
		}
		if (!schemas.has("scan_project_filesystem")) {
			ToolSchema s; s.tool_name = "scan_project_filesystem"; s.description = "Rescan project filesystem"; s.requires_scene = false; s.is_write_operation = false;
			schemas["scan_project_filesystem"] = s;
		}
		if (!schemas.has("rename_resource_references")) {
			ToolSchema s; s.tool_name = "rename_resource_references"; s.description = "Rename resource references project-wide"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ao; ao.name = "old_name"; ao.type_hint = Variant::STRING; ao.required = true; s.arguments.push_back(ao);
			ToolArgSchema an; an.name = "new_name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			schemas["rename_resource_references"] = s;
		}
	}

	// ── EDITOR ACTIONS ──
	reg_noarg_read("get_editor_settings", "Get editor settings", false);
	{
		if (!schemas.has("set_editor_main_screen")) {
			ToolSchema s; s.tool_name = "set_editor_main_screen"; s.description = "Switch editor main screen"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "screen"; a.type_hint = Variant::STRING; a.required = true; a.valid_values = Vector<String>{"3D", "2D", "Script", "Animation", "AssetLib"}; s.arguments.push_back(a);
			schemas["set_editor_main_screen"] = s;
		}
		if (!schemas.has("editor_undo")) {
			ToolSchema s; s.tool_name = "editor_undo"; s.description = "Undo editor actions"; s.requires_scene = true; s.is_write_operation = true;
			ToolArgSchema a; a.name = "steps"; a.type_hint = Variant::INT; a.required = false; a.min_value = 1; a.max_value = 50; s.arguments.push_back(a);
			schemas["editor_undo"] = s;
		}
		if (!schemas.has("edit_script")) {
			ToolSchema s; s.tool_name = "edit_script"; s.description = "Open script in editor"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "script_path"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["edit_script"] = s;
		}
		if (!schemas.has("capture_editor_viewport")) {
			ToolSchema s; s.tool_name = "capture_editor_viewport"; s.description = "Capture editor viewport as PNG"; s.requires_scene = true; s.is_write_operation = false;
			schemas["capture_editor_viewport"] = s;
		}
		if (!schemas.has("capture_game_viewport")) {
			ToolSchema s; s.tool_name = "capture_game_viewport"; s.description = "Capture running game viewport as PNG"; s.requires_scene = false; s.is_write_operation = false;
			schemas["capture_game_viewport"] = s;
		}
		if (!schemas.has("capture_dual_view")) {
			ToolSchema s; s.tool_name = "capture_dual_view"; s.description = "Capture both editor and game viewports"; s.requires_scene = true; s.is_write_operation = false;
			schemas["capture_dual_view"] = s;
		}
		if (!schemas.has("capture_subviewport")) {
			ToolSchema s; s.tool_name = "capture_subviewport"; s.description = "Capture a SubViewport as PNG"; s.requires_scene = true; s.is_write_operation = false;
			ToolArgSchema a; a.name = "node_path"; a.type_hint = Variant::STRING; a.required = true; a.auto_resolve_node_path = true; s.arguments.push_back(a);
			schemas["capture_subviewport"] = s;
		}
		if (!schemas.has("capture_texture_resource")) {
			ToolSchema s; s.tool_name = "capture_texture_resource"; s.description = "Capture a texture resource as PNG"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "path"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["capture_texture_resource"] = s;
		}
		if (!schemas.has("export_project")) {
			ToolSchema s; s.tool_name = "export_project"; s.description = "Export project using preset"; s.requires_scene = false; s.is_write_operation = true;
			schemas["export_project"] = s;
		}
		if (!schemas.has("run_scene_script")) {
			ToolSchema s; s.tool_name = "run_scene_script"; s.description = "Run a GDScript scene script"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "scene_path"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["run_scene_script"] = s;
		}
	}

	// ── SCRIPTING TOOLS ──
	{
		if (!schemas.has("get_gdscript_errors")) {
			ToolSchema s; s.tool_name = "get_gdscript_errors"; s.description = "Get GDScript parse errors"; s.requires_scene = false; s.is_write_operation = false;
			schemas["get_gdscript_errors"] = s;
		}
		if (!schemas.has("search_tool_catalog")) {
			ToolSchema s;
			s.tool_name = "search_tool_catalog";
			s.category = "core";
			s.description = "Search the full editor tool catalog by keyword and/or pack. Use when a needed capability is missing from the curated tool list.";
			s.requires_scene = false;
			s.is_write_operation = false;
			{
				ToolArgSchema a;
				a.name = "query";
				a.type_hint = Variant::STRING;
				a.required = false;
				a.description = "Substring match against tool names (e.g. tilemap, animation, runtime).";
				s.arguments.push_back(a);
			}
			{
				ToolArgSchema a;
				a.name = "pack";
				a.type_hint = Variant::STRING;
				a.required = false;
				a.description = "Optional pack filter: core,2d,3d,ui,anim,audio,shader,physics,debug,multiplayer.";
				s.arguments.push_back(a);
			}
			{
				ToolArgSchema a;
				a.name = "max_results";
				a.type_hint = Variant::INT;
				a.required = false;
				a.description = "Max rows to return (default 40).";
				s.arguments.push_back(a);
			}
			schemas["search_tool_catalog"] = s;
		}
		if (!schemas.has("request_tool_pack")) {
			ToolSchema s;
			s.tool_name = "request_tool_pack";
			s.category = "core";
			s.description = "Expand which tool packs are advertised on the next model turn (for GPT-5.x/Azure tool budget). Call after search_tool_catalog if you need a domain.";
			s.requires_scene = false;
			s.is_write_operation = false;
			{
				ToolArgSchema a;
				a.name = "packs";
				a.type_hint = Variant::ARRAY;
				a.required = true;
				a.description = "Pack names: 2d,3d,ui,anim,audio,shader,physics,debug,multiplayer.";
				s.arguments.push_back(a);
			}
			schemas["request_tool_pack"] = s;
		}
		if (!schemas.has("update_plan")) {
			ToolSchema s; s.tool_name = "update_plan"; s.category = "core";
			s.description = "Create or update your task plan as a checklist. Call this first for any multi-step task and keep it current: pass the full list of steps each time, marking exactly one as in_progress and completed ones as done. Status values: pending, in_progress, done.";
			s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "plan"; a.type_hint = Variant::ARRAY; a.required = true;
			a.description = "Full ordered list of steps. Each item is an object {\"step\": string, \"status\": \"pending\"|\"in_progress\"|\"done\"}.";
			s.arguments.push_back(a);
			schemas["update_plan"] = s;
		}
		// ── Hot always-advertised tools: explicit schemas (replace weak inference) ──
		if (!schemas.has("get_node_api")) {
			ToolSchema s; s.tool_name = "get_node_api"; s.category = "inspection";
			s.description = "List a node's class methods, properties, and signals (its scripting API). Call this before connect_signal or set_node_property to confirm exact member names.";
			s.requires_scene = true; s.is_write_operation = false;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; ap.description = "Path or bare name of the node to introspect."; s.arguments.push_back(ap);
			ToolArgSchema asp; asp.name = "scene_path"; asp.type_hint = Variant::STRING; asp.required = false; asp.description = "Scene to resolve the node in (defaults to the open scene)."; s.arguments.push_back(asp);
			ToolArgSchema apv; apv.name = "include_private"; apv.type_hint = Variant::BOOL; apv.required = false; apv.description = "Include underscore-prefixed members."; s.arguments.push_back(apv);
			ToolArgSchema amm; amm.name = "max_methods"; amm.type_hint = Variant::INT; amm.required = false; s.arguments.push_back(amm);
			ToolArgSchema amp; amp.name = "max_properties"; amp.type_hint = Variant::INT; amp.required = false; s.arguments.push_back(amp);
			ToolArgSchema ams; ams.name = "max_signals"; ams.type_hint = Variant::INT; ams.required = false; s.arguments.push_back(ams);
			schemas["get_node_api"] = s;
		}
		if (!schemas.has("write_project_file")) {
			ToolSchema s; s.tool_name = "write_project_file"; s.category = "files";
			s.description = "Create or overwrite a text/resource file at a res:// path. Prefer update_gdscript_file for scripts. Only overwrites an existing file when overwrite=true.";
			s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "path"; ap.type_hint = Variant::STRING; ap.required = true; ap.description = "Destination path, e.g. res://data/config.json."; s.arguments.push_back(ap);
			ToolArgSchema ac; ac.name = "contents"; ac.type_hint = Variant::STRING; ac.required = true; ac.description = "Full file contents to write."; s.arguments.push_back(ac);
			ToolArgSchema ao; ao.name = "overwrite"; ao.type_hint = Variant::BOOL; ao.required = false; ao.description = "Set true to replace an existing file."; s.arguments.push_back(ao);
			schemas["write_project_file"] = s;
		}
		if (!schemas.has("delete_project_file")) {
			ToolSchema s; s.tool_name = "delete_project_file"; s.category = "files";
			s.description = "Delete a file from the project. Destructive: you MUST pass confirm=true for the deletion to actually happen.";
			s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "path"; ap.type_hint = Variant::STRING; ap.required = true; ap.description = "res:// path of the file to delete."; s.arguments.push_back(ap);
			ToolArgSchema ac; ac.name = "confirm"; ac.type_hint = Variant::BOOL; ac.required = false; ac.description = "Must be true to perform the deletion."; s.arguments.push_back(ac);
			schemas["delete_project_file"] = s;
		}
		if (!schemas.has("copy_project_file")) {
			ToolSchema s; s.tool_name = "copy_project_file"; s.category = "files";
			s.description = "Copy a project file to a new res:// path. Only overwrites the destination when overwrite=true.";
			s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema af; af.name = "from_path"; af.type_hint = Variant::STRING; af.required = true; af.description = "Source res:// path."; s.arguments.push_back(af);
			ToolArgSchema at; at.name = "to_path"; at.type_hint = Variant::STRING; at.required = true; at.description = "Destination res:// path."; s.arguments.push_back(at);
			ToolArgSchema ao; ao.name = "overwrite"; ao.type_hint = Variant::BOOL; ao.required = false; s.arguments.push_back(ao);
			schemas["copy_project_file"] = s;
		}
		if (!schemas.has("move_project_file")) {
			ToolSchema s; s.tool_name = "move_project_file"; s.category = "files";
			s.description = "Move or rename a project file (updates the res:// path). Use for renaming assets/scripts.";
			s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema af; af.name = "from_path"; af.type_hint = Variant::STRING; af.required = true; af.description = "Current res:// path."; s.arguments.push_back(af);
			ToolArgSchema at; at.name = "to_path"; at.type_hint = Variant::STRING; at.required = true; at.description = "New res:// path."; s.arguments.push_back(at);
			schemas["move_project_file"] = s;
		}
		if (!schemas.has("disconnect_signal")) {
			ToolSchema s; s.tool_name = "disconnect_signal"; s.category = "scene";
			s.description = "Disconnect a previously connected signal between two nodes. Mirror of connect_signal.";
			s.requires_scene = true; s.is_write_operation = true;
			ToolArgSchema asn; asn.name = "source_node_path"; asn.type_hint = Variant::STRING; asn.required = true; asn.auto_resolve_node_path = true; asn.description = "Node emitting the signal."; s.arguments.push_back(asn);
			ToolArgSchema asig; asig.name = "signal_name"; asig.type_hint = Variant::STRING; asig.required = true; asig.description = "Signal to disconnect, e.g. pressed."; s.arguments.push_back(asig);
			ToolArgSchema atn; atn.name = "target_node_path"; atn.type_hint = Variant::STRING; atn.required = true; atn.auto_resolve_node_path = true; atn.description = "Node the signal was connected to."; s.arguments.push_back(atn);
			ToolArgSchema am; am.name = "method_name"; am.type_hint = Variant::STRING; am.required = true; am.description = "Method that was connected."; s.arguments.push_back(am);
			schemas["disconnect_signal"] = s;
		}
		if (!schemas.has("save_resource")) {
			ToolSchema s; s.tool_name = "save_resource"; s.category = "files";
			s.description = "Save an in-memory or generated resource to a res:// path (.res/.tres).";
			s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "path"; ap.type_hint = Variant::STRING; ap.required = true; ap.description = "Destination res:// path for the resource."; s.arguments.push_back(ap);
			ToolArgSchema am; am.name = "merge_with_existing"; am.type_hint = Variant::BOOL; am.required = false; s.arguments.push_back(am);
			schemas["save_resource"] = s;
		}
		if (!schemas.has("select_file")) {
			ToolSchema s; s.tool_name = "select_file"; s.category = "editor";
			s.description = "Reveal and select a file in the editor FileSystem dock.";
			s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema ap; ap.name = "path"; ap.type_hint = Variant::STRING; ap.required = true; ap.description = "res:// path of the file to select."; s.arguments.push_back(ap);
			schemas["select_file"] = s;
		}
		if (!schemas.has("get_class_reference")) {
			ToolSchema s; s.tool_name = "get_class_reference"; s.description = "Get ClassDB reference for a class"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "class_name"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["get_class_reference"] = s;
		}
		if (!schemas.has("search_class_db")) {
			ToolSchema s; s.tool_name = "search_class_db"; s.description = "Search ClassDB for classes by name"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "query"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["search_class_db"] = s;
		}
		if (!schemas.has("get_method_signature")) {
			ToolSchema s; s.tool_name = "get_method_signature"; s.description = "Get method signature from ClassDB"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema ac; ac.name = "class_name"; ac.type_hint = Variant::STRING; ac.required = true; s.arguments.push_back(ac);
			ToolArgSchema am; am.name = "method_name"; am.type_hint = Variant::STRING; am.required = true; s.arguments.push_back(am);
			schemas["get_method_signature"] = s;
		}
		if (!schemas.has("get_enum_values")) {
			ToolSchema s; s.tool_name = "get_enum_values"; s.description = "Get enum values from ClassDB"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema ac; ac.name = "class_name"; ac.type_hint = Variant::STRING; ac.required = true; s.arguments.push_back(ac);
			ToolArgSchema ae; ae.name = "enum_name"; ae.type_hint = Variant::STRING; ae.required = true; s.arguments.push_back(ae);
			schemas["get_enum_values"] = s;
		}
		if (!schemas.has("run_gdscript_expression")) {
			ToolSchema s; s.tool_name = "run_gdscript_expression"; s.description = "Evaluate a GDScript expression"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "expression"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["run_gdscript_expression"] = s;
		}
		if (!schemas.has("run_gdscript_test")) {
			ToolSchema s; s.tool_name = "run_gdscript_test"; s.description = "Run a GDScript test expression"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "expression"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["run_gdscript_test"] = s;
		}
		if (!schemas.has("inspect_runtime_variable")) {
			ToolSchema s; s.tool_name = "inspect_runtime_variable"; s.description = "Inspect a runtime variable value"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "variable_name"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["inspect_runtime_variable"] = s;
		}
		if (!schemas.has("search_project_settings_keys")) {
			ToolSchema s; s.tool_name = "search_project_settings_keys"; s.description = "Search project setting keys by substring."; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "pattern"; a.type_hint = Variant::STRING; a.required = true; a.description = "Case-insensitive substring to match against setting names."; s.arguments.push_back(a);
			ToolArgSchema am; am.name = "max_results"; am.type_hint = Variant::INT; am.required = false; s.arguments.push_back(am);
			schemas["search_project_settings_keys"] = s;
		}
	}

	// ── ANIMATION TOOLS ──
	{
		if (!schemas.has("remove_animation_track")) {
			ToolSchema s; s.tool_name = "remove_animation_track"; s.description = "Remove an animation track by index"; s.requires_scene = true; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			ToolArgSchema an; an.name = "animation_name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			ToolArgSchema ai; ai.name = "track_index"; ai.type_hint = Variant::INT; ai.required = true; s.arguments.push_back(ai);
			schemas["remove_animation_track"] = s;
		}
		if (!schemas.has("add_animation_transition")) {
			ToolSchema s; s.tool_name = "add_animation_transition"; s.description = "Add state machine transition"; s.requires_scene = true; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			schemas["add_animation_transition"] = s;
		}
		if (!schemas.has("set_animation_blend_amount")) {
			ToolSchema s; s.tool_name = "set_animation_blend_amount"; s.description = "Set blend tree parameter value"; s.requires_scene = true; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			schemas["set_animation_blend_amount"] = s;
		}
		if (!schemas.has("create_blend_tree")) {
			ToolSchema s; s.tool_name = "create_blend_tree"; s.description = "Create an AnimationTree blend tree"; s.requires_scene = true; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "parent_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			ToolArgSchema an; an.name = "name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			schemas["create_blend_tree"] = s;
		}
	}

	// ── PHYSICS TOOLS (already mostly covered but add missing ones) ──
	{
		if (!schemas.has("add_audio_bus_effect")) {
			ToolSchema s; s.tool_name = "add_audio_bus_effect"; s.description = "Add effect to audio bus"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ab; ab.name = "bus_name"; ab.type_hint = Variant::STRING; ab.required = true; s.arguments.push_back(ab);
			ToolArgSchema at; at.name = "effect_type"; at.type_hint = Variant::STRING; at.required = true; s.arguments.push_back(at);
			schemas["add_audio_bus_effect"] = s;
		}
	}

	// ── PROJECT/MANAGEMENT TOOLS ──
	{
		if (!schemas.has("manage_autoloads")) {
			ToolSchema s; s.tool_name = "manage_autoloads"; s.description = "Add or remove project autoloads"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema aa; aa.name = "action"; aa.type_hint = Variant::STRING; aa.required = true; aa.valid_values = Vector<String>{"add", "remove"}; s.arguments.push_back(aa);
			ToolArgSchema an; an.name = "name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			ToolArgSchema ap; ap.name = "path"; ap.type_hint = Variant::STRING; ap.required = false; s.arguments.push_back(ap);
			schemas["manage_autoloads"] = s;
		}
		if (!schemas.has("manage_editor_plugins")) {
			ToolSchema s; s.tool_name = "manage_editor_plugins"; s.description = "Enable/disable editor plugins"; s.requires_scene = false; s.is_write_operation = true;
			schemas["manage_editor_plugins"] = s;
		}
		if (!schemas.has("manage_export_presets")) {
			ToolSchema s; s.tool_name = "manage_export_presets"; s.description = "Add/remove/edit export presets"; s.requires_scene = false; s.is_write_operation = true;
			schemas["manage_export_presets"] = s;
		}
		if (!schemas.has("patch_editor_settings")) {
			ToolSchema s; s.tool_name = "patch_editor_settings"; s.description = "Patch editor settings"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "settings"; a.type_hint = Variant::DICTIONARY; a.required = true; s.arguments.push_back(a);
			schemas["patch_editor_settings"] = s;
		}
		if (!schemas.has("add_custom_class")) {
			ToolSchema s; s.tool_name = "add_custom_class"; s.description = "Register a custom global class"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ac; ac.name = "class_name"; ac.type_hint = Variant::STRING; ac.required = true; s.arguments.push_back(ac);
			ToolArgSchema asp; asp.name = "script_path"; asp.type_hint = Variant::STRING; asp.required = true; s.arguments.push_back(asp);
			ToolArgSchema ab; ab.name = "base_class"; ab.type_hint = Variant::STRING; ab.required = true; s.arguments.push_back(ab);
			schemas["add_custom_class"] = s;
		}
		if (!schemas.has("set_import_setting")) {
			ToolSchema s; s.tool_name = "set_import_setting"; s.description = "Set import setting on a file"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema af; af.name = "file_path"; af.type_hint = Variant::STRING; af.required = true; s.arguments.push_back(af);
			ToolArgSchema an; an.name = "setting_name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			schemas["set_import_setting"] = s;
		}
		if (!schemas.has("set_default_import_presets")) {
			ToolSchema s; s.tool_name = "set_default_import_presets"; s.description = "Set default import presets"; s.requires_scene = false; s.is_write_operation = true;
			schemas["set_default_import_presets"] = s;
		}
	}

	// ── REMAINING MISSING TOOLS (explicit) ──
	{
		// Resource creation tools (save_path required)
		const char *res_create_tools[] = { "create_gradient", "create_curve", "create_stylebox", "create_theme", "create_font", "create_texture_2d", "create_noise_texture", "create_tileset" };
		for (const char *t : res_create_tools) {
			String ts(t);
			if (schemas.has(ts)) continue;
			ToolSchema s; s.tool_name = ts; s.description = ts.replace("create_", "Create a ").replace("_", " ") + " resource"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "save_path"; a.type_hint = Variant::STRING; a.required = true; a.description = "Path to save resource (res://...)"; s.arguments.push_back(a);
			schemas[ts] = s;
		}
		if (!schemas.has("create_atlas_texture")) {
			ToolSchema s; s.tool_name = "create_atlas_texture"; s.description = "Create an AtlasTexture from a region"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema asp; asp.name = "source_path"; asp.type_hint = Variant::STRING; asp.required = true; s.arguments.push_back(asp);
			ToolArgSchema adp; adp.name = "save_path"; adp.type_hint = Variant::STRING; adp.required = true; s.arguments.push_back(adp);
			schemas["create_atlas_texture"] = s;
		}
		if (!schemas.has("create_animated_sprite_2d")) {
			ToolSchema s; s.tool_name = "create_animated_sprite_2d"; s.description = "Create an AnimatedSprite2D node"; s.requires_scene = true; s.is_write_operation = true; s.supports_batching = true;
			ToolArgSchema an; an.name = "name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			ToolArgSchema ap; ap.name = "parent_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			schemas["create_animated_sprite_2d"] = s;
		}
		if (!schemas.has("create_sky")) {
			ToolSchema s; s.tool_name = "create_sky"; s.description = "Create a sky in the current scene"; s.requires_scene = true; s.is_write_operation = true;
			schemas["create_sky"] = s;
		}
		if (!schemas.has("create_multiplayer_spawner")) {
			ToolSchema s; s.tool_name = "create_multiplayer_spawner"; s.description = "Create a MultiplayerSpawner node"; s.requires_scene = true; s.is_write_operation = true; s.supports_batching = true;
			ToolArgSchema an; an.name = "name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			ToolArgSchema ap; ap.name = "parent_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			schemas["create_multiplayer_spawner"] = s;
		}
		if (!schemas.has("create_multiplayer_synchronizer")) {
			ToolSchema s; s.tool_name = "create_multiplayer_synchronizer"; s.description = "Create a MultiplayerSynchronizer node"; s.requires_scene = true; s.is_write_operation = true; s.supports_batching = true;
			ToolArgSchema an; an.name = "name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			ToolArgSchema ap; ap.name = "parent_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			schemas["create_multiplayer_synchronizer"] = s;
		}
		if (!schemas.has("create_navigation_link")) {
			ToolSchema s; s.tool_name = "create_navigation_link"; s.description = "Create a NavigationLink node"; s.requires_scene = true; s.is_write_operation = true; s.supports_batching = true;
			ToolArgSchema an; an.name = "name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			ToolArgSchema ap; ap.name = "parent_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			schemas["create_navigation_link"] = s;
		}
		if (!schemas.has("create_navigation_obstacle")) {
			ToolSchema s; s.tool_name = "create_navigation_obstacle"; s.description = "Create a NavigationObstacle node"; s.requires_scene = true; s.is_write_operation = true; s.supports_batching = true;
			ToolArgSchema an; an.name = "name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			ToolArgSchema ap; ap.name = "parent_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			schemas["create_navigation_obstacle"] = s;
		}
		if (!schemas.has("add_tileset_atlas_source")) {
			ToolSchema s; s.tool_name = "add_tileset_atlas_source"; s.description = "Add atlas source to a tileset"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema atp; atp.name = "tileset_path"; atp.type_hint = Variant::STRING; atp.required = true; s.arguments.push_back(atp);
			ToolArgSchema atex; atex.name = "texture_path"; atex.type_hint = Variant::STRING; atex.required = true; s.arguments.push_back(atex);
			ToolArgSchema atx; atx.name = "tile_size_x"; atx.type_hint = Variant::INT; atx.required = true; s.arguments.push_back(atx);
			ToolArgSchema aty; aty.name = "tile_size_y"; aty.type_hint = Variant::INT; aty.required = true; s.arguments.push_back(aty);
			schemas["add_tileset_atlas_source"] = s;
		}
		if (!schemas.has("set_tilemap_cells")) {
			ToolSchema s; s.tool_name = "set_tilemap_cells"; s.description = "Set individual tilemap cells"; s.requires_scene = true; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			ToolArgSchema ac; ac.name = "cells"; ac.type_hint = Variant::ARRAY; ac.required = true; s.arguments.push_back(ac);
			schemas["set_tilemap_cells"] = s;
		}
		if (!schemas.has("set_tile_collision_polygon")) {
			ToolSchema s; s.tool_name = "set_tile_collision_polygon"; s.description = "Set collision polygon for a tile"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema atp; atp.name = "tileset_path"; atp.type_hint = Variant::STRING; atp.required = true; s.arguments.push_back(atp);
			schemas["set_tile_collision_polygon"] = s;
		}
		if (!schemas.has("get_project_tree")) {
			ToolSchema s; s.tool_name = "get_project_tree"; s.description = "Get project directory tree"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema ar; ar.name = "root"; ar.type_hint = Variant::STRING; ar.required = false; s.arguments.push_back(ar);
			schemas["get_project_tree"] = s;
		}
		if (!schemas.has("attach_script")) {
			ToolSchema s; s.tool_name = "attach_script"; s.description = "Attach a script to a node"; s.requires_scene = true; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			ToolArgSchema asp; asp.name = "script_path"; asp.type_hint = Variant::STRING; asp.required = true; s.arguments.push_back(asp);
			schemas["attach_script"] = s;
		}
		if (!schemas.has("batch_set_node_property")) {
			ToolSchema s; s.tool_name = "batch_set_node_property"; s.description = "Set properties on many nodes at once. Pass an 'entries' array; each entry is {node_path, property, value} (optionally scene_path)."; s.requires_scene = true; s.is_write_operation = true; s.supports_batching = true;
			ToolArgSchema ae; ae.name = "entries"; ae.type_hint = Variant::ARRAY; ae.required = true; ae.description = "Array of {node_path, property, value} objects."; s.arguments.push_back(ae);
			ToolArgSchema asp; asp.name = "scene_path"; asp.type_hint = Variant::STRING; asp.required = false; asp.description = "Default scene for entries that omit their own scene_path."; s.arguments.push_back(asp);
			schemas["batch_set_node_property"] = s;
		}
	}

	// ── SCAFFOLD TOOLS ──
	{
		const char *scaffolds[] = { "scaffold_platformer_player_2d", "scaffold_patrol_enemy_2d", "scaffold_collectible_2d", "scaffold_moving_platform_2d", "scaffold_game_hud_2d", "scaffold_main_menu_2d", "scaffold_pause_menu_2d", "scaffold_lighting_rig_2d" };
		for (const char *t : scaffolds) {
			String ts(t);
			if (schemas.has(ts)) continue;
			ToolSchema s;
			s.tool_name = ts;
			s.description = ts.replace("scaffold_", "Scaffold a ").replace("_2d", " (2D)").replace("_", " ");
			s.requires_scene = true;
			s.is_write_operation = true;
			s.supports_batching = true;
			ToolArgSchema an; an.name = "name"; an.type_hint = Variant::STRING; an.required = false; s.arguments.push_back(an);
			ToolArgSchema ap; ap.name = "parent_path"; ap.type_hint = Variant::STRING; ap.required = false; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			schemas[ts] = s;
		}
	}

	// ── GIT TOOLS ──
	reg_noarg_read("git_status", "Get git staged/unstaged files", false);
	reg_noarg_read("git_branch", "Get current git branch", false);
	{
		if (!schemas.has("git_diff_file")) {
			ToolSchema s; s.tool_name = "git_diff_file"; s.description = "Get git diff for a file"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "file_path"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["git_diff_file"] = s;
		}
		if (!schemas.has("git_log")) {
			ToolSchema s; s.tool_name = "git_log"; s.description = "Get git commit log"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "max_count"; a.type_hint = Variant::INT; a.required = false; s.arguments.push_back(a);
			schemas["git_log"] = s;
		}
	}

	// ── SCENE REFACTORING ──
	{
		if (!schemas.has("merge_scenes")) {
			ToolSchema s; s.tool_name = "merge_scenes"; s.description = "Merge one scene into another"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema as; as.name = "source_path"; as.type_hint = Variant::STRING; as.required = true; s.arguments.push_back(as);
			ToolArgSchema at; at.name = "target_path"; at.type_hint = Variant::STRING; at.required = true; s.arguments.push_back(at);
			schemas["merge_scenes"] = s;
		}
		if (!schemas.has("extract_sub_scene")) {
			ToolSchema s; s.tool_name = "extract_sub_scene"; s.description = "Extract a node subtree as a new scene"; s.requires_scene = true; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			ToolArgSchema asp; asp.name = "save_path"; asp.type_hint = Variant::STRING; asp.required = true; s.arguments.push_back(asp);
			schemas["extract_sub_scene"] = s;
		}
		if (!schemas.has("replace_node_with_scene")) {
			ToolSchema s; s.tool_name = "replace_node_with_scene"; s.description = "Replace a node with an instanced scene"; s.requires_scene = true; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			ToolArgSchema asp; asp.name = "scene_file_path"; asp.type_hint = Variant::STRING; asp.required = true; s.arguments.push_back(asp);
			schemas["replace_node_with_scene"] = s;
		}
		if (!schemas.has("batch_reparent_nodes")) {
			ToolSchema s; s.tool_name = "batch_reparent_nodes"; s.description = "Reparent multiple nodes at once"; s.requires_scene = true; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_paths"; ap.type_hint = Variant::ARRAY; ap.required = true; s.arguments.push_back(ap);
			ToolArgSchema anp; anp.name = "new_parent_path"; anp.type_hint = Variant::STRING; anp.required = true; anp.auto_resolve_node_path = true; s.arguments.push_back(anp);
			schemas["batch_reparent_nodes"] = s;
		}
	}

	// ── MEMORY TOOLS ──
	{
		const char *mem_tools[] = { "memory_record_entity", "memory_query_entities", "memory_get_entity", "memory_update_entity", "memory_delete_entity", "memory_get_summary" };
		for (const char *t : mem_tools) {
			String ts(t);
			if (schemas.has(ts)) continue;
			ToolSchema s;
			s.tool_name = ts;
			s.description = ts.replace("memory_", "Project memory: ").replace("_", " ");
			s.requires_scene = false;
			s.is_write_operation = ts.contains("record") || ts.contains("update") || ts.contains("delete");
			schemas[ts] = s;
		}
	}

	// ── ASSET INDEX TOOLS ──
	{
		if (!schemas.has("index_project_assets")) {
			ToolSchema s; s.tool_name = "index_project_assets"; s.description = "Scan and index all project image assets"; s.requires_scene = false; s.is_write_operation = true;
			schemas["index_project_assets"] = s;
		}
		if (!schemas.has("index_asset")) {
			ToolSchema s; s.tool_name = "index_asset"; s.description = "Index a single asset (deep mode optional)"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "path"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["index_asset"] = s;
		}
		if (!schemas.has("search_assets")) {
			ToolSchema s; s.tool_name = "search_assets"; s.description = "Search indexed assets by query/type/tags"; s.requires_scene = false; s.is_write_operation = false;
			schemas["search_assets"] = s;
		}
		if (!schemas.has("get_asset_manifest")) {
			ToolSchema s; s.tool_name = "get_asset_manifest"; s.description = "Get asset manifest by path or ID"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "path_or_id"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["get_asset_manifest"] = s;
		}
		if (!schemas.has("update_asset_manifest")) {
			ToolSchema s; s.tool_name = "update_asset_manifest"; s.description = "Patch an asset manifest"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "path_or_id"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["update_asset_manifest"] = s;
		}
		if (!schemas.has("confirm_asset_manifest")) {
			ToolSchema s; s.tool_name = "confirm_asset_manifest"; s.description = "Confirm an asset manifest as accurate"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "path_or_id"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["confirm_asset_manifest"] = s;
		}
		if (!schemas.has("list_asset_index_issues")) {
			ToolSchema s; s.tool_name = "list_asset_index_issues"; s.description = "List asset index quality issues"; s.requires_scene = false; s.is_write_operation = false;
			schemas["list_asset_index_issues"] = s;
		}
		if (!schemas.has("create_sprite_frames_from_manifest")) {
			ToolSchema s; s.tool_name = "create_sprite_frames_from_manifest"; s.description = "Create SpriteFrames from indexed manifest"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "asset_id"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["create_sprite_frames_from_manifest"] = s;
		}
		if (!schemas.has("create_tileset_from_manifest")) {
			ToolSchema s; s.tool_name = "create_tileset_from_manifest"; s.description = "Create TileSet from indexed manifest"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "asset_id"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["create_tileset_from_manifest"] = s;
		}
		if (!schemas.has("index_project_context")) {
			ToolSchema s; s.tool_name = "index_project_context"; s.description = "Build the embedding-backed project context retrieval index over code, docs, scenes, resources, and asset metadata"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "force"; a.type_hint = Variant::BOOL; a.required = false; s.arguments.push_back(a);
			ToolArgSchema e; e.name = "embed"; e.type_hint = Variant::BOOL; e.required = false; s.arguments.push_back(e);
			ToolArgSchema p; p.name = "embedding_provider"; p.type_hint = Variant::STRING; p.required = false; s.arguments.push_back(p);
			ToolArgSchema mo; mo.name = "embedding_model"; mo.type_hint = Variant::STRING; mo.required = false; s.arguments.push_back(mo);
			ToolArgSchema u; u.name = "embedding_base_url"; u.type_hint = Variant::STRING; u.required = false; s.arguments.push_back(u);
			ToolArgSchema mc; mc.name = "max_embedded_chunks"; mc.type_hint = Variant::INT; mc.required = false; s.arguments.push_back(mc);
			schemas["index_project_context"] = s;
		}
		if (!schemas.has("search_project_context")) {
			ToolSchema s; s.tool_name = "search_project_context"; s.description = "Retrieve relevant project context chunks by vector, lexical, or hybrid query"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema q; q.name = "query"; q.type_hint = Variant::STRING; q.required = true; s.arguments.push_back(q);
			ToolArgSchema k; k.name = "kind"; k.type_hint = Variant::STRING; k.required = false; s.arguments.push_back(k);
			ToolArgSchema m; m.name = "max_results"; m.type_hint = Variant::INT; m.required = false; s.arguments.push_back(m);
			ToolArgSchema c; c.name = "max_chars_per_result"; c.type_hint = Variant::INT; c.required = false; s.arguments.push_back(c);
			ToolArgSchema mode; mode.name = "mode"; mode.type_hint = Variant::STRING; mode.required = false; s.arguments.push_back(mode);
			schemas["search_project_context"] = s;
		}
	}

	// ── SPECIAL TOOLS WITH UNIQUE SIGNATURES ──
	{
		if (!schemas.has("create_pin_joint_2d")) {
			ToolSchema s; s.tool_name = "create_pin_joint_2d"; s.description = "Create a PinJoint2D between two bodies"; s.requires_scene = true; s.is_write_operation = true; s.supports_batching = true;
			ToolArgSchema an; an.name = "name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			ToolArgSchema ap; ap.name = "parent_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			ToolArgSchema aa; aa.name = "node_a"; aa.type_hint = Variant::STRING; aa.required = true; s.arguments.push_back(aa);
			ToolArgSchema ab; ab.name = "node_b"; ab.type_hint = Variant::STRING; ab.required = true; s.arguments.push_back(ab);
			schemas["create_pin_joint_2d"] = s;
		}
		if (!schemas.has("create_damped_spring_joint_2d")) {
			ToolSchema s; s.tool_name = "create_damped_spring_joint_2d"; s.description = "Create a DampedSpringJoint2D"; s.requires_scene = true; s.is_write_operation = true; s.supports_batching = true;
			ToolArgSchema an; an.name = "name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			ToolArgSchema ap; ap.name = "parent_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			ToolArgSchema aa; aa.name = "node_a"; aa.type_hint = Variant::STRING; aa.required = true; s.arguments.push_back(aa);
			ToolArgSchema ab; ab.name = "node_b"; ab.type_hint = Variant::STRING; ab.required = true; s.arguments.push_back(ab);
			schemas["create_damped_spring_joint_2d"] = s;
		}
		if (!schemas.has("create_touch_screen_button")) {
			ToolSchema s; s.tool_name = "create_touch_screen_button"; s.description = "Create a mobile touch button"; s.requires_scene = true; s.is_write_operation = true; s.supports_batching = true;
			ToolArgSchema an; an.name = "name"; an.type_hint = Variant::STRING; an.required = true; s.arguments.push_back(an);
			ToolArgSchema ap; ap.name = "parent_path"; ap.type_hint = Variant::STRING; ap.required = true; ap.auto_resolve_node_path = true; s.arguments.push_back(ap);
			schemas["create_touch_screen_button"] = s;
		}
		if (!schemas.has("connect_ui_signal")) {
			ToolSchema s; s.tool_name = "connect_ui_signal"; s.description = "Connect a Control's UI event to a handler method by preset (maps ui_event to the right signal for the source node type)."; s.requires_scene = true; s.is_write_operation = true;
			ToolArgSchema asn; asn.name = "source_node_path"; asn.type_hint = Variant::STRING; asn.required = true; asn.auto_resolve_node_path = true; asn.description = "The Control emitting the event (Button, LineEdit, Slider, ...)."; s.arguments.push_back(asn);
			ToolArgSchema aue; aue.name = "ui_event"; aue.type_hint = Variant::STRING; aue.required = true; aue.valid_values = Vector<String>{ "pressed", "toggled", "text_changed", "text_submitted", "value_changed", "item_selected", "tab_changed", "gui_input" }; aue.description = "Logical UI event; mapped to the correct signal for the node type."; s.arguments.push_back(aue);
			ToolArgSchema atn; atn.name = "target_node_path"; atn.type_hint = Variant::STRING; atn.required = true; atn.auto_resolve_node_path = true; atn.description = "Node whose method handles the event."; s.arguments.push_back(atn);
			ToolArgSchema am; am.name = "method_name"; am.type_hint = Variant::STRING; am.required = true; am.description = "Method on the target node to call."; s.arguments.push_back(am);
			schemas["connect_ui_signal"] = s;
		}
		if (!schemas.has("validate_scene")) {
			ToolSchema s; s.tool_name = "validate_scene"; s.description = "Validate scene structure and references"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "scene_path"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["validate_scene"] = s;
		}
		if (!schemas.has("reload_scene")) {
			ToolSchema s; s.tool_name = "reload_scene"; s.description = "Reload current or specified scene"; s.requires_scene = false; s.is_write_operation = true;
			schemas["reload_scene"] = s;
		}
		if (!schemas.has("get_input_actions")) {
			ToolSchema s; s.tool_name = "get_input_actions"; s.description = "List project input actions and events"; s.requires_scene = false; s.is_write_operation = false;
			schemas["get_input_actions"] = s;
		}
		if (!schemas.has("set_input_action_bindings")) {
			ToolSchema s; s.tool_name = "set_input_action_bindings"; s.description = "Set bindings for an input action"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "action_name"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["set_input_action_bindings"] = s;
		}
		if (!schemas.has("remove_input_action")) {
			ToolSchema s; s.tool_name = "remove_input_action"; s.description = "Remove an input action"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "action_name"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["remove_input_action"] = s;
		}
		if (!schemas.has("set_breakpoint")) {
			ToolSchema s; s.tool_name = "set_breakpoint"; s.description = "Set a debugger breakpoint"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema asp; asp.name = "script_path"; asp.type_hint = Variant::STRING; asp.required = true; s.arguments.push_back(asp);
			ToolArgSchema al; al.name = "line"; al.type_hint = Variant::INT; al.required = true; s.arguments.push_back(al);
			schemas["set_breakpoint"] = s;
		}
		if (!schemas.has("debugger_continue")) {
			ToolSchema s; s.tool_name = "debugger_continue"; s.description = "Continue debugger execution"; s.requires_scene = false; s.is_write_operation = true;
			schemas["debugger_continue"] = s;
		}
		if (!schemas.has("create_ui_element")) {
			ToolSchema s; s.tool_name = "create_ui_element"; s.description = "Create a UI element by type"; s.requires_scene = true; s.is_write_operation = true; s.supports_batching = true;
			ToolArgSchema at; at.name = "element_type"; at.type_hint = Variant::STRING; at.required = true; s.arguments.push_back(at);
			schemas["create_ui_element"] = s;
		}
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// END BULK BACKFILL
	// ═══════════════════════════════════════════════════════════════════════════

	// ── PHASE 1: RUNTIME GAME MANIPULATION SCHEMAS ──
	{
		if (!schemas.has("runtime_get_scene_tree")) {
			ToolSchema s; s.tool_name = "runtime_get_scene_tree"; s.description = "Get the live scene tree from the running game"; s.requires_scene = false; s.is_write_operation = false;
			schemas["runtime_get_scene_tree"] = s;
		}
		if (!schemas.has("runtime_eval")) {
			ToolSchema s; s.tool_name = "runtime_eval"; s.description = "Evaluate a GDScript expression in the running game"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "expression"; a.type_hint = Variant::STRING; a.required = true; a.description = "GDScript expression to evaluate"; s.arguments.push_back(a);
			schemas["runtime_eval"] = s;
		}
		if (!schemas.has("runtime_create_node")) {
			ToolSchema s; s.tool_name = "runtime_create_node"; s.description = "Create a node in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "parent_path"; ap.type_hint = Variant::STRING; ap.required = false; ap.description = "Parent node path in the running game (default /root)"; s.arguments.push_back(ap);
			ToolArgSchema at; at.name = "node_type"; at.type_hint = Variant::STRING; at.required = true; at.description = "Godot class type"; s.arguments.push_back(at);
			ToolArgSchema an; an.name = "node_name"; an.type_hint = Variant::STRING; an.required = true; an.description = "Name for the new node"; s.arguments.push_back(an);
			schemas["runtime_create_node"] = s;
		}
		if (!schemas.has("runtime_remove_node")) {
			ToolSchema s; s.tool_name = "runtime_remove_node"; s.description = "Remove a node from the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "node_path"; a.type_hint = Variant::STRING; a.required = true; a.description = "Path of node to remove"; s.arguments.push_back(a);
			schemas["runtime_remove_node"] = s;
		}
		if (!schemas.has("runtime_instantiate_scene")) {
			ToolSchema s; s.tool_name = "runtime_instantiate_scene"; s.description = "Instantiate a PackedScene in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "parent_path"; ap.type_hint = Variant::STRING; ap.required = false; s.arguments.push_back(ap);
			ToolArgSchema asp; asp.name = "scene_path"; asp.type_hint = Variant::STRING; asp.required = true; asp.description = "Path to .tscn file"; s.arguments.push_back(asp);
			ToolArgSchema an; an.name = "node_name"; an.type_hint = Variant::STRING; an.required = false; s.arguments.push_back(an);
			schemas["runtime_instantiate_scene"] = s;
		}
		if (!schemas.has("runtime_duplicate_node")) {
			ToolSchema s; s.tool_name = "runtime_duplicate_node"; s.description = "Duplicate a node in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "node_path"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			ToolArgSchema an; an.name = "new_name"; an.type_hint = Variant::STRING; an.required = false; s.arguments.push_back(an);
			schemas["runtime_duplicate_node"] = s;
		}
		if (!schemas.has("runtime_reparent_node")) {
			ToolSchema s; s.tool_name = "runtime_reparent_node"; s.description = "Move a node to a new parent in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "node_path"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			ToolArgSchema ap; ap.name = "new_parent_path"; ap.type_hint = Variant::STRING; ap.required = true; s.arguments.push_back(ap);
			schemas["runtime_reparent_node"] = s;
		}
		if (!schemas.has("runtime_set_property")) {
			ToolSchema s; s.tool_name = "runtime_set_property"; s.description = "Set a property on a remote object in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ai; ai.name = "object_id"; ai.type_hint = Variant::INT; ai.required = true; ai.description = "Remote object ID from runtime_get_scene_tree"; s.arguments.push_back(ai);
			ToolArgSchema ap; ap.name = "property"; ap.type_hint = Variant::STRING; ap.required = true; s.arguments.push_back(ap);
			ToolArgSchema av; av.name = "value"; av.type_hint = Variant::NIL; av.required = true; s.arguments.push_back(av);
			schemas["runtime_set_property"] = s;
		}
		if (!schemas.has("runtime_send_message")) {
			ToolSchema s; s.tool_name = "runtime_send_message"; s.description = "Send a raw debugger protocol message to the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema am; am.name = "message"; am.type_hint = Variant::STRING; am.required = true; am.description = "Protocol message name"; s.arguments.push_back(am);
			ToolArgSchema ad; ad.name = "data"; ad.type_hint = Variant::ARRAY; ad.required = false; s.arguments.push_back(ad);
			schemas["runtime_send_message"] = s;
		}
		if (!schemas.has("runtime_pause")) {
			ToolSchema s; s.tool_name = "runtime_pause"; s.description = "Pause or unpause the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "pause"; a.type_hint = Variant::BOOL; a.required = false; s.arguments.push_back(a);
			schemas["runtime_pause"] = s;
		}
		if (!schemas.has("runtime_inspect_object")) {
			ToolSchema s; s.tool_name = "runtime_inspect_object"; s.description = "Inspect a remote object by ID"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "object_id"; a.type_hint = Variant::INT; a.required = true; s.arguments.push_back(a);
			schemas["runtime_inspect_object"] = s;
		}
		if (!schemas.has("runtime_break")) {
			ToolSchema s; s.tool_name = "runtime_break"; s.description = "Break into debugger (pause execution)"; s.requires_scene = false; s.is_write_operation = true;
			schemas["runtime_break"] = s;
		}
		if (!schemas.has("runtime_step")) {
			ToolSchema s; s.tool_name = "runtime_step"; s.description = "Step debugger execution (continue/step_over/step_into)"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "action"; a.type_hint = Variant::STRING; a.required = true; a.valid_values = Vector<String>{"continue", "step_over", "step_into", "break"}; s.arguments.push_back(a);
			schemas["runtime_step"] = s;
		}
		if (!schemas.has("runtime_time_scale")) {
			ToolSchema s; s.tool_name = "runtime_time_scale"; s.description = "Get or set Engine.time_scale in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "value"; a.type_hint = Variant::FLOAT; a.required = false; a.description = "New time_scale value (omit to get current)"; s.arguments.push_back(a);
			schemas["runtime_time_scale"] = s;
		}
		// Phase 1 remaining tools
		if (!schemas.has("runtime_connect_signal")) {
			ToolSchema s; s.tool_name = "runtime_connect_signal"; s.description = "Connect a signal between nodes in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema as; as.name = "source_path"; as.type_hint = Variant::STRING; as.required = true; s.arguments.push_back(as);
			ToolArgSchema asn; asn.name = "signal_name"; asn.type_hint = Variant::STRING; asn.required = true; s.arguments.push_back(asn);
			ToolArgSchema at; at.name = "target_path"; at.type_hint = Variant::STRING; at.required = true; s.arguments.push_back(at);
			ToolArgSchema am; am.name = "method_name"; am.type_hint = Variant::STRING; am.required = true; s.arguments.push_back(am);
			schemas["runtime_connect_signal"] = s;
		}
		if (!schemas.has("runtime_disconnect_signal")) {
			ToolSchema s; s.tool_name = "runtime_disconnect_signal"; s.description = "Disconnect a signal in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema as; as.name = "source_path"; as.type_hint = Variant::STRING; as.required = true; s.arguments.push_back(as);
			ToolArgSchema asn; asn.name = "signal_name"; asn.type_hint = Variant::STRING; asn.required = true; s.arguments.push_back(asn);
			ToolArgSchema at; at.name = "target_path"; at.type_hint = Variant::STRING; at.required = true; s.arguments.push_back(at);
			ToolArgSchema am; am.name = "method_name"; am.type_hint = Variant::STRING; am.required = true; s.arguments.push_back(am);
			schemas["runtime_disconnect_signal"] = s;
		}
		if (!schemas.has("runtime_emit_signal")) {
			ToolSchema s; s.tool_name = "runtime_emit_signal"; s.description = "Emit a signal on a node in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; s.arguments.push_back(ap);
			ToolArgSchema asn; asn.name = "signal_name"; asn.type_hint = Variant::STRING; asn.required = true; s.arguments.push_back(asn);
			schemas["runtime_emit_signal"] = s;
		}
		if (!schemas.has("runtime_play_animation")) {
			ToolSchema s; s.tool_name = "runtime_play_animation"; s.description = "Play/stop/pause animation on AnimationPlayer"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; s.arguments.push_back(ap);
			ToolArgSchema an; an.name = "animation_name"; an.type_hint = Variant::STRING; an.required = false; s.arguments.push_back(an);
			ToolArgSchema aa; aa.name = "action"; aa.type_hint = Variant::STRING; aa.required = false; aa.valid_values = Vector<String>{"play", "stop", "pause"}; s.arguments.push_back(aa);
			schemas["runtime_play_animation"] = s;
		}
		if (!schemas.has("runtime_tween_property")) {
			ToolSchema s; s.tool_name = "runtime_tween_property"; s.description = "Smoothly tween a property with easing"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; s.arguments.push_back(ap);
			ToolArgSchema apr; apr.name = "property"; apr.type_hint = Variant::STRING; apr.required = true; s.arguments.push_back(apr);
			ToolArgSchema av; av.name = "final_value"; av.type_hint = Variant::STRING; av.required = true; av.description = "GDScript literal (e.g. Vector2(100,200))"; s.arguments.push_back(av);
			ToolArgSchema ad; ad.name = "duration"; ad.type_hint = Variant::FLOAT; ad.required = false; s.arguments.push_back(ad);
			schemas["runtime_tween_property"] = s;
		}
		if (!schemas.has("runtime_key_press")) {
			ToolSchema s; s.tool_name = "runtime_key_press"; s.description = "Simulate a key press or input action"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ak; ak.name = "key"; ak.type_hint = Variant::STRING; ak.required = false; ak.description = "Key constant (e.g. KEY_SPACE)"; s.arguments.push_back(ak);
			ToolArgSchema aa; aa.name = "action"; aa.type_hint = Variant::STRING; aa.required = false; aa.description = "Input action name (e.g. ui_jump)"; s.arguments.push_back(aa);
			schemas["runtime_key_press"] = s;
		}
		if (!schemas.has("runtime_key_hold")) {
			ToolSchema s; s.tool_name = "runtime_key_hold"; s.description = "Hold an input action down (no auto-release)"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "action"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["runtime_key_hold"] = s;
		}
		if (!schemas.has("runtime_key_release")) {
			ToolSchema s; s.tool_name = "runtime_key_release"; s.description = "Release a held input action"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "action"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["runtime_key_release"] = s;
		}
		if (!schemas.has("runtime_mouse_click")) {
			ToolSchema s; s.tool_name = "runtime_mouse_click"; s.description = "Click at a position in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ax; ax.name = "x"; ax.type_hint = Variant::FLOAT; ax.required = true; s.arguments.push_back(ax);
			ToolArgSchema ay; ay.name = "y"; ay.type_hint = Variant::FLOAT; ay.required = true; s.arguments.push_back(ay);
			ToolArgSchema ab; ab.name = "button"; ab.type_hint = Variant::INT; ab.required = false; s.arguments.push_back(ab);
			schemas["runtime_mouse_click"] = s;
		}
		if (!schemas.has("runtime_mouse_move")) {
			ToolSchema s; s.tool_name = "runtime_mouse_move"; s.description = "Move the mouse to a position"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ax; ax.name = "x"; ax.type_hint = Variant::FLOAT; ax.required = true; s.arguments.push_back(ax);
			ToolArgSchema ay; ay.name = "y"; ay.type_hint = Variant::FLOAT; ay.required = true; s.arguments.push_back(ay);
			schemas["runtime_mouse_move"] = s;
		}
		if (!schemas.has("runtime_get_camera")) {
			ToolSchema s; s.tool_name = "runtime_get_camera"; s.description = "Get active camera position and rotation"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "mode"; a.type_hint = Variant::STRING; a.required = false; a.valid_values = Vector<String>{"2d", "3d"}; s.arguments.push_back(a);
			schemas["runtime_get_camera"] = s;
		}
		if (!schemas.has("runtime_set_camera")) {
			ToolSchema s; s.tool_name = "runtime_set_camera"; s.description = "Set camera position/rotation/zoom in running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema am; am.name = "mode"; am.type_hint = Variant::STRING; am.required = false; am.valid_values = Vector<String>{"2d", "3d"}; s.arguments.push_back(am);
			schemas["runtime_set_camera"] = s;
		}
		if (!schemas.has("runtime_change_scene")) {
			ToolSchema s; s.tool_name = "runtime_change_scene"; s.description = "Change the current scene in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "scene_path"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["runtime_change_scene"] = s;
		}
		if (!schemas.has("runtime_get_nodes_in_group")) {
			ToolSchema s; s.tool_name = "runtime_get_nodes_in_group"; s.description = "Get all nodes in a group from the running game"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "group"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["runtime_get_nodes_in_group"] = s;
		}
		if (!schemas.has("runtime_manage_group")) {
			ToolSchema s; s.tool_name = "runtime_manage_group"; s.description = "Add or remove a node from a group at runtime"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; s.arguments.push_back(ap);
			ToolArgSchema ag; ag.name = "group"; ag.type_hint = Variant::STRING; ag.required = true; s.arguments.push_back(ag);
			ToolArgSchema aa; aa.name = "action"; aa.type_hint = Variant::STRING; aa.required = false; aa.valid_values = Vector<String>{"add", "remove"}; s.arguments.push_back(aa);
			schemas["runtime_manage_group"] = s;
		}
		if (!schemas.has("runtime_get_node_property")) {
			ToolSchema s; s.tool_name = "runtime_get_node_property"; s.description = "Get a property value from a node in the running game"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; s.arguments.push_back(ap);
			ToolArgSchema apr; apr.name = "property"; apr.type_hint = Variant::STRING; apr.required = true; s.arguments.push_back(apr);
			schemas["runtime_get_node_property"] = s;
		}
		if (!schemas.has("runtime_set_node_property")) {
			ToolSchema s; s.tool_name = "runtime_set_node_property"; s.description = "Set a property on a node by path in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; s.arguments.push_back(ap);
			ToolArgSchema apr; apr.name = "property"; apr.type_hint = Variant::STRING; apr.required = true; s.arguments.push_back(apr);
			ToolArgSchema av; av.name = "value"; av.type_hint = Variant::STRING; av.required = true; av.description = "GDScript literal value"; s.arguments.push_back(av);
			schemas["runtime_set_node_property"] = s;
		}
		if (!schemas.has("runtime_call_method")) {
			ToolSchema s; s.tool_name = "runtime_call_method"; s.description = "Call a method on a node in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = true; s.arguments.push_back(ap);
			ToolArgSchema am; am.name = "method"; am.type_hint = Variant::STRING; am.required = true; s.arguments.push_back(am);
			ToolArgSchema aa; aa.name = "args"; aa.type_hint = Variant::STRING; aa.required = false; aa.description = "Comma-separated GDScript args"; s.arguments.push_back(aa);
			schemas["runtime_call_method"] = s;
		}
		if (!schemas.has("runtime_window")) {
			ToolSchema s; s.tool_name = "runtime_window"; s.description = "Get or set window properties in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "action"; a.type_hint = Variant::STRING; a.required = false; a.valid_values = Vector<String>{"get", "set_size", "set_title", "set_fullscreen"}; s.arguments.push_back(a);
			schemas["runtime_window"] = s;
		}
		if (!schemas.has("runtime_get_performance")) {
			ToolSchema s; s.tool_name = "runtime_get_performance"; s.description = "Get FPS, memory, draw calls from the running game"; s.requires_scene = false; s.is_write_operation = false;
			schemas["runtime_get_performance"] = s;
		}
		if (!schemas.has("runtime_raycast")) {
			ToolSchema s; s.tool_name = "runtime_raycast"; s.description = "Cast a physics ray in the running game (2D or 3D)"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema am; am.name = "mode"; am.type_hint = Variant::STRING; am.required = false; am.valid_values = Vector<String>{"2d", "3d"}; s.arguments.push_back(am);
			schemas["runtime_raycast"] = s;
		}
		if (!schemas.has("runtime_serialize_state")) {
			ToolSchema s; s.tool_name = "runtime_serialize_state"; s.description = "Serialize or print node tree state for save/load"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = false; s.arguments.push_back(ap);
			ToolArgSchema aa; aa.name = "action"; aa.type_hint = Variant::STRING; aa.required = false; aa.valid_values = Vector<String>{"save", "print"}; s.arguments.push_back(aa);
			schemas["runtime_serialize_state"] = s;
		}
	}

	// ── PHASE 2: DEEP DEBUGGER TOOL SCHEMAS ──
	{
		if (!schemas.has("debugger_get_sessions")) {
			ToolSchema s; s.tool_name = "debugger_get_sessions"; s.description = "List all debugger sessions and their state"; s.requires_scene = false; s.is_write_operation = false;
			schemas["debugger_get_sessions"] = s;
		}
		if (!schemas.has("debugger_get_state")) {
			ToolSchema s; s.tool_name = "debugger_get_state"; s.description = "Get detailed debugger state (active, breaked, stack, errors)"; s.requires_scene = false; s.is_write_operation = false;
			schemas["debugger_get_state"] = s;
		}
		if (!schemas.has("debugger_get_stack")) {
			ToolSchema s; s.tool_name = "debugger_get_stack"; s.description = "Get stack frames when debugger is breaked"; s.requires_scene = false; s.is_write_operation = false;
			schemas["debugger_get_stack"] = s;
		}
		if (!schemas.has("debugger_get_variables")) {
			ToolSchema s; s.tool_name = "debugger_get_variables"; s.description = "Request variable inspection for a stack frame"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "frame"; a.type_hint = Variant::INT; a.required = false; a.description = "Stack frame index (default 0 = top)"; s.arguments.push_back(a);
			schemas["debugger_get_variables"] = s;
		}
		if (!schemas.has("debugger_step_out")) {
			ToolSchema s; s.tool_name = "debugger_step_out"; s.description = "Step out of the current function (resume until return)"; s.requires_scene = false; s.is_write_operation = true;
			schemas["debugger_step_out"] = s;
		}
		if (!schemas.has("debugger_toggle_profiler")) {
			ToolSchema s; s.tool_name = "debugger_toggle_profiler"; s.description = "Enable/disable a profiler in the running game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema ap; ap.name = "profiler"; ap.type_hint = Variant::STRING; ap.required = true; ap.valid_values = Vector<String>{"servers", "visual", "performance", "scripts", "multiplayer:bandwidth", "multiplayer:rpc", "multiplayer:replication"}; s.arguments.push_back(ap);
			ToolArgSchema ae; ae.name = "enable"; ae.type_hint = Variant::BOOL; ae.required = false; s.arguments.push_back(ae);
			schemas["debugger_toggle_profiler"] = s;
		}
		if (!schemas.has("debugger_evaluate")) {
			ToolSchema s; s.tool_name = "debugger_evaluate"; s.description = "Evaluate expression in debugger context (when breaked)"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "expression"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			ToolArgSchema af; af.name = "frame"; af.type_hint = Variant::INT; af.required = false; s.arguments.push_back(af);
			schemas["debugger_evaluate"] = s;
		}
		if (!schemas.has("debugger_await_condition")) {
			ToolSchema s; s.tool_name = "debugger_await_condition"; s.description = "Poll an expression until truthy or timeout"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "expression"; a.type_hint = Variant::STRING; a.required = true; a.description = "GDScript expression that should become truthy"; s.arguments.push_back(a);
			ToolArgSchema at; at.name = "timeout_ms"; at.type_hint = Variant::INT; at.required = false; s.arguments.push_back(at);
			ToolArgSchema ap; ap.name = "poll_interval_ms"; ap.type_hint = Variant::INT; ap.required = false; s.arguments.push_back(ap);
			schemas["debugger_await_condition"] = s;
		}
		if (!schemas.has("debugger_assert_condition")) {
			ToolSchema s; s.tool_name = "debugger_assert_condition"; s.description = "Assert a GDScript expression is truthy in the running game"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "expression"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			ToolArgSchema am; am.name = "message"; am.type_hint = Variant::STRING; am.required = false; s.arguments.push_back(am);
			schemas["debugger_assert_condition"] = s;
		}
		if (!schemas.has("debugger_get_errors")) {
			ToolSchema s; s.tool_name = "debugger_get_errors"; s.description = "Get error/warning counts and break location"; s.requires_scene = false; s.is_write_operation = false;
			schemas["debugger_get_errors"] = s;
		}
		if (!schemas.has("debugger_send_custom_message")) {
			ToolSchema s; s.tool_name = "debugger_send_custom_message"; s.description = "Send a custom EngineDebugger message to the game"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema am; am.name = "message"; am.type_hint = Variant::STRING; am.required = true; am.description = "Message name (e.g. mcp:ping)"; s.arguments.push_back(am);
			ToolArgSchema ad; ad.name = "data"; ad.type_hint = Variant::ARRAY; ad.required = false; s.arguments.push_back(ad);
			schemas["debugger_send_custom_message"] = s;
		}
		if (!schemas.has("debugger_get_performance_snapshot")) {
			ToolSchema s; s.tool_name = "debugger_get_performance_snapshot"; s.description = "Capture detailed performance snapshot (FPS, memory, objects, draw calls)"; s.requires_scene = false; s.is_write_operation = false;
			schemas["debugger_get_performance_snapshot"] = s;
		}
		if (!schemas.has("debugger_get_memory_info")) {
			ToolSchema s; s.tool_name = "debugger_get_memory_info"; s.description = "Get detailed memory usage from the running game"; s.requires_scene = false; s.is_write_operation = false;
			schemas["debugger_get_memory_info"] = s;
		}
		if (!schemas.has("debugger_reload_scripts")) {
			ToolSchema s; s.tool_name = "debugger_reload_scripts"; s.description = "Hot-reload all scripts in the running game"; s.requires_scene = false; s.is_write_operation = true;
			schemas["debugger_reload_scripts"] = s;
		}
	}

	// ── PHASE 3: HEADLESS SCENE/RESOURCE + PROJECT HEALTH SCHEMAS ──
	{
		if (!schemas.has("scene_read")) {
			ToolSchema s; s.tool_name = "scene_read"; s.description = "Read a .tscn file as structured JSON without opening it"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "scene_path"; a.type_hint = Variant::STRING; a.required = true; a.description = "Path to .tscn file"; s.arguments.push_back(a);
			ToolArgSchema am; am.name = "max_depth"; am.type_hint = Variant::INT; am.required = false; s.arguments.push_back(am);
			schemas["scene_read"] = s;
		}
		if (!schemas.has("scene_modify_node")) {
			ToolSchema s; s.tool_name = "scene_modify_node"; s.description = "Modify node properties in a .tscn file without opening it"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema asp; asp.name = "scene_path"; asp.type_hint = Variant::STRING; asp.required = true; s.arguments.push_back(asp);
			ToolArgSchema anp; anp.name = "node_path"; anp.type_hint = Variant::STRING; anp.required = true; anp.description = "Relative path within scene (e.g. Player/Sprite2D)"; s.arguments.push_back(anp);
			ToolArgSchema ap; ap.name = "properties"; ap.type_hint = Variant::DICTIONARY; ap.required = true; s.arguments.push_back(ap);
			schemas["scene_modify_node"] = s;
		}
		if (!schemas.has("scene_remove_node")) {
			ToolSchema s; s.tool_name = "scene_remove_node"; s.description = "Remove a node from a .tscn file without opening it"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema asp; asp.name = "scene_path"; asp.type_hint = Variant::STRING; asp.required = true; s.arguments.push_back(asp);
			ToolArgSchema anp; anp.name = "node_path"; anp.type_hint = Variant::STRING; anp.required = true; s.arguments.push_back(anp);
			schemas["scene_remove_node"] = s;
		}
		if (!schemas.has("resource_create")) {
			ToolSchema s; s.tool_name = "resource_create"; s.description = "Create a .tres resource file from a type"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema asp; asp.name = "save_path"; asp.type_hint = Variant::STRING; asp.required = true; s.arguments.push_back(asp);
			ToolArgSchema at; at.name = "resource_type"; at.type_hint = Variant::STRING; at.required = true; at.description = "ClassDB resource type (e.g. StandardMaterial3D, Theme)"; s.arguments.push_back(at);
			ToolArgSchema ap; ap.name = "properties"; ap.type_hint = Variant::DICTIONARY; ap.required = false; s.arguments.push_back(ap);
			schemas["resource_create"] = s;
		}
		if (!schemas.has("resource_read")) {
			ToolSchema s; s.tool_name = "resource_read"; s.description = "Read a .tres/.res resource as structured JSON"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "path"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["resource_read"] = s;
		}
		if (!schemas.has("resource_modify")) {
			ToolSchema s; s.tool_name = "resource_modify"; s.description = "Modify properties in a .tres/.res file"; s.requires_scene = false; s.is_write_operation = true;
			ToolArgSchema a; a.name = "path"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			ToolArgSchema ap; ap.name = "properties"; ap.type_hint = Variant::DICTIONARY; ap.required = true; s.arguments.push_back(ap);
			schemas["resource_modify"] = s;
		}
		if (!schemas.has("scene_get_signals")) {
			ToolSchema s; s.tool_name = "scene_get_signals"; s.description = "List all signal connections in a .tscn file"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "scene_path"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["scene_get_signals"] = s;
		}
		if (!schemas.has("project_detect_broken_scripts")) {
			ToolSchema s; s.tool_name = "project_detect_broken_scripts"; s.description = "Scan GDScript files for parse/compile errors"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "root"; a.type_hint = Variant::STRING; a.required = false; s.arguments.push_back(a);
			schemas["project_detect_broken_scripts"] = s;
		}
		if (!schemas.has("project_scan_missing_deps")) {
			ToolSchema s; s.tool_name = "project_scan_missing_deps"; s.description = "Find broken/missing resource dependencies"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "root"; a.type_hint = Variant::STRING; a.required = false; s.arguments.push_back(a);
			schemas["project_scan_missing_deps"] = s;
		}
		if (!schemas.has("project_scan_cyclic_deps")) {
			ToolSchema s; s.tool_name = "project_scan_cyclic_deps"; s.description = "Detect cyclic dependency chains in resources"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "root"; a.type_hint = Variant::STRING; a.required = false; s.arguments.push_back(a);
			schemas["project_scan_cyclic_deps"] = s;
		}
		if (!schemas.has("project_audit_health")) {
			ToolSchema s; s.tool_name = "project_audit_health"; s.description = "Comprehensive project health audit (scripts, deps, main scene, orphans)"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "root"; a.type_hint = Variant::STRING; a.required = false; s.arguments.push_back(a);
			schemas["project_audit_health"] = s;
		}
		if (!schemas.has("project_get_class_api")) {
			ToolSchema s; s.tool_name = "project_get_class_api"; s.description = "Get ClassDB metadata (methods, signals, properties, enums) for a class"; s.requires_scene = false; s.is_write_operation = false;
			ToolArgSchema a; a.name = "class_name"; a.type_hint = Variant::STRING; a.required = true; s.arguments.push_back(a);
			schemas["project_get_class_api"] = s;
		}
	}

	// ── PHASE 4: ADVANCED RUNTIME OPS SCHEMAS ──
	{
		const char *phase4_tools[] = {
			"runtime_mesh_instance", "runtime_light_3d", "runtime_gridmap",
			"runtime_environment", "runtime_sky", "runtime_debug_draw",
			"runtime_canvas_draw", "runtime_parallax",
			"runtime_audio_play", "runtime_audio_bus", "runtime_audio_effect",
			"runtime_ui_control", "runtime_ui_text", "runtime_ui_popup", "runtime_ui_range",
			"runtime_shader_param", "runtime_theme_override", "runtime_physics_body"
		};
		for (const char *t : phase4_tools) {
			String ts(t);
			if (schemas.has(ts)) continue;
			ToolSchema s;
			s.tool_name = ts;
			s.description = ts.replace("runtime_", "Runtime: ").replace("_", " ");
			s.requires_scene = false;
			s.is_write_operation = true;
			// Most take node_path as first arg
			ToolArgSchema ap; ap.name = "node_path"; ap.type_hint = Variant::STRING; ap.required = false; ap.description = "Target node path in running game"; s.arguments.push_back(ap);
			schemas[ts] = s;
		}
	}


	initialized = true;
	return schemas;
}

// ── OpenAI-compatible JSON Schema generation ────────────────────────────────

String YeetAIToolSchemaRegistry::_variant_type_to_json_schema_type(Variant::Type p_type) {
	switch (p_type) {
		case Variant::BOOL:
			return "boolean";
		case Variant::INT:
			return "integer";
		case Variant::FLOAT:
			return "number";
		case Variant::STRING:
			return "string";
		case Variant::ARRAY:
		case Variant::PACKED_STRING_ARRAY:
			return "array";
		case Variant::DICTIONARY:
			return "object";
		case Variant::VECTOR2:
		case Variant::VECTOR3:
		case Variant::COLOR:
			// Represent as objects with x/y/z or r/g/b fields, or accept arrays.
			return "object";
		default:
			return "string"; // Fallback for anything else
	}
}

Dictionary YeetAIToolSchemaRegistry::tool_schema_to_openai_function(const ToolSchema &p_schema) {
	Dictionary function;
	function["name"] = p_schema.tool_name;
	// Azure OpenAI / Foundry caps tool descriptions at 1024 characters.
	String desc = p_schema.description;
	if (desc.length() > 1024) {
		desc = desc.substr(0, 1021) + "...";
	}
	function["description"] = desc;

	Dictionary parameters;
	parameters["type"] = "object";

	Dictionary properties;
	Array required;

	for (int i = 0; i < p_schema.arguments.size(); i++) {
		const ToolArgSchema &arg = p_schema.arguments[i];

		Dictionary prop;
		if (arg.type_hint != Variant::NIL) {
			prop["type"] = _variant_type_to_json_schema_type(arg.type_hint);
		}
		if (!arg.description.is_empty()) {
			prop["description"] = arg.description;
		}

		// Add enum constraint if applicable
		if (!arg.valid_values.is_empty()) {
			Array enum_values;
			for (int v = 0; v < arg.valid_values.size(); v++) {
				enum_values.push_back(arg.valid_values[v]);
			}
			prop["enum"] = enum_values;
		}

		// Add numeric range constraints if applicable
		if (arg.type_hint == Variant::INT || arg.type_hint == Variant::FLOAT) {
			if (arg.min_value > -FLT_MAX) {
				prop["minimum"] = arg.min_value;
			}
			if (arg.max_value < FLT_MAX) {
				prop["maximum"] = arg.max_value;
			}
		}

		// For vector/color types, document the expected shape
		if (arg.type_hint == Variant::VECTOR3) {
			prop["description"] = String(arg.description) + " Format: {x:number,y:number,z:number} or [x,y,z]";
		} else if (arg.type_hint == Variant::VECTOR2) {
			prop["description"] = String(arg.description) + " Format: {x:number,y:number} or [x,y]";
		} else if (arg.type_hint == Variant::COLOR) {
			prop["description"] = String(arg.description) + " Format: {r:number,g:number,b:number,a:number} or [r,g,b,a] or hex string";
		}

		properties[arg.name] = prop;

		if (arg.required && p_schema.category != "inferred") {
			required.push_back(arg.name);
		}
	}

	parameters["properties"] = properties;
	if (!required.is_empty()) {
		parameters["required"] = required;
	}

	function["parameters"] = parameters;

	Dictionary tool_def;
	tool_def["type"] = "function";
	tool_def["function"] = function;

	return tool_def;
}

Array YeetAIToolSchemaRegistry::build_openai_tools_payload(const Vector<String> &p_tool_names) {
	Array tools;

	if (!p_tool_names.is_empty()) {
		// Build schemas for the specific tool names provided (uses inference for missing ones).
		for (const String &tool_name : p_tool_names) {
			const ToolSchema *schema = get_schema(tool_name);
			if (schema != nullptr) {
				tools.push_back(tool_schema_to_openai_function(*schema));
			}
		}
	} else {
		// Fallback: only explicitly registered schemas.
		const HashMap<String, ToolSchema> &all = get_all_schemas();
		for (const KeyValue<String, ToolSchema> &kv : all) {
			tools.push_back(tool_schema_to_openai_function(kv.value));
		}
	}

	return tools;
}
