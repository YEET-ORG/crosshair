/**************************************************************************/
/*  yeet_ai_tools_scripting.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/input/input_map.h"
#include "core/input/input_event.h"
#include "core/object/class_db.h"
#include "core/string/ustring.h"
#include "core/templates/list.h"
#include "core/templates/local_vector.h"

namespace {
Ref<InputEvent> _create_binding_event(const Dictionary &p_binding, String &r_error) {
	const String event_type = String(p_binding.get("event_type", "key")).to_lower();

	if (event_type == "key") {
		Ref<InputEventKey> ev;
		ev.instantiate();

		const int keycode = int(p_binding.get("keycode", 0));
		const int physical_keycode = int(p_binding.get("physical_keycode", 0));
		const int unicode = int(p_binding.get("unicode", 0));
		const bool shift = bool(p_binding.get("shift", false));
		const bool alt = bool(p_binding.get("alt", false));
		const bool ctrl = bool(p_binding.get("ctrl", false));
		const bool meta = bool(p_binding.get("meta", false));

		if (keycode != 0) {
			ev->set_keycode(Key(keycode));
		}
		if (physical_keycode != 0) {
			ev->set_physical_keycode(Key(physical_keycode));
		}
		if (unicode != 0) {
			ev->set_unicode(unicode);
		}
		ev->set_shift_pressed(shift);
		ev->set_alt_pressed(alt);
		ev->set_ctrl_pressed(ctrl);
		ev->set_meta_pressed(meta);

		const int device = int(p_binding.get("device", 0));
		ev->set_device(device);
		return ev;
	}

	if (event_type == "joy_button") {
		Ref<InputEventJoypadButton> ev;
		ev.instantiate();
		ev->set_button_index(JoyButton(int(p_binding.get("button_index", 0))));
		ev->set_pressed(true);
		ev->set_device(int(p_binding.get("device", 0)));
		return ev;
	}

	if (event_type == "joy_axis") {
		Ref<InputEventJoypadMotion> ev;
		ev.instantiate();
		ev->set_axis(JoyAxis(int(p_binding.get("axis", 0))));
		ev->set_axis_value(double(p_binding.get("axis_value", 1.0)));
		ev->set_device(int(p_binding.get("device", 0)));
		return ev;
	}

	if (event_type == "mouse") {
		Ref<InputEventMouseButton> ev;
		ev.instantiate();
		ev->set_button_index(MouseButton(int(p_binding.get("button_index", 1))));
		ev->set_pressed(true);
		ev->set_device(int(p_binding.get("device", 0)));
		return ev;
	}

	r_error = "Unknown event_type: " + event_type;
	return Ref<InputEvent>();
}

void _strip_inline_comment(String &r_line) {
	bool in_string = false;
	char32_t string_char = 0;
	for (int i = 0; i < r_line.length(); i++) {
		const char32_t c = r_line[i];
		if (in_string) {
			if (c == string_char && (i == 0 || r_line[i - 1] != '\\')) {
				in_string = false;
			}
			continue;
		}
		if (c == '"' || c == '\'') {
			in_string = true;
			string_char = c;
			continue;
		}
		if (c == '#') {
			r_line = r_line.substr(0, i);
			return;
		}
	}
}

String _extract_balanced(const String &p_source, int p_open_pos, char32_t p_open, char32_t p_close) {
	int depth = 0;
	for (int i = p_open_pos; i < p_source.length(); i++) {
		if (p_source[i] == p_open) {
			depth++;
		} else if (p_source[i] == p_close) {
			depth--;
			if (depth == 0) {
				return p_source.substr(p_open_pos + 1, i - p_open_pos - 1);
			}
		}
	}
	return p_source.substr(p_open_pos + 1);
}
} // namespace

Dictionary YeetAIDock::_tool_get_gdscript_symbols(const Dictionary &p_args) const {
	Dictionary result;
	const String file_path = _arg_string(p_args, "file_path");
	if (file_path.is_empty()) {
		return _make_error("file_path is required.");
	}
	if (!_is_allowed_text_file(file_path) && !file_path.to_lower().ends_with(".gd")) {
		return _make_error("file_path must be a .gd file.");
	}

	Error ferr = OK;
	const String source = FileAccess::get_file_as_string(file_path, &ferr);
	if (ferr != OK) {
		return _make_error(vformat("Failed to read file: %s", file_path));
	}

	String class_name;
	String extends_class;
	Array functions;
	Array variables;
	Array signals;
	Array constants;
	Array enums;

	const PackedStringArray lines = source.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i];
		const int comment_pos = line.find("#");
		if (comment_pos >= 0) {
			bool in_string = false;
			char32_t sc = 0;
			for (int ci = 0; ci < line.length(); ci++) {
				if (in_string) {
					if (line[ci] == sc && (ci == 0 || line[ci - 1] != '\\')) {
						in_string = false;
					}
					continue;
				}
				if (line[ci] == '"' || line[ci] == '\'') {
					in_string = true;
					sc = line[ci];
					continue;
				}
				if (line[ci] == '#') {
					line = line.substr(0, ci);
					break;
				}
			}
		}

		const String stripped = line.strip_edges();

		if (stripped.begins_with("class_name ")) {
			class_name = stripped.substr(String("class_name").length()).strip_edges();
			if (class_name.find("#") >= 0) {
				class_name = class_name.substr(0, class_name.find("#")).strip_edges();
			}
			continue;
		}

		if (stripped.begins_with("extends ")) {
			extends_class = stripped.substr(String("extends").length()).strip_edges();
			continue;
		}

		if (stripped.begins_with("signal ")) {
			const String sig_body = stripped.substr(String("signal").length()).strip_edges();
			Dictionary sig_entry;
			const int paren_open = sig_body.find("(");
			if (paren_open >= 0) {
				sig_entry["name"] = sig_body.substr(0, paren_open).strip_edges();
				const String args_str = _extract_balanced(sig_body, paren_open, '(', ')');
				sig_entry["args"] = args_str.strip_edges();
			} else {
				sig_entry["name"] = sig_body.strip_edges();
				sig_entry["args"] = "";
			}
			signals.push_back(sig_entry);
			continue;
		}

		if (stripped.begins_with("const ")) {
			const String const_body = stripped.substr(String("const").length()).strip_edges();
			Dictionary const_entry;
			const int eq_pos = const_body.find("=");
			if (eq_pos >= 0) {
				const_entry["name"] = const_body.substr(0, eq_pos).strip_edges();
				const_entry["value"] = const_body.substr(eq_pos + 1).strip_edges();
			} else {
				const_entry["name"] = const_body.strip_edges();
				const_entry["value"] = "";
			}
			const String name_part = const_entry["name"];
			const int colon_pos = name_part.find(":");
			if (colon_pos >= 0) {
				const_entry["type_hint"] = name_part.substr(colon_pos + 1).strip_edges();
				const_entry["name"] = name_part.substr(0, colon_pos).strip_edges();
			} else {
				const_entry["type_hint"] = "";
			}
			constants.push_back(const_entry);
			continue;
		}

		if (stripped.begins_with("enum ")) {
			const String enum_body = stripped.substr(String("enum").length()).strip_edges();
			Dictionary enum_entry;
			const int brace_open = enum_body.find("{");
			if (brace_open >= 0) {
				enum_entry["name"] = enum_body.substr(0, brace_open).strip_edges();
				const String values_str = _extract_balanced(enum_body, brace_open, '{', '}');
				Array values;
				const PackedStringArray parts = values_str.split(",");
				for (int pi = 0; pi < parts.size(); pi++) {
					String part = parts[pi].strip_edges();
					if (part.is_empty()) {
						continue;
					}
					Dictionary val_entry;
					const int veq = part.find("=");
					if (veq >= 0) {
						val_entry["name"] = part.substr(0, veq).strip_edges();
						val_entry["value"] = part.substr(veq + 1).strip_edges();
					} else {
						val_entry["name"] = part;
						val_entry["value"] = "";
					}
					values.push_back(val_entry);
				}
				enum_entry["values"] = values;
			} else {
				enum_entry["name"] = enum_body.strip_edges().rstrip("}").strip_edges();
				enum_entry["values"] = Array();
			}
			enums.push_back(enum_entry);
			continue;
		}

		if (stripped.begins_with("func ")) {
			const String func_body = stripped.substr(String("func").length()).strip_edges();
			Dictionary func_entry;
			const int paren_open = func_body.find("(");
			if (paren_open >= 0) {
				func_entry["name"] = func_body.substr(0, paren_open).strip_edges();
				const String args_str = _extract_balanced(func_body, paren_open, '(', ')');
				func_entry["args"] = args_str.strip_edges();
			} else {
				func_entry["name"] = func_body.strip_edges();
				func_entry["args"] = "";
			}
			const String after_paren = func_body.substr(func_body.find(")") + 1).strip_edges();
			if (after_paren.begins_with("->")) {
				String ret_type = after_paren.substr(2).strip_edges();
				const int colon_pos = ret_type.find(":");
				if (colon_pos >= 0) {
					ret_type = ret_type.substr(0, colon_pos).strip_edges();
				}
				func_entry["return_type"] = ret_type;
			} else {
				func_entry["return_type"] = "";
			}
			functions.push_back(func_entry);
			continue;
		}

		if (stripped.begins_with("var ") || stripped.begins_with("@onready var ")) {
			bool onready = stripped.begins_with("@onready");
			String var_body = stripped;
			if (onready) {
				var_body = stripped.substr(String("@onready").length()).strip_edges();
			}
			if (!var_body.begins_with("var ")) {
				continue;
			}
			var_body = var_body.substr(String("var").length()).strip_edges();

			Dictionary var_entry;
			var_entry["onready"] = onready;

			String name_part;
			const int eq_pos = var_body.find("=");
			if (eq_pos >= 0) {
				name_part = var_body.substr(0, eq_pos).strip_edges();
				var_entry["default_value"] = var_body.substr(eq_pos + 1).strip_edges();
			} else {
				name_part = var_body.strip_edges();
				var_entry["default_value"] = "";
			}

			const int colon_pos = name_part.find(":");
			if (colon_pos >= 0) {
				var_entry["name"] = name_part.substr(0, colon_pos).strip_edges();
				var_entry["type_hint"] = name_part.substr(colon_pos + 1).strip_edges();
			} else {
				var_entry["name"] = name_part.strip_edges();
				var_entry["type_hint"] = "";
			}
			variables.push_back(var_entry);
			continue;
		}
	}

	result["file_path"] = file_path;
	if (!class_name.is_empty()) {
		result["class_name"] = class_name;
	}
	if (!extends_class.is_empty()) {
		result["extends"] = extends_class;
	}
	result["functions"] = functions;
	result["variables"] = variables;
	result["signals"] = signals;
	result["constants"] = constants;
	result["enums"] = enums;
	return result;
}

Dictionary YeetAIDock::_tool_get_gdscript_docs(const Dictionary &p_args) const {
	Dictionary result;
	const String file_path = _arg_string(p_args, "file_path");
	if (file_path.is_empty()) {
		return _make_error("file_path is required.");
	}
	if (!_is_allowed_text_file(file_path) && !file_path.to_lower().ends_with(".gd")) {
		return _make_error("file_path must be a .gd file.");
	}

	Error ferr = OK;
	const String source = FileAccess::get_file_as_string(file_path, &ferr);
	if (ferr != OK) {
		return _make_error(vformat("Failed to read file: %s", file_path));
	}

	Array docs;
	const PackedStringArray lines = source.split("\n");

	String pending_doc;
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i];
		const String stripped = line.strip_edges();

		if (stripped.begins_with("##")) {
			if (!pending_doc.is_empty()) {
				pending_doc += "\n";
			}
			pending_doc += stripped.substr(2).strip_edges();
			continue;
		}

		if (pending_doc.is_empty()) {
			if (!stripped.is_empty() && !stripped.begins_with("#")) {
				pending_doc = "";
			}
			continue;
		}

		String symbol;
		String symbol_type;

		if (stripped.begins_with("func ")) {
			const String func_body = stripped.substr(String("func").length()).strip_edges();
			const int paren = func_body.find("(");
			symbol = (paren >= 0) ? func_body.substr(0, paren).strip_edges() : func_body.strip_edges();
			symbol_type = "function";
		} else if (stripped.begins_with("class_name ")) {
			symbol = stripped.substr(String("class_name").length()).strip_edges();
			symbol_type = "class";
		} else if (stripped.begins_with("signal ")) {
			const String sig_body = stripped.substr(String("signal").length()).strip_edges();
			const int paren = sig_body.find("(");
			symbol = (paren >= 0) ? sig_body.substr(0, paren).strip_edges() : sig_body.strip_edges();
			symbol_type = "signal";
		} else if (stripped.begins_with("var ") || stripped.begins_with("@onready var ")) {
			String var_body = stripped;
			if (var_body.begins_with("@onready ")) {
				var_body = var_body.substr(String("@onready").length()).strip_edges();
			}
			if (var_body.begins_with("var ")) {
				var_body = var_body.substr(String("var").length()).strip_edges();
				const int colon = var_body.find(":");
				const int eq = var_body.find("=");
				int end = var_body.length();
				if (colon >= 0) {
					end = colon;
				} else if (eq >= 0) {
					end = eq;
				}
				symbol = var_body.substr(0, end).strip_edges();
				symbol_type = "variable";
			}
		} else if (stripped.begins_with("const ")) {
			String const_body = stripped.substr(String("const").length()).strip_edges();
			const int eq = const_body.find("=");
			const int colon = const_body.find(":");
			int end = const_body.length();
			if (colon >= 0) {
				end = colon;
			} else if (eq >= 0) {
				end = eq;
			}
			symbol = const_body.substr(0, end).strip_edges();
			symbol_type = "constant";
		} else if (stripped.begins_with("enum ")) {
			String enum_body = stripped.substr(String("enum").length()).strip_edges();
			const int brace = enum_body.find("{");
			symbol = (brace >= 0) ? enum_body.substr(0, brace).strip_edges() : enum_body.strip_edges();
			symbol_type = "enum";
		}

		if (!symbol.is_empty()) {
			Dictionary entry;
			entry["symbol"] = symbol;
			entry["symbol_type"] = symbol_type;
			entry["doc_text"] = pending_doc;
			docs.push_back(entry);
		}

		pending_doc = "";
	}

	result["file_path"] = file_path;
	result["docs"] = docs;
	result["count"] = docs.size();
	return result;
}

Dictionary YeetAIDock::_tool_set_input_action_bindings(const Dictionary &p_args) const {
	Dictionary result;
	const String action_name = _arg_string(p_args, "action_name");
	if (action_name.is_empty()) {
		return _make_error("action_name is required.");
	}

	InputMap *input_map = InputMap::get_singleton();
	if (input_map == nullptr) {
		return _make_error("InputMap is unavailable.");
	}
	if (!input_map->has_action(action_name)) {
		return _make_error(vformat("Input action '%s' does not exist. Use create_input_action first.", action_name));
	}

	const Array bindings = _arg_array(p_args, "bindings");
	if (bindings.is_empty()) {
		return _make_error("bindings must be a non-empty array.");
	}

	Array added_events;
	for (int i = 0; i < bindings.size(); i++) {
		const Variant &bv = bindings[i];
		if (bv.get_type() != Variant::DICTIONARY) {
			return _make_error(vformat("bindings[%d] must be a dictionary.", i));
		}
		String event_error;
		Ref<InputEvent> ev = _create_binding_event(Dictionary(bv), event_error);
		if (ev.is_null()) {
			return _make_error(vformat("bindings[%d]: %s", i, event_error));
		}
		input_map->action_add_event(action_name, ev);
		added_events.push_back(ev);
	}

	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps) {
		const float dz = input_map->action_get_deadzone(action_name);
		Array all_events;
		const List<Ref<InputEvent>> *evs = input_map->action_get_events(action_name);
		if (evs) {
			for (const Ref<InputEvent> &e : *evs) {
				all_events.push_back(e);
			}
		}
		Dictionary action_dict;
		action_dict["deadzone"] = dz;
		action_dict["events"] = all_events;
		ps->set_setting("input/" + action_name, action_dict);
		ps->save();
	}

	result["action_name"] = action_name;
	result["added_count"] = added_events.size();
	result["saved"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_remove_input_action(const Dictionary &p_args) const {
	Dictionary result;
	const String action_name = _arg_string(p_args, "action_name");
	if (action_name.is_empty()) {
		return _make_error("action_name is required.");
	}

	InputMap *input_map = InputMap::get_singleton();
	if (input_map == nullptr) {
		return _make_error("InputMap is unavailable.");
	}
	if (!input_map->has_action(action_name)) {
		return _make_error(vformat("Input action '%s' does not exist.", action_name));
	}

	input_map->erase_action(action_name);
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps) {
		ps->clear("input/" + action_name);
		ps->save();
	}

	result["action_name"] = action_name;
	result["removed"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_run_gdscript_test(const Dictionary &p_args) const {
	return _make_error("run_gdscript_test is not available in the editor context. Use play_current_scene + get_runtime_debugger_state to test at runtime.");
}

Dictionary YeetAIDock::_tool_get_class_reference(const Dictionary &p_args) const {
	Dictionary result;
	const String class_name = _arg_string(p_args, "class_name");
	if (class_name.is_empty()) {
		return _make_error("class_name is required.");
	}

	if (!ClassDB::class_exists(class_name)) {
		return _make_error(vformat("Class '%s' not found in ClassDB.", class_name));
	}

	const bool include_inherited = _arg_bool(p_args, "include_inherited", false);

	Array properties;
	Array methods;
	Array signals_arr;
	Array constants_arr;
	Array enums_arr;

	String current_class = class_name;
	HashSet<String> seen_properties;
	HashSet<String> seen_methods;
	HashSet<String> seen_signals;
	HashSet<String> seen_constants;

	while (!current_class.is_empty()) {
		const StringName cn(current_class);

		List<PropertyInfo> property_list;
		ClassDB::get_property_list(cn, &property_list, true);
		for (const PropertyInfo &pi : property_list) {
			const String prop_name = pi.name;
			if (seen_properties.has(prop_name)) {
				continue;
			}
			seen_properties.insert(prop_name);
			Dictionary prop_entry;
			prop_entry["name"] = prop_name;
			prop_entry["type"] = Variant::get_type_name(pi.type);
			prop_entry["class"] = current_class;
			if (include_inherited) {
				prop_entry["defined_in"] = current_class;
			}
			properties.push_back(prop_entry);
		}

		List<MethodInfo> method_list;
		ClassDB::get_method_list(cn, &method_list, true);
		for (const MethodInfo &mi : method_list) {
			const String method_name = String(mi.name);
			if (seen_methods.has(method_name)) {
				continue;
			}
			seen_methods.insert(method_name);
			Dictionary method_entry;
			method_entry["name"] = method_name;
			if (include_inherited) {
				method_entry["defined_in"] = current_class;
			}
			methods.push_back(method_entry);
		}

		List<MethodInfo> signal_list;
		ClassDB::get_signal_list(cn, &signal_list, true);
		for (const MethodInfo &si : signal_list) {
			const String sig_name = String(si.name);
			if (seen_signals.has(sig_name)) {
				continue;
			}
			seen_signals.insert(sig_name);
			Dictionary sig_entry;
			sig_entry["name"] = sig_name;
			if (include_inherited) {
				sig_entry["defined_in"] = current_class;
			}
			signals_arr.push_back(sig_entry);
		}

		List<String> constant_names;
		ClassDB::get_integer_constant_list(cn, &constant_names, true);
		for (const String &const_name : constant_names) {
			if (seen_constants.has(const_name)) {
				continue;
			}
			seen_constants.insert(const_name);
			Dictionary const_entry;
			const_entry["name"] = const_name;
			const_entry["value"] = ClassDB::get_integer_constant(cn, StringName(const_name));
			if (include_inherited) {
				const_entry["defined_in"] = current_class;
			}
			constants_arr.push_back(const_entry);
		}

		List<StringName> enum_names;
		ClassDB::get_enum_list(cn, &enum_names, true);
		for (const StringName &enum_name : enum_names) {
			Dictionary enum_entry;
			enum_entry["name"] = String(enum_name);
			if (include_inherited) {
				enum_entry["defined_in"] = current_class;
			}
			List<StringName> enum_constants;
			ClassDB::get_enum_constants(cn, enum_name, &enum_constants, true);
			Array values;
			for (const StringName &ecn : enum_constants) {
				Dictionary val_entry;
				val_entry["name"] = String(ecn);
				val_entry["value"] = ClassDB::get_integer_constant(cn, ecn);
				values.push_back(val_entry);
			}
			enum_entry["values"] = values;
			enums_arr.push_back(enum_entry);
		}

		if (!include_inherited) {
			break;
		}
		current_class = String(ClassDB::get_parent_class(cn));
	}

	result["class_name"] = class_name;
	result["parent_class"] = String(ClassDB::get_parent_class(StringName(class_name)));
	result["properties"] = properties;
	result["methods"] = methods;
	result["signals"] = signals_arr;
	result["constants"] = constants_arr;
	result["enums"] = enums_arr;
	return result;
}

Dictionary YeetAIDock::_tool_search_class_db(const Dictionary &p_args) const {
	Dictionary result;
	const String query = _arg_string(p_args, "query");
	if (query.is_empty()) {
		return _make_error("query is required.");
	}

	const String parent_class = _arg_string(p_args, "parent_class");
	const int max_results = CLAMP(_arg_int(p_args, "max_results", 20), 1, 200);

	const String query_lower = query.to_lower();
	LocalVector<StringName> all_classes;
	ClassDB::get_class_list(all_classes);

	Array matches;
	for (uint32_t i = 0; i < all_classes.size() && matches.size() < max_results; i++) {
		const String cls = String(all_classes[i]);
		if (!cls.to_lower().contains(query_lower)) {
			continue;
		}
		if (!parent_class.is_empty()) {
			if (!ClassDB::is_parent_class(StringName(cls), StringName(parent_class))) {
				continue;
			}
		}
		matches.push_back(cls);
	}

	result["query"] = query;
	if (!parent_class.is_empty()) {
		result["parent_class"] = parent_class;
	}
	result["matches"] = matches;
	result["count"] = matches.size();
	return result;
}

Dictionary YeetAIDock::_tool_get_method_signature(const Dictionary &p_args) const {
	Dictionary result;
	const String class_name = _arg_string(p_args, "class_name");
	const String method_name = _arg_string(p_args, "method_name");
	if (class_name.is_empty()) {
		return _make_error("class_name is required.");
	}
	if (method_name.is_empty()) {
		return _make_error("method_name is required.");
	}

	if (!ClassDB::class_exists(class_name)) {
		return _make_error(vformat("Class '%s' not found in ClassDB.", class_name));
	}

	MethodInfo method_info;
	const bool found = ClassDB::get_method_info(StringName(class_name), StringName(method_name), &method_info, true);
	if (!found) {
		return _make_error(vformat("Method '%s' not found in class '%s'.", method_name, class_name));
	}

	result["return_type"] = Variant::get_type_name(method_info.return_val.type);

	Array arguments;
	for (const PropertyInfo &arg : method_info.arguments) {
		Dictionary arg_entry;
		arg_entry["name"] = String(arg.name);
		arg_entry["type"] = Variant::get_type_name(arg.type);
		arguments.push_back(arg_entry);
	}

	const int default_start = method_info.arguments.size() - method_info.default_arguments.size();
	for (int i = 0; i < method_info.default_arguments.size(); i++) {
		const int arg_index = default_start + i;
		if (arg_index < arguments.size()) {
			Dictionary arg_entry = arguments[arg_index];
			arg_entry["default"] = _json_safe_variant(method_info.default_arguments[i]);
		}
	}

	result["arguments"] = arguments;
	result["argument_count"] = arguments.size();
	return result;
}

Dictionary YeetAIDock::_tool_get_enum_values(const Dictionary &p_args) const {
	Dictionary result;
	const String class_name = _arg_string(p_args, "class_name");
	const String enum_name = _arg_string(p_args, "enum_name");
	if (class_name.is_empty()) {
		return _make_error("class_name is required.");
	}
	if (enum_name.is_empty()) {
		return _make_error("enum_name is required.");
	}

	if (!ClassDB::class_exists(class_name)) {
		return _make_error(vformat("Class '%s' not found in ClassDB.", class_name));
	}

	List<StringName> enum_constants;
	ClassDB::get_enum_constants(StringName(class_name), StringName(enum_name), &enum_constants, true);
	if (enum_constants.is_empty()) {
		return _make_error(vformat("Enum '%s' not found in class '%s'.", enum_name, class_name));
	}

	Array values;
	for (const StringName &ecn : enum_constants) {
		Dictionary val_entry;
		val_entry["name"] = String(ecn);
		val_entry["value"] = ClassDB::get_integer_constant(StringName(class_name), ecn);
		values.push_back(val_entry);
	}

	result["enum_name"] = enum_name;
	result["class_name"] = class_name;
	result["values"] = values;
	result["count"] = values.size();
	return result;
}

Dictionary YeetAIDock::_tool_lint_gdscript(const Dictionary &p_args) const {
	Dictionary result;
	const String file_path = _arg_string(p_args, "file_path");
	if (file_path.is_empty()) {
		return _make_error("file_path is required.");
	}
	if (!_is_allowed_text_file(file_path) && !file_path.to_lower().ends_with(".gd")) {
		return _make_error("file_path must be a .gd file.");
	}

	Error ferr = OK;
	const String source = FileAccess::get_file_as_string(file_path, &ferr);
	if (ferr != OK) {
		return _make_error(vformat("Failed to read file: %s", file_path));
	}

	Array errors;
	Array warnings;

	const PackedStringArray lines = source.split("\n");
	int prev_indent = -1;

	for (int i = 0; i < lines.size(); i++) {
		const String &line = lines[i];
		const String stripped = line.strip_edges(true, false);

		if (stripped.is_empty()) {
			continue;
		}

		int indent = 0;
		for (int c = 0; c < line.length(); c++) {
			if (line[c] == '\t') {
				indent++;
			} else if (line[c] == ' ') {
				indent++;
			} else {
				break;
			}
		}

		const String stripped_clean = line.strip_edges();

		if (stripped_clean.begins_with("func ") || stripped_clean.begins_with("if ") || stripped_clean.begins_with("elif ") || stripped_clean.begins_with("else:") || stripped_clean.begins_with("for ") || stripped_clean.begins_with("while ") || stripped_clean.begins_with("class ")) {
			if (!stripped_clean.ends_with(":") && !stripped_clean.contains(":")) {
				bool has_colon = false;
				for (int ci = 0; ci < stripped_clean.length(); ci++) {
					if (stripped_clean[ci] == ':') {
						has_colon = true;
						break;
					}
				}
				if (!has_colon) {
					Dictionary err_entry;
					err_entry["line"] = i + 1;
					err_entry["message"] = vformat("Missing colon after '%s'", stripped_clean.substr(0, stripped_clean.find(" ")).strip_edges());
					errors.push_back(err_entry);
				}
			}
		}

		bool in_string = false;
		char32_t string_char = 0;
		int string_start_line = -1;
		for (int ci = 0; ci < stripped_clean.length(); ci++) {
			const char32_t c = stripped_clean[ci];
			if (in_string) {
				if (c == '\\') {
					ci++;
					continue;
				}
				if (c == string_char) {
					in_string = false;
				}
				continue;
			}
			if (c == '"' || c == '\'') {
				in_string = true;
				string_char = c;
				string_start_line = i + 1;
			}
		}
		if (in_string) {
			Dictionary err_entry;
			err_entry["line"] = i + 1;
			err_entry["message"] = "Unclosed string literal";
			errors.push_back(err_entry);
		}

		if (prev_indent >= 0 && indent > prev_indent + 1 && !stripped_clean.begins_with("#")) {
			Dictionary warn_entry;
			warn_entry["line"] = i + 1;
			warn_entry["message"] = "Inconsistent indentation";
			warnings.push_back(warn_entry);
		}

		if (!stripped_clean.is_empty()) {
			prev_indent = indent;
		}
	}

	result["file_path"] = file_path;
	result["errors"] = errors;
	result["warnings"] = warnings;
	result["error_count"] = errors.size();
	result["warning_count"] = warnings.size();
	return result;
}
