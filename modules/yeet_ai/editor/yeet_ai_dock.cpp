/**************************************************************************/
/*  yeet_ai_dock.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/config/project_settings.h"
#include "core/input/input_map.h"
#include "core/templates/hash_set.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/property_info.h"
#include "core/os/keyboard.h"
#include "core/string/translation.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/text_edit.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/main/node.h"
#include "scene/main/http_request.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/packed_scene.h"

#include <cstring>

namespace {
constexpr int MAX_PROJECT_TREE_ENTRIES = 200;
constexpr int MAX_FILE_READ_BYTES = 24 * 1024;
constexpr int MAX_SCENE_TREE_NODES = 300;
constexpr int MAX_PROPERTY_COLLECTION_DEPTH = 4;

Dictionary make_message(const String &p_role, const String &p_content) {
	Dictionary message;
	message["role"] = p_role;
	message["content"] = p_content;
	return message;
}

// LLMs often stringify `calls` or use `tool_calls`; a raw String does not coerce to Array.
Array coerce_json_array_from_variant(const Variant &p_v) {
	if (p_v.get_type() == Variant::ARRAY) {
		return p_v;
	}
	if (p_v.get_type() == Variant::DICTIONARY) {
		Array single;
		single.push_back(p_v);
		return single;
	}
	if (p_v.get_type() == Variant::STRING) {
		Ref<JSON> json;
		json.instantiate();
		if (json->parse(String(p_v).strip_edges()) == OK) {
			const Variant data = json->get_data();
			if (data.get_type() == Variant::ARRAY) {
				return data;
			}
			if (data.get_type() == Variant::DICTIONARY) {
				Array single;
				single.push_back(data);
				return single;
			}
		}
	}
	return Array();
}

Dictionary coerce_json_dictionary_from_variant(const Variant &p_v) {
	if (p_v.get_type() == Variant::DICTIONARY) {
		return p_v;
	}
	if (p_v.get_type() == Variant::STRING) {
		Ref<JSON> json;
		json.instantiate();
		if (json->parse(String(p_v).strip_edges()) == OK) {
			const Variant data = json->get_data();
			if (data.get_type() == Variant::DICTIONARY) {
				return data;
			}
		}
	}
	return Dictionary();
}

bool is_batch_tool_name_alias(const String &p_name) {
	const String s = p_name.to_lower().strip_edges();
	return s == "batch_tool_calls" || s == "batch_tools" || s == "batch_tool_call" || s == "batch";
}

// Reasoning / distilled models often wrap chain-of-thought in tags; strip so JSON can be found.
String strip_reasoning_markers(String p_text) {
	String s = p_text;
	for (int safety = 0; safety < 256; safety++) {
		bool changed = false;
		struct TagPair {
			const char *open;
			const char *close;
		};
		static const TagPair tags[] = {
			{ "`<redacted_thinking>`", "`</redacted_thinking>`" },
			{ "<reasoning>", "</reasoning>" },
			{ "<redacted_thinking>", "</redacted_thinking>" },
		};
		for (const TagPair &tp : tags) {
			const int a = s.find(tp.open);
			if (a == -1) {
				continue;
			}
			const int b = s.find(tp.close, a + int(strlen(tp.open)));
			if (b == -1) {
				continue;
			}
			s = s.substr(0, a) + s.substr(b + int(strlen(tp.close)));
			changed = true;
			break;
		}
		if (!changed) {
			break;
		}
	}
	return s.strip_edges();
}

// Balanced `}` for JSON object starting at `p_start` (respects strings and escapes).
int find_json_object_end(const String &p_s, int p_start) {
	if (p_start < 0 || p_start >= p_s.length() || p_s[p_start] != '{') {
		return -1;
	}
	int depth = 0;
	bool in_string = false;
	bool escape = false;
	for (int i = p_start; i < p_s.length(); i++) {
		const char32_t c = p_s[i];
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

bool contains_string(const Vector<String> &p_values, const String &p_value) {
	for (const String &value : p_values) {
		if (value == p_value) {
			return true;
		}
	}
	return false;
}

bool is_valid_input_action_name(const String &p_name) {
	if (p_name.is_empty()) {
		return false;
	}
	return !p_name.contains("/") && !p_name.contains(":") && !p_name.contains("=") && !p_name.contains("\\") && !p_name.contains("\"");
}

BaseMaterial3D::Transparency parse_transparency_mode(const String &p_value, bool &r_ok) {
	r_ok = true;
	const String value = p_value.to_lower().strip_edges();
	if (value.is_empty() || value == "disabled" || value == "opaque") {
		return BaseMaterial3D::TRANSPARENCY_DISABLED;
	}
	if (value == "alpha") {
		return BaseMaterial3D::TRANSPARENCY_ALPHA;
	}
	if (value == "alpha_scissor" || value == "scissor") {
		return BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR;
	}
	if (value == "alpha_hash" || value == "hash") {
		return BaseMaterial3D::TRANSPARENCY_ALPHA_HASH;
	}
	if (value == "alpha_depth_pre_pass" || value == "depth_pre_pass") {
		return BaseMaterial3D::TRANSPARENCY_ALPHA_DEPTH_PRE_PASS;
	}
	r_ok = false;
	return BaseMaterial3D::TRANSPARENCY_DISABLED;
}

Ref<InputEvent> create_input_event_from_definition(const Variant &p_definition, String &r_error) {
	String key_name;
	bool shift = false;
	bool alt = false;
	bool ctrl = false;
	bool meta = false;
	bool cmd_or_ctrl = false;
	bool physical = false;

	if (p_definition.get_type() == Variant::STRING) {
		key_name = String(p_definition);
	} else if (p_definition.get_type() == Variant::DICTIONARY) {
		const Dictionary event_dict = p_definition;
		const String type = String(event_dict.get("type", "key")).to_lower();
		if (type != "key") {
			r_error = "Only key input events are supported right now.";
			return Ref<InputEvent>();
		}

		key_name = String(event_dict.get("key", event_dict.get("keycode", "")));
		shift = bool(event_dict.get("shift", false));
		alt = bool(event_dict.get("alt", false));
		ctrl = bool(event_dict.get("ctrl", false));
		meta = bool(event_dict.get("meta", false));
		cmd_or_ctrl = bool(event_dict.get("cmd_or_ctrl", false));
		physical = bool(event_dict.get("physical", false));
	} else {
		r_error = "Input events must be strings or dictionaries.";
		return Ref<InputEvent>();
	}

	key_name = key_name.strip_edges();
	if (key_name.is_empty()) {
		r_error = "Key event is missing a key name.";
		return Ref<InputEvent>();
	}

	const Key key = find_keycode(key_name);
	if (key == Key::NONE) {
		r_error = "Unknown key name: " + key_name;
		return Ref<InputEvent>();
	}

	Key keycode = key;
	if (shift) {
		keycode = Key(uint32_t(keycode) | uint32_t(KeyModifierMask::SHIFT));
	}
	if (alt) {
		keycode = Key(uint32_t(keycode) | uint32_t(KeyModifierMask::ALT));
	}
	if (cmd_or_ctrl) {
		keycode = Key(uint32_t(keycode) | uint32_t(KeyModifierMask::CMD_OR_CTRL));
	} else {
		if (ctrl) {
			keycode = Key(uint32_t(keycode) | uint32_t(KeyModifierMask::CTRL));
		}
		if (meta) {
			keycode = Key(uint32_t(keycode) | uint32_t(KeyModifierMask::META));
		}
	}

	return InputEventKey::create_reference(keycode, physical);
}
}

void YeetAIDock::_bind_methods() {
}

void YeetAIDock::_apply_dock_theme() {
	const Ref<StyleBox> panel_fg = get_theme_stylebox(SNAME("PanelForeground"), EditorStringName(EditorStyles));
	if (panel_fg.is_valid()) {
		if (header_panel) {
			header_panel->add_theme_style_override(SNAME("panel"), panel_fg);
		}
		if (chat_panel) {
			chat_panel->add_theme_style_override(SNAME("panel"), panel_fg);
		}
		if (input_panel) {
			input_panel->add_theme_style_override(SNAME("panel"), panel_fg);
		}
	}

	const int font_size = has_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			? get_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			: 0;
	const Color font_color = get_theme_color(SNAME("font_color"), EditorStringName(Editor));
	const Color font_muted = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));

	if (chat_log) {
		if (font_size > 0) {
			chat_log->add_theme_font_size_override(SNAME("normal_font_size"), font_size);
			chat_log->add_theme_font_size_override(SNAME("bold_font_size"), font_size);
			chat_log->add_theme_font_size_override(SNAME("italics_font_size"), font_size);
			chat_log->add_theme_font_size_override(SNAME("mono_font_size"), MAX(11, font_size - 1));
		}
		chat_log->add_theme_color_override(SNAME("default_color"), font_color);
		chat_log->add_theme_constant_override(SNAME("line_separation"), int(3.0f * EDSCALE));
		chat_log->add_theme_constant_override(SNAME("paragraph_separation"), int(6.0f * EDSCALE));
	}

	if (prompt_input && font_size > 0) {
		prompt_input->add_theme_font_size_override(SNAME("font_size"), font_size);
	}

	if (status_label) {
		if (font_size > 0) {
			status_label->add_theme_font_size_override(SNAME("font_size"), MAX(10, font_size - 1));
		}
		status_label->add_theme_color_override(SNAME("font_color"), font_muted);
	}
}

YeetAIDock::YeetAIDock() {
	set_name(TTR("Crosshair AI"));
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	set_custom_minimum_size(Size2(430, 0) * EDSCALE);

	outer_margin = memnew(MarginContainer);
	outer_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	outer_margin->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	outer_margin->add_theme_constant_override("margin_left", int(10.0f * EDSCALE));
	outer_margin->add_theme_constant_override("margin_right", int(10.0f * EDSCALE));
	outer_margin->add_theme_constant_override("margin_top", int(8.0f * EDSCALE));
	outer_margin->add_theme_constant_override("margin_bottom", int(10.0f * EDSCALE));
	add_child(outer_margin);

	VBoxContainer *main_column = memnew(VBoxContainer);
	main_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_column->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_column->add_theme_constant_override("separation", int(10.0f * EDSCALE));
	outer_margin->add_child(main_column);

	header_panel = memnew(PanelContainer);
	header_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_column->add_child(header_panel);

	MarginContainer *header_mc = memnew(MarginContainer);
	header_mc->add_theme_constant_override("margin_left", int(12.0f * EDSCALE));
	header_mc->add_theme_constant_override("margin_right", int(12.0f * EDSCALE));
	header_mc->add_theme_constant_override("margin_top", int(10.0f * EDSCALE));
	header_mc->add_theme_constant_override("margin_bottom", int(10.0f * EDSCALE));
	header_panel->add_child(header_mc);

	HBoxContainer *header_row = memnew(HBoxContainer);
	header_row->add_theme_constant_override("separation", int(8.0f * EDSCALE));
	header_mc->add_child(header_row);

	Label *title = memnew(Label);
	title->set_theme_type_variation("HeaderSmall");
	title->set_text(TTR("Crosshair AI"));
	title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	header_row->add_child(title);

	status_label = memnew(Label);
	status_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	status_label->set_clip_text(true);
	status_label->set_text(TTR("Ready"));
	header_row->add_child(status_label);

	chat_panel = memnew(PanelContainer);
	chat_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_column->add_child(chat_panel);

	MarginContainer *chat_panel_mc = memnew(MarginContainer);
	chat_panel_mc->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_panel_mc->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_panel_mc->add_theme_constant_override("margin_left", int(10.0f * EDSCALE));
	chat_panel_mc->add_theme_constant_override("margin_right", int(10.0f * EDSCALE));
	chat_panel_mc->add_theme_constant_override("margin_top", int(8.0f * EDSCALE));
	chat_panel_mc->add_theme_constant_override("margin_bottom", int(10.0f * EDSCALE));
	chat_panel->add_child(chat_panel_mc);

	ScrollContainer *chat_scroll = memnew(ScrollContainer);
	chat_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
	chat_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_SHOW_NEVER);
	chat_panel_mc->add_child(chat_scroll);

	MarginContainer *chat_margin = memnew(MarginContainer);
	chat_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_margin->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_scroll->add_child(chat_margin);

	chat_log = memnew(RichTextLabel);
	chat_log->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_log->set_fit_content(true);
	chat_log->set_scroll_active(false);
	chat_log->set_selection_enabled(true);
	chat_log->set_context_menu_enabled(true);
	chat_log->set_use_bbcode(true);
	chat_margin->add_child(chat_log);

	input_panel = memnew(PanelContainer);
	input_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_column->add_child(input_panel);

	MarginContainer *input_mc = memnew(MarginContainer);
	input_mc->add_theme_constant_override("margin_left", int(12.0f * EDSCALE));
	input_mc->add_theme_constant_override("margin_right", int(12.0f * EDSCALE));
	input_mc->add_theme_constant_override("margin_top", int(10.0f * EDSCALE));
	input_mc->add_theme_constant_override("margin_bottom", int(12.0f * EDSCALE));
	input_panel->add_child(input_mc);

	VBoxContainer *input_column = memnew(VBoxContainer);
	input_column->add_theme_constant_override("separation", int(8.0f * EDSCALE));
	input_mc->add_child(input_column);

	prompt_input = memnew(TextEdit);
	prompt_input->set_custom_minimum_size(Size2(0, 100) * EDSCALE);
	prompt_input->set_placeholder(TTR("Describe what you want. The assistant can read the project and run tools to edit scenes. (Ctrl+Enter to send)"));
	prompt_input->connect("gui_input", callable_mp(this, &YeetAIDock::_on_prompt_gui_input));
	input_column->add_child(prompt_input);

	HBoxContainer *button_row = memnew(HBoxContainer);
	button_row->add_theme_constant_override("separation", int(8.0f * EDSCALE));
	button_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input_column->add_child(button_row);

	send_button = memnew(Button);
	send_button->set_text(TTR("Send"));
	send_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	send_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_send_prompt));
	button_row->add_child(send_button);

	clear_button = memnew(Button);
	clear_button->set_theme_type_variation("FlatButton");
	clear_button->set_text(TTR("Clear"));
	clear_button->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
	clear_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_clear_chat));
	button_row->add_child(clear_button);

	request = memnew(HTTPRequest);
	request->set_use_threads(true);
	request->set_timeout(90.0);
	request->connect("request_completed", callable_mp(this, &YeetAIDock::_on_request_completed));
	add_child(request);
}

void YeetAIDock::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_THEME_CHANGED) {
		_apply_dock_theme();
	}
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		if (!intro_message_added) {
			intro_message_added = true;
			_append_message("assistant", TTR("Minimal v0 is ready. I can inspect the project, scenes, selected nodes, and perform basic scene edits."));
		}
		send_button->set_button_icon(get_editor_theme_icon(SNAME("Play")));
		clear_button->set_button_icon(get_editor_theme_icon(SNAME("Clear")));
	}
}

void YeetAIDock::_send_prompt() {
	if (waiting_for_response) {
		return;
	}

	const String prompt = prompt_input->get_text().strip_edges();
	if (prompt.is_empty()) {
		return;
	}

	prompt_input->clear();
	conversation_messages.append(make_message("user", prompt));
	_append_message("user", prompt);
	tool_round_trips = 0;
	turn_context_prompt = _build_runtime_context_prompt();
	_request_model_response();
}

void YeetAIDock::_on_prompt_gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if (key->get_keycode() == Key::ENTER && key->is_ctrl_pressed()) {
			_send_prompt();
			get_viewport()->set_input_as_handled();
		}
	}
}

void YeetAIDock::_clear_chat() {
	if (waiting_for_response) {
		request->cancel_request();
	}

	waiting_for_response = false;
	conversation_messages.clear();
	turn_context_prompt = String();
	chat_log->clear();
	status_label->set_text(TTR("Ready"));
	_append_message("assistant", TTR("Chat cleared. Ask me about the current project."));
}

void YeetAIDock::_append_message(const String &p_role, const String &p_text) {
	const Color font_base = get_theme_color(SNAME("font_color"), EditorStringName(Editor));
	const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color success = get_theme_color(SNAME("success_color"), EditorStringName(Editor));
	const Color dim = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	const Color warning = get_theme_color(SNAME("warning_color"), EditorStringName(Editor));

	String label;
	if (p_role == "user") {
		label = TTR("You");
	} else if (p_role == "assistant") {
		label = TTR("Assistant");
	} else if (p_role == "tool") {
		label = TTR("Tool");
	} else {
		label = p_role.capitalize();
	}

	const Color label_color = (p_role == "user") ? accent : ((p_role == "assistant") ? success : ((p_role == "tool") ? warning : dim));

	chat_log->push_color(accent);
	chat_log->add_text(String::utf8("\xe2\x94\x82")); // Box drawings light vertical U+2502
	chat_log->add_text(" ");
	chat_log->pop();

	chat_log->push_color(label_color);
	chat_log->push_bold();
	chat_log->add_text(label);
	chat_log->pop();
	chat_log->pop();
	chat_log->append_text("\n");

	chat_log->push_indent(1);
	chat_log->push_color(font_base);
	chat_log->append_text(_escape_bbcode(p_text));
	chat_log->pop();
	chat_log->pop();

	chat_log->append_text("\n\n");
	chat_log->scroll_to_line(MAX(0, chat_log->get_line_count() - 1));
}

void YeetAIDock::_append_tool_result(const String &p_tool_name, const Dictionary &p_args, const ToolExecutionResult &p_result) {
	const Color font_base = get_theme_color(SNAME("font_color"), EditorStringName(Editor));
	const Color font_dim = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color success = get_theme_color(SNAME("success_color"), EditorStringName(Editor));
	const Color error = get_theme_color(SNAME("error_color"), EditorStringName(Editor));

	const int base_fs = get_theme_font_size(SNAME("font_size"), EditorStringName(Editor));
	const int caption_fs = MAX(10, int(base_fs * 0.82f));

	const String title = _humanize_tool_name(p_tool_name);

	chat_log->push_color(accent);
	chat_log->add_text(String::utf8("\xe2\x94\x82")); // U+2502
	chat_log->add_text(" ");
	chat_log->pop();

	chat_log->push_color(font_base);
	chat_log->push_bold();
	chat_log->add_text(title);
	chat_log->pop();
	chat_log->pop();

	chat_log->push_color(font_dim);
	chat_log->add_text("  ·  ");
	chat_log->pop();

	chat_log->push_color(p_result.ok ? success : error);
	chat_log->push_bold();
	chat_log->add_text(p_result.ok ? TTR("OK") : TTR("ERR"));
	chat_log->pop();
	chat_log->pop();

	chat_log->append_text("\n");

	chat_log->push_indent(1);

	const Vector<String> paths = _collect_relevant_paths(p_args, p_result.payload);
	if (!paths.is_empty()) {
		chat_log->push_font_size(caption_fs);
		chat_log->push_color(font_dim);
		chat_log->add_text(TTR("Files"));
		chat_log->pop();
		chat_log->pop();
		chat_log->append_text("\n");
		for (int i = 0; i < paths.size(); i++) {
			chat_log->push_color(font_dim);
			chat_log->add_text(paths.size() > 1 ? "• " : "");
			chat_log->pop();
			chat_log->push_color(font_base);
			chat_log->push_mono();
			chat_log->add_text(paths[i]);
			chat_log->pop();
			chat_log->pop();
			chat_log->append_text("\n");
		}
	}

	const String args_compact = _truncate_preview(JSON::stringify(p_args), 220);
	chat_log->push_font_size(caption_fs);
	chat_log->push_color(font_dim);
	chat_log->add_text(TTR("Arguments"));
	chat_log->pop();
	chat_log->pop();
	chat_log->append_text("\n");
	chat_log->push_color(font_dim);
	chat_log->push_mono();
	chat_log->append_text(_escape_bbcode(args_compact));
	chat_log->pop();
	chat_log->pop();
	chat_log->append_text("\n");

	const String preview = _truncate_preview(p_result.display_text, 520);
	chat_log->push_font_size(caption_fs);
	chat_log->push_color(font_dim);
	chat_log->add_text(TTR("Output"));
	chat_log->pop();
	chat_log->pop();
	chat_log->append_text("\n");
	chat_log->push_color(font_base);
	chat_log->push_mono();
	chat_log->append_text(_escape_bbcode(preview));
	chat_log->pop();
	chat_log->pop();

	chat_log->pop();
	chat_log->append_text("\n\n");
	chat_log->scroll_to_line(MAX(0, chat_log->get_line_count() - 1));
}

String YeetAIDock::_humanize_tool_name(const String &p_tool) const {
	const String s = p_tool.strip_edges();
	if (s.is_empty()) {
		return TTR("Tool");
	}
	return s.replace("_", " ").capitalize();
}

Vector<String> YeetAIDock::_collect_relevant_paths(const Dictionary &p_args, const Dictionary &p_payload) const {
	static const char *keys[] = {
		"path", "scene_path", "script_path", "packed_scene_path", "resource_path", "owner_scene_path", "material_path", "main_scene"
	};
	Vector<String> out;
	auto add_from = [&](const Dictionary &d) {
		for (int k = 0; k < int(sizeof(keys) / sizeof(keys[0])); k++) {
			const String key = String(keys[k]);
			if (!d.has(key)) {
				continue;
			}
			const Variant v = d[key];
			if (v.get_type() != Variant::STRING) {
				continue;
			}
			const String path = v;
			if (path.is_empty()) {
				continue;
			}
			if (!contains_string(out, path)) {
				out.push_back(path);
			}
		}
	};
	add_from(p_args);
	add_from(p_payload);
	return out;
}

String YeetAIDock::_truncate_preview(const String &p_text, int p_max_chars) const {
	if (p_text.length() <= p_max_chars) {
		return p_text;
	}
	return p_text.substr(0, p_max_chars) + String::utf8("\xe2\x80\xa6"); // …
}

void YeetAIDock::_set_waiting(bool p_waiting, const String &p_status) {
	waiting_for_response = p_waiting;
	send_button->set_disabled(p_waiting);
	status_label->set_text(p_status);
}

void YeetAIDock::_request_model_response() {
	const String endpoint = _get_editor_setting_string("yeet_ai/chat/completions_url", "https://llm.adityaberry.me/v1/chat/completions");
	const String model = _get_editor_setting_string("yeet_ai/chat/model", "berrymodel");
	const String api_key = _get_editor_setting_string("yeet_ai/chat/api_key", "");
	const int max_tokens = _get_editor_setting_int("yeet_ai/chat/max_tokens", 16384);

	if (endpoint.is_empty()) {
		_append_message("assistant", TTR("The LLM endpoint is empty. Set `yeet_ai/chat/completions_url` or use the default endpoint."));
		return;
	}

	Dictionary payload;
	payload["model"] = model;
	payload["max_tokens"] = max_tokens;

	Array messages;
	messages.append(make_message("system", _build_system_prompt()));
	if (!turn_context_prompt.is_empty()) {
		messages.append(make_message("system", turn_context_prompt));
	}
	for (const Variant &message : conversation_messages) {
		messages.append(message);
	}
	payload["messages"] = messages;

	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	if (!api_key.is_empty()) {
		headers.push_back("Authorization: Bearer " + api_key);
	}

	const Error err = request->request(endpoint, headers, HTTPClient::METHOD_POST, JSON::stringify(payload));
	if (err != OK) {
		_append_message("assistant", vformat(TTR("Failed to start request: %s"), itos(err)));
		_set_waiting(false, TTR("Request failed"));
		return;
	}

	_set_waiting(true, TTR("Thinking..."));
}

void YeetAIDock::_on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	(void)p_headers;

	if (p_result != HTTPRequest::RESULT_SUCCESS) {
		_set_waiting(false, TTR("Ready"));
		_append_message("assistant", vformat(TTR("Network error talking to the LLM endpoint: %d"), p_result));
		return;
	}

	if (p_response_code < 200 || p_response_code >= 300) {
		_set_waiting(false, TTR("Ready"));
		const String body_text = String::utf8(reinterpret_cast<const char *>(p_body.ptr()), p_body.size());
		_append_message("assistant", vformat(TTR("LLM request failed with HTTP %d:\n%s"), p_response_code, body_text));
		return;
	}

	const String body_text = String::utf8(reinterpret_cast<const char *>(p_body.ptr()), p_body.size());
	Dictionary response_json;
	if (!_parse_json_dictionary_quiet(body_text, response_json)) {
		_set_waiting(false, TTR("Ready"));
		_append_message("assistant", TTR("The LLM response was not valid JSON."));
		return;
	}
	const String content = _extract_message_content(response_json);
	if (content.is_empty()) {
		_set_waiting(false, TTR("Ready"));
		_append_message("assistant", TTR("The LLM returned no message content."));
		return;
	}

	// Keep the waiting state active while we parse and potentially re-enter
	// the tool loop. _handle_model_response will call _set_waiting(false) only
	// when the loop terminates, or _request_model_response will update the
	// status to "Thinking..." for the next round-trip.
	_handle_model_response(content);
}

void YeetAIDock::_handle_model_response(const String &p_content) {
	Dictionary envelope = _extract_response_envelope(p_content);
	const String type = envelope.get("type", "");

	if (type == "tool_call") {
		const int max_round_trips = _get_editor_setting_int("yeet_ai/chat/max_tool_round_trips", 100);
		if (tool_round_trips >= max_round_trips) {
			_set_waiting(false, TTR("Ready"));
			_append_message("assistant", TTR("Stopping because the tool-call loop hit the configured limit."));
			return;
		}

		const String tool_name = envelope.get("tool", "");
		const Dictionary args = envelope.get("arguments", Dictionary());
		_set_waiting(true, vformat(TTR("Running: %s"), _humanize_tool_name(tool_name)));
		ToolExecutionResult result = _execute_tool(tool_name, args);
		conversation_messages.append(make_message("assistant", JSON::stringify(envelope)));
		_append_tool_result(tool_name, args, result);

		Dictionary tool_payload = result.payload;
		tool_payload["ok"] = result.ok && !result.payload.has("error");
		conversation_messages.append(make_message("user", vformat("Tool `%s` finished with the following JSON result:\n%s\n\nIf you need more context, return another tool_call JSON object. Otherwise return a final JSON object.", tool_name, JSON::stringify(tool_payload, "\t", false, true))));

		tool_round_trips++;
		_request_model_response();
		return;
	}

	if (type == "final") {
		_set_waiting(false, TTR("Ready"));
		const String message = envelope.get("message", "");
		conversation_messages.append(make_message("assistant", message));
		_append_message("assistant", message.is_empty() ? p_content : message);
		return;
	}

	_set_waiting(false, TTR("Ready"));
	conversation_messages.append(make_message("assistant", p_content));
	_append_message("assistant", p_content);
}

String YeetAIDock::_build_runtime_context_prompt() const {
	Dictionary context;
	context["project_settings"] = _tool_get_project_settings(Dictionary());
	context["open_scenes"] = _tool_get_open_scenes();
	context["current_scene"] = _tool_get_current_scene();
	context["selected_nodes"] = _tool_get_selected_nodes();

	Dictionary input_args;
	input_args["include_events"] = false;
	input_args["max_actions"] = 24;
	context["input_actions"] = _tool_get_input_actions(input_args);

	Dictionary tree_args;
	tree_args["root"] = "res://";
	tree_args["max_depth"] = 2;
	Array include_extensions;
	include_extensions.push_back(".godot");
	include_extensions.push_back(".tscn");
	include_extensions.push_back(".gd");
	include_extensions.push_back(".tres");
	tree_args["include_extensions"] = include_extensions;
	Dictionary project_tree = _tool_get_project_tree(tree_args);
	if (project_tree.has("entries")) {
		Array entries = project_tree["entries"];
		if (entries.size() > 48) {
			Array trimmed_entries;
			for (int i = 0; i < 48; i++) {
				trimmed_entries.push_back(entries[i]);
			}
			project_tree["entries"] = trimmed_entries;
			project_tree["truncated"] = true;
		}
	}
	context["project_tree"] = project_tree;

	const Dictionary current_scene = context["current_scene"];
	if (!current_scene.has("error")) {
		Dictionary scene_tree_args;
		scene_tree_args["max_depth"] = 2;
		Array include_properties;
		include_properties.push_back("script");
		include_properties.push_back("position");
		scene_tree_args["include_properties"] = include_properties;
		context["current_scene_tree"] = _tool_get_scene_tree(scene_tree_args);
	}

	return String() +
			"Live editor and project snapshot follows as JSON.\n"
			"Use this context before deciding whether more inspection tools are needed.\n"
			"Prefer reusing current scenes, scripts, inputs, and nodes when they already exist.\n"
			+ JSON::stringify(context, "\t", false, true);
}

YeetAIDock::ToolExecutionResult YeetAIDock::_execute_tool(const String &p_tool_name, const Dictionary &p_args) const {
	ToolExecutionResult result;

	if (p_tool_name == "get_project_tree") {
		result.ok = true;
		result.payload = _tool_get_project_tree(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "read_project_file") {
		result.ok = true;
		result.payload = _tool_read_project_file(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_open_scenes") {
		result.ok = true;
		result.payload = _tool_get_open_scenes();
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_current_scene") {
		result.ok = true;
		result.payload = _tool_get_current_scene();
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_selected_nodes") {
		result.ok = true;
		result.payload = _tool_get_selected_nodes();
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_project_settings") {
		result.ok = true;
		result.payload = _tool_get_project_settings(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_input_actions") {
		result.ok = true;
		result.payload = _tool_get_input_actions(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "find_project_files") {
		result.ok = true;
		result.payload = _tool_find_project_files(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_scene_tree") {
		result.ok = true;
		result.payload = _tool_get_scene_tree(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_node_details") {
		result.ok = true;
		result.payload = _tool_get_node_details(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_node_api") {
		result.ok = true;
		result.payload = _tool_get_node_api(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "batch_tool_calls" || is_batch_tool_name_alias(p_tool_name)) {
		result.ok = true;
		result.payload = _tool_batch_tool_calls(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "open_scene") {
		result.ok = true;
		result.payload = _tool_open_scene(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "save_current_scene") {
		result.ok = true;
		result.payload = _tool_save_current_scene();
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "create_scene_file") {
		result.ok = true;
		result.payload = _tool_create_scene_file(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "create_gdscript_file") {
		result.ok = true;
		result.payload = _tool_create_gdscript_file(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "update_gdscript_file") {
		result.ok = true;
		result.payload = _tool_update_gdscript_file(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "attach_script") {
		result.ok = true;
		result.payload = _tool_attach_script(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "add_node") {
		result.ok = true;
		result.payload = _tool_add_node(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "instantiate_scene") {
		result.ok = true;
		result.payload = _tool_instantiate_scene(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "add_primitive_mesh") {
		result.ok = true;
		result.payload = _tool_add_primitive_mesh(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "add_collision_shape") {
		result.ok = true;
		result.payload = _tool_add_collision_shape(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "create_standard_material") {
		result.ok = true;
		result.payload = _tool_create_standard_material(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "assign_resource_to_property") {
		result.ok = true;
		result.payload = _tool_assign_resource_to_property(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "connect_signal") {
		result.ok = true;
		result.payload = _tool_connect_signal(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "create_input_action") {
		result.ok = true;
		result.payload = _tool_create_input_action(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "set_main_scene") {
		result.ok = true;
		result.payload = _tool_set_main_scene(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "play_current_scene") {
		result.ok = true;
		result.payload = _tool_play_current_scene();
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "play_main_scene") {
		result.ok = true;
		result.payload = _tool_play_main_scene();
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "stop_playing_scene") {
		result.ok = true;
		result.payload = _tool_stop_playing_scene();
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "remove_node") {
		result.ok = true;
		result.payload = _tool_remove_node(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "set_node_property") {
		result.ok = true;
		result.payload = _tool_set_node_property(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	result.payload["error"] = "Unknown tool";
	result.display_text = vformat("Unknown tool: %s", p_tool_name);
	return result;
}

String YeetAIDock::_get_editor_setting_string(const String &p_setting, const String &p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	return settings->get_setting(p_setting);
}

int YeetAIDock::_get_editor_setting_int(const String &p_setting, int p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	return int(settings->get_setting(p_setting));
}

Node *YeetAIDock::_resolve_scene_root(const String &p_scene_path, String &r_error) const {
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		r_error = "EditorInterface is unavailable.";
		return nullptr;
	}

	if (p_scene_path.is_empty()) {
		Node *scene_root = editor->get_edited_scene_root();
		if (scene_root == nullptr) {
			r_error = "There is no current edited scene.";
		}
		return scene_root;
	}

	TypedArray<Node> open_roots = editor->get_open_scene_roots();
	for (int i = 0; i < open_roots.size(); i++) {
		Node *open_root = Object::cast_to<Node>(open_roots[i]);
		if (open_root != nullptr && open_root->get_scene_file_path() == p_scene_path) {
			return open_root;
		}
	}

	if (!FileAccess::exists(p_scene_path)) {
		r_error = "Scene path does not exist.";
		return nullptr;
	}

	editor->open_scene_from_path(p_scene_path);

	// Re-scan all open roots. The newly loaded scene may not be the active
	// editor tab (e.g. when another scene was previously focused), so
	// get_edited_scene_root() is not reliable here.
	open_roots = editor->get_open_scene_roots();
	for (int i = 0; i < open_roots.size(); i++) {
		Node *open_root = Object::cast_to<Node>(open_roots[i]);
		if (open_root != nullptr && open_root->get_scene_file_path() == p_scene_path) {
			return open_root;
		}
	}

	r_error = "Failed to open the requested scene.";
	return nullptr;
}

Node *YeetAIDock::_resolve_node_target(Node *p_scene_root, const String &p_node_path, String &r_error) const {
	if (p_scene_root == nullptr) {
		r_error = "Scene root is unavailable.";
		return nullptr;
	}

	if (p_node_path.is_empty() || p_node_path == "." || p_node_path == "/") {
		return p_scene_root;
	}

	if (p_node_path == String(p_scene_root->get_path()) || p_node_path == String(p_scene_root->get_name())) {
		return p_scene_root;
	}

	Node *node = p_scene_root->get_node_or_null(NodePath(p_node_path));
	if (node != nullptr) {
		return node;
	}

	const String root_name_path = String(p_scene_root->get_name()) + "/" + p_node_path;
	node = p_scene_root->get_node_or_null(NodePath(root_name_path));
	if (node != nullptr) {
		return node;
	}

	r_error = "Node path was not found in the target scene.";
	return nullptr;
}

void YeetAIDock::_set_owner_recursive(Node *p_node, Node *p_owner) const {
	if (p_node == nullptr || p_owner == nullptr) {
		return;
	}

	p_node->set_owner(p_owner);
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *child = p_node->get_child(i);
		if (child != nullptr && !child->is_internal()) {
			_set_owner_recursive(child, p_owner);
		}
	}
}

Dictionary YeetAIDock::_tool_get_project_tree(const Dictionary &p_args) const {
	String root = p_args.get("root", "res://");
	if (!root.begins_with("res://")) {
		root = "res://";
	}

	int max_depth = CLAMP(int(p_args.get("max_depth", 3)), 0, 8);
	Vector<String> include_extensions = _variant_array_to_string_vector(p_args.get("include_extensions", Array()));

	Array entries;
	int entry_count = 0;
	_collect_project_entries(root, 0, max_depth, include_extensions, entries, entry_count);

	Dictionary result;
	result["root"] = root;
	result["entry_count"] = entries.size();
	result["entries"] = entries;
	return result;
}

Dictionary YeetAIDock::_tool_read_project_file(const Dictionary &p_args) const {
	Dictionary result;
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		result["error"] = "Path must start with res://";
		return result;
	}

	if (!_is_allowed_text_file(path)) {
		result["error"] = "Only safe text project files are allowed in v0.";
		return result;
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(path, &err);
	if (err != OK) {
		result["error"] = vformat("Failed to read file: %d", err);
		return result;
	}

	if (content.length() > MAX_FILE_READ_BYTES) {
		content = content.substr(0, MAX_FILE_READ_BYTES) + "\n...[truncated]";
	}

	result["path"] = path;
	result["content"] = content;
	result["resource_type"] = ResourceLoader::get_resource_type(path);
	return result;
}

Dictionary YeetAIDock::_tool_get_open_scenes() const {
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	result["open_scenes"] = editor->get_open_scenes();
	result["unsaved_scenes"] = editor->get_unsaved_scenes();
	return result;
}

Dictionary YeetAIDock::_tool_get_current_scene() const {
	Dictionary result;
	String error;
	Node *scene_root = _resolve_scene_root(String(), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	result["scene_path"] = scene_root->get_scene_file_path();
	result["root_name"] = scene_root->get_name();
	result["root_type"] = scene_root->get_class();
	result["root_node_path"] = String(scene_root->get_path());
	result["child_count"] = scene_root->get_child_count();
	return result;
}

Dictionary YeetAIDock::_tool_get_selected_nodes() const {
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr || editor->get_selection() == nullptr) {
		result["error"] = "Editor selection is unavailable.";
		return result;
	}

	Array selected_nodes;
	TypedArray<Node> selection = editor->get_selection()->get_selected_nodes();
	for (int i = 0; i < selection.size(); i++) {
		Node *node = Object::cast_to<Node>(selection[i]);
		if (node == nullptr) {
			continue;
		}

		Dictionary entry;
		entry["name"] = node->get_name();
		entry["type"] = node->get_class();
		entry["node_path"] = String(node->get_path());
		entry["owner_scene_path"] = node->get_owner() != nullptr ? node->get_owner()->get_scene_file_path() : String();
		selected_nodes.push_back(entry);
	}

	result["selected_nodes"] = selected_nodes;
	result["count"] = selected_nodes.size();
	return result;
}

Dictionary YeetAIDock::_tool_get_project_settings(const Dictionary &p_args) const {
	Dictionary result;
	Array keys = p_args.get("keys", Array());
	if (keys.is_empty()) {
		keys.push_back("application/config/name");
		keys.push_back("application/run/main_scene");
		keys.push_back("display/window/size/viewport_width");
		keys.push_back("display/window/size/viewport_height");
		keys.push_back("rendering/renderer/rendering_method");
	}

	Dictionary values;
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	for (int i = 0; i < keys.size(); i++) {
		const String key = String(keys[i]).strip_edges();
		if (key.is_empty()) {
			continue;
		}
		if (project_settings->has_setting(key)) {
			values[key] = _json_safe_variant(project_settings->get_setting_with_override(key));
		}
	}

	result["values"] = values;
	result["project_data_dir"] = project_settings->get_project_data_path();
	result["resource_path"] = project_settings->get_resource_path();
	return result;
}

Dictionary YeetAIDock::_tool_get_input_actions(const Dictionary &p_args) const {
	Dictionary result;
	InputMap *input_map = InputMap::get_singleton();
	if (input_map == nullptr) {
		result["error"] = "InputMap is unavailable.";
		return result;
	}

	const bool include_events = bool(p_args.get("include_events", true));
	const int max_actions = CLAMP(int(p_args.get("max_actions", 64)), 1, 256);
	const String query = String(p_args.get("query", "")).to_lower();

	Array actions_array;
	const TypedArray<StringName> actions = input_map->get_actions();
	for (int i = 0; i < actions.size() && actions_array.size() < max_actions; i++) {
		const String action_name = String(actions[i]);
		if (!query.is_empty() && !action_name.to_lower().contains(query)) {
			continue;
		}

		Dictionary action_entry;
		action_entry["name"] = action_name;
		action_entry["deadzone"] = input_map->action_get_deadzone(action_name);

		if (include_events) {
			Array events_array;
			const List<Ref<InputEvent>> *events = input_map->action_get_events(action_name);
			if (events != nullptr) {
				for (const Ref<InputEvent> &event : *events) {
					if (event.is_null()) {
						continue;
					}
					Dictionary event_entry;
					event_entry["class"] = event->get_class();
					event_entry["text"] = event->as_text();
					events_array.push_back(event_entry);
				}
			}
			action_entry["events"] = events_array;
		}

		actions_array.push_back(action_entry);
	}

	result["actions"] = actions_array;
	result["count"] = actions_array.size();
	return result;
}

Dictionary YeetAIDock::_tool_find_project_files(const Dictionary &p_args) const {
	Dictionary result;
	String root = p_args.get("root", "res://");
	if (!root.begins_with("res://")) {
		root = "res://";
	}

	const String query = String(p_args.get("query", "")).to_lower().strip_edges();
	if (query.is_empty()) {
		result["error"] = "query is required.";
		return result;
	}

	const int max_depth = CLAMP(int(p_args.get("max_depth", 6)), 0, 12);
	const int max_results = CLAMP(int(p_args.get("max_results", 40)), 1, 200);
	const Vector<String> include_extensions = _variant_array_to_string_vector(p_args.get("include_extensions", Array()));

	Array entries;
	int entry_count = 0;
	_find_project_entries(root, query, 0, max_depth, include_extensions, entries, entry_count, max_results);

	result["root"] = root;
	result["query"] = query;
	result["count"] = entries.size();
	result["entries"] = entries;
	return result;
}

Dictionary YeetAIDock::_tool_get_scene_tree(const Dictionary &p_args) const {
	Dictionary result;
	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	const int max_depth = CLAMP(int(p_args.get("max_depth", 4)), 0, 8);
	const Vector<String> include_properties = _variant_array_to_string_vector(p_args.get("include_properties", Array()));
	int node_count = 0;

	result["scene_path"] = scene_root->get_scene_file_path();
	result["tree"] = _serialize_node(scene_root, 0, max_depth, include_properties, node_count);
	result["node_count"] = node_count;
	result["truncated"] = node_count >= MAX_SCENE_TREE_NODES;
	return result;
}

Dictionary YeetAIDock::_tool_get_node_details(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty()) {
		result["error"] = "node_path is required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *node = _resolve_node_target(scene_root, node_path, error);
	if (node == nullptr) {
		result["error"] = error;
		return result;
	}

	const Vector<String> include_properties = _variant_array_to_string_vector(p_args.get("include_properties", Array()));
	result["scene_path"] = scene_root->get_scene_file_path();
	result["name"] = node->get_name();
	result["type"] = node->get_class();
	result["node_path"] = String(node->get_path());
	result["parent_path"] = node->get_parent() != nullptr ? String(node->get_parent()->get_path()) : String();
	result["child_count"] = node->get_child_count();
	result["properties"] = _serialize_node_properties(node, include_properties);
	return result;
}

Dictionary YeetAIDock::_tool_get_node_api(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty()) {
		result["error"] = "node_path is required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *node = _resolve_node_target(scene_root, node_path, error);
	if (node == nullptr) {
		result["error"] = error;
		return result;
	}

	const bool include_private = bool(p_args.get("include_private", false));
	const int max_methods = CLAMP(int(p_args.get("max_methods", 80)), 1, 400);
	const int max_signals = CLAMP(int(p_args.get("max_signals", 60)), 1, 200);
	const int max_properties = CLAMP(int(p_args.get("max_properties", 80)), 1, 400);

	Array methods_array;
	List<MethodInfo> methods;
	node->get_method_list(&methods);
	for (const MethodInfo &method_info : methods) {
		const String method_name = String(method_info.name);
		if (!include_private && method_name.begins_with("_")) {
			continue;
		}
		Dictionary entry;
		entry["name"] = method_name;
		entry["arg_count"] = method_info.arguments.size();
		methods_array.push_back(entry);
		if (methods_array.size() >= max_methods) {
			break;
		}
	}

	Array signals_array;
	List<MethodInfo> signals;
	node->get_signal_list(&signals);
	for (const MethodInfo &signal_info : signals) {
		const String signal_name = String(signal_info.name);
		if (!include_private && signal_name.begins_with("_")) {
			continue;
		}
		Dictionary entry;
		entry["name"] = signal_name;
		entry["arg_count"] = signal_info.arguments.size();
		signals_array.push_back(entry);
		if (signals_array.size() >= max_signals) {
			break;
		}
	}

	Array properties_array;
	List<PropertyInfo> properties;
	node->get_property_list(&properties);
	for (const PropertyInfo &property_info : properties) {
		const String property_name = String(property_info.name);
		if (!include_private && property_name.begins_with("_")) {
			continue;
		}
		Dictionary entry;
		entry["name"] = property_name;
		entry["type"] = Variant::get_type_name(property_info.type);
		properties_array.push_back(entry);
		if (properties_array.size() >= max_properties) {
			break;
		}
	}

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(node->get_path());
	result["type"] = node->get_class();
	result["methods"] = methods_array;
	result["signals"] = signals_array;
	result["properties"] = properties_array;
	if (_node_has_property(node, "script")) {
		const Variant script_variant = node->get("script");
		if (script_variant.get_type() == Variant::OBJECT) {
			Object *script_object = script_variant;
			if (const Resource *script_resource = Object::cast_to<Resource>(script_object)) {
				result["script_path"] = script_resource->get_path();
			}
		}
	}
	return result;
}

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
	if (calls.is_empty()) {
		result["error"] = "calls is required and must contain at least one tool call.";
		return result;
	}

	Array results;
	int executed_count = 0;
	bool had_failure = false;
	for (int i = 0; i < calls.size(); i++) {
		const Variant call_var = calls[i];
		Dictionary call;
		if (call_var.get_type() == Variant::DICTIONARY) {
			call = call_var;
		} else if (call_var.get_type() == Variant::STRING) {
			if (!_parse_json_dictionary_quiet(String(call_var).strip_edges(), call)) {
				result["error"] = "Each batch call must be a JSON object (or a JSON object encoded as a string).";
				result["failed_index"] = i;
				return result;
			}
		} else {
			result["error"] = "Each batch call must be a dictionary.";
			result["failed_index"] = i;
			return result;
		}
		String tool_name = String(call.get("tool", "")).strip_edges();
		if (tool_name.is_empty()) {
			tool_name = String(call.get("name", "")).strip_edges();
		}
		if (tool_name.is_empty()) {
			const Dictionary fn = call.get("function", Dictionary());
			tool_name = String(fn.get("name", "")).strip_edges();
		}
		if (tool_name.is_empty()) {
			result["error"] = "Each batch call needs a tool name (`tool`, `name`, or `function.name`).";
			result["failed_index"] = i;
			return result;
		}
		if (tool_name == "batch_tool_calls") {
			result["error"] = "batch_tool_calls cannot invoke itself recursively.";
			result["failed_index"] = i;
			return result;
		}

		Dictionary call_args;
		if (call.has("arguments")) {
			const Variant av = call["arguments"];
			if (av.get_type() == Variant::STRING) {
				Dictionary parsed;
				if (_parse_json_dictionary_quiet(String(av).strip_edges(), parsed)) {
					call_args = parsed;
				}
			} else if (av.get_type() == Variant::DICTIONARY) {
				call_args = av;
			}
		} else if (call.has("parameters") && call["parameters"].get_type() == Variant::DICTIONARY) {
			call_args = call["parameters"];
		}
		Array shared_keys = shared_arguments.keys();
		for (int key_index = 0; key_index < shared_keys.size(); key_index++) {
			const Variant shared_key_variant = shared_keys[key_index];
			const String shared_key = String(shared_key_variant);
			if (!call_args.has(shared_key)) {
				call_args[shared_key] = shared_arguments[shared_key_variant];
			}
		}

		ToolExecutionResult call_result = _execute_tool(tool_name, call_args);
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
		"attach_script", nullptr
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
		EditorInterface *editor = EditorInterface::get_singleton();
		if (editor != nullptr) {
			Array saved_paths;
			for (const String &scene_path : scenes_to_save) {
				editor->save_scene_as(scene_path, false);
				saved_paths.push_back(scene_path);
			}
			result["auto_saved_scenes"] = saved_paths;
		}
	}

	return result;
}

Dictionary YeetAIDock::_tool_open_scene(const Dictionary &p_args) const {
	Dictionary result;
	const String scene_path = p_args.get("scene_path", "");
	if (!scene_path.begins_with("res://")) {
		result["error"] = "scene_path must start with res://";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(scene_path, error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	result["scene_path"] = scene_root->get_scene_file_path();
	result["root_name"] = scene_root->get_name();
	result["root_type"] = scene_root->get_class();
	return result;
}

Dictionary YeetAIDock::_tool_save_current_scene() const {
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	Node *scene_root = editor->get_edited_scene_root();
	if (scene_root == nullptr) {
		result["error"] = "There is no current scene to save.";
		return result;
	}

	const Error err = editor->save_scene();
	result["scene_path"] = scene_root->get_scene_file_path();
	result["error_code"] = err;
	if (err != OK) {
		result["error"] = vformat("save_scene failed with error %d", err);
	}
	return result;
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
		result["error"] = vformat("Failed to pack scene: %d", pack_error);
		return result;
	}

	const Error save_error = ResourceSaver::save(packed_scene, scene_path, ResourceSaver::FLAG_CHANGE_PATH);
	memdelete(root);
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

	result["script_path"] = script_path;
	result["bytes_written"] = contents.to_utf8_buffer().size();
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

	result["script_path"] = script_path;
	result["operation"] = operation;
	result["created"] = !exists;
	result["bytes_written"] = final_contents.to_utf8_buffer().size();
	return result;
}

Dictionary YeetAIDock::_tool_attach_script(const Dictionary &p_args) const {
	Dictionary result;
	const String script_path = p_args.get("script_path", "");
	const String node_path = p_args.get("node_path", "");
	if (script_path.is_empty() || node_path.is_empty()) {
		result["error"] = "script_path and node_path are required.";
		return result;
	}

	Dictionary resource_args;
	resource_args["scene_path"] = p_args.get("scene_path", "");
	resource_args["node_path"] = node_path;
	resource_args["property"] = "script";
	resource_args["resource_path"] = script_path;
	return _tool_assign_resource_to_property(resource_args);
}

Dictionary YeetAIDock::_tool_add_node(const Dictionary &p_args) const {
	Dictionary result;
	const String node_type = p_args.get("node_type", "");
	const String node_name = p_args.get("node_name", "");
	const String parent_path = p_args.get("parent_path", ".");
	if (node_type.is_empty() || node_name.is_empty()) {
		result["error"] = "node_type and node_name are required.";
		return result;
	}

	if (!ClassDB::class_exists(node_type) || !ClassDB::can_instantiate(node_type) || ClassDB::is_virtual(node_type)) {
		result["error"] = "node_type is not instantiable.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *parent_node = _resolve_node_target(scene_root, parent_path, error);
	if (parent_node == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *new_node = Object::cast_to<Node>(ClassDB::instantiate(node_type));
	if (new_node == nullptr) {
		result["error"] = "Failed to instantiate node_type as a Node.";
		return result;
	}

	new_node->set_name(node_name);
	parent_node->add_child(new_node, true);
	new_node->set_name(parent_node->validate_child_name(new_node));
	new_node->set_owner(scene_root);

	EditorInterface::get_singleton()->mark_scene_as_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(new_node->get_path());
	result["node_name"] = new_node->get_name();
	result["node_type"] = new_node->get_class();
	return result;
}

Dictionary YeetAIDock::_tool_instantiate_scene(const Dictionary &p_args) const {
	Dictionary result;
	const String packed_scene_path = p_args.get("packed_scene_path", "");
	const String parent_path = p_args.get("parent_path", ".");
	if (!packed_scene_path.begins_with("res://")) {
		result["error"] = "packed_scene_path must start with res://";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *parent_node = _resolve_node_target(scene_root, parent_path, error);
	if (parent_node == nullptr) {
		result["error"] = error;
		return result;
	}

	Error load_error = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(packed_scene_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_REPLACE, &load_error);
	if (packed_scene.is_null() || load_error != OK) {
		result["error"] = vformat("Failed to load PackedScene: %d", load_error);
		return result;
	}

	Node *instance = packed_scene->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	if (instance == nullptr) {
		result["error"] = "Failed to instantiate PackedScene.";
		return result;
	}

	const String node_name = p_args.get("node_name", "");
	if (!node_name.is_empty()) {
		instance->set_name(node_name);
	}

	parent_node->add_child(instance, true);
	instance->set_name(parent_node->validate_child_name(instance));
	_set_owner_recursive(instance, scene_root);
	EditorInterface::get_singleton()->mark_scene_as_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["packed_scene_path"] = packed_scene_path;
	result["node_path"] = String(instance->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_add_primitive_mesh(const Dictionary &p_args) const {
	Dictionary result;
	const String node_name = p_args.get("node_name", "");
	const String mesh_type = String(p_args.get("mesh_type", "box")).to_lower();
	const String parent_path = p_args.get("parent_path", ".");
	if (node_name.is_empty()) {
		result["error"] = "node_name is required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *parent_node = _resolve_node_target(scene_root, parent_path, error);
	if (parent_node == nullptr) {
		result["error"] = error;
		return result;
	}

	Dictionary parameters = p_args.get("parameters", Dictionary());
	Ref<Mesh> mesh;
	if (mesh_type == "box") {
		Ref<BoxMesh> box;
		box.instantiate();
		if (parameters.has("size")) {
			box->set_size((Vector3)_variant_from_json(parameters["size"], Variant::VECTOR3));
		}
		if (parameters.has("subdivide_width")) {
			box->set_subdivide_width(int(parameters["subdivide_width"]));
		}
		if (parameters.has("subdivide_height")) {
			box->set_subdivide_height(int(parameters["subdivide_height"]));
		}
		if (parameters.has("subdivide_depth")) {
			box->set_subdivide_depth(int(parameters["subdivide_depth"]));
		}
		mesh = box;
	} else if (mesh_type == "sphere") {
		Ref<SphereMesh> sphere;
		sphere.instantiate();
		if (parameters.has("radius")) {
			sphere->set_radius((double)parameters["radius"]);
		}
		if (parameters.has("height")) {
			sphere->set_height((double)parameters["height"]);
		}
		if (parameters.has("radial_segments")) {
			sphere->set_radial_segments(int(parameters["radial_segments"]));
		}
		if (parameters.has("rings")) {
			sphere->set_rings(int(parameters["rings"]));
		}
		if (parameters.has("is_hemisphere")) {
			sphere->set_is_hemisphere((bool)parameters["is_hemisphere"]);
		}
		mesh = sphere;
	} else if (mesh_type == "capsule") {
		Ref<CapsuleMesh> capsule;
		capsule.instantiate();
		if (parameters.has("radius")) {
			capsule->set_radius((double)parameters["radius"]);
		}
		if (parameters.has("height")) {
			capsule->set_height((double)parameters["height"]);
		}
		if (parameters.has("radial_segments")) {
			capsule->set_radial_segments(int(parameters["radial_segments"]));
		}
		if (parameters.has("rings")) {
			capsule->set_rings(int(parameters["rings"]));
		}
		mesh = capsule;
	} else if (mesh_type == "cylinder") {
		Ref<CylinderMesh> cylinder;
		cylinder.instantiate();
		if (parameters.has("top_radius")) {
			cylinder->set_top_radius((double)parameters["top_radius"]);
		}
		if (parameters.has("bottom_radius")) {
			cylinder->set_bottom_radius((double)parameters["bottom_radius"]);
		}
		if (parameters.has("height")) {
			cylinder->set_height((double)parameters["height"]);
		}
		if (parameters.has("radial_segments")) {
			cylinder->set_radial_segments(int(parameters["radial_segments"]));
		}
		if (parameters.has("rings")) {
			cylinder->set_rings(int(parameters["rings"]));
		}
		if (parameters.has("cap_top")) {
			cylinder->set_cap_top((bool)parameters["cap_top"]);
		}
		if (parameters.has("cap_bottom")) {
			cylinder->set_cap_bottom((bool)parameters["cap_bottom"]);
		}
		mesh = cylinder;
	} else if (mesh_type == "plane") {
		Ref<PlaneMesh> plane;
		plane.instantiate();
		if (parameters.has("size")) {
			plane->set_size((Vector2)_variant_from_json(parameters["size"], Variant::VECTOR2));
		}
		if (parameters.has("subdivide_width")) {
			plane->set_subdivide_width(int(parameters["subdivide_width"]));
		}
		if (parameters.has("subdivide_depth")) {
			plane->set_subdivide_depth(int(parameters["subdivide_depth"]));
		}
		if (parameters.has("center_offset")) {
			plane->set_center_offset((Vector3)_variant_from_json(parameters["center_offset"], Variant::VECTOR3));
		}
		if (parameters.has("orientation")) {
			const String orientation = String(parameters["orientation"]).to_lower();
			if (orientation == "x") {
				plane->set_orientation(PlaneMesh::FACE_X);
			} else if (orientation == "z") {
				plane->set_orientation(PlaneMesh::FACE_Z);
			} else {
				plane->set_orientation(PlaneMesh::FACE_Y);
			}
		}
		mesh = plane;
	} else {
		result["error"] = "mesh_type must be one of: box, sphere, capsule, cylinder, plane.";
		return result;
	}

	MeshInstance3D *mesh_node = memnew(MeshInstance3D);
	mesh_node->set_name(node_name);
	mesh_node->set_mesh(mesh);
	parent_node->add_child(mesh_node, true);
	mesh_node->set_name(parent_node->validate_child_name(mesh_node));
	mesh_node->set_owner(scene_root);
	EditorInterface::get_singleton()->mark_scene_as_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(mesh_node->get_path());
	result["mesh_type"] = mesh_type;
	return result;
}

Dictionary YeetAIDock::_tool_add_collision_shape(const Dictionary &p_args) const {
	Dictionary result;
	const String node_name = p_args.get("node_name", "");
	const String shape_type = String(p_args.get("shape_type", "box")).to_lower();
	const String parent_path = p_args.get("parent_path", ".");
	if (node_name.is_empty()) {
		result["error"] = "node_name is required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *parent_node = _resolve_node_target(scene_root, parent_path, error);
	if (parent_node == nullptr) {
		result["error"] = error;
		return result;
	}

	Dictionary parameters = p_args.get("parameters", Dictionary());
	Ref<Shape3D> shape;
	if (shape_type == "box") {
		Ref<BoxShape3D> box;
		box.instantiate();
		if (parameters.has("size")) {
			box->set_size((Vector3)_variant_from_json(parameters["size"], Variant::VECTOR3));
		}
		shape = box;
	} else if (shape_type == "sphere") {
		Ref<SphereShape3D> sphere;
		sphere.instantiate();
		if (parameters.has("radius")) {
			sphere->set_radius((double)parameters["radius"]);
		}
		shape = sphere;
	} else if (shape_type == "capsule") {
		Ref<CapsuleShape3D> capsule;
		capsule.instantiate();
		if (parameters.has("radius")) {
			capsule->set_radius((double)parameters["radius"]);
		}
		if (parameters.has("height")) {
			capsule->set_height((double)parameters["height"]);
		}
		shape = capsule;
	} else if (shape_type == "cylinder") {
		Ref<CylinderShape3D> cylinder;
		cylinder.instantiate();
		if (parameters.has("radius")) {
			cylinder->set_radius((double)parameters["radius"]);
		}
		if (parameters.has("height")) {
			cylinder->set_height((double)parameters["height"]);
		}
		shape = cylinder;
	} else {
		result["error"] = "shape_type must be one of: box, sphere, capsule, cylinder.";
		return result;
	}

	CollisionShape3D *collision_node = memnew(CollisionShape3D);
	collision_node->set_name(node_name);
	collision_node->set_shape(shape);
	if (p_args.has("disabled")) {
		collision_node->set_disabled(bool(p_args["disabled"]));
	}
	parent_node->add_child(collision_node, true);
	collision_node->set_name(parent_node->validate_child_name(collision_node));
	collision_node->set_owner(scene_root);
	EditorInterface::get_singleton()->mark_scene_as_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(collision_node->get_path());
	result["shape_type"] = shape_type;
	return result;
}

Dictionary YeetAIDock::_tool_create_standard_material(const Dictionary &p_args) const {
	Dictionary result;
	const String material_path = p_args.get("material_path", "");
	if (!material_path.begins_with("res://") || material_path.get_extension().is_empty()) {
		result["error"] = "material_path must be a res:// path with a resource extension.";
		return result;
	}
	if (FileAccess::exists(material_path) && !bool(p_args.get("overwrite", false))) {
		result["error"] = "material_path already exists. Pass overwrite=true to replace it.";
		return result;
	}

	Ref<StandardMaterial3D> std_material;
	std_material.instantiate();
	if (p_args.has("albedo")) {
		std_material->set_albedo((Color)_variant_from_json(p_args["albedo"], Variant::COLOR));
	}
	if (p_args.has("metallic")) {
		std_material->set_metallic((double)p_args["metallic"]);
	}
	if (p_args.has("roughness")) {
		std_material->set_roughness((double)p_args["roughness"]);
	}
	if (p_args.has("transparency")) {
		bool transparency_ok = false;
		const BaseMaterial3D::Transparency transparency = parse_transparency_mode(String(p_args["transparency"]), transparency_ok);
		if (!transparency_ok) {
			result["error"] = "transparency must be one of: disabled, alpha, alpha_scissor, alpha_hash, alpha_depth_pre_pass.";
			return result;
		}
		std_material->set_transparency(transparency);
	}

	const Error dir_error = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(material_path.get_base_dir()));
	if (dir_error != OK) {
		result["error"] = vformat("Failed to create material directory: %d", dir_error);
		return result;
	}

	const Error save_error = ResourceSaver::save(std_material, material_path, ResourceSaver::FLAG_CHANGE_PATH);
	if (save_error != OK) {
		result["error"] = vformat("Failed to save material: %d", save_error);
		return result;
	}

	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->update_file(material_path);
	}

	result["material_path"] = material_path;
	if (p_args.has("scene_path") && p_args.has("node_path")) {
		Dictionary assign_args;
		assign_args["scene_path"] = p_args.get("scene_path", "");
		assign_args["node_path"] = p_args.get("node_path", "");
		assign_args["property"] = p_args.get("property", "material_override");
		assign_args["resource_path"] = material_path;
		const Dictionary assign_result = _tool_assign_resource_to_property(assign_args);
		if (assign_result.has("error")) {
			return assign_result;
		}
		result["assigned_to"] = assign_result.get("node_path", "");
		result["property"] = assign_result.get("property", "");
	}
	return result;
}

Dictionary YeetAIDock::_tool_assign_resource_to_property(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	const String property_name = p_args.get("property", "");
	const String resource_path = p_args.get("resource_path", "");
	if (node_path.is_empty() || property_name.is_empty() || resource_path.is_empty()) {
		result["error"] = "node_path, property, and resource_path are required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *target = _resolve_node_target(scene_root, node_path, error);
	if (target == nullptr) {
		result["error"] = error;
		return result;
	}

	PropertyInfo property_info;
	if (!_get_node_property_info(target, property_name, property_info)) {
		result["error"] = "The target node does not expose that property.";
		return result;
	}

	if (property_info.type != Variant::OBJECT) {
		result["error"] = "Property is not a resource/object property.";
		return result;
	}

	Ref<Resource> resource = _load_resource_for_property(resource_path, property_info.hint_string, error);
	if (resource.is_null()) {
		result["error"] = error;
		return result;
	}

	target->set(property_name, resource);
	EditorInterface::get_singleton()->mark_scene_as_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(target->get_path());
	result["property"] = property_name;
	result["resource_path"] = resource_path;
	return result;
}

Dictionary YeetAIDock::_tool_connect_signal(const Dictionary &p_args) const {
	Dictionary result;
	const String source_node_path = p_args.get("source_node_path", "");
	const String signal_name = p_args.get("signal_name", "");
	const String target_node_path = p_args.get("target_node_path", "");
	const String method_name = p_args.get("method_name", "");
	if (source_node_path.is_empty() || signal_name.is_empty() || target_node_path.is_empty() || method_name.is_empty()) {
		result["error"] = "source_node_path, signal_name, target_node_path, and method_name are required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *source = _resolve_node_target(scene_root, source_node_path, error);
	if (source == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *target = _resolve_node_target(scene_root, target_node_path, error);
	if (target == nullptr) {
		result["error"] = error;
		return result;
	}

	if (!source->has_signal(signal_name)) {
		result["error"] = "The source node does not expose that signal.";
		return result;
	}
	if (!target->has_method(method_name)) {
		result["error"] = "The target node does not expose that method.";
		return result;
	}

	const Callable callable(target, method_name);
	if (source->is_connected(signal_name, callable)) {
		result["scene_path"] = scene_root->get_scene_file_path();
		result["source_node_path"] = String(source->get_path());
		result["target_node_path"] = String(target->get_path());
		result["signal_name"] = signal_name;
		result["method_name"] = method_name;
		result["already_connected"] = true;
		return result;
	}

	const uint32_t flags = uint32_t(int(p_args.get("flags", Object::CONNECT_PERSIST)));
	const Error connect_error = source->connect(signal_name, callable, flags);
	if (connect_error != OK) {
		result["error"] = vformat("Failed to connect signal: %d", connect_error);
		return result;
	}

	EditorInterface::get_singleton()->mark_scene_as_unsaved();
	result["scene_path"] = scene_root->get_scene_file_path();
	result["source_node_path"] = String(source->get_path());
	result["target_node_path"] = String(target->get_path());
	result["signal_name"] = signal_name;
	result["method_name"] = method_name;
	result["flags"] = int(flags);
	return result;
}

Dictionary YeetAIDock::_tool_create_input_action(const Dictionary &p_args) const {
	Dictionary result;
	const String action_name = String(p_args.get("action_name", "")).strip_edges();
	if (!is_valid_input_action_name(action_name)) {
		result["error"] = "action_name is invalid. It cannot be empty or contain / : = \\ or \".";
		return result;
	}

	const float deadzone = p_args.has("deadzone") ? float(p_args["deadzone"]) : InputMap::DEFAULT_DEADZONE;
	const Array event_definitions = p_args.get("events", Array());
	const bool replace_events = bool(p_args.get("replace_events", true));
	if (event_definitions.is_empty()) {
		result["error"] = "events is required and must contain at least one input event.";
		return result;
	}

	Array events;
	for (int i = 0; i < event_definitions.size(); i++) {
		String event_error;
		Ref<InputEvent> input_event = create_input_event_from_definition(event_definitions[i], event_error);
		if (input_event.is_null()) {
			result["error"] = "Failed to parse input event at index " + itos(i) + ": " + event_error;
			return result;
		}
		events.push_back(input_event);
	}

	InputMap *input_map = InputMap::get_singleton();
	if (input_map == nullptr) {
		result["error"] = "InputMap is unavailable.";
		return result;
	}
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	const String property_name = "input/" + action_name;
	Array saved_events;
	if (!replace_events && project_settings->has_setting(property_name)) {
		const Dictionary old_action = Dictionary(project_settings->get_setting_with_override(property_name));
		if (old_action.has("events")) {
			saved_events = old_action["events"];
		}
	}

	if (!input_map->has_action(action_name)) {
		input_map->add_action(action_name, deadzone);
	}
	input_map->action_set_deadzone(action_name, deadzone);
	if (replace_events) {
		input_map->action_erase_events(action_name);
	}
	for (const Variant &event_variant : events) {
		const Ref<InputEvent> input_event = Ref<InputEvent>(event_variant);
		input_map->action_add_event(action_name, input_event);
		saved_events.push_back(input_event);
	}

	Dictionary action;
	action["deadzone"] = deadzone;
	action["events"] = saved_events;
	project_settings->set_setting(property_name, action);
	const Error save_error = project_settings->save();
	if (save_error != OK) {
		result["error"] = vformat("Failed to save project settings: %d", save_error);
		return result;
	}

	result["action_name"] = action_name;
	result["deadzone"] = deadzone;
	result["event_count"] = events.size();
	result["total_event_count"] = saved_events.size();
	result["saved"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_set_main_scene(const Dictionary &p_args) const {
	Dictionary result;
	const String scene_path = p_args.get("scene_path", "");
	if (!scene_path.begins_with("res://")) {
		result["error"] = "scene_path must start with res://";
		return result;
	}
	if (!FileAccess::exists(scene_path)) {
		result["error"] = "scene_path does not exist.";
		return result;
	}

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	const String old_main_scene = String(project_settings->get_setting_with_override("application/run/main_scene"));
	project_settings->set_setting("application/run/main_scene", scene_path);
	const Error save_error = project_settings->save();
	if (save_error != OK) {
		result["error"] = vformat("Failed to save project settings: %d", save_error);
		return result;
	}

	result["main_scene"] = scene_path;
	result["previous_main_scene"] = old_main_scene;
	result["saved"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_play_current_scene() const {
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	editor->play_current_scene();
	result["is_playing"] = editor->is_playing_scene();
	result["playing_scene"] = editor->get_playing_scene();
	return result;
}

Dictionary YeetAIDock::_tool_play_main_scene() const {
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	editor->play_main_scene();
	result["is_playing"] = editor->is_playing_scene();
	result["playing_scene"] = editor->get_playing_scene();
	return result;
}

Dictionary YeetAIDock::_tool_stop_playing_scene() const {
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	const String previous_scene = editor->get_playing_scene();
	editor->stop_playing_scene();
	result["stopped_scene"] = previous_scene;
	result["is_playing"] = editor->is_playing_scene();
	return result;
}

Dictionary YeetAIDock::_tool_remove_node(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty()) {
		result["error"] = "node_path is required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *target = _resolve_node_target(scene_root, node_path, error);
	if (target == nullptr) {
		result["error"] = error;
		return result;
	}

	if (target == scene_root) {
		result["error"] = "Refusing to remove the scene root.";
		return result;
	}

	const String removed_path = String(target->get_path());
	target->queue_free();
	EditorInterface::get_singleton()->mark_scene_as_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["removed_node_path"] = removed_path;
	return result;
}

Dictionary YeetAIDock::_tool_set_node_property(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	const String property_name = p_args.get("property", "");
	if (node_path.is_empty() || property_name.is_empty() || !p_args.has("value")) {
		result["error"] = "node_path, property, and value are required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *target = _resolve_node_target(scene_root, node_path, error);
	if (target == nullptr) {
		result["error"] = error;
		return result;
	}

	PropertyInfo property_info;
	if (!_get_node_property_info(target, property_name, property_info)) {
		result["error"] = "The target node does not expose that property.";
		return result;
	}

	Variant converted_value;
	if (property_info.type == Variant::OBJECT && p_args["value"].get_type() == Variant::STRING && String(p_args["value"]).begins_with("res://")) {
		Ref<Resource> resource = _load_resource_for_property(p_args["value"], property_info.hint_string, error);
		if (resource.is_null()) {
			result["error"] = error;
			return result;
		}
		converted_value = resource;
	} else {
		const Variant current_value = target->get(property_name);
		const Variant::Type hint_type = current_value.get_type() != Variant::NIL ? current_value.get_type() : property_info.type;
		converted_value = _variant_from_json(p_args["value"], hint_type);
	}

	target->set(property_name, converted_value);
	EditorInterface::get_singleton()->mark_scene_as_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(target->get_path());
	result["property"] = property_name;
	result["value"] = _json_safe_variant(target->get(property_name));
	return result;
}

void YeetAIDock::_collect_project_entries(const String &p_dir_path, int p_depth, int p_max_depth, const Vector<String> &p_include_extensions, Array &r_entries, int &r_entry_count) const {
	if (r_entry_count >= MAX_PROJECT_TREE_ENTRIES) {
		return;
	}

	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(p_dir_path, &open_error);
	if (dir.is_null() || open_error != OK) {
		return;
	}

	dir->set_include_hidden(false);
	dir->set_include_navigational(false);
	if (dir->list_dir_begin() != OK) {
		return;
	}

	while (r_entry_count < MAX_PROJECT_TREE_ENTRIES) {
		const String name = dir->get_next();
		if (name.is_empty()) {
			break;
		}

		const String full_path = p_dir_path.path_join(name);
		Dictionary entry;
		entry["path"] = full_path;
		if (dir->current_is_dir()) {
			entry["kind"] = "dir";
			r_entries.push_back(entry);
			r_entry_count++;
			if (p_depth < p_max_depth) {
				_collect_project_entries(full_path, p_depth + 1, p_max_depth, p_include_extensions, r_entries, r_entry_count);
			}
			continue;
		}

		if (!p_include_extensions.is_empty()) {
			const String ext = "." + full_path.get_extension().to_lower();
			if (!contains_string(p_include_extensions, ext)) {
				continue;
			}
		}

		entry["kind"] = "file";
		entry["resource_type"] = ResourceLoader::get_resource_type(full_path);
		r_entries.push_back(entry);
		r_entry_count++;
	}

	dir->list_dir_end();
}

void YeetAIDock::_find_project_entries(const String &p_dir_path, const String &p_query, int p_depth, int p_max_depth, const Vector<String> &p_include_extensions, Array &r_entries, int &r_entry_count, int p_max_results) const {
	if (r_entry_count >= p_max_results) {
		return;
	}

	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(p_dir_path, &open_error);
	if (dir.is_null() || open_error != OK) {
		return;
	}

	dir->set_include_hidden(false);
	dir->set_include_navigational(false);
	if (dir->list_dir_begin() != OK) {
		return;
	}

	while (r_entry_count < p_max_results) {
		const String name = dir->get_next();
		if (name.is_empty()) {
			break;
		}

		const String full_path = p_dir_path.path_join(name);
		if (dir->current_is_dir()) {
			if (p_depth < p_max_depth) {
				_find_project_entries(full_path, p_query, p_depth + 1, p_max_depth, p_include_extensions, r_entries, r_entry_count, p_max_results);
			}
			continue;
		}

		if (!p_include_extensions.is_empty()) {
			const String ext = "." + full_path.get_extension().to_lower();
			if (!contains_string(p_include_extensions, ext)) {
				continue;
			}
		}

		const String lowered_path = full_path.to_lower();
		if (!lowered_path.contains(p_query)) {
			continue;
		}

		Dictionary entry;
		entry["path"] = full_path;
		entry["kind"] = "file";
		entry["resource_type"] = ResourceLoader::get_resource_type(full_path);
		r_entries.push_back(entry);
		r_entry_count++;
	}

	dir->list_dir_end();
}

Dictionary YeetAIDock::_serialize_node(Node *p_node, int p_depth, int p_max_depth, const Vector<String> &p_include_properties, int &r_node_count) const {
	Dictionary result;
	if (p_node == nullptr || r_node_count >= MAX_SCENE_TREE_NODES) {
		return result;
	}

	r_node_count++;
	result["name"] = p_node->get_name();
	result["type"] = p_node->get_class();
	result["node_path"] = String(p_node->get_path());
	result["child_count"] = p_node->get_child_count();

	if (!p_include_properties.is_empty()) {
		result["properties"] = _serialize_node_properties(p_node, p_include_properties);
	}

	Array children;
	if (p_depth < p_max_depth) {
		const int child_count = p_node->get_child_count();
		for (int i = 0; i < child_count && r_node_count < MAX_SCENE_TREE_NODES; i++) {
			children.push_back(_serialize_node(p_node->get_child(i), p_depth + 1, p_max_depth, p_include_properties, r_node_count));
		}
	}
	result["children"] = children;
	return result;
}

Dictionary YeetAIDock::_serialize_node_properties(Node *p_node, const Vector<String> &p_include_properties) const {
	Dictionary properties;
	for (const String &property_name : p_include_properties) {
		if (!_node_has_property(p_node, property_name)) {
			continue;
		}
		properties[property_name] = _json_safe_variant(p_node->get(property_name));
	}
	return properties;
}

bool YeetAIDock::_node_has_property(Node *p_node, const StringName &p_property) const {
	PropertyInfo property_info;
	return _get_node_property_info(p_node, p_property, property_info);
}

bool YeetAIDock::_get_node_property_info(Node *p_node, const StringName &p_property, PropertyInfo &r_info) const {
	if (p_node == nullptr) {
		return false;
	}

	List<PropertyInfo> property_list;
	p_node->get_property_list(&property_list);
	for (const PropertyInfo &property_info : property_list) {
		if (property_info.name == p_property) {
			r_info = property_info;
			return true;
		}
	}
	return false;
}

Ref<Resource> YeetAIDock::_load_resource_for_property(const String &p_resource_path, const String &p_expected_type, String &r_error) const {
	if (!p_resource_path.begins_with("res://")) {
		r_error = "resource_path must start with res://";
		return Ref<Resource>();
	}

	String type_hint = p_expected_type.get_slice(",", 0).strip_edges();
	Error load_error = OK;
	Ref<Resource> resource = ResourceLoader::load(p_resource_path, type_hint, ResourceFormatLoader::CACHE_MODE_REPLACE, &load_error);
	if (resource.is_null() || load_error != OK) {
		r_error = vformat("Failed to load resource: %d", load_error);
		return Ref<Resource>();
	}
	return resource;
}

Variant YeetAIDock::_json_safe_variant(const Variant &p_value, int p_depth) const {
	if (p_depth >= MAX_PROPERTY_COLLECTION_DEPTH) {
		return String("<max-depth>");
	}

	switch (p_value.get_type()) {
		case Variant::NIL:
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::STRING:
			return p_value;
		case Variant::STRING_NAME:
			return String(p_value);
		case Variant::NODE_PATH:
			return String(p_value);
		case Variant::VECTOR2: {
			const Vector2 value = p_value;
			Dictionary dict;
			dict["x"] = value.x;
			dict["y"] = value.y;
			return dict;
		}
		case Variant::VECTOR2I: {
			const Vector2i value = p_value;
			Dictionary dict;
			dict["x"] = value.x;
			dict["y"] = value.y;
			return dict;
		}
		case Variant::VECTOR3: {
			const Vector3 value = p_value;
			Dictionary dict;
			dict["x"] = value.x;
			dict["y"] = value.y;
			dict["z"] = value.z;
			return dict;
		}
		case Variant::VECTOR3I: {
			const Vector3i value = p_value;
			Dictionary dict;
			dict["x"] = value.x;
			dict["y"] = value.y;
			dict["z"] = value.z;
			return dict;
		}
		case Variant::VECTOR4: {
			const Vector4 value = p_value;
			Dictionary dict;
			dict["x"] = value.x;
			dict["y"] = value.y;
			dict["z"] = value.z;
			dict["w"] = value.w;
			return dict;
		}
		case Variant::COLOR: {
			const Color value = p_value;
			Dictionary dict;
			dict["r"] = value.r;
			dict["g"] = value.g;
			dict["b"] = value.b;
			dict["a"] = value.a;
			return dict;
		}
		case Variant::ARRAY: {
			Array array = p_value;
			Array json_array;
			const int count = MIN(array.size(), 32);
			for (int i = 0; i < count; i++) {
				json_array.push_back(_json_safe_variant(array[i], p_depth + 1));
			}
			if (array.size() > count) {
				json_array.push_back("...[truncated]");
			}
			return json_array;
		}
		case Variant::DICTIONARY: {
			Dictionary dictionary = p_value;
			Dictionary json_dict;
			Array keys = dictionary.keys();
			const int count = MIN(keys.size(), 32);
			for (int i = 0; i < count; i++) {
				const Variant key = keys[i];
				json_dict[String(key)] = _json_safe_variant(dictionary[key], p_depth + 1);
			}
			if (keys.size() > count) {
				json_dict["__truncated__"] = true;
			}
			return json_dict;
		}
		case Variant::OBJECT: {
			Object *object = p_value;
			if (object == nullptr) {
				return Variant();
			}

			Dictionary dict;
			dict["class"] = object->get_class();
			if (const Resource *resource = Object::cast_to<Resource>(object)) {
				dict["resource_path"] = resource->get_path();
			}
			return dict;
		}
		default:
			return String(p_value);
	}
}

Variant YeetAIDock::_variant_from_json(const Variant &p_input, Variant::Type p_hint_type) const {
	if (p_input.get_type() == Variant::DICTIONARY) {
		const Dictionary dict = p_input;
		if ((p_hint_type == Variant::VECTOR2 || p_hint_type == Variant::NIL) && dict.has("x") && dict.has("y") && !dict.has("z")) {
			return Vector2(dict.get("x", 0.0), dict.get("y", 0.0));
		}
		// Model may pass a 3-axis dict {x,y,z} for a 2D parameter (e.g. PlaneMesh
		// size). Interpret x as width and z as depth, dropping the irrelevant y.
		if (p_hint_type == Variant::VECTOR2 && dict.has("x") && dict.has("z")) {
			return Vector2(dict.get("x", 0.0), dict.get("z", 0.0));
		}
		if ((p_hint_type == Variant::VECTOR2I) && dict.has("x") && dict.has("y") && !dict.has("z")) {
			return Vector2i(int(dict.get("x", 0)), int(dict.get("y", 0)));
		}
		if ((p_hint_type == Variant::VECTOR3 || p_hint_type == Variant::NIL) && dict.has("x") && dict.has("y") && dict.has("z") && !dict.has("w")) {
			return Vector3(dict.get("x", 0.0), dict.get("y", 0.0), dict.get("z", 0.0));
		}
		if (p_hint_type == Variant::VECTOR3I && dict.has("x") && dict.has("y") && dict.has("z") && !dict.has("w")) {
			return Vector3i(int(dict.get("x", 0)), int(dict.get("y", 0)), int(dict.get("z", 0)));
		}
		if ((p_hint_type == Variant::VECTOR4 || p_hint_type == Variant::NIL) && dict.has("x") && dict.has("y") && dict.has("z") && dict.has("w")) {
			return Vector4(dict.get("x", 0.0), dict.get("y", 0.0), dict.get("z", 0.0), dict.get("w", 0.0));
		}
		if ((p_hint_type == Variant::COLOR || p_hint_type == Variant::NIL) && dict.has("r") && dict.has("g") && dict.has("b")) {
			return Color(dict.get("r", 0.0), dict.get("g", 0.0), dict.get("b", 0.0), dict.get("a", 1.0));
		}
	}

	switch (p_hint_type) {
		case Variant::BOOL:
			return bool(p_input);
		case Variant::INT:
			return int64_t(p_input);
		case Variant::FLOAT:
			return double(p_input);
		case Variant::STRING:
			return String(p_input);
		case Variant::STRING_NAME:
			return StringName(String(p_input));
		case Variant::NODE_PATH:
			return NodePath(String(p_input));
		default:
			return p_input;
	}
}

bool YeetAIDock::_parse_json_dictionary_quiet(const String &p_text, Dictionary &r_result) const {
	Ref<JSON> json;
	json.instantiate();
	const Error error = json->parse(p_text);
	if (error != OK) {
		return false;
	}

	const Variant json_root = json->get_data();
	if (json_root.get_type() != Variant::DICTIONARY) {
		return false;
	}

	r_result = json_root;
	return true;
}

String YeetAIDock::_build_system_prompt() const {
	return String() +
			"You are Crosshair AI embedded inside the editor.\n"
			"You are building playable Godot game slices, not just isolated assets.\n"
			"You must respond with valid JSON only.\n"
			"You can use exactly one of these JSON shapes:\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_project_tree\",\"arguments\":{\"root\":\"res://\",\"max_depth\":3,\"include_extensions\":[\".godot\",\".tscn\",\".gd\"]}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"find_project_files\",\"arguments\":{\"query\":\"player\",\"include_extensions\":[\".tscn\",\".gd\"],\"max_results\":20}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"read_project_file\",\"arguments\":{\"path\":\"res://project.godot\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_project_settings\",\"arguments\":{\"keys\":[\"application/config/name\",\"application/run/main_scene\"]}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_input_actions\",\"arguments\":{\"include_events\":true,\"max_actions\":40}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_open_scenes\",\"arguments\":{}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_current_scene\",\"arguments\":{}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_selected_nodes\",\"arguments\":{}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_scene_tree\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"max_depth\":4,\"include_properties\":[\"script\",\"position\",\"mesh\",\"material_override\"]}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_node_details\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"Player\",\"include_properties\":[\"script\",\"position\"]}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_node_api\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"Player\",\"max_methods\":80,\"max_signals\":40,\"max_properties\":60}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"batch_tool_calls\",\"arguments\":{\"shared_arguments\":{\"scene_path\":\"res://levels/main.tscn\"},\"calls\":[{\"tool\":\"add_node\",\"arguments\":{\"parent_path\":\".\",\"node_type\":\"Node3D\",\"node_name\":\"GameplayRoot\"}},{\"tool\":\"add_primitive_mesh\",\"arguments\":{\"parent_path\":\"GameplayRoot\",\"node_name\":\"Crate\",\"mesh_type\":\"box\",\"parameters\":{\"size\":{\"x\":1,\"y\":1,\"z\":1}}}}]}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"open_scene\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"save_current_scene\",\"arguments\":{}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"create_scene_file\",\"arguments\":{\"scene_path\":\"res://levels/generated_level.tscn\",\"root_type\":\"Node3D\",\"root_name\":\"GeneratedLevel\",\"open\":true}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"create_gdscript_file\",\"arguments\":{\"script_path\":\"res://scripts/player_controller.gd\",\"contents\":\"extends CharacterBody3D\\n\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"update_gdscript_file\",\"arguments\":{\"script_path\":\"res://scripts/player_controller.gd\",\"operation\":\"append\",\"contents\":\"\\nfunc _ready():\\n\\tprint(\\\"ready\\\")\\n\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"attach_script\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"Player\",\"script_path\":\"res://scripts/player_controller.gd\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"add_node\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"parent_path\":\".\",\"node_type\":\"Node3D\",\"node_name\":\"GeneratedNode\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"instantiate_scene\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"parent_path\":\".\",\"packed_scene_path\":\"res://actors/enemy.tscn\",\"node_name\":\"Enemy\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"add_primitive_mesh\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"parent_path\":\".\",\"node_name\":\"Crate\",\"mesh_type\":\"box\",\"parameters\":{\"size\":{\"x\":1,\"y\":1,\"z\":1}}}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"add_collision_shape\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"parent_path\":\"Player\",\"node_name\":\"PlayerCollision\",\"shape_type\":\"capsule\",\"parameters\":{\"radius\":0.45,\"height\":1.8}}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"create_standard_material\",\"arguments\":{\"material_path\":\"res://materials/crate.tres\",\"albedo\":{\"r\":0.7,\"g\":0.45,\"b\":0.22,\"a\":1.0},\"roughness\":0.85,\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"Crate\",\"property\":\"material_override\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"assign_resource_to_property\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"Crate\",\"property\":\"script\",\"resource_path\":\"res://scripts/crate.gd\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"connect_signal\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"source_node_path\":\"Button\",\"signal_name\":\"pressed\",\"target_node_path\":\"Button\",\"method_name\":\"_on_button_pressed\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"create_input_action\",\"arguments\":{\"action_name\":\"jump\",\"events\":[{\"type\":\"key\",\"key\":\"Space\"},{\"type\":\"key\",\"key\":\"J\"}],\"deadzone\":0.5}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"set_main_scene\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"play_current_scene\",\"arguments\":{}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"play_main_scene\",\"arguments\":{}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"stop_playing_scene\",\"arguments\":{}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"remove_node\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"GeneratedNode\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"set_node_property\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"GeneratedNode\",\"property\":\"position\",\"value\":{\"x\":0,\"y\":1,\"z\":0}}}\n"
			"{\"type\":\"final\",\"message\":\"Your answer here\"}\n"
			"Core behavior:\n"
			"- Never wrap JSON in markdown.\n"
			"- You will receive a live project/editor snapshot in a separate system message. Use it first.\n"
			"- Prefer one `batch_tool_calls` request for multi-step changes, but keep each reply's JSON small enough to finish in one response; use multiple tool_call rounds if many edits are needed.\n"
			"- Before wiring signals, inspect `get_node_api` if you do not know the signal or method names.\n"
			"- Before writing gameplay, inspect existing scripts and input actions so you reuse conventions.\n"
			"- Prefer `update_gdscript_file` over recreating files when iterating.\n"
			"- Prefer reusing existing scenes, nodes, and scripts over creating duplicates.\n"
			"Game-building heuristics:\n"
			"- Aim for a playable loop, not decorative output.\n"
			"- A minimal playable slice usually needs: a main scene, one controllable actor or interaction point, input actions, collisions where relevant, at least one visible object, and scripts connected to the scene.\n"
			"- If the project has no obvious entry scene, create one, set it as main, save it, and mention that in the final answer.\n"
			"- When adding 3D gameplay, prefer sensible node types like `Node3D`, `CharacterBody3D`, `StaticBody3D`, `Area3D`, `MeshInstance3D`, `CollisionShape3D`, `Camera3D`, and `DirectionalLight3D` when appropriate.\n"
			"- Use `create_input_action` when gameplay needs controls; do not assume input actions already exist.\n"
			"- Use materials and meshes sparingly but make scenes readable and testable.\n"
			"- `add_primitive_mesh` mesh_type values: box (size:{x,y,z}), sphere (radius, height), capsule (radius, height), cylinder (top_radius, bottom_radius, height), plane (size:{x,z} — 2D, no y).\n"
			"- For physics floors/walls use `add_node` StaticBody3D first, then `add_primitive_mesh` + `add_collision_shape` as children.\n"
			"- For a playable character use `add_node` CharacterBody3D, then `add_collision_shape` + `attach_script` as children.\n"
			"- `set_node_property` args: scene_path, node_path, property (string), value. Example: {\"property\":\"position\",\"value\":{\"x\":0,\"y\":5,\"z\":0}}.\n"
			"Context tools:\n"
			"- `get_project_tree` lists project files and directories.\n"
			"- `find_project_files` searches project paths by substring.\n"
			"- `read_project_file` reads safe text files including `project.godot`, scenes, scripts, and resources.\n"
			"- `get_project_settings` reads key project settings such as main scene and project name.\n"
			"- `get_input_actions` reads current input actions and bindings.\n"
			"- `get_open_scenes`, `get_current_scene`, `get_selected_nodes`, `get_scene_tree`, `get_node_details`, and `get_node_api` inspect editor state and scene capabilities.\n"
			"Write tools:\n"
			"- `create_scene_file`, `open_scene`, `save_current_scene`, `add_node`, `instantiate_scene`, `add_primitive_mesh`, `add_collision_shape`, `set_node_property`, `remove_node`, `create_standard_material`, `assign_resource_to_property`, `attach_script`, `create_gdscript_file`, `update_gdscript_file`, `connect_signal`, `create_input_action`, and `set_main_scene` mutate the project.\n"
			"- `play_current_scene`, `play_main_scene`, and `stop_playing_scene` control editor play mode.\n"
			"Response policy:\n"
			"- After each tool result, either call another tool or return `final`.\n"
			"- Keep final answers concise and specific about what changed.\n";
}

String YeetAIDock::_escape_bbcode(const String &p_text) const {
	return p_text.replace("[", "[lb]");
}

String YeetAIDock::_extract_message_content(const Dictionary &p_response_json) const {
	const Array choices = p_response_json.get("choices", Array());
	if (choices.is_empty()) {
		return String();
	}

	const Dictionary choice = choices[0];
	const Dictionary message = choice.get("message", Dictionary());
	const Variant content = message.get("content", "");
	String text;
	if (content.get_type() == Variant::STRING) {
		text = String(content);
	} else if (content.get_type() == Variant::ARRAY) {
		const Array parts = content;
		for (const Variant &part_variant : parts) {
			if (part_variant.get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary part = part_variant;
			if (part.get("type", "") == "text") {
				text += String(part.get("text", ""));
			}
		}
	} else {
		text = String(content);
	}

	if (!text.strip_edges().is_empty()) {
		return text;
	}

	// OpenAI-style: assistant message with tool_calls and empty content.
	const Array tool_calls = message.get("tool_calls", Array());
	if (!tool_calls.is_empty() && tool_calls[0].get_type() == Variant::DICTIONARY) {
		const Dictionary tc = tool_calls[0];
		const Dictionary fn = tc.get("function", Dictionary());
		const String fn_name = String(fn.get("name", "")).strip_edges();
		if (!fn_name.is_empty()) {
			Dictionary envelope;
			envelope["type"] = "tool_call";
			const String canonical_tool = is_batch_tool_name_alias(fn_name) ? String("batch_tool_calls") : fn_name;
			envelope["tool"] = canonical_tool;
			const String fn_args_str = String(fn.get("arguments", "")).strip_edges();
			Dictionary parsed_args;
			if (!fn_args_str.is_empty() && _parse_json_dictionary_quiet(fn_args_str, parsed_args)) {
				envelope["arguments"] = parsed_args;
			} else {
				envelope["arguments"] = Dictionary();
			}
			if (canonical_tool == "batch_tool_calls") {
				envelope["arguments"] = _normalize_batch_tool_arguments(envelope["arguments"]);
			}
			return JSON::stringify(envelope);
		}
	}

	return text;
}

Dictionary YeetAIDock::_extract_response_envelope(const String &p_content) const {
	String cleaned = p_content.strip_edges();
	cleaned = strip_reasoning_markers(cleaned);

	// Strip leaked stop tokens from common model families.
	static const char *stop_tokens[] = {
		"<|im_end|>",
		"<|endoftext|>",
		"<|end|>",
		"</s>",
	};
	for (const char *token : stop_tokens) {
		if (cleaned.ends_with(token)) {
			cleaned = cleaned.substr(0, cleaned.length() - String(token).length()).strip_edges();
		}
	}

	if (cleaned.begins_with("```")) {
		const int first_newline = cleaned.find("\n");
		if (first_newline != -1) {
			cleaned = cleaned.substr(first_newline + 1).strip_edges();
		}
		if (cleaned.ends_with("```")) {
			cleaned = cleaned.substr(0, cleaned.length() - 3).strip_edges();
		}
	}

	Dictionary parsed;
	if (_parse_json_dictionary_quiet(cleaned, parsed)) {
		return _normalize_envelope(parsed);
	}

	// Reasoning models often emit commentary before the JSON tool payload. Prefer balanced
	// extraction from known markers (try last match first — JSON is usually at the end).
	static const char *json_markers[] = {
		"{\"type\":\"tool_call\"",
		"{\"type\": \"tool_call\"",
		"{\"type\":\"final\"",
		"{\"type\": \"final\"",
	};
	for (const char *marker : json_markers) {
		const String mk(marker);
		Vector<int> hits;
		for (int p = 0;;) {
			const int hit = cleaned.find(mk, p);
			if (hit == -1) {
				break;
			}
			hits.push_back(hit);
			p = hit + 1;
		}
		for (int h = hits.size() - 1; h >= 0; h--) {
			const int pos = hits[h];
			const int obj_end = find_json_object_end(cleaned, pos);
			if (obj_end == -1) {
				continue;
			}
			const String sub = cleaned.substr(pos, obj_end - pos + 1);
			if (_parse_json_dictionary_quiet(sub, parsed)) {
				return _normalize_envelope(parsed);
			}
		}
	}

	const int json_start = cleaned.find("{");
	const int json_end = cleaned.rfind("}");
	if (json_start != -1 && json_end != -1 && json_end > json_start) {
		if (_parse_json_dictionary_quiet(cleaned.substr(json_start, json_end - json_start + 1), parsed)) {
			return _normalize_envelope(parsed);
		}
	}

	for (int p = 0; p < cleaned.length(); p++) {
		if (cleaned[p] != '{') {
			continue;
		}
		const int obj_end = find_json_object_end(cleaned, p);
		if (obj_end == -1) {
			continue;
		}
		const String sub = cleaned.substr(p, obj_end - p + 1);
		if (_parse_json_dictionary_quiet(sub, parsed)) {
			return _normalize_envelope(parsed);
		}
	}

	Dictionary fallback;
	fallback["type"] = "final";
	// Truncated or malformed tool JSON is common when max_tokens is too low for batch_tool_calls.
	if (cleaned.contains("\"type\":\"tool_call\"") || cleaned.contains("\"batch_tool_calls\"") || cleaned.contains("batch_tool_calls")) {
		fallback["message"] = TTR("The model output was not valid JSON. Common causes: (1) response truncated by the completion token limit, (2) reasoning / commentary before the JSON that confused an older parser (try rebuilding), (3) malformed JSON from the model.\n\n"
				"In Editor Settings, search for `yeet_ai` and raise `yeet_ai/chat/max_tokens` (16384 or higher is recommended for large batch_tool_calls). You can also ask the model to use smaller batches or fewer steps per reply.\n\n"
				"--- Raw output (preview) ---\n") +
				_truncate_preview(cleaned, 4000);
	} else {
		fallback["message"] = cleaned;
	}
	return fallback;
}

Dictionary YeetAIDock::_normalize_batch_tool_arguments(const Dictionary &p_args) const {
	Dictionary out = p_args.duplicate();
	Variant calls_src;
	if (out.has("calls")) {
		calls_src = out["calls"];
	} else if (out.has("tool_calls")) {
		calls_src = out["tool_calls"];
	} else {
		return out;
	}
	const Array calls = coerce_json_array_from_variant(calls_src);
	out["calls"] = calls;
	Array normalized;
	for (int i = 0; i < calls.size(); i++) {
		Variant call_var = calls[i];
		Dictionary call;
		if (call_var.get_type() == Variant::DICTIONARY) {
			Dictionary raw = call_var;
			call = raw.duplicate();
		} else if (call_var.get_type() == Variant::STRING) {
			if (!_parse_json_dictionary_quiet(String(call_var).strip_edges(), call)) {
				normalized.push_back(call_var);
				continue;
			}
		} else {
			normalized.push_back(call_var);
			continue;
		}
		String tn = String(call.get("tool", "")).strip_edges();
		if (tn.is_empty()) {
			tn = String(call.get("name", "")).strip_edges();
		}
		if (tn.is_empty()) {
			const Dictionary fn = call.get("function", Dictionary());
			tn = String(fn.get("name", "")).strip_edges();
		}
		if (!tn.is_empty()) {
			call["tool"] = tn;
		}
		if (call.has("arguments")) {
			const Variant args_v = call["arguments"];
			if (args_v.get_type() == Variant::STRING) {
				Dictionary parsed;
				if (_parse_json_dictionary_quiet(String(args_v).strip_edges(), parsed)) {
					call["arguments"] = parsed;
				}
			}
		} else if (call.has("parameters")) {
			call["arguments"] = call["parameters"];
		}
		normalized.push_back(call);
	}
	out["calls"] = normalized;
	return out;
}

Dictionary YeetAIDock::_normalize_envelope(const Dictionary &p_envelope) const {
	const String type = p_envelope.get("type", "");
	const String tool = p_envelope.get("tool", "");

	if (type == "final") {
		return p_envelope;
	}

	// tool_call: repair missing tool name and normalize batch payloads (must not return early before this).
	if (type == "tool_call") {
		Dictionary out = p_envelope.duplicate();
		if (out.has("arguments") && out["arguments"].get_type() == Variant::STRING) {
			Dictionary parsed;
			if (_parse_json_dictionary_quiet(String(out["arguments"]).strip_edges(), parsed)) {
				out["arguments"] = parsed;
			}
		}
		String t = String(out.get("tool", "")).strip_edges();
		if (is_batch_tool_name_alias(t)) {
			out["tool"] = "batch_tool_calls";
			t = "batch_tool_calls";
		}
		if (t.is_empty() && out.has("arguments")) {
			const Variant args_var = out["arguments"];
			if (args_var.get_type() == Variant::DICTIONARY) {
				const Dictionary args_dict = args_var;
				if (args_dict.has("calls") || args_dict.has("tool_calls")) {
					out["tool"] = "batch_tool_calls";
					t = "batch_tool_calls";
				}
			}
		}
		if (t == "batch_tool_calls" && out.has("arguments") && out["arguments"].get_type() == Variant::DICTIONARY) {
			out["arguments"] = _normalize_batch_tool_arguments(out["arguments"]);
		}
		return out;
	}

	// ── Shape 1 ──────────────────────────────────────────────────────────────
	// Model put the tool name in "type":
	//   {"type":"batch_tool_calls","arguments":{...}}
	if (!type.is_empty() && p_envelope.has("arguments")) {
		Dictionary fixed;
		fixed["type"] = "tool_call";
		fixed["tool"] = is_batch_tool_name_alias(type) ? String("batch_tool_calls") : type;
		Dictionary args_dict;
		const Variant args_var = p_envelope["arguments"];
		if (args_var.get_type() == Variant::STRING) {
			if (!_parse_json_dictionary_quiet(String(args_var).strip_edges(), args_dict)) {
				args_dict = Dictionary();
			}
		} else if (args_var.get_type() == Variant::DICTIONARY) {
			args_dict = args_var;
		}
		fixed["arguments"] = args_dict;
		if (String(fixed["tool"]) == "batch_tool_calls") {
			fixed["arguments"] = _normalize_batch_tool_arguments(args_dict);
		}
		return fixed;
	}

	// ── Shape 2 ──────────────────────────────────────────────────────────────
	// Model included "tool" but omitted "type":
	//   {"tool":"batch_tool_calls","arguments":{...}}
	if (!tool.is_empty() && p_envelope.has("arguments")) {
		Dictionary fixed;
		fixed["type"] = "tool_call";
		fixed["tool"] = is_batch_tool_name_alias(tool) ? String("batch_tool_calls") : tool;
		Dictionary args_dict;
		const Variant args_var = p_envelope["arguments"];
		if (args_var.get_type() == Variant::STRING) {
			if (!_parse_json_dictionary_quiet(String(args_var).strip_edges(), args_dict)) {
				args_dict = Dictionary();
			}
		} else if (args_var.get_type() == Variant::DICTIONARY) {
			args_dict = args_var;
		}
		fixed["arguments"] = args_dict;
		if (String(fixed["tool"]) == "batch_tool_calls") {
			fixed["arguments"] = _normalize_batch_tool_arguments(args_dict);
		}
		return fixed;
	}

	// ── Shape 3 ──────────────────────────────────────────────────────────────
	// Model omitted both "type" and "tool" and wrapped the arguments dict:
	//   {"arguments":{"calls":[...]}}   → batch_tool_calls
	//   {"arguments":{"path":"..."}}    → impossible to infer tool, skip
	if (p_envelope.has("arguments")) {
		const Variant args_var = p_envelope["arguments"];
		if (args_var.get_type() == Variant::DICTIONARY) {
			const Dictionary args_dict = args_var;
			if (args_dict.has("calls") || args_dict.has("tool_calls")) {
				Dictionary fixed;
				fixed["type"] = "tool_call";
				fixed["tool"] = "batch_tool_calls";
				fixed["arguments"] = _normalize_batch_tool_arguments(args_dict);
				return fixed;
			}
		}
	}

	// ── Shape 4 ──────────────────────────────────────────────────────────────
	// Model put "calls" at the very top level (skipped both wrapper layers):
	//   {"calls":[{"tool":"add_node","arguments":{...}},...]}
	if (p_envelope.has("calls") || p_envelope.has("tool_calls")) {
		Dictionary args;
		if (p_envelope.has("calls")) {
			args["calls"] = p_envelope["calls"];
		} else {
			args["calls"] = p_envelope["tool_calls"];
		}
		if (p_envelope.has("shared_arguments")) {
			args["shared_arguments"] = p_envelope["shared_arguments"];
		}
		if (p_envelope.has("stop_on_error")) {
			args["stop_on_error"] = p_envelope["stop_on_error"];
		}
		Dictionary fixed;
		fixed["type"] = "tool_call";
		fixed["tool"] = "batch_tool_calls";
		fixed["arguments"] = _normalize_batch_tool_arguments(args);
		return fixed;
	}

	return p_envelope;
}

Vector<String> YeetAIDock::_variant_array_to_string_vector(const Array &p_values) const {
	Vector<String> values;
	values.reserve(p_values.size());
	for (const Variant &value : p_values) {
		values.push_back(String(value).to_lower());
	}
	return values;
}

bool YeetAIDock::_is_allowed_text_file(const String &p_path) const {
	static const char *allowed_extensions[] = {
		"godot",
		"gd",
		"tscn",
		"tres",
		"cfg",
		"ini",
		"txt",
		"json",
		"md",
		"shader",
		"gdshader",
	};

	const String ext = p_path.get_extension().to_lower();
	for (const char *allowed_extension : allowed_extensions) {
		if (ext == allowed_extension) {
			return true;
		}
	}
	return false;
}
