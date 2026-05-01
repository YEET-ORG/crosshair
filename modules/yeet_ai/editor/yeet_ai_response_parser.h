/**************************************************************************/
/*  yeet_ai_response_parser.h                                             */
/**************************************************************************/
/*  Structured response parsing for reliable tool call extraction.        */
/*  Replaces fragile multi-fallback parsing with structured protocol.     */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "core/string/ustring.h"

// Response types the parser recognizes
enum class YeetAIResponseType {
	TOOL_CALL,
	FINAL_ANSWER,
	THINKING,
	ERROR,
	UNKNOWN
};

// Structured tool call with validated arguments
struct YeetAIToolCall {
	String tool_name;
	Dictionary arguments;
	
	bool is_valid() const {
		return !tool_name.is_empty() && !arguments.is_empty();
	}
};

// Structured response envelope
struct YeetAIResponseEnvelope {
	YeetAIResponseType type;
	Dictionary raw_data;
	
	// For TOOL_CALL
	YeetAIToolCall tool_call;
	
	// For FINAL_ANSWER
	String message;
	
	// For ERROR
	String error_code;
	String error_message;
	
	bool is_tool_call() const { return type == YeetAIResponseType::TOOL_CALL; }
	bool is_final_answer() const { return type == YeetAIResponseType::FINAL_ANSWER; }
	bool is_error() const { return type == YeetAIResponseType::ERROR; }
};

// Structured response parser
class YeetAIResponseParser {
public:
	// Parse response with structured validation
	// Returns validated envelope or null on failure
	static YeetAIResponseEnvelope parse(const String &raw_response);
	
	// Extract JSON from response with context awareness
	static String extract_json_content(const String &raw);
	
	// Validate tool call arguments against expected schema
	static bool validate_tool_call(const String &tool_name, const Dictionary &args);
	
	// Check if response appears to be thinking/reasoning
	static bool is_thinking_response(const String &raw);
	
	// Detect and extract tool call from fragmented JSON
	static bool merge_fragmented_tool_call(const String &raw, YeetAIToolCall &r_tool_call);
	
private:
	// Find matching closing brace for JSON object
	static int find_json_end(const String &s, int start);
	
	// Parse JSON dictionary with error handling
	static bool parse_json_dict(const String &json, Dictionary &r_result, String &r_error);
	
	// Extract tool name from dictionary (handles variations)
	static String extract_tool_name(const Dictionary &d);
	
	// Normalize response type from string
	static YeetAIResponseType normalize_type(const String &type_str);
	
	// Extract JSON using marker-based search
	static String extract_by_marker(const String &text, const String &marker);
	
	// Fix common JSON issues (unquoted keys, etc.)
	static String fix_json_syntax(const String &json);
};
