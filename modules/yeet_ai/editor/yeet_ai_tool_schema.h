/**************************************************************************/
/*  yeet_ai_tool_schema.h                                                 */
/**************************************************************************/
/*  Tool schema definitions for validation.                               */
/*  Each tool has a schema that defines:                                  */
/*  - Required/optional arguments                                         */
/*  - Type constraints                                                    */
/*  - Valid value ranges                                                  */
/*  - Whether node_path should be auto-resolved                           */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "scene/main/node.h"
#include <cfloat>

// Schema for a single argument
struct ToolArgSchema {
	StringName name;
	String description;
	Variant::Type type_hint = Variant::NIL;
	bool required = false;
	Variant default_value;
	
	// For node_path arguments: auto-resolve bare names to full paths
	bool auto_resolve_node_path = false;
	
	// For value arguments: constrain to specific enum values
	Vector<String> valid_values;
	
	// For numeric arguments: min/max constraints
	float min_value = -FLT_MAX;
	float max_value = FLT_MAX;
};

// Schema for a tool
struct ToolSchema {
	StringName tool_name;
	String description;
	String category;
	
	Vector<ToolArgSchema> arguments;
	
	// Whether this tool can be run without a scene
	bool requires_scene = true;
	
	// Whether this tool modifies the scene (write operation)
	bool is_write_operation = false;
	
	// Whether this tool should be batched together with similar operations
	bool supports_batching = false;
};

// Schema registry - lookup by tool name
class YeetAIToolSchemaRegistry {
public:
	static const ToolSchema* get_schema(const String &p_tool_name);
	static bool validate_arguments(const ToolSchema &p_schema, const Dictionary &p_args, Array &r_errors);
	static String auto_resolve_node_path(const String &p_node_path, Node *p_scene_root, String &r_error);
	static bool is_valid_node_path(const String &p_path);
	
	// Generate OpenAI-compatible tool definitions for native function calling.
	// Returns an Array of Dictionary objects, each with "type", "function", "name", "description", "parameters".
	static Array build_openai_tools_payload();
	// Convert a single ToolSchema to an OpenAI-compatible function definition Dictionary.
	static Dictionary tool_schema_to_openai_function(const ToolSchema &p_schema);
	
private:
	static const HashMap<String, ToolSchema> &get_all_schemas();
	// Map Godot Variant::Type to JSON Schema type string.
	static String _variant_type_to_json_schema_type(Variant::Type p_type);
};
