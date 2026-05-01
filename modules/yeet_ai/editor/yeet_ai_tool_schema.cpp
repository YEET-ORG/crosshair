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

	// Try exact name match first
	Node *found = p_scene_root->get_node(p_node_path);
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

		if (current->get_name() == p_node_path) {
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

// Generate a fallback schema for tools without explicit definitions.
// This keeps the native tools payload comprehensive even when we haven't
// hand-written every schema.
static ToolSchema _infer_schema(const String &p_tool_name) {
	ToolSchema schema;
	schema.tool_name = p_tool_name;
	schema.category = "inferred";
	schema.description = p_tool_name.replace("_", " ").capitalize();
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

		if (s.contains("camera_3d") || s.contains("light") || s.contains("rigid") || s.contains("static") || s.contains("character") || s.contains("area_3d") || s.contains("ray_cast_3d") || s.contains("shape_cast_3d") || s.contains("path_3d") || s.contains("vehicle") || s.contains("fog_volume") || s.contains("reflection_probe") || s.contains("gi_probe") || s.contains("particle_emitter")) {
			ToolArgSchema arg_pos;
			arg_pos.name = "position";
			arg_pos.type_hint = Variant::VECTOR3;
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
		if (s == "capture_editor_viewport" || s == "capture_game_viewport" || s == "capture_texture_resource") {
			ToolArgSchema arg_max;
			arg_max.name = s == "capture_texture_resource" ? "path" : "max_width";
			arg_max.type_hint = s == "capture_texture_resource" ? Variant::STRING : Variant::INT;
			arg_max.required = false;
			schema.arguments.push_back(arg_max);
		}
		if (s == "capture_subviewport") {
			ToolArgSchema arg_path;
			arg_path.name = "node_path";
			arg_path.type_hint = Variant::STRING;
			arg_path.required = true;
			arg_path.auto_resolve_node_path = true;
			schema.arguments.push_back(arg_path);
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
	function["description"] = p_schema.description;

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
