/**************************************************************************/
/*  yeet_ai_response_parser.cpp                                           */
/**************************************************************************/
/*  Structured response parsing implementation.                           */
/*  Provides reliable tool call extraction with validation.               */
/**************************************************************************/

#include "yeet_ai_response_parser.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"

// ── Main parse entry point ──────────────────────────────────────────────────

YeetAIResponseEnvelope YeetAIResponseParser::parse(const String &raw_response) {
	YeetAIResponseEnvelope envelope;
	envelope.type = YeetAIResponseType::UNKNOWN;
	envelope.raw_data.clear();
	
	// Clean response
	String cleaned = raw_response.strip_edges();
	cleaned = _clean_content_artifacts(cleaned);
	cleaned = strip_reasoning_markers(cleaned);
	
	// Check for thinking markers
	if (is_thinking_response(cleaned)) {
		envelope.type = YeetAIResponseType::THINKING;
		envelope.raw_data["content"] = cleaned;
		return envelope;
	}
	
	// Try to extract JSON
	String json_content = extract_json_content(cleaned);
	if (json_content.is_empty()) {
		envelope.type = YeetAIResponseType::UNKNOWN;
		envelope.raw_data["content"] = cleaned;
		return envelope;
	}
	
	// Parse JSON
	Dictionary parsed;
	String parse_error;
	if (!parse_json_dict(json_content, parsed, parse_error)) {
		// Try fixing syntax
		String fixed = fix_json_syntax(json_content);
		if (!parse_json_dict(fixed, parsed, parse_error)) {
			envelope.type = YeetAIResponseType::ERROR;
			envelope.error_code = "PARSE_ERROR";
			envelope.error_message = parse_error;
			envelope.raw_data = parsed;
			return envelope;
		}
	}
	
	envelope.raw_data = parsed;
	
	// Determine response type
	String type_str = String(parsed.get("type", "")).strip_edges().to_lower();
	envelope.type = normalize_type(type_str);
	
	if (envelope.type == YeetAIResponseType::TOOL_CALL) {
		// Extract tool call
		String tool_name = extract_tool_name(parsed);
		Dictionary args = parsed.get("arguments", Dictionary());
		
		envelope.tool_call.tool_name = tool_name;
		envelope.tool_call.arguments = args;
		
		// Validate
		if (!validate_tool_call(tool_name, args)) {
			envelope.type = YeetAIResponseType::ERROR;
			envelope.error_code = "INVALID_TOOL_CALL";
			envelope.error_message = "Tool call has invalid arguments";
		}
	} else if (envelope.type == YeetAIResponseType::FINAL_ANSWER) {
		envelope.message = String(parsed.get("message", parsed.get("content", ""))).strip_edges();
	}
	
	return envelope;
}

// ── JSON extraction ─────────────────────────────────────────────────────────

String YeetAIResponseParser::extract_json_content(const String &raw) {
	String cleaned = raw.strip_edges();
	
	// Strip code fences
	if (cleaned.begins_with("```")) {
		int first_nl = cleaned.find("\n");
		if (first_nl != -1) {
			cleaned = cleaned.substr(first_nl + 1).strip_edges();
		}
		if (cleaned.ends_with("```")) {
			cleaned = cleaned.substr(0, cleaned.length() - 3).strip_edges();
		}
	}
	
	// Strip stop tokens
	static const char *stop_tokens[] = {
		"\"\n", "<|end|>", "</s>", "<tool_call|>", "<|tool_sep|>",
		"<|eot_id|>", "<|tool_call|>", "<|start_header_id|>", "<|end_header_id|>",
	};
	for (int pass = 0; pass < 10; pass++) {
		bool changed = false;
		for (const char *token : stop_tokens) {
			String tok(token);
			if (tok.is_empty()) continue;
			if (cleaned.ends_with(tok)) {
				cleaned = cleaned.substr(0, cleaned.length() - tok.length()).strip_edges();
				changed = true;
			}
			if (cleaned.begins_with(tok)) {
				cleaned = cleaned.substr(tok.length()).strip_edges();
				changed = true;
			}
		}
		if (!changed) break;
	}
	
	// Find JSON object boundaries
	int json_start = cleaned.find("{");
	int json_end = cleaned.rfind("}");
	
	if (json_start == -1 || json_end == -1 || json_end <= json_start) {
		return "";
	}
	
	return cleaned.substr(json_start, json_end - json_start + 1);
}

// ── Validation ──────────────────────────────────────────────────────────────

bool YeetAIResponseParser::validate_tool_call(const String &tool_name, const Dictionary &args) {
	if (tool_name.is_empty()) {
		return false;
	}
	
	// Check for required fields based on tool type
	if (tool_name == "batch_tool_calls") {
		// Batch needs calls array
		return args.has("calls") || args.has("tool_calls");
	}
	
	if (tool_name == "add_node" || tool_name == "instantiate_scene") {
		// Node creation needs name and type/scene_path
		return args.has("name") && (args.has("type") || args.has("scene_path"));
	}
	
	if (tool_name == "create_gdscript_file" || tool_name == "update_gdscript_file") {
		// Script operations need path and contents
		return args.has("script_path") && args.has("contents");
	}
	
	if (tool_name == "set_node_property") {
		// Property setting needs path, property, and value
		return args.has("node_path") && args.has("property") && args.has("value");
	}
	
	// Default: at least some arguments should be present
	return !args.is_empty();
}

bool YeetAIResponseParser::is_thinking_response(const String &raw) {
	String lower = raw.to_lower().strip_edges();
	
	// Common thinking markers
	static const char *markers[] = {
		"let me think", "let's see", "here's what i'll do",
		"i need to", "first i should", "next i'll",
		"ok so", "alright so", "okay so",
	};
	
	for (const char *marker : markers) {
		if (lower.contains(marker)) {
			return true;
		}
	}
	
	return false;
}

bool YeetAIResponseParser::merge_fragmented_tool_call(const String &raw, YeetAIToolCall &r_tool_call) {
	String cleaned = raw.strip_edges();
	
	// Find all JSON objects
	Vector<int> json_starts;
	Vector<int> json_ends;
	
	for (int i = 0; i < cleaned.length(); i++) {
		if (cleaned[i] == '{') {
			int end = find_json_end(cleaned, i);
			if (end != -1) {
				json_starts.push_back(i);
				json_ends.push_back(end);
				i = end;
			}
		}
	}
	
	if (json_starts.size() < 2) {
		return false;
	}
	
	// Try to merge two adjacent JSON objects
	Dictionary d1, d2;
	String err1, err2;
	
	if (!parse_json_dict(cleaned.substr(json_starts[0], json_ends[0] - json_starts[0] + 1), d1, err1)) {
		return false;
	}
	if (!parse_json_dict(cleaned.substr(json_starts[1], json_ends[1] - json_starts[1] + 1), d2, err2)) {
		return false;
	}
	
	// Check if one has arguments and other has tool name
	String tool_name = extract_tool_name(d1);
	Dictionary args = d2.get("arguments", Dictionary());
	
	if (tool_name.is_empty()) {
		tool_name = extract_tool_name(d2);
		args = d1.get("arguments", Dictionary());
	}
	
	if (tool_name.is_empty()) {
		return false;
	}
	
	r_tool_call.tool_name = tool_name;
	r_tool_call.arguments = args;
	
	return validate_tool_call(tool_name, args);
}

// ── Private helpers ─────────────────────────────────────────────────────────

int YeetAIResponseParser::find_json_end(const String &s, int start) {
	if (start < 0 || start >= s.length() || s[start] != '{') {
		return -1;
	}
	
	int depth = 0;
	bool in_string = false;
	bool escape = false;
	
	for (int i = start; i < s.length(); i++) {
		const char32_t c = s[i];
		
		if (in_string) {
			if (escape) {
				escape = false;
				continue;
			}
			if (c == '\\') {
				escape = true;
				continue;
			}
			if (c == '"') {
				in_string = false;
			}
			continue;
		}
		
		if (c == '"') {
			in_string = true;
			continue;
		}
		
		if (c == '{') {
			depth++;
			continue;
		}
		
		if (c == '}') {
			depth--;
			if (depth == 0) {
				return i;
			}
		}
	}
	
	return -1;
}

bool YeetAIResponseParser::parse_json_dict(const String &json, Dictionary &r_result, String &r_error) {
	// Try parsing with JSON class
	Error err = ERR_FAILED;
	Variant parsed = JSON::parse_string(json, &err);
	
	if (err != OK || parsed.get_type() != Variant::DICTIONARY) {
		r_error = "JSON parse failed: " + String::num_int64((int64_t)err);
		return false;
	}
	
	r_result = parsed;
	return true;
}

String YeetAIResponseParser::extract_tool_name(const Dictionary &d) {
	String tool = String(d.get("tool", "")).strip_edges();
	if (!tool.is_empty()) {
		return tool;
	}
	
	String name = String(d.get("name", "")).strip_edges();
	if (!name.is_empty()) {
		return name;
	}
	
	return "";
}

YeetAIResponseType YeetAIResponseParser::normalize_type(const String &type_str) {
	if (type_str == "tool_call") {
		return YeetAIResponseType::TOOL_CALL;
	}
	if (type_str == "final" || type_str == "final_answer") {
		return YeetAIResponseType::FINAL_ANSWER;
	}
	if (type_str == "error") {
		return YeetAIResponseType::ERROR;
	}
	if (type_str == "thinking") {
		return YeetAIResponseType::THINKING;
	}
	return YeetAIResponseType::UNKNOWN;
}

String YeetAIResponseParser::extract_by_marker(const String &text, const String &marker) {
	int pos = text.find(marker);
	if (pos == -1) {
		return "";
	}
	
	int end = find_json_end(text, pos);
	if (end == -1) {
		return "";
	}
	
	return text.substr(pos, end - pos + 1);
}

String YeetAIResponseParser::fix_json_syntax(const String &json) {
	String fixed;
	fixed.reserve(json.length() + json.length() / 4);
	
	bool in_string = false;
	char32_t string_delim = 0;
	
	for (int i = 0; i < json.length(); i++) {
		const char32_t c = json[i];
		
		if (in_string) {
			fixed += c;
			if (c == string_delim && (i == 0 || json[i - 1] != '\\')) {
				in_string = false;
			}
			continue;
		}
		
		if (c == '"' || c == '\'') {
			in_string = true;
			string_delim = c;
			fixed += c;
			continue;
		}
		
		// Check for unquoted key followed by colon
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
			int start = i;
			while (i < json.length() && ((json[i] >= 'a' && json[i] <= 'z') || 
					(json[i] >= 'A' && json[i] <= 'Z') || json[i] == '_' || 
					(json[i] >= '0' && json[i] <= '9'))) {
				i++;
			}
			
			const String ident = json.substr(start, i - start);
			
			// Skip past whitespace
			int k = i;
			while (k < json.length() && (json[k] == ' ' || json[k] == '\t')) {
				k++;
			}
			
			// Check for colon (key-value separator)
			if (k < json.length() && json[k] == ':' &&
					ident != "true" && ident != "false" && ident != "null") {
				fixed += '"';
				fixed += ident;
				fixed += '"';
			} else {
				fixed += ident;
			}
			i--; // will be incremented by loop
			continue;
		}
		
		fixed += c;
	}
	
	return fixed;
}

// ── Utility functions (need to be accessible) ───────────────────────────────

static String _clean_content_artifacts(String p_text) {
	String out;
	out.reserve(p_text.length());
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t c = p_text[i];
		if ((c >= 32 && c <= 126) || c == '\t' || c == '\n' || c == '\r') {
			out += c;
		}
	}
	return out;
}

// Forward declaration for strip_reasoning_markers (defined in yeet_ai_dock.cpp)
extern String strip_reasoning_markers(String p_text);
