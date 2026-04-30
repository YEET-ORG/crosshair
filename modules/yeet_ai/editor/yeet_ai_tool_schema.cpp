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
	if ((p_expected == Variant::INT && p_actual == Variant::FLOAT) ||
			(p_expected == Variant::FLOAT && p_actual == Variant::INT)) {
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
			if (arg.required) {
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

// Get schema for a tool (returns null if no schema defined)
const ToolSchema* YeetAIToolSchemaRegistry::get_schema(const String &p_tool_name) {
	const HashMap<String, ToolSchema> &all_schemas = get_all_schemas();
	const ToolSchema *schema = all_schemas.getptr(p_tool_name);
	return schema;
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
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = true;
		arg_name.description = "Name for the new node";
		schema.arguments.push_back(arg_name);
		
		ToolArgSchema arg_type;
		arg_type.name = "type";
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
		arg_scene.name = "scene_path";
		arg_scene.type_hint = Variant::STRING;
		arg_scene.required = true;
		arg_scene.description = "Path to the scene file (e.g., 'res://scenes/player.tscn')";
		schema.arguments.push_back(arg_scene);
		
		ToolArgSchema arg_name;
		arg_name.name = "name";
		arg_name.type_hint = Variant::STRING;
		arg_name.required = false;
		arg_name.default_value = String();
		arg_name.description = "Name for the instance (optional)";
		schema.arguments.push_back(arg_name);
		
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
		schema.requires_scene = true;
		schema.is_write_operation = false;
		schema.supports_batching = false; // Special tool
		
		ToolArgSchema arg_calls;
		arg_calls.name = "calls";
		arg_calls.type_hint = Variant::ARRAY;
		arg_calls.required = true;
		arg_calls.description = "Array of tool call dictionaries";
		schema.arguments.push_back(arg_calls);
		
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
		arg_name.name = "name";
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
		arg_index.name = "index";
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
		schema.description = "Add a primitive mesh to a StaticBody3D or child";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		
		ToolArgSchema arg_body;
		arg_body.name = "node_path";
		arg_body.type_hint = Variant::STRING;
		arg_body.required = true;
		arg_body.description = "Target node (auto-resolved)";
		arg_body.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_body);
		
		ToolArgSchema arg_type;
		arg_type.name = "primitive_type";
		arg_type.type_hint = Variant::STRING;
		arg_type.required = true;
		arg_type.description = "Mesh type";
		arg_type.valid_values = Vector<String>{"box", "cylinder", "cone", "sphere"};
		schema.arguments.push_back(arg_type);
		
		ToolArgSchema arg_size;
		arg_size.name = "size";
		arg_size.type_hint = Variant::VECTOR3;
		arg_size.required = false;
		arg_size.default_value = Vector3(1, 1, 1);
		arg_size.description = "Size of the primitive";
		schema.arguments.push_back(arg_size);
		
		schemas["add_primitive_mesh"] = schema;
	}
	
	{
		ToolSchema schema;
		schema.tool_name = "add_collision_shape";
		schema.description = "Add a collision shape to a physics body";
		schema.requires_scene = true;
		schema.is_write_operation = true;
		
		ToolArgSchema arg_body;
		arg_body.name = "node_path";
		arg_body.type_hint = Variant::STRING;
		arg_body.required = true;
		arg_body.description = "Physics body (auto-resolved)";
		arg_body.auto_resolve_node_path = true;
		schema.arguments.push_back(arg_body);
		
		ToolArgSchema arg_type;
		arg_type.name = "shape_type";
		arg_type.type_hint = Variant::STRING;
		arg_type.required = true;
		arg_type.description = "Collision shape type";
		arg_type.valid_values = Vector<String>{"box", "sphere", "capsule", "cylinder", "terrain"};
		schema.arguments.push_back(arg_type);
		
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
		
		ToolArgSchema arg_event;
		arg_event.name = "event";
		arg_event.type_hint = Variant::STRING;
		arg_event.required = true;
		arg_event.description = "Input event to bind (e.g., 'InputEventKey', 'InputEventMouseButton')";
		schema.arguments.push_back(arg_event);
		
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
		schema.requires_scene = true;
		schema.is_write_operation = true;
		
		ToolArgSchema arg_color;
		arg_color.name = "albedo_color";
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
		prop["type"] = _variant_type_to_json_schema_type(arg.type_hint);
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
		
		if (arg.required) {
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

Array YeetAIToolSchemaRegistry::build_openai_tools_payload() {
	const HashMap<String, ToolSchema> &all = get_all_schemas();
	Array tools;
	
	for (const KeyValue<String, ToolSchema> &kv : all) {
		// Skip internal/meta tools that shouldn't be exposed to the LLM directly
		if (kv.key == "batch_tool_calls") {
			continue;
		}
		tools.push_back(tool_schema_to_openai_function(kv.value));
	}
	
	return tools;
}
