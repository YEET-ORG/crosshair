/**************************************************************************/
/*  yeet_ai_gdscript_autofix.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/
/*  Heuristic auto-fixes for common GDScript parse errors.               */
/*  Called automatically when create_gdscript_file detects errors.       */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/file_access.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

// ── Heuristic fix: add missing colons after control flow statements ───────
static String _fix_missing_colons(const String &p_code) {
	Vector<String> lines = p_code.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i].strip_edges();
		// Skip comments and strings
		if (line.begins_with("#")) {
			continue;
		}
		// Check for control flow statements that need colons
		static const char *needs_colon[] = {
			"if ", "elif ", "else", "for ", "while ", "func ", "class ",
			"match ", "when ", "signal "
		};
		for (const char *kw : needs_colon) {
			if (line.begins_with(kw)) {
				// Check if line already ends with colon
				if (!line.ends_with(":")) {
					// Check it's not inside a string or comment
					int comment_pos = line.find("#");
					if (comment_pos < 0 || comment_pos > line.length() - 2) {
						lines.write[i] = lines[i] + ":";
					}
				}
				break;
			}
		}
	}
	String result;
	for (int i = 0; i < lines.size(); i++) {
		if (i > 0) {
			result += "\n";
		}
		result += lines[i];
	}
	return result;
}

// ── Heuristic fix: add pass to empty indented blocks ──────────────────────
static String _fix_empty_blocks(const String &p_code) {
	Vector<String> lines = p_code.split("\n");
	for (int i = 0; i < lines.size() - 1; i++) {
		String line = lines[i].strip_edges();
		// Check if this line ends with a colon (starts a block)
		if (line.ends_with(":")) {
			// Check next line
			if (i + 1 < lines.size()) {
				String next = lines[i + 1];
				String next_stripped = next.strip_edges();
				// If next line is at same indent or less, block is empty
				int current_indent = lines[i].length() - lines[i].strip_edges(true, false).length();
				int next_indent = next.length() - next.strip_edges(true, false).length();
				if (next_indent <= current_indent && !next_stripped.is_empty() && !next_stripped.begins_with("#")) {
					// Insert pass with proper indentation
					String indent;
					for (int s = 0; s < current_indent + 1; s++) {
						indent += "\t";
					}
					lines.insert(i + 1, indent + "pass");
				}
			}
		}
	}
	String result;
	for (int i = 0; i < lines.size(); i++) {
		if (i > 0) {
			result += "\n";
		}
		result += lines[i];
	}
	return result;
}

// ── Heuristic fix: common misspellings ────────────────────────────────────
static String _fix_common_misspellings(const String &p_code) {
	String result = p_code;
	// Common misspellings from AI-generated code
	static const char *misspellings[][2] = {
		{"move_distanc", "move_distance"},
		{"velocity.x = ", "velocity.x = "},
		{"velocity.y = ", "velocity.y = "},
		{"position.x = ", "position.x = "},
		{"position.y = ", "position.y = "},
	};
	for (const auto &pair : misspellings) {
		result = result.replace(pair[0], pair[1]);
	}
	return result;
}

// ── Heuristic fix: remove trailing whitespace and normalize newlines ──────
static String _normalize_whitespace(const String &p_code) {
	String result = p_code;
	result = result.replace("\r\n", "\n");
	result = result.replace("\r", "\n");
	// Remove trailing whitespace from each line
	Vector<String> lines = result.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		lines.write[i] = lines[i].strip_edges(false, true);
	}
	result = "";
	for (int i = 0; i < lines.size(); i++) {
		if (i > 0) {
			result += "\n";
		}
		result += lines[i];
	}
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Public auto-fix entry point
// ═══════════════════════════════════════════════════════════════════════════

String YeetAIDock::_auto_fix_gdscript(const String &p_code, const String &p_error) const {
	String fixed = p_code;

	// Apply fixes based on error message
	if (p_error.contains("Expected indented block") || p_error.contains("expected indented block")) {
		fixed = _fix_empty_blocks(fixed);
	}

	if (p_error.contains("Expected ':'") || p_error.contains("expected ':'") || p_error.contains("Expected closing ':'")) {
		fixed = _fix_missing_colons(fixed);
	}

	if (p_error.contains("not declared") || p_error.contains("not found")) {
		fixed = _fix_common_misspellings(fixed);
	}

	// Always normalize whitespace
	fixed = _normalize_whitespace(fixed);

	return fixed;
}

// Returns true if any fix was applied (code changed)
bool YeetAIDock::_attempt_gdscript_auto_fix(const String &p_script_path, const String &p_error, String &r_fixed_contents) const {
	Error read_err = OK;
	String contents = FileAccess::get_file_as_string(p_script_path, &read_err);
	if (read_err != OK) {
		return false;
	}

	String fixed = _auto_fix_gdscript(contents, p_error);
	if (fixed == contents) {
		return false; // No changes made
	}

	if (!_commit_ai_text_file_change(p_script_path, contents, true, fixed, "Auto-Fix GDScript File")) {
		return false;
	}

	r_fixed_contents = fixed;
	return true;
}
