/**************************************************************************/
/*  yeet_ai_dock.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/config/project_settings.h"
#include "core/core_bind.h"
#include "core/input/input_map.h"
#include "core/io/image.h"
#include "core/templates/hash_set.h"
#include "core/templates/list.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/property_info.h"
#include "core/string/string_name.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/os/keyboard.h"
#include "core/string/translation.h"
#include "main/main.h"
#include "servers/display/display_server.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/run/editor_run.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/text_edit.h"
#include "scene/2d/navigation/navigation_region_2d.h"
#include "scene/2d/physics/collision_object_2d.h"
#include "scene/2d/tile_map.h"
#include "scene/3d/navigation/navigation_region_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/main/node.h"
#include "core/crypto/crypto.h"
#include "core/io/http_client.h"
#include "core/math/math_funcs.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/os/time.h"
#include "editor/file_system/editor_paths.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/item_list.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/main/http_request.h"
#include "scene/main/viewport.h"
#include "scene/animation/animation_player.h"
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
constexpr int MAX_FILE_WRITE_BYTES = 512 * 1024;
constexpr int MAX_SCENE_TREE_NODES = 300;
constexpr int MAX_PROPERTY_COLLECTION_DEPTH = 4;

Dictionary make_message(const String &p_role, const String &p_content) {
	Dictionary message;
	message["role"] = p_role;
	message["content"] = p_content;
	return message;
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

// contains_string is defined in yeet_ai_helpers.cpp

// Some models emit two JSON objects: {"arguments":{...}}, {"tool":"x","type":"tool_call"} — invalid as one value but recoverable.
static String _json_frag_effective_tool_name(const Dictionary &d) {
	String t = String(d.get("tool", "")).strip_edges();
	if (!t.is_empty()) {
		return t;
	}
	return String(d.get("name", "")).strip_edges();
}

static bool _json_frag_type_is_allowed_on_args_fragment(const String &p_typ) {
	const String t = p_typ.strip_edges();
	if (t.is_empty()) {
		return true;
	}
	return t.to_lower() == "tool_call";
}

// First fragment: arguments dict present; tool name not yet in this object (may have type:"tool_call" split across two objects).
static bool _json_frag_is_arguments_only(const Dictionary &d) {
	if (!d.has("arguments") || d["arguments"].get_type() != Variant::DICTIONARY) {
		return false;
	}
	if (!_json_frag_effective_tool_name(d).is_empty()) {
		return false;
	}
	const String typ = String(d.get("type", "")).strip_edges();
	return _json_frag_type_is_allowed_on_args_fragment(typ);
}

static bool _json_frag_is_tool_header(const Dictionary &d) {
	const String tool = _json_frag_effective_tool_name(d);
	if (tool.is_empty()) {
		return false;
	}
	const String typ = String(d.get("type", "")).strip_edges();
	if (typ.to_lower() == "final") {
		return false;
	}
	if (!typ.is_empty() && typ.to_lower() != "tool_call") {
		return false;
	}
	return true;
}

static bool _merge_tool_call_json_pair(const Dictionary &a, const Dictionary &b, Dictionary &r_out) {
	if (_json_frag_is_arguments_only(a) && _json_frag_is_tool_header(b)) {
		r_out.clear();
		r_out["type"] = "tool_call";
		r_out["tool"] = _json_frag_effective_tool_name(b);
		r_out["arguments"] = a["arguments"];
		return true;
	}
	if (_json_frag_is_tool_header(a) && _json_frag_is_arguments_only(b)) {
		r_out.clear();
		r_out["type"] = "tool_call";
		r_out["tool"] = _json_frag_effective_tool_name(a);
		r_out["arguments"] = b["arguments"];
		return true;
	}
	return false;
}

static bool _envelope_normalized_is_dispatchable(const Dictionary &p_norm) {
	const String t = String(p_norm.get("type", "")).strip_edges();
	return t == "tool_call" || t == "final";
}

static bool _is_json_ws(char32_t c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// ── URL parsing for streaming ──────────────────────────────────────────────────
static bool yeet_parse_url(const String &p_url, String &r_host, int &r_port, String &r_path, bool &r_use_tls) {
	int scheme_end = p_url.find("://");
	if (scheme_end < 0) {
		return false;
	}
	const String scheme = p_url.substr(0, scheme_end).to_lower().strip_edges();
	r_use_tls = (scheme == "https");
	const String rest = p_url.substr(scheme_end + 3);
	int slash = rest.find("/");
	String host_part;
	if (slash < 0) {
		host_part = rest;
		r_path = "/";
	} else {
		host_part = rest.substr(0, slash);
		r_path = rest.substr(slash);
	}
	int colon = host_part.rfind(":");
	if (colon >= 0) {
		r_host = host_part.substr(0, colon).strip_edges();
		r_port = host_part.substr(colon + 1).strip_edges().to_int();
	} else {
		r_host = host_part.strip_edges();
		r_port = r_use_tls ? 443 : 80;
	}
	return !r_host.is_empty();
}

// Streaming used TLSOptions::client_unsafe() for all https:// URLs. In mbedTLS that clears the TLS
// hostname/SNI field, and many remote APIs (CDN, reverse proxy) require SNI to complete the handshake.
// Use TLSOptions::client() for public hosts (proper SNI + default CA bundle); keep unsafe for LAN/loopback.
static bool yeet_stream_host_prefers_insecure_tls(const String &p_host) {
	const String h = p_host.to_lower().strip_edges();
	if (h == "localhost" || h == "127.0.0.1" || h == "::1") {
		return true;
	}
	if (h.begins_with("127.")) {
		return true;
	}
	if (h.begins_with("10.")) {
		return true;
	}
	if (h.begins_with("192.168.")) {
		return true;
	}
	if (h.begins_with("172.")) {
		const PackedStringArray parts = h.split(".");
		if (parts.size() >= 2) {
			const int o2 = parts[1].to_int();
			if (o2 >= 16 && o2 <= 31) {
				return true;
			}
		}
	}
	return false;
}

static String yeet_describe_http_client_status(HTTPClient::Status p_s) {
	switch (p_s) {
		case HTTPClient::STATUS_CANT_RESOLVE:
			return TTR("Could not resolve the hostname (DNS). Check the URL and network.");
		case HTTPClient::STATUS_CANT_CONNECT:
			return TTR("TCP connect failed (refused or timeout). Check host, port, and that the inference server is running.");
		case HTTPClient::STATUS_TLS_HANDSHAKE_ERROR:
			return TTR("TLS handshake failed. Local servers (Ollama, LM Studio, vLLM) usually need http://127.0.0.1:PORT/... not https://. For a remote https:// API, check the URL in a browser, try another network, and disable VPN/firewall/proxy interference.");
		case HTTPClient::STATUS_CONNECTION_ERROR:
			return TTR("Connection error after connect.");
		default:
			return vformat(TTR("HTTP client status code %d."), (int)p_s);
	}
}
} // namespace

bool is_valid_input_action_name(const String &p_name) {
	if (p_name.is_empty()) {
		return false;
	}
	return !p_name.contains("/") && !p_name.contains(":") && !p_name.contains("=") && !p_name.contains("\\") && !p_name.contains("\"");
}

BaseMaterial3D::Transparency YeetAIDock::parse_transparency_mode(const String &p_value, bool &r_ok) {
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

	auto apply_log_theme = [&](RichTextLabel *log) {
		if (!log) {
			return;
		}
		if (font_size > 0) {
			log->add_theme_font_size_override(SNAME("normal_font_size"), font_size);
			log->add_theme_font_size_override(SNAME("bold_font_size"), font_size);
			log->add_theme_font_size_override(SNAME("italics_font_size"), font_size);
			log->add_theme_font_size_override(SNAME("mono_font_size"), MAX(11, font_size - 1));
		}
		log->add_theme_color_override(SNAME("default_color"), font_color);
		// Cursor-like chat density: tighter baseline rhythm.
		log->add_theme_constant_override(SNAME("line_separation"), int(2.0f * EDSCALE));
		log->add_theme_constant_override(SNAME("paragraph_separation"), int(4.0f * EDSCALE));
	};
	apply_log_theme(chat_log);
	apply_log_theme(stream_label);

	if (prompt_input && font_size > 0) {
		prompt_input->add_theme_font_size_override(SNAME("font_size"), font_size);
	}

	if (status_label) {
		if (font_size > 0) {
			status_label->add_theme_font_size_override(SNAME("font_size"), MAX(10, font_size - 1));
		}
		status_label->add_theme_color_override(SNAME("font_color"), font_muted);
	}

	if (_token_count_label) {
		if (font_size > 0) {
			_token_count_label->add_theme_font_size_override(SNAME("font_size"), MAX(10, font_size - 1));
		}
		_token_count_label->add_theme_color_override(SNAME("font_color"), font_muted);
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

	// Thin progress bar below header, visible during multi-tool round trips
	_tool_progress_bar = memnew(ProgressBar);
	_tool_progress_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	_tool_progress_bar->set_custom_minimum_size(Size2(0, 3) * EDSCALE);
	_tool_progress_bar->set_show_percentage(false);
	_tool_progress_bar->set_min(0.0);
	_tool_progress_bar->set_max(1.0);
	_tool_progress_bar->set_value(0.0);
	_tool_progress_bar->set_visible(false);
	_tool_progress_bar->set_modulate(Color(1, 1, 1, 0.8));
	main_column->add_child(_tool_progress_bar);

	MarginContainer *header_mc = memnew(MarginContainer);
	header_mc->add_theme_constant_override("margin_left", int(12.0f * EDSCALE));
	header_mc->add_theme_constant_override("margin_right", int(12.0f * EDSCALE));
	header_mc->add_theme_constant_override("margin_top", int(10.0f * EDSCALE));
	header_mc->add_theme_constant_override("margin_bottom", int(10.0f * EDSCALE));
	header_panel->add_child(header_mc);

	HBoxContainer *header_row = memnew(HBoxContainer);
	header_row->add_theme_constant_override("separation", int(4.0f * EDSCALE));
	header_mc->add_child(header_row);

	// Chat selector dropdown
	_chat_selector = memnew(OptionButton);
	_chat_selector->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	_chat_selector->set_custom_minimum_size(Size2(120, 0) * EDSCALE);
	_chat_selector->set_clip_text(true);
	_chat_selector->connect("item_selected", callable_mp(this, &YeetAIDock::_on_chat_selected));
	header_row->add_child(_chat_selector);

	// New chat button
	_new_chat_button = memnew(Button);
	_new_chat_button->set_theme_type_variation("FlatButton");
	_new_chat_button->set_tooltip_text(TTR("New Chat"));
	_new_chat_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_on_new_chat_pressed));
	header_row->add_child(_new_chat_button);

	// Delete chat button
	_delete_chat_button = memnew(Button);
	_delete_chat_button->set_theme_type_variation("FlatButton");
	_delete_chat_button->set_tooltip_text(TTR("Delete Chat"));
	_delete_chat_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_on_delete_chat_pressed));
	header_row->add_child(_delete_chat_button);

	// Separator
	HSeparator *header_sep = memnew(HSeparator);
	header_sep->set_custom_minimum_size(Size2(1, 20) * EDSCALE);
	header_sep->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	header_row->add_child(header_sep);

	// Agent selector
	_agent_selector = memnew(OptionButton);
	_agent_selector->set_custom_minimum_size(Size2(100, 0) * EDSCALE);
	_agent_selector->set_clip_text(true);
	_agent_selector->connect("item_selected", callable_mp(this, &YeetAIDock::_on_agent_selected));
	header_row->add_child(_agent_selector);

	// Model selector button
	_model_selector_button = memnew(Button);
	_model_selector_button->set_theme_type_variation("FlatButton");
	_model_selector_button->set_tooltip_text(TTR("Select Model"));
	_model_selector_button->set_clip_text(true);
	_model_selector_button->set_custom_minimum_size(Size2(80, 0) * EDSCALE);
	_model_selector_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_on_model_selector_pressed));
	header_row->add_child(_model_selector_button);

	// Status dot + label on the right
	_status_dot = memnew(ColorRect);
	_status_dot->set_custom_minimum_size(Size2(8, 8) * EDSCALE);
	_status_dot->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	_status_dot->set_color(Color(0.5f, 0.5f, 0.5f, 0.35f));
	header_row->add_child(_status_dot);

	status_label = memnew(Label);
	status_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	status_label->set_clip_text(true);
	status_label->set_text(TTR("Ready"));
	header_row->add_child(status_label);

	// Build agent presets and populate selectors
	_build_agent_presets();
	_create_new_chat();
	_update_chat_selector();

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

	chat_scroll = memnew(ScrollContainer);
	chat_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
	chat_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_SHOW_NEVER);
	chat_panel_mc->add_child(chat_scroll);

	MarginContainer *chat_margin = memnew(MarginContainer);
	chat_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_margin->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_scroll->add_child(chat_margin);

	// MarginContainer places every child in the same rect; two RichTextLabels would fully overlap.
	// Stack history + live stream vertically so prior messages and tool output stay visible.
	VBoxContainer *chat_column = memnew(VBoxContainer);
	chat_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_column->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_column->add_theme_constant_override("separation", int(6.0f * EDSCALE));
	chat_margin->add_child(chat_column);

	chat_log = memnew(RichTextLabel);
	chat_log->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_log->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_log->set_fit_content(true);
	chat_log->set_scroll_active(false);
	chat_log->set_selection_enabled(true);
	chat_log->set_context_menu_enabled(true);
	chat_log->set_use_bbcode(true);
	chat_column->add_child(chat_log);

	// Live streaming label — in-progress assistant tokens; sits below chat history while active
	stream_label = memnew(RichTextLabel);
	stream_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	stream_label->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
	stream_label->set_fit_content(true);
	stream_label->set_scroll_active(false);
	stream_label->set_use_bbcode(true);
	stream_label->set_visible(false);
	chat_column->add_child(stream_label);

	// Files-modified tracker label (RichTextLabel for meta links)
	_files_modified_label = memnew(RichTextLabel);
	_files_modified_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	_files_modified_label->set_fit_content(true);
	_files_modified_label->set_scroll_active(false);
	_files_modified_label->set_use_bbcode(true);
	_files_modified_label->set_selection_enabled(false);
	_files_modified_label->set_visible(false);
	_files_modified_label->connect("meta_clicked", callable_mp(this, &YeetAIDock::_on_files_meta_clicked));
	main_column->add_child(_files_modified_label);

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
	prompt_input->set_custom_minimum_size(Size2(0, 92) * EDSCALE);
	prompt_input->set_placeholder(TTR("Ask Crosshair AI... (Ctrl+Enter to send, Alt+Up/Down for history)"));
	prompt_input->connect("gui_input", callable_mp(this, &YeetAIDock::_on_prompt_gui_input));
	input_column->add_child(prompt_input);

	HBoxContainer *button_row = memnew(HBoxContainer);
	button_row->add_theme_constant_override("separation", int(8.0f * EDSCALE));
	button_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input_column->add_child(button_row);

	_token_count_label = memnew(Label);
	_token_count_label->set_h_size_flags(Control::SIZE_SHRINK_END);
	_token_count_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	_token_count_label->set_visible(false);
	button_row->add_child(_token_count_label);

	send_button = memnew(Button);
	send_button->set_text(TTR("Send"));
	send_button->set_theme_type_variation("FlatButton");
	send_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	send_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_send_prompt));
	button_row->add_child(send_button);

	// Stop button — visible only while the AI is generating
	stop_button = memnew(Button);
	stop_button->set_text(TTR("Stop"));
	stop_button->set_theme_type_variation("FlatButton");
	stop_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	stop_button->set_visible(false);
	stop_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_on_stop_pressed));
	button_row->add_child(stop_button);

	clear_button = memnew(Button);
	clear_button->set_theme_type_variation("FlatButton");
	clear_button->set_text(TTR("Clear"));
	clear_button->set_h_size_flags(Control::SIZE_SHRINK_END);
	clear_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_clear_chat));
	button_row->add_child(clear_button);

	request = memnew(HTTPRequest);
	request->set_use_threads(true);
	request->set_timeout(90.0);
	request->connect("request_completed", callable_mp(this, &YeetAIDock::_on_request_completed));
	add_child(request);

	_model_tags_request = memnew(HTTPRequest);
	_model_tags_request->set_use_threads(true);
	_model_tags_request->set_timeout(15.0);
	_model_tags_request->connect("request_completed", callable_mp(this, &YeetAIDock::_on_model_tags_request_completed));
	add_child(_model_tags_request);
}

YeetAIDock::~YeetAIDock() {
	_cancel_streaming();
	_save_all_chats();
}

void YeetAIDock::_on_files_meta_clicked(const Variant &p_meta) {
	const String meta = String(p_meta);
	if (meta.is_empty()) {
		return;
	}
	if (meta.begins_with("http://") || meta.begins_with("https://")) {
		OS::get_singleton()->shell_open(meta);
		return;
	}
	if (meta.begins_with("res://")) {
		EditorNode::get_singleton()->load_scene_or_resource(meta);
	}
}

void YeetAIDock::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_THEME_CHANGED) {
		_apply_dock_theme();
		if (p_what == NOTIFICATION_ENTER_TREE) {
			_load_chats();
			if (!_chat_sessions.is_empty()) {
				_switch_to_chat(_chat_sessions.size() - 1);
			}
		}
	}
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		if (!intro_message_added) {
			intro_message_added = true;
			_append_message("assistant", TTR("Crosshair AI is ready. I can inspect the project, scenes, selected nodes, and perform scene and script edits."));
		}
		send_button->set_button_icon(get_editor_theme_icon(SNAME("Play")));
		stop_button->set_button_icon(get_editor_theme_icon(SNAME("Stop")));
		clear_button->set_button_icon(get_editor_theme_icon(SNAME("Clear")));
		if (_new_chat_button) {
			_new_chat_button->set_button_icon(get_editor_theme_icon(SNAME("Add")));
		}
		if (_delete_chat_button) {
			_delete_chat_button->set_button_icon(get_editor_theme_icon(SNAME("Remove")));
		}
		if (_model_selector_button) {
			_model_selector_button->set_button_icon(get_editor_theme_icon(SNAME("GuiOptionArrow")));
			_update_model_button_label();
		}
	}
	if (p_what == NOTIFICATION_PROCESS) {
		_drain_stream_queue();
		_update_animation(get_process_delta_time());
	}
	if (p_what == NOTIFICATION_EXIT_TREE) {
		_cancel_streaming();
	}
}

void YeetAIDock::_on_game_screenshot_cb(int64_t p_w, int64_t p_h, const String &p_path, Rect2i p_rect) {
	(void)p_rect;
	game_screenshot_w = p_w;
	game_screenshot_h = p_h;
	game_screenshot_path = p_path;
	game_screenshot_done = true;
}

// ── Animation & live-stream UI ────────────────────────────────────────────────

void YeetAIDock::_update_animation(double p_delta) {
	_anim_time += float(p_delta);

	if (waiting_for_response && _status_dot) {
		const float alpha = 0.35f + 0.65f * (0.5f + 0.5f * Math::sin(_anim_time * 4.5f));
		Color dot_color = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
		dot_color.a = alpha;
		_status_dot->set_color(dot_color);
	}

	// Animate the progress bar fill when visible
	if (_tool_progress_bar && _tool_progress_bar->is_visible()) {
		const float indeterminate = 0.3f + 0.7f * (0.5f + 0.5f * Math::sin(_anim_time * 3.0f));
		_tool_progress_bar->set_value(indeterminate);
	}

	// Pulse the typing indicator in stream label
	if (waiting_for_response && stream_label && stream_label->is_visible() && _stream_accumulated.is_empty()) {
		// Only update the dots periodically (every ~0.5s) to avoid excessive redraws
		if (int(_anim_time * 2.0f) != int((_anim_time - float(p_delta)) * 2.0f)) {
			_update_stream_label();
		}
	}
}

void YeetAIDock::_update_stream_label() {
	if (!stream_label) {
		return;
	}

	const Color font_color = get_theme_color(SNAME("font_color"), EditorStringName(Editor));
	const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color success = get_theme_color(SNAME("success_color"), EditorStringName(Editor));
	const Color font_dim = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));

	stream_label->clear();

	// Role header
	stream_label->push_color(success);
	stream_label->push_bold();
	stream_label->add_text(TTR("Assistant"));
	stream_label->pop();
	stream_label->pop();
	stream_label->add_text("  ");
	{
		Ref<Texture2D> dot_icon = get_editor_theme_icon("GuiProgressBar");
		if (dot_icon.is_valid()) {
			stream_label->add_image(dot_icon, int(8 * EDSCALE), int(8 * EDSCALE));
		} else {
			stream_label->push_color(accent);
			stream_label->add_text("-");
			stream_label->pop();
		}
	}
	stream_label->append_text("\n");

	stream_label->push_indent(1);

	if (_stream_accumulated.is_empty()) {
		// Show animated thinking dots
		const int dot_count = 1 + (int(_anim_time * 2.0f) % 3);
		stream_label->push_color(font_dim);
		stream_label->push_italics();
		stream_label->add_text(TTR("Thinking"));
		for (int i = 0; i < dot_count; i++) {
			stream_label->add_text(".");
		}
		// Add subtle pulsing cursor
		const float cursor_alpha = 0.3f + 0.7f * (0.5f + 0.5f * Math::sin(_anim_time * 5.0f));
		stream_label->pop();
		stream_label->pop();
		stream_label->push_color(Color(accent.r, accent.g, accent.b, cursor_alpha));
		{
			Ref<Texture2D> cursor_icon = get_editor_theme_icon("Progress1");
			if (cursor_icon.is_valid()) {
				stream_label->add_image(cursor_icon, int(12 * EDSCALE), int(12 * EDSCALE));
			} else {
				stream_label->add_text("...");
			}
		}
		stream_label->pop();
	} else {
		// Streaming content
		stream_label->push_color(font_color);
		stream_label->append_text(_escape_bbcode(_stream_accumulated));
		// Blinking block cursor
		const float cursor_alpha = 0.4f + 0.6f * (0.5f + 0.5f * Math::sin(_anim_time * 6.0f));
		stream_label->push_color(Color(accent.r, accent.g, accent.b, cursor_alpha));
		{
			Ref<Texture2D> block_cursor_icon = get_editor_theme_icon("Play");
			if (block_cursor_icon.is_valid()) {
				stream_label->add_image(block_cursor_icon, int(10 * EDSCALE), int(14 * EDSCALE));
			} else {
				stream_label->add_text("|");
			}
		}
		stream_label->pop();
		stream_label->pop();
	}

	stream_label->pop(); // indent

	_scroll_to_bottom();
}

void YeetAIDock::_scroll_to_bottom() {
	if (chat_scroll) {
		// Deferred so layout is computed first
		callable_mp((ScrollContainer *)chat_scroll, &ScrollContainer::set_v_scroll)
				.call_deferred(INT32_MAX);
	}
}

void YeetAIDock::_on_stop_pressed() {
	_cancel_streaming();
	_set_waiting(false, TTR("Stopped"));
	stream_label->set_visible(false);
	stream_label->clear();

	if (!_stream_accumulated.is_empty()) {
		const String partial = _stream_accumulated + "\n\n" + TTR("[Stopped by user]");
		_append_message("assistant", partial);
		conversation_messages.append(make_message("assistant", _stream_accumulated));
	}
	_stream_accumulated = "";
}

void YeetAIDock::_append_status_row(const String &p_text) {
	const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color font_dim = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	const int base_fs = has_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			? get_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			: 0;

	if (base_fs > 0) {
		chat_log->push_font_size(MAX(10, int(base_fs * 0.82f)));
	}
	Ref<Texture2D> status_icon = get_editor_theme_icon("Refresh");
	if (status_icon.is_valid()) {
		chat_log->add_image(status_icon, int(14 * EDSCALE), int(14 * EDSCALE));
	}
	chat_log->add_text(" ");
	chat_log->push_color(font_dim);
	chat_log->push_italics();
	chat_log->add_text(p_text);
	chat_log->pop();
	chat_log->pop();
	if (base_fs > 0) {
		chat_log->pop();
	}
	chat_log->append_text("\n\n");
	_scroll_to_bottom();
}

String YeetAIDock::_icon_for_tool(const String &p_tool_name) const {
	const String s = p_tool_name.to_lower();
	if (s.contains("write") || s.contains("update_gdscript") || s.contains("create_gdscript")) {
		return "Edit";
	}
	if (s.contains("create") || s.contains("add_node") || s.contains("instantiate") || s.contains("add_primitive") || s.contains("add_collision")) {
		return "Add";
	}
	if (s.contains("delete") || s.contains("remove")) {
		return "Remove";
	}
	if (s.contains("move") || s.contains("rename") || s.contains("reparent")) {
		return "MoveUp";
	}
	if (s.contains("play") || s.contains("run")) {
		return "Play";
	}
	if (s.contains("stop_playing")) {
		return "Stop";
	}
	if (s.contains("capture") || s.contains("screenshot")) {
		return "Camera";
	}
	if (s.contains("save")) {
		return "Save";
	}
	if (s.contains("attach") || s.contains("connect") || s.contains("assign")) {
		return "Link";
	}
	if (s.contains("read") || s.contains("get") || s.contains("list") || s.contains("find") || s.contains("grep")) {
		return "Search";
	}
	return "Tool";
}

// ─────────────────────────────────────────────────────────────────────────────

void YeetAIDock::_send_prompt() {
	if (waiting_for_response) {
		return;
	}

	const String prompt = prompt_input->get_text().strip_edges();
	if (prompt.is_empty()) {
		return;
	}

	prompt_input->clear();
	if (!prompt.is_empty()) {
		if (_prompt_history.is_empty() || _prompt_history[_prompt_history.size() - 1] != prompt) {
			_prompt_history.push_back(prompt);
		}
		if (_prompt_history.size() > 100) {
			_prompt_history.remove_at(0);
		}
	}
	_prompt_history_index = -1;
	_prompt_history_draft = "";
	conversation_messages.append(make_message("user", prompt));
	_append_message("user", prompt);
	tool_round_trips = 0;
	turn_context_prompt = _build_runtime_context_prompt() + _build_task_hints_for_user_prompt(prompt);
	_update_chat_title();
	_update_token_counter();
	_request_model_response();
}

void YeetAIDock::_on_prompt_gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if (key->get_keycode() == Key::ENTER && key->is_ctrl_pressed()) {
			_send_prompt();
			get_viewport()->set_input_as_handled();
		} else if (key->get_keycode() == Key::UP && key->is_alt_pressed()) {
			_history_navigate(-1);
			get_viewport()->set_input_as_handled();
		} else if (key->get_keycode() == Key::DOWN && key->is_alt_pressed()) {
			_history_navigate(1);
			get_viewport()->set_input_as_handled();
		}
	}
}

void YeetAIDock::_clear_chat() {
	_cancel_streaming();
	_set_waiting(false, TTR("Ready"));

	conversation_messages.clear();
	turn_context_prompt = String();
	chat_log->clear();
	stream_label->set_visible(false);
	stream_label->clear();
	_stream_accumulated = "";
	_chat_records.clear();
	_session_modified_files.clear();
	_update_token_counter();
	_update_files_modified_label();

	if (_active_chat_index >= 0 && _active_chat_index < _chat_sessions.size()) {
		_chat_sessions.write[_active_chat_index].messages.clear();
		_chat_sessions.write[_active_chat_index].records.clear();
		_chat_sessions.write[_active_chat_index].title = TTR("New Chat");
		_update_chat_selector();
	}

	_append_message("assistant", TTR("Chat cleared. Ask me about the current project."));
}

void YeetAIDock::_append_message(const String &p_role, const String &p_text) {
	const Color font_base = get_theme_color(SNAME("font_color"), EditorStringName(Editor));
	const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color success = get_theme_color(SNAME("success_color"), EditorStringName(Editor));
	const Color dim = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	const Color warning = get_theme_color(SNAME("warning_color"), EditorStringName(Editor));

	String label;
	String role_icon_name;
	if (p_role == "user") {
		label = TTR("You");
		role_icon_name = "User";
	} else if (p_role == "assistant") {
		label = TTR("Assistant");
		role_icon_name = "AI";
	} else if (p_role == "tool") {
		label = TTR("Tool");
		role_icon_name = "Tool";
	} else {
		label = p_role.capitalize();
		role_icon_name = "GuiOptionArrow";
	}

	const Color label_color = (p_role == "user") ? accent : ((p_role == "assistant") ? success : ((p_role == "tool") ? warning : dim));
	const Color bar_color = (p_role == "user") ? Color(accent.r, accent.g, accent.b, 0.5f) : Color(success.r, success.g, success.b, 0.35f);
	const int base_fs = has_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			? get_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			: 14;
	const int label_fs = MAX(10, int(base_fs * 0.84f));
	const int caption_fs = MAX(10, int(base_fs * 0.82f));

	// Left colored accent bar
	chat_log->push_color(bar_color);
	chat_log->push_font_size(caption_fs);
	chat_log->add_text("| ");
	chat_log->pop();
	chat_log->pop();

	// Role icon + label
	Ref<Texture2D> role_icon_tex = get_editor_theme_icon(role_icon_name);
	if (role_icon_tex.is_valid()) {
		chat_log->add_image(role_icon_tex, int(16 * EDSCALE), int(16 * EDSCALE));
	}
	chat_log->add_text(" ");
	chat_log->push_color(label_color);
	chat_log->push_font_size(label_fs);
	chat_log->push_bold();
	chat_log->add_text(label);
	chat_log->pop();
	chat_log->pop();
	chat_log->pop();

	chat_log->add_text("  ");
	{
		Ref<Texture2D> sep_icon = get_editor_theme_icon("GuiProgressBar");
		if (sep_icon.is_valid()) {
			chat_log->add_image(sep_icon, int(8 * EDSCALE), int(8 * EDSCALE));
		} else {
			chat_log->push_color(dim);
			chat_log->add_text("-");
			chat_log->pop();
		}
	}
	chat_log->append_text("\n");

	// Message body with indent
	chat_log->push_indent(1);
	chat_log->push_color(font_base);
	chat_log->append_text(_escape_bbcode(p_text));
	chat_log->pop();
	chat_log->pop();

	chat_log->append_text("\n\n");
	_scroll_to_bottom();
}

void YeetAIDock::_append_tool_running(const String &p_tool_name, const Dictionary &p_args) {
	const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color font_dim = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	const Color font_base = get_theme_color(SNAME("font_color"), EditorStringName(Editor));

	const int base_fs = has_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			? get_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			: 14;
	const int caption_fs = MAX(10, int(base_fs * 0.82f));
	const int small_fs = MAX(9, int(base_fs * 0.76f));

	const String title = _humanize_tool_name(p_tool_name);
	const String icon = _icon_for_tool(p_tool_name);
	const Vector<String> paths = _collect_relevant_paths(p_args, Dictionary());

	// Left colored bar (accent, indicating in-progress)
	chat_log->push_color(Color(accent.r, accent.g, accent.b, 0.5f));
	chat_log->push_font_size(caption_fs);
	chat_log->add_text("| ");
	chat_log->pop();
	chat_log->pop();

	// Spinner icon
	Ref<Texture2D> spinner_icon = get_editor_theme_icon("Progress1");
	if (spinner_icon.is_valid()) {
		chat_log->add_image(spinner_icon, int(14 * EDSCALE), int(14 * EDSCALE));
	}
	chat_log->add_text(" ");

	// Tool name
	chat_log->push_font_size(caption_fs);
	chat_log->push_color(font_base);
	chat_log->push_bold();
	chat_log->add_text(title);
	chat_log->pop();
	chat_log->pop();
	chat_log->pop();

	// Path
	if (!paths.is_empty()) {
		chat_log->add_text("  ");
		chat_log->push_color(accent);
		chat_log->push_mono();
		chat_log->push_font_size(small_fs);
		const String p = paths[0];
		const int last_slash = p.rfind("/");
		const String short_path = (last_slash > 4) ? ("..." + p.substr(last_slash)) : p;
		chat_log->add_text(short_path);
		chat_log->pop();
		chat_log->pop();
		chat_log->pop();
	}

	// Running status pill
	chat_log->add_text("  ");
	chat_log->push_color(accent);
	chat_log->push_font_size(caption_fs);
	chat_log->push_italics();
	chat_log->add_text("Running...");
	chat_log->pop();
	chat_log->pop();
	chat_log->pop();

	chat_log->append_text("\n\n");
	_scroll_to_bottom();
}

void YeetAIDock::_append_tool_result(const String &p_tool_name, const Dictionary &p_args, const ToolExecutionResult &p_result) {
	const Color font_base = get_theme_color(SNAME("font_color"), EditorStringName(Editor));
	const Color font_dim = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color success = get_theme_color(SNAME("success_color"), EditorStringName(Editor));
	const Color warning = get_theme_color(SNAME("warning_color"), EditorStringName(Editor));
	const Color error = get_theme_color(SNAME("error_color"), EditorStringName(Editor));

	const int base_fs = has_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			? get_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			: 14;
	const int caption_fs = MAX(10, int(base_fs * 0.82f));
	const int small_fs = MAX(9, int(base_fs * 0.76f));

	const String title = _humanize_tool_name(p_tool_name);
	const String icon = _icon_for_tool(p_tool_name);
	const Vector<String> paths = _collect_relevant_paths(p_args, p_result.payload);

	// ── Card: left bar + icon + title + path + status ──
	// Left colored bar (2 chars wide, using block element)
	chat_log->push_color(p_result.ok ? Color(success.r, success.g, success.b, 0.5f) : Color(error.r, error.g, error.b, 0.5f));
	chat_log->push_font_size(caption_fs);
	chat_log->add_text("| ");
	chat_log->pop();
	chat_log->pop();

	// Icon (editor theme icon)
	Ref<Texture2D> icon_tex = get_editor_theme_icon(icon);
	if (icon_tex.is_valid()) {
		chat_log->add_image(icon_tex, int(16 * EDSCALE), int(16 * EDSCALE));
	}
	chat_log->add_text(" ");

	// Tool action label
	chat_log->push_font_size(caption_fs);
	chat_log->push_color(font_base);
	chat_log->push_bold();
	chat_log->add_text(title);
	chat_log->pop();
	chat_log->pop();
	chat_log->pop();

	// Primary path inline (accent colored, monospace)
	if (!paths.is_empty()) {
		chat_log->add_text("  ");
		chat_log->push_color(accent);
		chat_log->push_mono();
		chat_log->push_font_size(small_fs);
		const String p = paths[0];
		const int last_slash = p.rfind("/");
		const String short_path = (last_slash > 4) ? ("..." + p.substr(last_slash)) : p;
		chat_log->add_text(short_path);
		chat_log->pop();
		chat_log->pop();
		chat_log->pop();
		if (paths.size() > 1) {
			chat_log->push_color(font_dim);
			chat_log->push_font_size(small_fs);
			chat_log->add_text(vformat(" +%d", paths.size() - 1));
			chat_log->pop();
			chat_log->pop();
		}
	}

	// Status pill: ✓ Done or ✗ Failed
	chat_log->add_text("  ");
	if (p_result.ok) {
		Ref<Texture2D> ok_icon = get_editor_theme_icon("StatusSuccess");
		if (ok_icon.is_valid()) {
			chat_log->add_image(ok_icon, int(14 * EDSCALE), int(14 * EDSCALE));
		} else {
			chat_log->push_color(Color(0.2f, 0.7f, 0.3f));
			chat_log->push_font_size(caption_fs);
			chat_log->push_bold();
			chat_log->add_text("Done");
			chat_log->pop();
			chat_log->pop();
			chat_log->pop();
		}
	} else {
		Ref<Texture2D> fail_icon = get_editor_theme_icon("StatusError");
		if (fail_icon.is_valid()) {
			chat_log->add_image(fail_icon, int(14 * EDSCALE), int(14 * EDSCALE));
		} else {
			chat_log->push_color(Color(0.9f, 0.3f, 0.3f));
			chat_log->push_font_size(caption_fs);
			chat_log->push_bold();
			chat_log->add_text("Failed");
			chat_log->pop();
			chat_log->pop();
			chat_log->pop();
		}
	}

	chat_log->append_text("\n");

	// ── Additional paths ──
	if (paths.size() > 1) {
		chat_log->push_indent(1);
		chat_log->push_font_size(small_fs);
		for (int i = 1; i < paths.size(); i++) {
			chat_log->push_color(font_dim);
			chat_log->add_text("  -> ");
			chat_log->pop();
			chat_log->push_color(accent);
			chat_log->push_mono();
			chat_log->add_text(paths[i]);
			chat_log->pop();
			chat_log->pop();
			chat_log->append_text("\n");
		}
		chat_log->pop();
		chat_log->pop();
	}

	// ── Output preview ──
	const String preview = _truncate_preview(p_result.display_text, 280);
	if (!preview.is_empty() && !p_result.ok) {
		chat_log->push_indent(1);
		chat_log->push_font_size(small_fs);
		chat_log->push_color(Color(error.r, error.g, error.b, 0.85f));
		chat_log->push_mono();
		chat_log->append_text(_escape_bbcode(preview));
		chat_log->pop();
		chat_log->pop();
		chat_log->pop();
		chat_log->pop();
		chat_log->append_text("\n");
	} else if (!preview.is_empty()) {
		const String one_line = _truncate_preview(preview.replace("\n", " "), 120);
		if (!one_line.strip_edges().is_empty()) {
			chat_log->push_indent(1);
			chat_log->push_font_size(small_fs);
			chat_log->push_color(Color(font_dim.r, font_dim.g, font_dim.b, 0.7f));
			chat_log->push_mono();
			chat_log->append_text(_escape_bbcode(one_line));
			chat_log->pop();
			chat_log->pop();
			chat_log->pop();
			chat_log->pop();
			chat_log->append_text("\n");
		}
	}

	chat_log->append_text("\n");
	_scroll_to_bottom();
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
		"path", "scene_path", "script_path", "packed_scene_path", "resource_path", "owner_scene_path", "material_path", "main_scene",
		"from_path", "to_path"
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
			if (!out.has(path)) {
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
	return p_text.substr(0, p_max_chars) + "...";
}

void YeetAIDock::_set_waiting(bool p_waiting, const String &p_status) {
	waiting_for_response = p_waiting;

	send_button->set_visible(!p_waiting);
	stop_button->set_visible(p_waiting);

	status_label->set_text(p_status);

	if (_status_dot) {
		if (p_waiting) {
			Color dot = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
			dot.a = 1.0f;
			_status_dot->set_color(dot);
		} else {
			_status_dot->set_color(Color(0.5f, 0.5f, 0.5f, 0.35f));
		}
	}

	// Show/hide the progress bar during active processing
	if (_tool_progress_bar) {
		_tool_progress_bar->set_visible(p_waiting);
		_tool_progress_bar->set_value(p_waiting ? 0.5f : 0.0f);
	}

	set_process(p_waiting);
	if (!p_waiting) {
		_anim_time = 0.0f;
	}
}

void YeetAIDock::_request_model_response() {
	// 0 = Berry (OpenAI-compatible), 1 = Gemini, 2 = OpenRouter, 3 = Yeet Models (in-house Ollama-compatible).
	const int provider = _get_editor_setting_int("yeet_ai/chat/provider", 0);
	static const char *k_gemini_openai_url = "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions";
	static const char *k_openrouter_url = "https://openrouter.ai/api/v1/chat/completions";
	static const char *k_yeet_chat_default = "https://gpt.yeetlabs.fun/v1/chat/completions";

	String endpoint;
	String api_key;
	if (provider == 1) {
		endpoint = String::utf8(k_gemini_openai_url);
		api_key = _get_editor_setting_string("yeet_ai/chat/gemini_api_key", "");
	} else if (provider == 2) {
		endpoint = String::utf8(k_openrouter_url);
		api_key = _get_editor_setting_string("yeet_ai/chat/openrouter_api_key", "");
	} else if (provider == 3) {
		endpoint = _get_editor_setting_string("yeet_ai/chat/yeet_chat_url", k_yeet_chat_default);
		if (endpoint.strip_edges().is_empty()) {
			endpoint = String::utf8(k_yeet_chat_default);
		}
		api_key = _get_editor_setting_string("yeet_ai/chat/yeet_api_key", "");
	} else {
		endpoint = _get_editor_setting_string("yeet_ai/chat/completions_url", "https://llm.adityaberry.me/v1/chat/completions");
		api_key = _get_editor_setting_string("yeet_ai/chat/api_key", "");
	}

	String model = _get_editor_setting_string("yeet_ai/chat/model", "berrymodel");
	// Berry (provider 0) is fixed to the server’s model id (Ollama-style tags list: `berrymodel`).
	if (provider == 0) {
		model = "berrymodel";
	} else if (provider == 1 && model.strip_edges().is_empty()) {
		model = "gemini-2.0-flash";
	} else if (provider == 2 && model.strip_edges().is_empty()) {
		model = "qwen/qwen3.6-plus:free";
	} else if (provider == 3 && model.strip_edges().is_empty()) {
		model = "qwen3-coder:latest";
	}

	if (provider == 0 && endpoint.strip_edges().is_empty()) {
		_append_message("assistant", TTR("The LLM endpoint is empty. Set `yeet_ai/chat/completions_url` in Editor Settings → Crosshair, or pick a provider."));
		return;
	}
	if (provider == 1 && api_key.strip_edges().is_empty()) {
		_append_message("assistant", TTR("Google Gemini is selected but `yeet_ai/chat/gemini_api_key` is empty. Add your API key in Editor Settings → Crosshair."));
		return;
	}
	if (provider == 2 && api_key.strip_edges().is_empty()) {
		_append_message("assistant", TTR("OpenRouter is selected but `yeet_ai/chat/openrouter_api_key` is empty. Add your API key in Editor Settings → Crosshair."));
		return;
	}

	const int max_tokens = _get_editor_setting_int("yeet_ai/chat/max_tokens", 32768);
	const float temperature = _get_editor_setting_float("yeet_ai/chat/temperature", 0.25f);

	Dictionary payload;
	payload["model"] = model;
	payload["max_tokens"] = max_tokens;
	// temperature < 0 omits the field (use server default). Otherwise prefer ~0.2–0.35 for structured JSON (Qwen, etc.).
	if (temperature >= 0.0f) {
		payload["temperature"] = temperature;
	}

	Array messages;
	String system_prompt = _build_system_prompt();
	const String agent_id = _get_current_agent_id();
	if (agent_id != "general") {
		for (int i = 0; i < _agent_presets.size(); i++) {
			if (_agent_presets[i].id == agent_id && !_agent_presets[i].system_suffix.is_empty()) {
				system_prompt += _agent_presets[i].system_suffix;
				break;
			}
		}
	}
	messages.append(make_message("system", system_prompt));
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
	if (provider == 2) {
		// OpenRouter optional attribution (see https://openrouter.ai/docs).
		headers.push_back("X-Title: Crosshair");
	}

	// Enable SSE streaming — all OpenAI-compatible providers support this.
	payload["stream"] = true;

	_stream_endpoint = endpoint;
	_stream_req_headers = headers;
	_stream_req_body = JSON::stringify(payload);
	_set_waiting(true, TTR("Thinking..."));
	_start_streaming();
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

	String finish_reason;
	const Array choices = response_json.get("choices", Array());
	if (!choices.is_empty() && choices[0].get_type() == Variant::DICTIONARY) {
		const Dictionary choice0 = choices[0];
		finish_reason = String(choice0.get("finish_reason", "")).strip_edges();
	}
	if (finish_reason == "length") {
		_append_message("assistant",
				TTR("The API stopped at the output token limit (finish_reason=length). Raise Editor Setting `yeet_ai/chat/max_tokens` and increase your inference server’s max output / max_new_tokens (defaults are often far below 32k). Do not put GDScript and editor tools in the same `batch_tool_calls`; use a scene-only batch first, then a GDScript-only batch."));
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
		_append_tool_running(tool_name, args);
		ToolExecutionResult result = _execute_tool(tool_name, args);
		conversation_messages.append(make_message("assistant", JSON::stringify(envelope)));
		_append_tool_result(tool_name, args, result);

		if (result.ok) {
			const Vector<String> modified = _collect_relevant_paths(args, result.payload);
			for (const String &p : modified) {
				if (!_session_modified_files.has(p)) {
					_session_modified_files.insert(p);
				}
			}
			_update_files_modified_label();
		}

		Dictionary tool_payload = result.payload;
		tool_payload["ok"] = result.ok && !result.payload.has("error");
		conversation_messages.append(_make_user_message_with_optional_vision(tool_name, tool_payload));

		tool_round_trips++;
		_update_tool_progress_bar();
		// Show a subtle planning indicator before the next model call
		_append_status_row(TTR("Planning next moves..."));
		_request_model_response();
		return;
	}

	if (type == "final") {
		_set_waiting(false, TTR("Ready"));
		const String message = envelope.get("message", "");
		conversation_messages.append(make_message("assistant", message.is_empty() ? p_content : message));
		_append_message("assistant", message.is_empty() ? p_content : message);
		_update_token_counter();
		return;
	}

	_set_waiting(false, TTR("Ready"));
	conversation_messages.append(make_message("assistant", p_content));
	_append_message("assistant", p_content);
	_update_token_counter();
}

String YeetAIDock::_build_runtime_context_prompt() const {
	Dictionary context;
	context["project_settings"] = _tool_get_project_settings(Dictionary());
	const Dictionary _empty_tool_args;
	context["open_scenes"] = _tool_get_open_scenes(_empty_tool_args);
	context["current_scene"] = _tool_get_current_scene(_empty_tool_args);
	context["selected_nodes"] = _tool_get_selected_nodes(_empty_tool_args);

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

String YeetAIDock::_build_task_hints_for_user_prompt(const String &p_user_prompt) const {
	const String s = p_user_prompt.to_lower();
	if (s.is_empty()) {
		return String();
	}

	const bool wants_move =
			s.contains("wasd") || s.contains("move") || s.contains("control") || s.contains("controller") ||
			s.contains("character") || s.contains("player") || s.contains("walk") || s.contains("arrow") ||
			s.contains("keyboard");
	const bool wants_jump = s.contains("jump") || s.contains("space");
	const bool wants_color =
			s.contains("color") || s.contains("colour") || s.contains("material") || s.contains("tint") ||
			s.contains("blue") || s.contains("red") || s.contains("green") || s.contains("yellow");

	if (!wants_move && !wants_jump && !wants_color) {
		return String();
	}

	String out = String("\n\n") +
			"--- Task hints (auto-generated from the user message) ---\n";

	if (wants_move || wants_jump) {
		out += "Controls / movement: Do NOT respond with only `add_primitive_mesh` or mesh + collision alone. "
			   "A playable character needs input actions, a physics body (e.g. CharacterBody3D), collision, and a GDScript that reads Input actions in `_physics_process` or `_input`. "
			   "**Required split:** one `batch_tool_calls` with only editor/scene tools (`create_input_action`, nodes, mesh, material, `attach_script`, etc.) — **never** `create_gdscript_file` or `update_gdscript_file` there. "
			   "A **separate** tool_call round with a batch containing **only** `create_gdscript_file` and/or `update_gdscript_file` for the script body (mixing is rejected by the editor). "
			   "If the user asked for jump, bind Space (and/or an action name) explicitly.\n";
	}

	if (wants_color) {
		out += "Color / material: `add_primitive_mesh` does not set color by itself. Use `create_standard_material` (albedo color) and `assign_resource_to_property` or assign `material_override` on the MeshInstance3D.\n";
	}

	out += "Completeness: If the user asked for several things (e.g. controls + color + capsule), address every part across `batch_tool_calls` and follow-up tool_call rounds if needed—do not omit scripts, input, collision, or materials. Prefer multiple smaller rounds over one truncated JSON.\n";
	return out;
}

// _execute_tool is in yeet_ai_tools.cpp

String YeetAIDock::_get_editor_setting_string(const String &p_setting, const String &p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	// Use Object::get(), not get_setting(): get_setting() applies per-project editor overrides from
	// ProjectSettings, which would mask globally saved API keys when switching projects.
	bool valid = false;
	const Variant v = settings->get(StringName(p_setting), &valid);
	if (!valid) {
		return p_default;
	}
	return String(v);
}

// _get_editor_setting_int/bool are in yeet_ai_helpers.cpp

float YeetAIDock::_get_editor_setting_float(const String &p_setting, float p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	bool valid = false;
	const Variant v = settings->get(StringName(p_setting), &valid);
	if (!valid) {
		return p_default;
	}
	return float(v);
}

// _get_editor_setting_bool is in yeet_ai_helpers.cpp

Dictionary YeetAIDock::_make_user_message_with_optional_vision(const String &p_tool_name, const Dictionary &p_tool_payload) const {
	const bool vision = _get_editor_setting_bool("yeet_ai/chat/vision_enabled", false);
	const int max_b64 = _get_editor_setting_int("yeet_ai/chat/max_base64_chars", 2000000);

	Dictionary payload_for_text = p_tool_payload.duplicate();
	if (payload_for_text.has("png_base64")) {
		const String b64 = String(payload_for_text["png_base64"]);
		payload_for_text["png_base64"] = vformat("<png base64 omitted in text; %d chars>", b64.length());
	}

	const String text_body = vformat(
			"Tool `%s` finished with the following JSON result:\n%s\n\nIf you need more context, return another tool_call JSON object. Otherwise return a final JSON object.",
			p_tool_name,
			JSON::stringify(payload_for_text, "\t", false, true));

	if (!vision || !p_tool_payload.has("png_base64")) {
		return make_message("user", text_body);
	}

	const String b64 = String(p_tool_payload["png_base64"]);
	if (b64.length() > max_b64) {
		const String note = vformat(
				"\n\n[vision] Image base64 is too large (%d chars; max %d). Enable smaller captures or raise `yeet_ai/chat/max_base64_chars`. Text-only context follows.",
				b64.length(),
				max_b64);
		return make_message("user", text_body + note);
	}

	Array content;
	Dictionary text_part;
	text_part["type"] = "text";
	text_part["text"] = text_body;
	content.push_back(text_part);

	Dictionary img_part;
	img_part["type"] = "image_url";
	Dictionary img_url;
	img_url["url"] = "data:image/png;base64," + b64;
	img_part["image_url"] = img_url;
	content.push_back(img_part);

	Dictionary msg;
	msg["role"] = "user";
	msg["content"] = content;
	return msg;
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


// ═══════════════════════════════════════════════════════════════════════════
// Tool implementations have been moved to yeet_ai_tools_*.cpp files:
//   yeet_ai_tools_scene_query.cpp    - get_project_tree, get_open_scenes, get_current_scene, get_selected_nodes, get_project_settings, get_input_actions, get_scene_tree, get_node_details, get_node_api, get_autoloads
//   yeet_ai_tools_inspect.cpp        - find_project_files, file_exists, list_directory, grep_project_files, get_node_groups, get_node_collision_layers, get_animation_player_state, get_tilemap_info, get_navigation_region_info, open_scene, save_current_scene, save_all_scenes, reload_scene, select_file, get_unsaved_scenes
//   yeet_ai_tools_file_ops.cpp       - create_scene_file, create_gdscript_file, update_gdscript_file, write_project_file, create_project_folder, delete_project_file, move_project_file
//   yeet_ai_tools_node_create.cpp    - add_node, instantiate_scene, add_primitive_mesh, add_collision_shape, create_standard_material
//   yeet_ai_tools_node_ops.cpp       - remove_node, set_node_property, reparent_node, rename_node, duplicate_node, move_child
//   yeet_ai_tools_signal_project.cpp - assign_resource_to_property, connect_signal, create_input_action, set_main_scene, play_current_scene, play_main_scene, stop_playing_scene, set_editor_main_screen, edit_script
//   yeet_ai_tools_batch_capture.cpp  - batch_tool_calls, get_editor_log, editor_undo
//   yeet_ai_tools_capture.cpp        - capture_editor_viewport, capture_game_viewport, get_runtime_debugger_state, get_debug_snapshot
// ═══════════════════════════════════════════════════════════════════════════
// _build_system_prompt is in yeet_ai_system_prompt.cpp

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

bool YeetAIDock::_try_merge_adjacent_tool_call_json(const String &p_cleaned, Dictionary &r_envelope) const {
	const String &s = p_cleaned;
	for (int p = 0; p < s.length(); p++) {
		if (s[p] != '{') {
			continue;
		}
		const int e1 = find_json_object_end(s, p);
		if (e1 == -1) {
			continue;
		}
		Dictionary d1;
		if (!_parse_json_dictionary_quiet(s.substr(p, e1 - p + 1), d1)) {
			continue;
		}
		int q = e1 + 1;
		while (q < s.length() && _is_json_ws(s[q])) {
			q++;
		}
		if (q < s.length() && s[q] == ',') {
			q++;
		}
		while (q < s.length() && _is_json_ws(s[q])) {
			q++;
		}
		if (q >= s.length() || s[q] != '{') {
			continue;
		}
		const int e2 = find_json_object_end(s, q);
		if (e2 == -1) {
			continue;
		}
		Dictionary d2;
		if (!_parse_json_dictionary_quiet(s.substr(q, e2 - q + 1), d2)) {
			continue;
		}
		if (_merge_tool_call_json_pair(d1, d2, r_envelope)) {
			return true;
		}
		if (_merge_tool_call_json_pair(d2, d1, r_envelope)) {
			return true;
		}
	}
	return false;
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
	for (int strip_pass = 0; strip_pass < 8; strip_pass++) {
		bool any = false;
		for (const char *token : stop_tokens) {
			const String tok(token);
			if (cleaned.ends_with(tok)) {
				cleaned = cleaned.substr(0, cleaned.length() - tok.length()).strip_edges();
				any = true;
			}
		}
		if (!any) {
			break;
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
		Dictionary norm = _normalize_envelope(parsed);
		if (_envelope_normalized_is_dispatchable(norm)) {
			return norm;
		}
	}

	// Two-object split: {"arguments":{...}}, {"tool":"...","type":"tool_call"}
	if (_try_merge_adjacent_tool_call_json(cleaned, parsed)) {
		return _normalize_envelope(parsed);
	}

	// Reasoning models often emit commentary before the JSON tool payload. Prefer balanced
	// extraction from known markers (try last match first — JSON is usually at the end).
	static const char *json_markers[] = {
		"{\"type\":\"tool_call\"",
		"{\"type\": \"tool_call\"",
		"{\"tool\":",
		"{\"tool\": ",
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
				Dictionary norm = _normalize_envelope(parsed);
				if (_envelope_normalized_is_dispatchable(norm)) {
					return norm;
				}
			}
		}
	}

	const int json_start = cleaned.find("{");
	const int json_end = cleaned.rfind("}");
	if (json_start != -1 && json_end != -1 && json_end > json_start) {
		if (_parse_json_dictionary_quiet(cleaned.substr(json_start, json_end - json_start + 1), parsed)) {
			Dictionary norm = _normalize_envelope(parsed);
			if (_envelope_normalized_is_dispatchable(norm)) {
				return norm;
			}
		}
	}

	// Prefer later JSON objects (tool payloads usually follow commentary).
	for (int p = cleaned.length() - 1; p >= 0; p--) {
		if (cleaned[p] != '{') {
			continue;
		}
		const int obj_end = find_json_object_end(cleaned, p);
		if (obj_end == -1) {
			continue;
		}
		const String sub = cleaned.substr(p, obj_end - p + 1);
		if (!_parse_json_dictionary_quiet(sub, parsed)) {
			continue;
		}
		Dictionary norm = _normalize_envelope(parsed);
		if (_envelope_normalized_is_dispatchable(norm)) {
			return norm;
		}
	}

	Dictionary fallback;
	fallback["type"] = "final";
	// Truncated or malformed tool JSON is common when max_tokens is too low for batch_tool_calls.
	if (cleaned.contains("\"type\":\"tool_call\"") || cleaned.contains("\"batch_tool_calls\"") || cleaned.contains("batch_tool_calls")) {
		fallback["message"] = TTR("The model output was not valid JSON. Common causes: (1) response truncated by the completion token limit, (2) reasoning / commentary before the JSON that confused an older parser (try rebuilding), (3) malformed JSON from the model.\n\n"
				"In Editor Settings, search for `yeet_ai` and raise `yeet_ai/chat/max_tokens` (32768 or higher for large batches). On the inference server, raise max output tokens too—client settings do nothing if the server caps lower. Never mix `create_gdscript_file`/`update_gdscript_file` with other tools in one `batch_tool_calls`; use a GDScript-only batch in a follow-up tool_call round.\n\n"
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

// _variant_array_to_string_vector and _is_allowed_text_file are in yeet_ai_helpers.cpp

void YeetAIDock::_build_agent_presets() {
	_agent_presets.clear();

	AgentPreset general;
	general.id = "general";
	general.name = TTR("General");
	general.system_suffix = "";
	_agent_presets.push_back(general);

	AgentPreset coder;
	coder.id = "coder";
	coder.name = TTR("Coder");
	coder.system_suffix = "\nYou are in coding mode. Focus on writing clean, idiomatic GDScript. Prefer concise implementations. When creating gameplay, always include proper input handling, physics, and game loop logic.";
	_agent_presets.push_back(coder);

	AgentPreset architect;
	architect.id = "architect";
	architect.name = TTR("Architect");
	architect.system_suffix = "\nYou are in architecture mode. Focus on scene structure, node hierarchies, and component design. Prefer modular scenes, reusable PackedScenes, and clean separation of concerns. Document your design decisions.";
	_agent_presets.push_back(architect);

	AgentPreset artist;
	artist.id = "artist";
	artist.name = TTR("Artist");
	artist.system_suffix = "\nYou are in art mode. Focus on visual quality: materials, lighting, shaders, environment, and camera work. Prefer visually polished results with proper lighting setups and material properties.";
	_agent_presets.push_back(artist);

	AgentPreset debugger;
	debugger.id = "debugger";
	debugger.name = TTR("Debugger");
	debugger.system_suffix = "\nYou are in debugging mode. Focus on finding and fixing issues. Start by reading relevant scripts and logs, inspect node states, check signal connections, and verify physics layers. Be methodical and explain your reasoning.";
	_agent_presets.push_back(debugger);

	if (_agent_selector) {
		_agent_selector->clear();
		for (int i = 0; i < _agent_presets.size(); i++) {
			_agent_selector->add_item(_agent_presets[i].name);
		}
		_agent_selector->select(0);
	}
}

void YeetAIDock::_on_agent_selected(int p_index) {
	// Agent selection takes effect on the next message sent
}

String YeetAIDock::_get_current_agent_id() const {
	if (_agent_selector && _agent_selector->get_selected_id() >= 0) {
		const int idx = _agent_selector->get_selected();
		if (idx >= 0 && idx < _agent_presets.size()) {
			return _agent_presets[idx].id;
		}
	}
	return "general";
}

void YeetAIDock::_on_chat_selected(int p_index) {
	if (p_index < 0 || p_index >= _chat_sessions.size()) {
		return;
	}
	_switch_to_chat(p_index);
}

void YeetAIDock::_on_new_chat_pressed() {
	_create_new_chat();
}

void YeetAIDock::_on_delete_chat_pressed() {
	if (_chat_sessions.size() <= 1) {
		return;
	}
	_chat_sessions.remove_at(_active_chat_index);
	if (_active_chat_index >= _chat_sessions.size()) {
		_active_chat_index = _chat_sessions.size() - 1;
	}
	_switch_to_chat(_active_chat_index);
	_update_chat_selector();
	_save_all_chats();
}

void YeetAIDock::_create_new_chat() {
	ChatSession session;
	session.id = String::num_int64(OS::get_singleton()->get_unix_time());
	session.title = TTR("New Chat");
	session.agent_preset = _get_current_agent_id();
	session.created_at = OS::get_singleton()->get_unix_time();
	session.updated_at = session.created_at;
	_chat_sessions.push_back(session);
	_active_chat_index = _chat_sessions.size() - 1;
	_switch_to_chat(_active_chat_index);
	_update_chat_selector();
}

void YeetAIDock::_switch_to_chat(int p_index) {
	if (p_index < 0 || p_index >= _chat_sessions.size()) {
		return;
	}

	// Save current chat state
	if (_active_chat_index >= 0 && _active_chat_index < _chat_sessions.size()) {
		ChatSession &old = _chat_sessions.write[_active_chat_index];
		old.messages = conversation_messages;
		old.records = _chat_records;
	}

	_cancel_streaming();
	_set_waiting(false, TTR("Ready"));

	_active_chat_index = p_index;
	const ChatSession &session = _chat_sessions[p_index];
	conversation_messages = session.messages;
	_chat_records = session.records;
	tool_round_trips = 0;
	turn_context_prompt = String();
	_session_modified_files.clear();

	// Rebuild the chat log
	chat_log->clear();
	stream_label->set_visible(false);
	stream_label->clear();
	_stream_accumulated = "";

	// Set agent selector to match chat's agent
	for (int i = 0; i < _agent_presets.size(); i++) {
		if (_agent_presets[i].id == session.agent_preset) {
			if (_agent_selector) {
				_agent_selector->select(i);
			}
			break;
		}
	}

	// Rebuild from records
	intro_message_added = false;
	if (_chat_records.is_empty()) {
		intro_message_added = true;
		_append_message("assistant", TTR("Crosshair AI is ready. I can inspect the project, scenes, selected nodes, and perform scene and script edits."));
	} else {
		for (int i = 0; i < _chat_records.size(); i++) {
			_append_message(_chat_records[i].role, _chat_records[i].text);
		}
	}

	_update_chat_selector();
	_update_token_counter();
	_scroll_to_bottom();
}

void YeetAIDock::_update_chat_selector() {
	if (!_chat_selector) {
		return;
	}
	_chat_selector->clear();
	for (int i = 0; i < _chat_sessions.size(); i++) {
		String title = _chat_sessions[i].title;
		if (title.length() > 40) {
			title = title.substr(0, 37) + "...";
		}
		_chat_selector->add_item(title);
	}
	if (_active_chat_index >= 0 && _active_chat_index < _chat_sessions.size()) {
		_chat_selector->select(_active_chat_index);
	}
}

void YeetAIDock::_update_chat_title() {
	if (_active_chat_index < 0 || _active_chat_index >= _chat_sessions.size()) {
		return;
	}
	// Use the first user message as the chat title
	for (int i = 0; i < _chat_records.size(); i++) {
		if (_chat_records[i].role == "user") {
			String title = _chat_records[i].text.strip_edges();
			if (title.length() > 50) {
				title = title.substr(0, 47) + "...";
			}
			_chat_sessions.write[_active_chat_index].title = title;
			_update_chat_selector();
			return;
		}
	}
}

String YeetAIDock::_get_chats_dir() const {
	const String base = EditorPaths::get_singleton()->get_project_data_dir();
	return base.path_join("yeet_ai_chats");
}

void YeetAIDock::_save_all_chats() const {
	const String dir = _get_chats_dir();
	Ref<DirAccess> da = DirAccess::open("res://");
	if (da.is_null()) {
		return;
	}
	da->make_dir_recursive(dir);

	for (int i = 0; i < _chat_sessions.size(); i++) {
		const ChatSession &s = _chat_sessions[i];
		Dictionary data;
		data["id"] = s.id;
		data["title"] = s.title;
		data["agent_preset"] = s.agent_preset;
		data["created_at"] = s.created_at;
		data["updated_at"] = s.updated_at;
		data["messages"] = s.messages;

		Array records;
		for (int j = 0; j < s.records.size(); j++) {
			Dictionary rec;
			rec["role"] = s.records[j].role;
			rec["text"] = s.records[j].text;
			records.push_back(rec);
		}
		data["records"] = records;

		const String path = dir.path_join(s.id + ".json");
		const String json_str = JSON::stringify(data, "\t");
		Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_string(json_str);
		}
	}
}

void YeetAIDock::_load_chats() {
	EditorPaths *ep = EditorPaths::get_singleton();
	if (!ep) {
		return;
	}
	const String dir = _get_chats_dir();
	Ref<DirAccess> da = DirAccess::open(dir);
	if (da.is_null()) {
		return;
	}

	_chat_sessions.clear();
	da->list_dir_begin();
	String fn = da->get_next();
	while (!fn.is_empty()) {
		if (fn.ends_with(".json")) {
			const String path = dir.path_join(fn);
			Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
			if (f.is_valid()) {
				const String json_str = f->get_as_utf8_string();
				const Variant parsed = JSON::parse_string(json_str);
				if (parsed.get_type() == Variant::DICTIONARY) {
					const Dictionary data = parsed;
					ChatSession session;
					session.id = data.get("id", "");
					session.title = data.get("title", TTR("Chat"));
					session.agent_preset = data.get("agent_preset", "general");
					session.created_at = data.get("created_at", 0);
					session.updated_at = data.get("updated_at", 0);
					session.messages = data.get("messages", Array());

					const Array rec_arr = data.get("records", Array());
					for (int i = 0; i < rec_arr.size(); i++) {
						const Dictionary rec = rec_arr[i];
						MessageRecord mr;
						mr.role = rec.get("role", "");
						mr.text = rec.get("text", "");
						session.records.push_back(mr);
					}
					if (!session.id.is_empty()) {
						_chat_sessions.push_back(session);
					}
				}
			}
		}
		fn = da->get_next();
	}
	da->list_dir_end();

	// Chats loaded in file-system order — no custom sort needed.
}

// ── Model selector ─────────────────────────────────────────────────────────

void YeetAIDock::_on_model_selector_pressed() {
	if (!_model_popup) {
		_model_popup = memnew(PopupPanel);
		_model_popup->set_title(TTR("Select Model"));

		VBoxContainer *vb = memnew(VBoxContainer);
		_model_popup->add_child(vb);

		_model_search_edit = memnew(LineEdit);
		_model_search_edit->set_placeholder(TTR("Search models..."));
		_model_search_edit->set_clear_button_enabled(true);
		_model_search_edit->connect("text_changed", callable_mp(this, &YeetAIDock::_on_model_search_changed));
		vb->add_child(_model_search_edit);

		_model_item_list = memnew(ItemList);
		_model_item_list->set_custom_minimum_size(Size2(280, 320) * EDSCALE);
		_model_item_list->connect("item_activated", callable_mp(this, &YeetAIDock::_on_model_item_activated));
		vb->add_child(_model_item_list);

		add_child(_model_popup);
	}

	_populate_model_list();
	_model_search_edit->set_text("");

	Vector2 gp = _model_selector_button->get_screen_position();
	Rect2i screen_rect = Rect2i(gp.x, gp.y + _model_selector_button->get_size().height, 300 * EDSCALE, 380 * EDSCALE);
	_model_popup->set_position(screen_rect.position);
	_model_popup->set_size(screen_rect.size);
	_model_popup->popup(screen_rect);
	_model_search_edit->grab_focus();

	const int provider = _get_editor_setting_int("yeet_ai/chat/provider", 0);
	if (provider == 0 || provider == 3) {
		String tags_url;
		if (provider == 0) {
			tags_url = _get_editor_setting_string("yeet_ai/chat/completions_url", "");
		} else {
			tags_url = _get_editor_setting_string("yeet_ai/chat/yeet_chat_url", "https://gpt.yeetlabs.fun/v1/chat/completions");
		}
		if (!tags_url.is_empty()) {
			String base = tags_url;
			int cp_index = base.find("/v1/");
			if (cp_index >= 0) {
				base = base.substr(0, cp_index);
			}
			const String url = base + "/v1/models";
			if (_model_tags_request) {
				_model_tags_request->request(url);
			}
		}
	}
}

void YeetAIDock::_populate_model_list(const String &p_filter) {
	if (!_model_item_list) {
		return;
	}
	_model_item_list->clear();

	const int provider = _get_editor_setting_int("yeet_ai/chat/provider", 0);
	const String current_model = _get_editor_setting_string("yeet_ai/chat/model", "berrymodel");

	Vector<String> models;

	if (provider == 1) {
		models.push_back("gemini-2.5-pro");
		models.push_back("gemini-2.5-flash");
		models.push_back("gemini-2.0-flash");
		models.push_back("gemini-2.0-flash-lite");
		models.push_back("gemini-1.5-pro");
		models.push_back("gemini-1.5-flash");
	} else if (provider == 2) {
		models.push_back("qwen/qwen3-235b-a22b:free");
		models.push_back("qwen/qwen3-30b-a3b:free");
		models.push_back("qwen/qwen3-14b:free");
		models.push_back("qwen/qwen3-8b:free");
		models.push_back("qwen/qwen3-4b:free");
		models.push_back("deepseek/deepseek-r1-0528:free");
		models.push_back("deepseek/deepseek-chat-v3-0324:free");
		models.push_back("google/gemini-2.0-flash-exp:free");
		models.push_back("meta-llama/llama-4-maverick:free");
		models.push_back("meta-llama/llama-4-scout:free");
		models.push_back("mistralai/mistral-small-3.1-24b-instruct:free");
	} else {
		models.push_back("berrymodel");
		for (const String &tag : _fetched_model_tags) {
			if (!models.has(tag)) {
				models.push_back(tag);
			}
		}
	}

	for (int i = 0; i < models.size(); i++) {
		const String &m = models[i];
		if (!p_filter.is_empty() && !m.to_lower().contains(p_filter.to_lower())) {
			continue;
		}
		const int idx = _model_item_list->add_item(m);
		if (m == current_model) {
			_model_item_list->select(idx);
		}
	}
}

void YeetAIDock::_on_model_search_changed(const String &p_text) {
	_populate_model_list(p_text);
}

void YeetAIDock::_on_model_item_activated(int p_index) {
	if (!_model_item_list || p_index < 0) {
		return;
	}
	const String selected = _model_item_list->get_item_text(p_index);
	if (!selected.is_empty()) {
		EditorSettings *settings = EditorSettings::get_singleton();
		if (settings) {
			if (!settings->has_setting("yeet_ai/chat/model")) {
				settings->set_initial_value("yeet_ai/chat/model", selected);
			} else {
				settings->set_setting("yeet_ai/chat/model", selected);
			}
			settings->save();
		}
		_update_model_button_label();
	}
	if (_model_popup) {
		_model_popup->hide();
	}
}

void YeetAIDock::_on_model_tags_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	(void)p_headers;
	_fetched_model_tags.clear();

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code < 200 || p_response_code >= 300) {
		return;
	}

	const String body_text = String::utf8(reinterpret_cast<const char *>(p_body.ptr()), p_body.size());
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(body_text) != OK) {
		return;
	}
	const Variant data = json->get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		return;
	}
	const Dictionary d = data;
	const Variant data_var = d.get("data", d.get("models", Variant()));
	Array model_list;
	if (data_var.get_type() == Variant::ARRAY) {
		model_list = data_var;
	} else if (data_var.get_type() == Variant::DICTIONARY) {
		model_list.push_back(data_var);
	}

	for (int i = 0; i < model_list.size(); i++) {
		const Variant mv = model_list[i];
		String model_id;
		if (mv.get_type() == Variant::STRING) {
			model_id = String(mv);
		} else if (mv.get_type() == Variant::DICTIONARY) {
			const Dictionary md = mv;
			model_id = String(md.get("id", md.get("name", md.get("model", ""))));
		}
		if (!model_id.is_empty() && !_fetched_model_tags.has(model_id)) {
			_fetched_model_tags.push_back(model_id);
		}
	}

	if (_model_popup && _model_popup->is_visible()) {
		_populate_model_list(_model_search_edit ? _model_search_edit->get_text() : "");
	}
}

void YeetAIDock::_update_model_button_label() {
	if (!_model_selector_button) {
		return;
	}
	EditorSettings *settings = EditorSettings::get_singleton();
	if (!settings) {
		return;
	}
	const String model = _get_editor_setting_string("yeet_ai/chat/model", "berrymodel");
	String display = model;
	const int slash = display.rfind("/");
	if (slash >= 0) {
		display = display.substr(slash + 1);
	}
	if (display.length() > 16) {
		display = display.substr(0, 13) + "...";
	}
	_model_selector_button->set_text(display);
}

// ── Token counter ──────────────────────────────────────────────────────────

void YeetAIDock::_update_token_counter() {
	if (!_token_count_label) {
		return;
	}
	int approx_tokens = 0;
	for (const Variant &msg : conversation_messages) {
		if (msg.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary d = msg;
		const String content = d.get("content", "");
		approx_tokens += content.length() / 4;
	}
	if (approx_tokens > 0) {
		_token_count_label->set_text(vformat(TTR("~%d tokens"), approx_tokens));
		_token_count_label->set_visible(true);
	} else {
		_token_count_label->set_visible(false);
	}
}

// ── Tool progress bar ─────────────────────────────────────────────────────

void YeetAIDock::_update_tool_progress_bar() {
	if (!_tool_progress_bar) {
		return;
	}
	if (tool_round_trips <= 0) {
		_tool_progress_bar->set_visible(false);
		return;
	}
	const int max_rounds = _get_editor_setting_int("yeet_ai/chat/max_tool_round_trips", 100);
	const float progress = CLAMP(float(tool_round_trips) / float(max_rounds), 0.0f, 1.0f);
	_tool_progress_bar->set_value(progress);
	_tool_progress_bar->set_visible(true);
}

// ── Files modified label ──────────────────────────────────────────────────

void YeetAIDock::_update_files_modified_label() {
	if (!_files_modified_label) {
		return;
	}
	if (_session_modified_files.is_empty()) {
		_files_modified_label->set_visible(false);
		return;
	}
	_files_modified_label->clear();
	_files_modified_label->set_visible(true);

	const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color font_dim = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	const int base_fs = has_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			? get_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			: 14;
	const int small_fs = MAX(9, int(base_fs * 0.8f));

	Ref<Texture2D> file_icon = get_editor_theme_icon("File");
	if (file_icon.is_valid()) {
		_files_modified_label->add_image(file_icon, int(14 * EDSCALE), int(14 * EDSCALE));
	}
	_files_modified_label->add_text(" ");
	_files_modified_label->push_font_size(small_fs);
	_files_modified_label->push_color(font_dim);
	_files_modified_label->push_bold();
	_files_modified_label->add_text(vformat(TTR("Files modified (%d):"), _session_modified_files.size()));
	_files_modified_label->pop();
	_files_modified_label->pop();
	_files_modified_label->pop();
	_files_modified_label->append_text("\n");

	int count = 0;
	for (const String &f : _session_modified_files) {
		if (count >= 8) {
			_files_modified_label->push_color(font_dim);
			_files_modified_label->push_font_size(small_fs);
			_files_modified_label->add_text(vformat("  +%d more", _session_modified_files.size() - count));
			_files_modified_label->pop();
			_files_modified_label->pop();
			break;
		}
		_files_modified_label->push_font_size(small_fs);
		_files_modified_label->push_color(accent);
		_files_modified_label->push_meta(f);
		_files_modified_label->add_text(f);
		_files_modified_label->pop();
		_files_modified_label->pop();
		_files_modified_label->pop();
		_files_modified_label->append_text("\n");
		count++;
	}
}

// ── Prompt history navigation ──────────────────────────────────────────────

void YeetAIDock::_history_navigate(int p_direction) {
	if (_prompt_history.is_empty() || !prompt_input) {
		return;
	}

	if (_prompt_history_index < 0) {
		_prompt_history_draft = prompt_input->get_text();
	}

	int new_index = _prompt_history_index + p_direction;
	if (new_index < 0) {
		new_index = 0;
	}
	if (new_index >= _prompt_history.size()) {
		new_index = _prompt_history.size() - 1;
	}
	_prompt_history_index = new_index;

	if (_prompt_history_index >= 0 && _prompt_history_index < _prompt_history.size()) {
		prompt_input->set_text(_prompt_history[_prompt_history_index]);
	} else {
		prompt_input->set_text(_prompt_history_draft);
	}

	prompt_input->set_caret_column(prompt_input->get_text().length());
}
