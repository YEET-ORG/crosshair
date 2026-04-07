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
	_tool_progress_bar->set_custom_minimum_size(Size2(0, 4) * EDSCALE);
	_tool_progress_bar->set_show_percentage(false);
	_tool_progress_bar->set_min(0.0);
	_tool_progress_bar->set_max(1.0);
	_tool_progress_bar->set_value(0.0);
	_tool_progress_bar->set_visible(false);
	main_column->add_child(_tool_progress_bar);

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

	// Animated status dot (pulses while the AI is active)
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
	prompt_input->set_placeholder(TTR("Ask Crosshair AI to inspect files, run tools, and edit scenes/scripts. (Ctrl+Enter)"));
	prompt_input->connect("gui_input", callable_mp(this, &YeetAIDock::_on_prompt_gui_input));
	input_column->add_child(prompt_input);

	HBoxContainer *button_row = memnew(HBoxContainer);
	button_row->add_theme_constant_override("separation", int(8.0f * EDSCALE));
	button_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input_column->add_child(button_row);

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
}

YeetAIDock::~YeetAIDock() {
	_cancel_streaming();
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
	}
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		if (!intro_message_added) {
			intro_message_added = true;
			_append_message("assistant", TTR("Crosshair AI is ready. I can inspect the project, scenes, selected nodes, and perform scene and script edits."));
		}
		send_button->set_button_icon(get_editor_theme_icon(SNAME("Play")));
		stop_button->set_button_icon(get_editor_theme_icon(SNAME("Stop")));
		clear_button->set_button_icon(get_editor_theme_icon(SNAME("Clear")));
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

// ── SSE Streaming ─────────────────────────────────────────────────────────────

void YeetAIDock::_stream_thread_trampoline(void *p_user) {
	static_cast<YeetAIDock *>(p_user)->_stream_thread_body();
}

void YeetAIDock::_start_streaming() {
	_stream_accumulated = "";
	_stream_done_flag = false;
	_stream_error_flag = false;
	_stream_error_msg = "";
	_stream_should_stop = false;
	_stream_active = true;

	stream_label->clear();
	stream_label->set_visible(true);

	_stream_thread.start(&YeetAIDock::_stream_thread_trampoline, this);
}

void YeetAIDock::_cancel_streaming() {
	if (!_stream_active && !_stream_thread.is_started()) {
		return;
	}
	_stream_should_stop = true;
	if (_stream_thread.is_started()) {
		_stream_thread.wait_to_finish();
	}
	_stream_active = false;
	{
		MutexLock lock(_stream_mutex);
		_pending_chunks.clear();
		_stream_done_flag = false;
		_stream_error_flag = false;
	}
}

void YeetAIDock::_stream_thread_body() {
	String host;
	int port = 80;
	String path;
	bool use_tls = false;

	if (!yeet_parse_url(_stream_endpoint, host, port, path, use_tls)) {
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = "Failed to parse endpoint URL: " + _stream_endpoint;
		_stream_done_flag = true;
		return;
	}

	HTTPClient *client = HTTPClient::create();
	client->set_blocking_mode(false);

	Ref<TLSOptions> tls_opts;
	if (use_tls) {
		if (yeet_stream_host_prefers_insecure_tls(host)) {
			tls_opts = TLSOptions::client_unsafe(Ref<X509Certificate>());
		} else {
			tls_opts = TLSOptions::client();
		}
	}

	Error conn_err = client->connect_to_host(host, port, tls_opts);
	if (conn_err != OK) {
		memdelete(client);
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat("connect_to_host failed (err %d)", (int)conn_err);
		_stream_done_flag = true;
		return;
	}

	// Poll until connected
	while (true) {
		if (_stream_should_stop) {
			memdelete(client);
			MutexLock lock(_stream_mutex);
			_stream_done_flag = true;
			return;
		}
		HTTPClient::Status s = client->get_status();
		if (s == HTTPClient::STATUS_CONNECTED) {
			break;
		}
		if (s == HTTPClient::STATUS_CONNECTING || s == HTTPClient::STATUS_RESOLVING) {
			client->poll();
			OS::get_singleton()->delay_usec(5000);
			continue;
		}
		memdelete(client);
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat(TTR("Connection failed (status %d): %s"), (int)s, yeet_describe_http_client_status(s));
		_stream_done_flag = true;
		return;
	}

	// Send the HTTP POST request
	const CharString body_utf8 = _stream_req_body.utf8();
	Error req_err = client->request(
			HTTPClient::METHOD_POST,
			path,
			_stream_req_headers,
			reinterpret_cast<const uint8_t *>(body_utf8.get_data()),
			body_utf8.length());

	if (req_err != OK) {
		memdelete(client);
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat("Request send failed (err %d)", (int)req_err);
		_stream_done_flag = true;
		return;
	}

	// Poll until the server starts responding
	while (true) {
		if (_stream_should_stop) {
			memdelete(client);
			MutexLock lock(_stream_mutex);
			_stream_done_flag = true;
			return;
		}
		HTTPClient::Status s = client->get_status();
		if (s == HTTPClient::STATUS_BODY || s == HTTPClient::STATUS_CONNECTED) {
			break;
		}
		if (s == HTTPClient::STATUS_REQUESTING) {
			client->poll();
			OS::get_singleton()->delay_usec(5000);
			continue;
		}
		memdelete(client);
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat(TTR("Waiting for response failed (status %d): %s"), (int)s, yeet_describe_http_client_status(s));
		_stream_done_flag = true;
		return;
	}

	if (!client->has_response()) {
		memdelete(client);
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = "No response from server.";
		_stream_done_flag = true;
		return;
	}

	const int resp_code = client->get_response_code();
	if (resp_code < 200 || resp_code >= 300) {
		String err_body;
		while (client->get_status() == HTTPClient::STATUS_BODY && !_stream_should_stop) {
			client->poll();
			PackedByteArray chunk = client->read_response_body_chunk();
			if (!chunk.is_empty()) {
				err_body += String::utf8(reinterpret_cast<const char *>(chunk.ptr()), chunk.size());
			}
			OS::get_singleton()->delay_usec(1000);
		}
		memdelete(client);
		MutexLock lock(_stream_mutex);
		_stream_error_flag = true;
		_stream_error_msg = vformat("HTTP %d: %s", resp_code, err_body.substr(0, 400));
		_stream_done_flag = true;
		return;
	}

	// ── Read SSE body ──────────────────────────────────────────────────────────
	String sse_buf;
	bool sse_done = false;

	while (!sse_done && client->get_status() == HTTPClient::STATUS_BODY) {
		if (_stream_should_stop) {
			break;
		}
		client->poll();
		PackedByteArray raw = client->read_response_body_chunk();
		if (raw.is_empty()) {
			OS::get_singleton()->delay_usec(1000);
			continue;
		}
		sse_buf += String::utf8(reinterpret_cast<const char *>(raw.ptr()), raw.size());

		// Process all complete lines in the buffer
		while (!sse_done) {
			int nl = sse_buf.find("\n");
			if (nl < 0) {
				break;
			}
			String line = sse_buf.substr(0, nl).strip_edges();
			sse_buf = sse_buf.substr(nl + 1);

			if (!line.begins_with("data:")) {
				continue;
			}
			const String sse_payload = line.substr(5).strip_edges();
			if (sse_payload == "[DONE]") {
				sse_done = true;
				break;
			}

			// Parse delta JSON
			Ref<JSON> jobj;
			jobj.instantiate();
			if (jobj->parse(sse_payload) != OK) {
				continue;
			}
			const Variant parsed = jobj->get_data();
			if (parsed.get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary d = parsed;
			const Array choices = d.get("choices", Array());
			if (choices.is_empty()) {
				continue;
			}
			const Variant c0v = choices[0];
			if (c0v.get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary c0 = c0v;
			const Variant dv = c0.get("delta", Variant());
			if (dv.get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary delta = dv;
			const String content_chunk = delta.get("content", "");
			if (content_chunk.is_empty()) {
				continue;
			}
			MutexLock lock(_stream_mutex);
			_pending_chunks.push_back(content_chunk);
		}
	}

	memdelete(client);
	MutexLock lock(_stream_mutex);
	_stream_done_flag = true;
}

void YeetAIDock::_drain_stream_queue() {
	if (!_stream_active) {
		return;
	}

	// Collect pending data under lock
	Vector<String> chunks;
	bool done = false;
	bool had_error = false;
	String error_msg;
	{
		MutexLock lock(_stream_mutex);
		chunks = _pending_chunks;
		_pending_chunks.clear();
		done = _stream_done_flag;
		had_error = _stream_error_flag;
		error_msg = _stream_error_msg;
	}

	bool updated = !chunks.is_empty();
	for (const String &chunk : chunks) {
		_stream_accumulated += chunk;
	}
	if (updated) {
		_update_stream_label();
	}

	if (done) {
		if (_stream_thread.is_started()) {
			_stream_thread.wait_to_finish();
		}
		_stream_active = false;

		stream_label->set_visible(false);
		stream_label->clear();

		if (had_error) {
			_set_waiting(false, TTR("Error"));
			_append_message("assistant", TTR("Streaming error: ") + error_msg);
		} else {
			_finalize_stream();
		}
	}
}

void YeetAIDock::_finalize_stream() {
	// Hand the fully accumulated SSE text off to the existing response handler.
	// This re-uses all existing JSON parsing, tool-call dispatch, etc.
	_handle_model_response(_stream_accumulated);
}

// ── Animation & live-stream UI ────────────────────────────────────────────────

void YeetAIDock::_update_animation(double p_delta) {
	_anim_time += float(p_delta);

	// Pulse the status dot with a sine-wave alpha when active
	if (waiting_for_response && _status_dot) {
		const float alpha = 0.35f + 0.65f * (0.5f + 0.5f * Math::sin(_anim_time * 4.5f));
		Color dot_color = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
		dot_color.a = alpha;
		_status_dot->set_color(dot_color);
	}
}

void YeetAIDock::_update_stream_label() {
	if (!stream_label) {
		return;
	}

	const Color font_color = get_theme_color(SNAME("font_color"), EditorStringName(Editor));
	const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color success = get_theme_color(SNAME("success_color"), EditorStringName(Editor));

	stream_label->clear();

	// Role header
	stream_label->push_color(success);
	stream_label->push_bold();
	stream_label->add_text(TTR("Assistant"));
	stream_label->pop();
	stream_label->pop();
	stream_label->push_color(accent);
	stream_label->add_text("  ");
	stream_label->add_text(String::utf8("\xe2\x80\xa2")); // •
	stream_label->pop();
	stream_label->append_text("\n");

	stream_label->push_indent(1);
	stream_label->push_color(font_color);
	stream_label->append_text(_escape_bbcode(_stream_accumulated));

	// Static block cursor (no periodic full redraw — avoids flicker)
	stream_label->push_color(accent);
	stream_label->add_text(String::utf8("\xe2\x96\x8b")); // ▋
	stream_label->pop();
	stream_label->pop();
	stream_label->pop();

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
	const Color dim = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	const int base_fs = has_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			? get_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			: 0;

	if (base_fs > 0) {
		chat_log->push_font_size(MAX(10, int(base_fs * 0.84f)));
	}
	chat_log->push_color(dim);
	chat_log->push_italics();
	chat_log->add_text("  " + p_text);
	chat_log->pop(); // italics
	chat_log->pop(); // color
	if (base_fs > 0) {
		chat_log->pop(); // font_size
	}
	chat_log->append_text("\n\n");
	_scroll_to_bottom();
}

String YeetAIDock::_icon_for_tool(const String &p_tool_name) const {
	const String s = p_tool_name.to_lower();
	// Write / create / update
	if (s.contains("write") || s.contains("update_gdscript") || s.contains("create_gdscript")) {
		return String::utf8("\xe2\x9c\x8e "); // ✎
	}
	// Create nodes / scenes / folders
	if (s.contains("create") || s.contains("add_node") || s.contains("instantiate") || s.contains("add_primitive") || s.contains("add_collision")) {
		return String::utf8("\xe2\x9c\x9a "); // ✚
	}
	// Delete / remove
	if (s.contains("delete") || s.contains("remove")) {
		return String::utf8("\xe2\x9c\x96 "); // ✖
	}
	// Move / rename / reparent
	if (s.contains("move") || s.contains("rename") || s.contains("reparent")) {
		return String::utf8("\xe2\x86\x92 "); // →
	}
	// Play / run / stop scene
	if (s.contains("play") || s.contains("run")) {
		return String::utf8("\xe2\x96\xb6 "); // ▶
	}
	if (s.contains("stop_playing")) {
		return String::utf8("\xe2\x96\xa0 "); // ■
	}
	// Screenshot / capture
	if (s.contains("capture") || s.contains("screenshot")) {
		return String::utf8("\xe2\x8c\x96 "); // ⌖
	}
	// Save
	if (s.contains("save")) {
		return String::utf8("\xe2\x9c\x94 "); // ✔
	}
	// Attach / connect
	if (s.contains("attach") || s.contains("connect") || s.contains("assign")) {
		return String::utf8("\xe2\x97\x8e "); // ◎
	}
	// Read / get / list / find / grep
	if (s.contains("read") || s.contains("get") || s.contains("list") || s.contains("find") || s.contains("grep")) {
		return String::utf8("\xe2\x97\x89 "); // ◉
	}
	return String::utf8("\xe2\x97\x86 "); // ◆ default
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
	conversation_messages.append(make_message("user", prompt));
	_append_message("user", prompt);
	tool_round_trips = 0;
	turn_context_prompt = _build_runtime_context_prompt() + _build_task_hints_for_user_prompt(prompt);
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
	_cancel_streaming();
	_set_waiting(false, TTR("Ready"));

	conversation_messages.clear();
	turn_context_prompt = String();
	chat_log->clear();
	stream_label->set_visible(false);
	stream_label->clear();
	_stream_accumulated = "";

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
	const int base_fs = has_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			? get_theme_font_size(SNAME("font_size"), EditorStringName(Editor))
			: 14;
	const int label_fs = MAX(10, int(base_fs * 0.84f));

	// Role row.
	chat_log->push_font_size(label_fs);
	chat_log->push_color(label_color);
	chat_log->push_bold();
	chat_log->add_text(label);
	chat_log->pop();
	chat_log->pop();
	chat_log->pop();
	chat_log->push_color(dim);
	chat_log->add_text("  ");
	chat_log->add_text(String::utf8("\xe2\x80\xa2")); // •
	chat_log->pop();
	chat_log->append_text("\n");

	// Message body.
	chat_log->push_indent(1);
	chat_log->push_color(font_base);
	chat_log->append_text(_escape_bbcode(p_text));
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

	const String title = _humanize_tool_name(p_tool_name);
	const String icon = _icon_for_tool(p_tool_name);
	const Vector<String> paths = _collect_relevant_paths(p_args, p_result.payload);

	// ── Compact card header ────────────────────────────────────────────────────
	// "  icon  Tool Name   path/to/file   ·  ✓ OK"
	// ─────────────────────────────────────────────
	chat_log->push_color(font_dim);
	chat_log->add_text("  "); // slight indent, no vertical bar (distinguishes from chat messages)
	chat_log->pop();

	// Icon glyph
	chat_log->push_color(font_dim);
	chat_log->add_text(icon);
	chat_log->pop();

	// Tool action label (dim, smaller)
	chat_log->push_font_size(caption_fs);
	chat_log->push_color(font_dim);
	chat_log->add_text(title);
	chat_log->pop();
	chat_log->pop();

	// Primary path inline (warning/yellow for visibility, monospace)
	if (!paths.is_empty()) {
		chat_log->add_text("  ");
		chat_log->push_color(warning);
		chat_log->push_mono();
		chat_log->push_font_size(caption_fs);
		// Shorten path: keep last two segments for readability
		const String p = paths[0];
		const int last_slash = p.rfind("/");
		const String short_path = (last_slash > 4) ? ("\xe2\x80\xa6" + p.substr(last_slash)) : p; // …/filename
		chat_log->add_text(short_path);
		chat_log->pop();
		chat_log->pop();
		chat_log->pop();
		if (paths.size() > 1) {
			chat_log->push_color(font_dim);
			chat_log->push_font_size(caption_fs);
			chat_log->add_text(vformat(" +%d", paths.size() - 1));
			chat_log->pop();
			chat_log->pop();
		}
	}

	// Status badge  ·  ✓ OK  or  ✗ Err
	chat_log->push_color(font_dim);
	chat_log->add_text("  \xe2\x80\xb9"); // ‹ arrow right-pointing
	chat_log->pop();
	chat_log->push_color(p_result.ok ? success : error);
	chat_log->push_font_size(caption_fs);
	chat_log->add_text(p_result.ok
					? String::utf8("  \xe2\x9c\x93") // ✓
					: String::utf8("  \xe2\x9c\x97")); // ✗
	chat_log->pop();
	chat_log->pop();

	chat_log->append_text("\n");

	// ── Additional paths (if multiple) ────────────────────────────────────────
	if (paths.size() > 1) {
		chat_log->push_indent(1);
		chat_log->push_font_size(caption_fs);
		for (int i = 1; i < paths.size(); i++) {
			chat_log->push_color(font_dim);
			chat_log->add_text("  \xe2\x80\xa2 "); // •
			chat_log->pop();
			chat_log->push_color(warning);
			chat_log->push_mono();
			chat_log->add_text(paths[i]);
			chat_log->pop();
			chat_log->pop();
			chat_log->append_text("\n");
		}
		chat_log->pop();
		chat_log->pop();
	}

	// ── Compact output preview (collapsed, dim monospace) ─────────────────────
	const String preview = _truncate_preview(p_result.display_text, 280);
	if (!preview.is_empty() && !p_result.ok) {
		// Always show output on error so the user sees what went wrong
		chat_log->push_indent(1);
		chat_log->push_font_size(caption_fs);
		chat_log->push_color(error);
		chat_log->push_mono();
		chat_log->append_text(_escape_bbcode(preview));
		chat_log->pop();
		chat_log->pop();
		chat_log->pop();
		chat_log->pop();
		chat_log->append_text("\n");
	} else if (!preview.is_empty()) {
		// On success: single-line dim preview (first 120 chars)
		const String one_line = _truncate_preview(preview.replace("\n", " "), 120);
		if (!one_line.strip_edges().is_empty()) {
			chat_log->push_indent(1);
			chat_log->push_font_size(caption_fs);
			chat_log->push_color(font_dim);
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

	// Swap Send ↔ Stop buttons
	send_button->set_visible(!p_waiting);
	stop_button->set_visible(p_waiting);

	status_label->set_text(p_status);

	// Status dot: accent color pulsing when active, dim when idle
	if (_status_dot) {
		if (p_waiting) {
			Color dot = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
			dot.a = 1.0f;
			_status_dot->set_color(dot);
		} else {
			_status_dot->set_color(Color(0.5f, 0.5f, 0.5f, 0.35f));
		}
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
		ToolExecutionResult result = _execute_tool(tool_name, args);
		conversation_messages.append(make_message("assistant", JSON::stringify(envelope)));
		_append_tool_result(tool_name, args, result);

		Dictionary tool_payload = result.payload;
		tool_payload["ok"] = result.ok && !result.payload.has("error");
		conversation_messages.append(_make_user_message_with_optional_vision(tool_name, tool_payload));

		tool_round_trips++;
		// Show a subtle planning indicator before the next model call
		_append_status_row(TTR("Planning next moves\xe2\x80\xa6")); // …
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

YeetAIDock::ToolExecutionResult YeetAIDock::_execute_tool(const String &p_tool_name, const Dictionary &p_args) {
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

	if (p_tool_name == "write_project_file") {
		result.ok = true;
		result.payload = _tool_write_project_file(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "save_all_scenes") {
		result.ok = true;
		result.payload = _tool_save_all_scenes();
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "reload_scene") {
		result.ok = true;
		result.payload = _tool_reload_scene(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "set_editor_main_screen") {
		result.ok = true;
		result.payload = _tool_set_editor_main_screen(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "select_file") {
		result.ok = true;
		result.payload = _tool_select_file(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_unsaved_scenes") {
		result.ok = true;
		result.payload = _tool_get_unsaved_scenes();
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "reparent_node") {
		result.ok = true;
		result.payload = _tool_reparent_node(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "rename_node") {
		result.ok = true;
		result.payload = _tool_rename_node(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "file_exists") {
		result.ok = true;
		result.payload = _tool_file_exists(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "list_directory") {
		result.ok = true;
		result.payload = _tool_list_directory(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "duplicate_node") {
		result.ok = true;
		result.payload = _tool_duplicate_node(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "edit_script") {
		result.ok = true;
		result.payload = _tool_edit_script(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "move_child") {
		result.ok = true;
		result.payload = _tool_move_child(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "create_project_folder") {
		result.ok = true;
		result.payload = _tool_create_project_folder(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "delete_project_file") {
		result.ok = true;
		result.payload = _tool_delete_project_file(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_editor_log") {
		result.ok = true;
		result.payload = _tool_get_editor_log(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "capture_editor_viewport") {
		result.ok = true;
		result.payload = _tool_capture_editor_viewport(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_debug_snapshot") {
		result.ok = true;
		result.payload = _tool_get_debug_snapshot(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "grep_project_files") {
		result.ok = true;
		result.payload = _tool_grep_project_files(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_autoloads") {
		result.ok = true;
		result.payload = _tool_get_autoloads();
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_node_groups") {
		result.ok = true;
		result.payload = _tool_get_node_groups(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_node_collision_layers") {
		result.ok = true;
		result.payload = _tool_get_node_collision_layers(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "move_project_file") {
		result.ok = true;
		result.payload = _tool_move_project_file(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_animation_player_state") {
		result.ok = true;
		result.payload = _tool_get_animation_player_state(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_tilemap_info") {
		result.ok = true;
		result.payload = _tool_get_tilemap_info(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_navigation_region_info") {
		result.ok = true;
		result.payload = _tool_get_navigation_region_info(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "capture_game_viewport") {
		result.ok = true;
		result.payload = _tool_capture_game_viewport(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "get_runtime_debugger_state") {
		result.ok = true;
		result.payload = _tool_get_runtime_debugger_state();
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	if (p_tool_name == "editor_undo") {
		result.ok = true;
		result.payload = _tool_editor_undo(p_args);
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
	// Use Object::get(), not get_setting(): get_setting() applies per-project editor overrides from
	// ProjectSettings, which would mask globally saved API keys when switching projects.
	bool valid = false;
	const Variant v = settings->get(StringName(p_setting), &valid);
	if (!valid) {
		return p_default;
	}
	return String(v);
}

int YeetAIDock::_get_editor_setting_int(const String &p_setting, int p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	bool valid = false;
	const Variant v = settings->get(StringName(p_setting), &valid);
	if (!valid) {
		return p_default;
	}
	return int(v);
}

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

bool YeetAIDock::_get_editor_setting_bool(const String &p_setting, bool p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	bool valid = false;
	const Variant v = settings->get(StringName(p_setting), &valid);
	if (!valid) {
		return p_default;
	}
	return bool(v);
}

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

Dictionary YeetAIDock::_parse_one_batch_call(const Variant &p_call_var, int p_index, const Dictionary &p_shared_arguments) const {
	Dictionary out;
	Dictionary call;
	if (p_call_var.get_type() == Variant::DICTIONARY) {
		call = p_call_var;
	} else if (p_call_var.get_type() == Variant::STRING) {
		if (!_parse_json_dictionary_quiet(String(p_call_var).strip_edges(), call)) {
			out["error"] = "Each batch call must be a JSON object (or a JSON object encoded as a string).";
			out["failed_index"] = p_index;
			return out;
		}
	} else {
		out["error"] = "Each batch call must be a dictionary.";
		out["failed_index"] = p_index;
		return out;
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
		out["error"] = "Each batch call needs a tool name (`tool`, `name`, or `function.name`).";
		out["failed_index"] = p_index;
		return out;
	}
	if (tool_name == "batch_tool_calls") {
		out["error"] = "batch_tool_calls cannot invoke itself recursively.";
		out["failed_index"] = p_index;
		return out;
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

	const Array shared_keys = p_shared_arguments.keys();
	for (int key_index = 0; key_index < shared_keys.size(); key_index++) {
		const Variant shared_key_variant = shared_keys[key_index];
		const String shared_key = String(shared_key_variant);
		if (!call_args.has(shared_key)) {
			call_args[shared_key] = p_shared_arguments[shared_key_variant];
		}
	}

	out["tool"] = tool_name;
	out["arguments"] = call_args;
	return out;
}

Dictionary YeetAIDock::_tool_batch_tool_calls(const Dictionary &p_args) {
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
	const bool dry_run = bool(p_args.get("dry_run", false));
	if (calls.is_empty()) {
		result["error"] = "calls is required and must contain at least one tool call.";
		return result;
	}

	// GDScript file tools blow up JSON size; never mix them with scene/editor tools in one batch.
	{
		bool any_gdscript_tool = false;
		bool any_non_gdscript_tool = false;
		for (int i = 0; i < calls.size(); i++) {
			const Dictionary parsed = _parse_one_batch_call(calls[i], i, shared_arguments);
			if (parsed.has("error")) {
				result["error"] = parsed["error"];
				result["failed_index"] = parsed["failed_index"];
				return result;
			}
			const String tn = String(parsed["tool"]).strip_edges();
			const bool is_gd = (tn == "create_gdscript_file" || tn == "update_gdscript_file");
			if (is_gd) {
				any_gdscript_tool = true;
			} else {
				any_non_gdscript_tool = true;
			}
		}
		if (any_gdscript_tool && any_non_gdscript_tool) {
			result["error"] =
					"batch_tool_calls cannot mix `create_gdscript_file` or `update_gdscript_file` with any other tool in one batch. "
					"Use one tool_call round with only scene/editor tools (inputs, nodes, meshes, attach_script, etc.), then a separate tool_call round whose batch contains only `create_gdscript_file` and/or `update_gdscript_file` calls.";
			return result;
		}
	}

	if (dry_run) {
		Array planned;
		for (int i = 0; i < calls.size(); i++) {
			const Dictionary parsed = _parse_one_batch_call(calls[i], i, shared_arguments);
			if (parsed.has("error")) {
				result["error"] = parsed["error"];
				result["failed_index"] = parsed["failed_index"];
				return result;
			}
			Dictionary entry;
			entry["tool"] = parsed["tool"];
			entry["arguments"] = parsed["arguments"];
			planned.push_back(entry);
		}
		result["dry_run"] = true;
		result["planned_calls"] = planned;
		result["planned_count"] = planned.size();
		result["ok"] = true;
		return result;
	}

	Array results;
	int executed_count = 0;
	bool had_failure = false;
	for (int i = 0; i < calls.size(); i++) {
		const Dictionary parsed = _parse_one_batch_call(calls[i], i, shared_arguments);
		if (parsed.has("error")) {
			result["error"] = parsed["error"];
			result["failed_index"] = parsed["failed_index"];
			return result;
		}
		const String tool_name = String(parsed["tool"]);
		const Dictionary call_args = parsed["arguments"];

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
		"attach_script", "reparent_node", "rename_node",
		"duplicate_node", "move_child", nullptr
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

Dictionary YeetAIDock::_tool_write_project_file(const Dictionary &p_args) const {
	Dictionary result;
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		result["error"] = "Path must start with res://";
		return result;
	}
	if (!_is_allowed_text_file(path)) {
		result["error"] = "Only safe text project extensions are allowed (same allowlist as read_project_file).";
		return result;
	}
	if (!p_args.has("contents")) {
		result["error"] = "contents is required.";
		return result;
	}
	const String contents = String(p_args["contents"]);
	if (contents.length() > MAX_FILE_WRITE_BYTES) {
		result["error"] = vformat("contents exceeds max length (%d bytes).", MAX_FILE_WRITE_BYTES);
		return result;
	}
	if (FileAccess::exists(path) && !bool(p_args.get("overwrite", false))) {
		result["error"] = "File exists. Pass overwrite=true to replace it.";
		return result;
	}

	const Error dir_error = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(path.get_base_dir()));
	if (dir_error != OK) {
		result["error"] = vformat("Failed to create parent directory: %d", dir_error);
		return result;
	}

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		result["error"] = "Failed to open file for writing.";
		return result;
	}
	file->store_string(contents);
	file->flush();

	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->update_file(path);
	}

	result["path"] = path;
	result["bytes_written"] = contents.length();
	return result;
}

Dictionary YeetAIDock::_tool_save_all_scenes() const {
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}
	editor->save_all_scenes();
	result["ok"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_reload_scene(const Dictionary &p_args) const {
	Dictionary result;
	const String scene_path = p_args.get("scene_path", "");
	if (!scene_path.begins_with("res://")) {
		result["error"] = "scene_path must start with res://";
		return result;
	}
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}
	editor->reload_scene_from_path(scene_path);
	result["scene_path"] = scene_path;
	result["reloaded"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_set_editor_main_screen(const Dictionary &p_args) const {
	Dictionary result;
	const String screen = String(p_args.get("screen", "")).strip_edges();
	if (screen.is_empty()) {
		result["error"] = "screen is required (e.g. 2D, 3D, Script, Game, AssetLib).";
		return result;
	}
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}
	editor->set_main_screen_editor(screen);
	result["screen"] = screen;
	return result;
}

Dictionary YeetAIDock::_tool_select_file(const Dictionary &p_args) const {
	Dictionary result;
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		result["error"] = "path must start with res://";
		return result;
	}
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}
	editor->select_file(path);
	result["path"] = path;
	return result;
}

Dictionary YeetAIDock::_tool_get_unsaved_scenes() const {
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}
	result["unsaved_scenes"] = editor->get_unsaved_scenes();
	return result;
}

Dictionary YeetAIDock::_tool_reparent_node(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	const String new_parent_path = p_args.get("new_parent_path", "");
	if (node_path.is_empty() || new_parent_path.is_empty()) {
		result["error"] = "node_path and new_parent_path are required (paths relative to scene root).";
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
		result["error"] = "Cannot reparent the scene root.";
		return result;
	}

	Node *new_parent = _resolve_node_target(scene_root, new_parent_path, error);
	if (new_parent == nullptr) {
		result["error"] = error;
		return result;
	}
	if (new_parent == target) {
		result["error"] = "Invalid reparent: new_parent cannot be the node itself.";
		return result;
	}
	if (target->is_ancestor_of(new_parent)) {
		result["error"] = "Invalid reparent: cannot move a node under one of its descendants.";
		return result;
	}

	const bool keep_global = bool(p_args.get("keep_global_transform", true));
	target->reparent(new_parent, keep_global);
	EditorInterface::get_singleton()->mark_scene_as_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(target->get_path());
	result["new_parent_path"] = String(new_parent->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_rename_node(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	const String new_name = String(p_args.get("new_name", "")).strip_edges();
	if (node_path.is_empty() || new_name.is_empty()) {
		result["error"] = "node_path and new_name are required.";
		return result;
	}
	if (new_name.contains("/") || new_name.contains("\\")) {
		result["error"] = "new_name must be a single segment (no path separators).";
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
		result["error"] = "Cannot rename the scene root via this tool (use save_scene_file or edit scene).";
		return result;
	}

	Node *parent = target->get_parent();
	if (parent == nullptr) {
		result["error"] = "Node has no parent.";
		return result;
	}

	const String validated = parent->prevalidate_child_name(target, StringName(new_name));
	target->set_name(validated);
	EditorInterface::get_singleton()->mark_scene_as_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(target->get_path());
	result["new_name"] = target->get_name();
	return result;
}

Dictionary YeetAIDock::_tool_file_exists(const Dictionary &p_args) const {
	Dictionary result;
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		result["error"] = "path must start with res://";
		return result;
	}
	const bool is_file = FileAccess::exists(path);
	const bool is_dir = DirAccess::exists(path);
	result["path"] = path;
	result["exists_as_file"] = is_file;
	result["exists_as_directory"] = is_dir;
	result["exists"] = is_file || is_dir;
	if (is_file) {
		result["resource_type"] = ResourceLoader::get_resource_type(path);
		Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
		if (f.is_valid()) {
			result["size_bytes"] = f->get_length();
		}
	}
	return result;
}

Dictionary YeetAIDock::_tool_list_directory(const Dictionary &p_args) const {
	Dictionary result;
	String dir_path = p_args.get("path", "res://");
	if (!dir_path.begins_with("res://")) {
		dir_path = "res://";
	}
	if (!dir_path.ends_with("/")) {
		dir_path += "/";
	}
	const int max_entries = CLAMP(int(p_args.get("max_entries", 200)), 1, 2000);
	const Array ext_filter = p_args.get("include_extensions", Array());

	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(dir_path, &open_error);
	if (dir.is_null() || open_error != OK) {
		result["error"] = vformat("Cannot open directory: %d", open_error);
		return result;
	}

	dir->set_include_hidden(false);
	dir->set_include_navigational(false);
	if (dir->list_dir_begin() != OK) {
		result["error"] = "list_dir_begin failed.";
		return result;
	}

	Array entries;
	int count = 0;
	while (count < max_entries) {
		const String name = dir->get_next();
		if (name.is_empty()) {
			break;
		}
		const String full_path = dir_path.path_join(name);
		if (!ext_filter.is_empty() && !dir->current_is_dir()) {
			const String ext = "." + full_path.get_extension().to_lower();
			if (!contains_string(_variant_array_to_string_vector(ext_filter), ext)) {
				continue;
			}
		}
		Dictionary entry;
		entry["name"] = name;
		entry["path"] = full_path;
		entry["kind"] = dir->current_is_dir() ? "dir" : "file";
		if (!dir->current_is_dir()) {
			entry["resource_type"] = ResourceLoader::get_resource_type(full_path);
		}
		entries.push_back(entry);
		count++;
	}
	dir->list_dir_end();

	result["path"] = dir_path;
	result["entries"] = entries;
	result["count"] = entries.size();
	return result;
}

Dictionary YeetAIDock::_tool_duplicate_node(const Dictionary &p_args) const {
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
		result["error"] = "Cannot duplicate the scene root.";
		return result;
	}

	Node *dup_parent = target->get_parent();
	const String parent_path_arg = String(p_args.get("parent_path", "")).strip_edges();
	if (!parent_path_arg.is_empty()) {
		dup_parent = _resolve_node_target(scene_root, parent_path_arg, error);
		if (dup_parent == nullptr) {
			result["error"] = error;
			return result;
		}
	}
	if (dup_parent == nullptr) {
		result["error"] = "Could not resolve parent for duplicate.";
		return result;
	}
	if (target->is_ancestor_of(dup_parent)) {
		result["error"] = "Invalid parent_path: cannot parent duplicate under a descendant of the source.";
		return result;
	}

	Node *dup = target->duplicate(Node::DUPLICATE_SIGNALS | Node::DUPLICATE_GROUPS | Node::DUPLICATE_SCRIPTS);
	dup_parent->add_child(dup, true);
	const String base_name = String(p_args.get("new_name", String(target->get_name()) + "Copy")).strip_edges();
	dup->set_name(dup_parent->prevalidate_child_name(dup, StringName(base_name)));
	_set_owner_recursive(dup, scene_root);
	EditorInterface::get_singleton()->mark_scene_as_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(dup->get_path());
	result["duplicated_from"] = node_path;
	return result;
}

Dictionary YeetAIDock::_tool_edit_script(const Dictionary &p_args) const {
	Dictionary result;
	const String script_path = p_args.get("script_path", "");
	if (!script_path.begins_with("res://") || !script_path.ends_with(".gd")) {
		result["error"] = "script_path must be a res:// path to a .gd file.";
		return result;
	}
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	const Ref<Script> scr = ResourceLoader::load(script_path);
	if (scr.is_null()) {
		result["error"] = "Failed to load script (not found or not a Script resource).";
		return result;
	}

	const int line = int(p_args.get("line", -1));
	const int col = int(p_args.get("column", 0));
	const bool grab_focus = bool(p_args.get("grab_focus", true));
	editor->edit_script(scr, line, col, grab_focus);
	result["script_path"] = script_path;
	result["line"] = line;
	return result;
}

Dictionary YeetAIDock::_tool_move_child(const Dictionary &p_args) const {
	Dictionary result;
	const String node_path = p_args.get("node_path", "");
	if (node_path.is_empty() || !p_args.has("new_index")) {
		result["error"] = "node_path and new_index are required.";
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
	Node *parent = target->get_parent();
	if (parent == nullptr) {
		result["error"] = "Node has no parent.";
		return result;
	}

	int new_index = int(p_args["new_index"]);
	new_index = CLAMP(new_index, 0, parent->get_child_count() - 1);
	parent->move_child(target, new_index);
	EditorInterface::get_singleton()->mark_scene_as_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(target->get_path());
	result["new_index"] = new_index;
	return result;
}

Dictionary YeetAIDock::_tool_create_project_folder(const Dictionary &p_args) const {
	Dictionary result;
	const String folder_path = p_args.get("path", "");
	if (!folder_path.begins_with("res://")) {
		result["error"] = "path must start with res://";
		return result;
	}
	const Error err = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(folder_path));
	if (err != OK) {
		result["error"] = vformat("make_dir_recursive failed: %d", err);
		return result;
	}
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->scan();
	}
	result["path"] = folder_path;
	return result;
}

Dictionary YeetAIDock::_tool_delete_project_file(const Dictionary &p_args) const {
	Dictionary result;
	if (!bool(p_args.get("confirm", false))) {
		result["error"] = "Refusing to delete without confirm:true.";
		return result;
	}
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		result["error"] = "path must start with res://";
		return result;
	}
	if (!_is_allowed_text_file(path)) {
		result["error"] = "Only the same safe extensions as write_project_file are allowed.";
		return result;
	}
	if (!FileAccess::exists(path)) {
		result["error"] = "File does not exist.";
		return result;
	}

	Ref<DirAccess> da = DirAccess::open(path.get_base_dir());
	if (da.is_null()) {
		result["error"] = "Cannot open parent directory.";
		return result;
	}
	const Error err = da->remove(path.get_file());
	if (err != OK) {
		result["error"] = vformat("remove failed: %d", err);
		return result;
	}

	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->update_file(path);
	}

	result["path"] = path;
	result["deleted"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_get_editor_log(const Dictionary &p_args) const {
	Dictionary result;
	EditorLog *log = EditorNode::get_log();
	if (log == nullptr) {
		result["error"] = "Editor log is unavailable.";
		return result;
	}
	const int max_lines = CLAMP(int(p_args.get("max_lines", 200)), 1, 2000);
	const Array messages = log->get_recent_messages(max_lines);
	result["messages"] = messages;
	result["count"] = messages.size();
	return result;
}

Dictionary YeetAIDock::_tool_capture_editor_viewport(const Dictionary &p_args) const {
	Dictionary result;
	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	const String target = String(p_args.get("target", "editor_3d")).to_lower();
	SubViewport *vp = nullptr;
	if (target == "editor_2d" || target == "2d") {
		vp = editor->get_editor_viewport_2d();
	} else {
		const int idx = CLAMP(int(p_args.get("viewport_sub_index", 0)), 0, 7);
		vp = editor->get_editor_viewport_3d(idx);
	}

	if (vp == nullptr) {
		result["error"] = "Editor viewport is not available (wrong target or index).";
		return result;
	}

	const Ref<ViewportTexture> tex = vp->get_texture();
	if (tex.is_null()) {
		result["error"] = "Viewport has no texture.";
		return result;
	}

	Ref<Image> img = tex->get_image();
	if (img.is_null() || img->is_empty()) {
		result["error"] = "Viewport image is empty; try switching to the 2D/3D editor tab and retry.";
		return result;
	}

	const int max_w = CLAMP(int(p_args.get("max_width", 640)), 64, 4096);
	if (img->get_width() > max_w) {
		const int nh = MAX(1, int(img->get_height() * (float(max_w) / float(img->get_width()))));
		img->resize(max_w, nh, Image::INTERPOLATE_BILINEAR);
	}

	CoreBind::Marshalls *marshalls = CoreBind::Marshalls::get_singleton();
	if (marshalls == nullptr) {
		result["error"] = "Marshalls singleton is unavailable.";
		return result;
	}

	const Vector<uint8_t> png = img->save_png_to_buffer();
	String b64 = marshalls->raw_to_base64(png);
	const int max_b64 = _get_editor_setting_int("yeet_ai/chat/max_base64_chars", 2000000);
	if (b64.length() > max_b64) {
		result["warning"] = vformat("png_base64 was truncated from %d to %d characters (yeet_ai/chat/max_base64_chars).", b64.length(), max_b64);
		b64 = b64.substr(0, max_b64);
	}
	result["width"] = img->get_width();
	result["height"] = img->get_height();
	result["format"] = "png";
	result["png_base64"] = b64;
	result["target"] = target;
	result["source"] = "editor_viewport";
	return result;
}

Dictionary YeetAIDock::_tool_capture_game_viewport(const Dictionary &p_args) const {
	Dictionary result;
	EditorInterface *ei = EditorInterface::get_singleton();
	if (ei == nullptr || !ei->is_playing_scene()) {
		result["error"] = "No game is running (start play mode with embedded game first).";
		return result;
	}

	YeetAIDock *dock = const_cast<YeetAIDock *>(this);
	dock->game_screenshot_done = false;
	dock->game_screenshot_path = String();
	if (!EditorRun::request_screenshot(callable_mp(dock, &YeetAIDock::_on_game_screenshot_cb))) {
		result["error"] = "Could not request a game screenshot (embedded game view may be unavailable).";
		return result;
	}

	const int timeout_ms = CLAMP(_get_editor_setting_int("yeet_ai/chat/game_screenshot_timeout_ms", 8000), 500, 60000);
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + uint64_t(timeout_ms);
	while (!dock->game_screenshot_done && OS::get_singleton()->get_ticks_msec() < deadline) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}

	if (!dock->game_screenshot_done) {
		result["error"] = "Timed out waiting for game screenshot.";
		return result;
	}

	Ref<Image> img = Image::load_from_file(dock->game_screenshot_path);
	if (img.is_null() || img->is_empty()) {
		result["error"] = "Failed to load screenshot image from temporary path.";
		return result;
	}

	const int max_w = CLAMP(int(p_args.get("max_width", 640)), 64, 4096);
	if (img->get_width() > max_w) {
		const int nh = MAX(1, int(img->get_height() * (float(max_w) / float(img->get_width()))));
		img->resize(max_w, nh, Image::INTERPOLATE_BILINEAR);
	}

	CoreBind::Marshalls *marshalls = CoreBind::Marshalls::get_singleton();
	if (marshalls == nullptr) {
		result["error"] = "Marshalls singleton is unavailable.";
		return result;
	}

	const Vector<uint8_t> png = img->save_png_to_buffer();
	String b64 = marshalls->raw_to_base64(png);
	const int max_b64 = _get_editor_setting_int("yeet_ai/chat/max_base64_chars", 2000000);
	if (b64.length() > max_b64) {
		result["warning"] = vformat("png_base64 was truncated from %d to %d characters (yeet_ai/chat/max_base64_chars).", b64.length(), max_b64);
		b64 = b64.substr(0, max_b64);
	}

	result["width"] = img->get_width();
	result["height"] = img->get_height();
	result["format"] = "png";
	result["png_base64"] = b64;
	result["source"] = "embedded_game";
	result["raw_width"] = int(dock->game_screenshot_w);
	result["raw_height"] = int(dock->game_screenshot_h);
	return result;
}

Dictionary YeetAIDock::_tool_get_runtime_debugger_state() const {
	Dictionary result;
	EditorDebuggerNode *edn = EditorDebuggerNode::get_singleton();
	if (edn == nullptr) {
		result["error"] = "EditorDebuggerNode is unavailable.";
		return result;
	}

	ScriptEditorDebugger *dbg = edn->get_default_debugger();
	if (dbg == nullptr) {
		result["error"] = "No script debugger instance (start a debug session by running the project).";
		return result;
	}

	result["session_active"] = dbg->is_session_active();
	result["error_count"] = dbg->get_error_count();
	result["warning_count"] = dbg->get_warning_count();
	result["is_breaked"] = dbg->is_breaked();
	result["stack_script_file"] = dbg->get_stack_script_file();
	result["stack_script_line"] = dbg->get_stack_script_line();
	result["stack_script_frame"] = dbg->get_stack_script_frame();
	result["remote_pid"] = dbg->get_remote_pid();
	return result;
}

Dictionary YeetAIDock::_tool_editor_undo(const Dictionary &p_args) const {
	Dictionary result;
	EditorInterface *ei = EditorInterface::get_singleton();
	if (ei == nullptr) {
		result["error"] = "EditorInterface is unavailable.";
		return result;
	}

	EditorUndoRedoManager *urm = ei->get_editor_undo_redo();
	if (urm == nullptr) {
		result["error"] = "EditorUndoRedoManager is unavailable.";
		return result;
	}

	const int steps = CLAMP(int(p_args.get("steps", 1)), 1, 50);
	int undone = 0;
	for (int i = 0; i < steps; i++) {
		if (!urm->undo()) {
			break;
		}
		undone++;
	}

	result["undone_steps"] = undone;
	result["requested_steps"] = steps;
	return result;
}

Dictionary YeetAIDock::_tool_get_debug_snapshot(const Dictionary &p_args) const {
	Dictionary result;
	const int max_log = CLAMP(int(p_args.get("max_log_lines", 80)), 1, 500);
	Dictionary log_args;
	log_args["max_lines"] = max_log;
	result["editor_log"] = _tool_get_editor_log(log_args);
	result["current_scene"] = _tool_get_current_scene();
	result["selected_nodes"] = _tool_get_selected_nodes();
	result["unsaved_scenes"] = _tool_get_unsaved_scenes();
	result["open_scenes"] = _tool_get_open_scenes();
	EditorInterface *ei = EditorInterface::get_singleton();
	if (ei != nullptr) {
		result["is_playing"] = ei->is_playing_scene();
		result["playing_scene"] = ei->get_playing_scene();
	}
	return result;
}

Dictionary YeetAIDock::_tool_grep_project_files(const Dictionary &p_args) const {
	Dictionary result;
	const String query = String(p_args.get("query", "")).strip_edges();
	if (query.is_empty()) {
		result["error"] = "query is required.";
		return result;
	}

	String root = String(p_args.get("path", "res://"));
	if (!root.begins_with("res://")) {
		root = "res://";
	}
	if (!root.ends_with("/")) {
		root += "/";
	}

	const int max_matches = CLAMP(int(p_args.get("max_results", 40)), 1, 200);
	const int max_bytes = CLAMP(int(p_args.get("max_file_bytes", 256 * 1024)), 1024, 2 * 1024 * 1024);
	const bool case_sensitive = bool(p_args.get("case_sensitive", false));

	Vector<String> exts = _variant_array_to_string_vector(p_args.get("include_extensions", Array()));
	if (exts.is_empty()) {
		static const char *defaults[] = {
			".gd", ".tscn", ".godot", ".tres", ".cfg", ".gdshader", ".shader", ".md", ".txt", ".json", nullptr
		};
		for (int i = 0; defaults[i] != nullptr; i++) {
			exts.push_back(String(defaults[i]));
		}
	}

	Array matches;
	int match_count = 0;
	_grep_project_files_recursive(root, query, case_sensitive, exts, max_bytes, max_matches, match_count, matches);

	result["matches"] = matches;
	result["match_count"] = matches.size();
	result["root"] = root;
	return result;
}

void YeetAIDock::_grep_project_files_recursive(const String &p_dir, const String &p_query, bool p_case_sensitive, const Vector<String> &p_extensions, int p_max_bytes, int p_max_matches, int &r_match_count, Array &r_matches) const {
	if (r_match_count >= p_max_matches) {
		return;
	}

	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(p_dir, &open_error);
	if (dir.is_null() || open_error != OK) {
		return;
	}

	dir->set_include_hidden(false);
	dir->set_include_navigational(false);
	if (dir->list_dir_begin() != OK) {
		return;
	}

	while (r_match_count < p_max_matches) {
		const String name = dir->get_next();
		if (name.is_empty()) {
			break;
		}

		const String full_path = p_dir.path_join(name);
		if (dir->current_is_dir()) {
			if (name == ".godot" || name == ".git") {
				continue;
			}
			_grep_project_files_recursive(full_path, p_query, p_case_sensitive, p_extensions, p_max_bytes, p_max_matches, r_match_count, r_matches);
			continue;
		}

		if (!p_extensions.is_empty()) {
			const String ext = "." + full_path.get_extension().to_lower();
			if (!contains_string(p_extensions, ext)) {
				continue;
			}
		}

		Error ferr = OK;
		const String content = FileAccess::get_file_as_string(full_path, &ferr);
		if (ferr != OK) {
			continue;
		}

		String scan = content;
		if (scan.length() > p_max_bytes) {
			scan = scan.substr(0, p_max_bytes);
		}

		const String hay = p_case_sensitive ? scan : scan.to_lower();
		const String needle = p_case_sensitive ? p_query : p_query.to_lower();
		if (!hay.contains(needle)) {
			continue;
		}

		int first_line = 1;
		const PackedStringArray lines = scan.split("\n");
		for (int i = 0; i < lines.size(); i++) {
			const String line_text = p_case_sensitive ? lines[i] : lines[i].to_lower();
			if (line_text.contains(needle)) {
				first_line = i + 1;
				break;
			}
		}

		Dictionary hit;
		hit["path"] = full_path;
		hit["first_line"] = first_line;
		r_matches.push_back(hit);
		r_match_count++;
	}

	dir->list_dir_end();
}

Dictionary YeetAIDock::_tool_get_autoloads() const {
	Dictionary result;
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps == nullptr) {
		result["error"] = "ProjectSettings is unavailable.";
		return result;
	}

	Array autoloads;
	const HashMap<StringName, ProjectSettings::AutoloadInfo> &map = ps->get_autoload_list();
	for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &E : map) {
		Dictionary entry;
		entry["name"] = String(E.key);
		entry["path"] = E.value.path;
		entry["singleton"] = E.value.is_singleton;
		autoloads.push_back(entry);
	}

	result["autoloads"] = autoloads;
	result["count"] = autoloads.size();
	return result;
}

Dictionary YeetAIDock::_tool_get_node_groups(const Dictionary &p_args) const {
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

	List<Node::GroupInfo> groups;
	node->get_groups(&groups);
	Array group_names;
	for (const Node::GroupInfo &gi : groups) {
		group_names.push_back(String(gi.name));
	}

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(node->get_path());
	result["groups"] = group_names;
	return result;
}

Dictionary YeetAIDock::_tool_get_node_collision_layers(const Dictionary &p_args) const {
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

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(node->get_path());

	if (CollisionObject3D *co3 = Object::cast_to<CollisionObject3D>(node)) {
		result["dimension"] = "3d";
		result["collision_layer"] = co3->get_collision_layer();
		result["collision_mask"] = co3->get_collision_mask();
		return result;
	}
	if (CollisionObject2D *co2 = Object::cast_to<CollisionObject2D>(node)) {
		result["dimension"] = "2d";
		result["collision_layer"] = co2->get_collision_layer();
		result["collision_mask"] = co2->get_collision_mask();
		return result;
	}

	result["error"] = "Node is not a CollisionObject2D or CollisionObject3D.";
	return result;
}

Dictionary YeetAIDock::_tool_move_project_file(const Dictionary &p_args) const {
	Dictionary result;
	const String from_path = p_args.get("from_path", "");
	const String to_path = p_args.get("to_path", "");
	if (!from_path.begins_with("res://") || !to_path.begins_with("res://")) {
		result["error"] = "from_path and to_path must start with res://";
		return result;
	}
	if (from_path == to_path) {
		result["error"] = "from_path and to_path must differ.";
		return result;
	}
	if (!_is_allowed_text_file(from_path) || !_is_allowed_text_file(to_path)) {
		result["error"] = "Only safe text/resource extensions are allowed (same allowlist as write_project_file).";
		return result;
	}
	if (!FileAccess::exists(from_path)) {
		result["error"] = "Source file does not exist.";
		return result;
	}
	if (FileAccess::exists(to_path) || DirAccess::exists(to_path)) {
		result["error"] = "Target path already exists.";
		return result;
	}

	const Error mkdir_err = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(to_path.get_base_dir()));
	if (mkdir_err != OK) {
		result["error"] = vformat("Failed to create target directory: %d", mkdir_err);
		return result;
	}

	const String from_abs = ProjectSettings::get_singleton()->globalize_path(from_path);
	const String to_abs = ProjectSettings::get_singleton()->globalize_path(to_path);
	const Error err = DirAccess::rename_absolute(from_abs, to_abs);
	if (err != OK) {
		result["error"] = vformat("rename_absolute failed: %d", err);
		return result;
	}

	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->scan();
	}

	result["from_path"] = from_path;
	result["to_path"] = to_path;
	result["moved"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_get_animation_player_state(const Dictionary &p_args) const {
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

	AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(node);
	if (ap == nullptr) {
		result["error"] = "Node is not an AnimationPlayer.";
		return result;
	}

	const LocalVector<StringName> sorted = ap->get_sorted_animation_list();
	Array animations;
	for (const StringName &n : sorted) {
		animations.push_back(String(n));
	}

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(ap->get_path());
	result["animations"] = animations;
	result["current_animation"] = String(ap->get_current_animation());
	result["assigned_animation"] = String(ap->get_assigned_animation());
	result["current_position"] = ap->get_current_animation_position();
	result["current_length"] = ap->get_current_animation_length();
	result["active"] = ap->is_active();
	return result;
}

Dictionary YeetAIDock::_tool_get_tilemap_info(const Dictionary &p_args) const {
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

	TileMap *tm = Object::cast_to<TileMap>(node);
	if (tm == nullptr) {
		result["error"] = "Node is not a TileMap.";
		return result;
	}

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(tm->get_path());
	result["layers_count"] = tm->get_layers_count();

	const Ref<TileSet> ts = tm->get_tileset();
	if (ts.is_valid()) {
		result["tileset_resource_path"] = ts->get_path();
	} else {
		result["tileset_resource_path"] = String();
	}

	const Rect2i ur = tm->get_used_rect();
	Dictionary urd;
	urd["x"] = ur.position.x;
	urd["y"] = ur.position.y;
	urd["w"] = ur.size.x;
	urd["h"] = ur.size.y;
	result["used_rect"] = urd;

	Array layer_names;
	const int lc = tm->get_layers_count();
	for (int i = 0; i < lc; i++) {
		layer_names.push_back(tm->get_layer_name(i));
	}
	result["layer_names"] = layer_names;

	return result;
}

Dictionary YeetAIDock::_tool_get_navigation_region_info(const Dictionary &p_args) const {
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

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(node->get_path());

	if (NavigationRegion3D *r3 = Object::cast_to<NavigationRegion3D>(node)) {
		result["dimension"] = "3d";
		result["enabled"] = r3->is_enabled();
		result["navigation_layers"] = r3->get_navigation_layers();
		result["is_baking"] = r3->is_baking();
		const AABB b = r3->get_bounds();
		Dictionary bd;
		bd["position"] = _json_safe_variant(b.position);
		bd["size"] = _json_safe_variant(b.size);
		result["bounds"] = bd;
		const Ref<NavigationMesh> nm = r3->get_navigation_mesh();
		result["has_navigation_mesh"] = nm.is_valid();
		return result;
	}

	if (NavigationRegion2D *r2 = Object::cast_to<NavigationRegion2D>(node)) {
		result["dimension"] = "2d";
		result["enabled"] = r2->is_enabled();
		result["navigation_layers"] = r2->get_navigation_layers();
		result["is_baking"] = r2->is_baking();
		const Rect2 b = r2->get_bounds();
		Dictionary bd;
		bd["x"] = b.position.x;
		bd["y"] = b.position.y;
		bd["w"] = b.size.x;
		bd["h"] = b.size.y;
		result["bounds"] = bd;
		const Ref<NavigationPolygon> np = r2->get_navigation_polygon();
		result["has_navigation_polygon"] = np.is_valid();
		return result;
	}

	result["error"] = "Node is not a NavigationRegion2D or NavigationRegion3D.";
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
			"You are queried through an OpenAI-compatible `/v1/chat/completions` API (local models such as Qwen3.5 are common). "
			"Reply with a single JSON object only: no markdown code fences, no analysis before or after the JSON, no `<think>` blocks.\n"
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
			"{\"type\":\"tool_call\",\"tool\":\"write_project_file\",\"arguments\":{\"path\":\"res://notes.txt\",\"contents\":\"hello\",\"overwrite\":true}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"save_all_scenes\",\"arguments\":{}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"reload_scene\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"set_editor_main_screen\",\"arguments\":{\"screen\":\"3D\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"select_file\",\"arguments\":{\"path\":\"res://scripts/player.gd\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_unsaved_scenes\",\"arguments\":{}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"reparent_node\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"Crate\",\"new_parent_path\":\"GameplayRoot\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"rename_node\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"Crate\",\"new_name\":\"PhysicsCrate\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"file_exists\",\"arguments\":{\"path\":\"res://scripts/player.gd\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"list_directory\",\"arguments\":{\"path\":\"res://scripts/\",\"max_entries\":50,\"include_extensions\":[\".gd\"]}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"duplicate_node\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"Crate\",\"parent_path\":\".\",\"new_name\":\"Crate2\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"edit_script\",\"arguments\":{\"script_path\":\"res://scripts/player.gd\",\"line\":10,\"column\":0}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"move_child\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"Crate\",\"new_index\":0}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"create_project_folder\",\"arguments\":{\"path\":\"res://generated/\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"delete_project_file\",\"arguments\":{\"path\":\"res://tmp.txt\",\"confirm\":true}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_editor_log\",\"arguments\":{\"max_lines\":120}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"capture_editor_viewport\",\"arguments\":{\"target\":\"editor_3d\",\"max_width\":640}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_debug_snapshot\",\"arguments\":{\"max_log_lines\":60}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"grep_project_files\",\"arguments\":{\"query\":\"func _physics_process\",\"path\":\"res://\",\"max_results\":30}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_autoloads\",\"arguments\":{}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_node_groups\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"Player\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_node_collision_layers\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"Player\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"move_project_file\",\"arguments\":{\"from_path\":\"res://scripts/old.gd\",\"to_path\":\"res://scripts/new.gd\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_animation_player_state\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"Player/AnimationPlayer\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_tilemap_info\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"TileMap\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_navigation_region_info\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"node_path\":\"NavigationRegion3D\"}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"capture_game_viewport\",\"arguments\":{\"max_width\":640}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"get_runtime_debugger_state\",\"arguments\":{}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"editor_undo\",\"arguments\":{\"steps\":1}}\n"
			"{\"type\":\"tool_call\",\"tool\":\"batch_tool_calls\",\"arguments\":{\"dry_run\":true,\"calls\":[{\"tool\":\"add_node\",\"arguments\":{\"parent_path\":\".\",\"node_type\":\"Node3D\",\"node_name\":\"Test\"}}]}}\n"
			"{\"type\":\"final\",\"message\":\"Your answer here\"}\n"
			"Core behavior:\n"
			"- Never wrap JSON in markdown.\n"
			"- You will receive a live project/editor snapshot in a separate system message. Use it first.\n"
			"- **GDScript vs editor batches:** Never put `create_gdscript_file` or `update_gdscript_file` in the same `batch_tool_calls` as any other tool. GDScript payloads are large; mixing them with scene/editor steps causes truncation. Pattern: tool_call #1 = `batch_tool_calls` with only non-GDScript tools (scene, inputs, meshes, materials, `attach_script`, `write_project_file` for non-.gd, etc.); tool_call #2 = `batch_tool_calls` whose `calls` array contains **only** `create_gdscript_file` and/or `update_gdscript_file` (you may include multiple script steps in that batch). The editor rejects mixed batches.\n"
			"- Prefer one editor `batch_tool_calls` for multi-step scene work when the JSON stays small; use additional rounds for more editor work or for GDScript-only batches as above.\n"
			"- If your JSON is cut off or invalid, raise Editor Setting `yeet_ai/chat/max_tokens` and raise the inference server’s max output tokens; large batches need high headroom on both sides.\n"
			"- Reliability: When the user asks for multiple outcomes (movement + jump + color + mesh shape), you must implement all of them. A single `add_primitive_mesh` is never sufficient for \"controllable\" or \"WASD\" requests.\n"
			"- `batch_tool_calls` supports `dry_run:true` to list resolved calls without executing.\n"
			"- `capture_game_viewport` grabs the embedded running game (after play); `capture_editor_viewport` grabs the 2D/3D editor view.\n"
			"- Enable Editor Settings `yeet_ai/chat/vision_enabled` to send screenshot base64 to vision-capable chat models (uses OpenAI-style image_url content parts).\n"
			"- `get_runtime_debugger_state` reads the script debugger (errors, warnings, stack file/line when broken).\n"
			"- `editor_undo` runs the editor undo stack (default one step; max 50).\n"
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
			"- Player controller requests (WASD, jump, space, \"control\"): always combine input (`get_input_actions` / `create_input_action`), physics body + collision, movement script, and visual (mesh/material). Never ship only the mesh.\n"
			"- `set_node_property` args: scene_path, node_path, property (string), value. Example: {\"property\":\"position\",\"value\":{\"x\":0,\"y\":5,\"z\":0}}.\n"
			"Context tools:\n"
			"- `get_project_tree` lists project files and directories.\n"
			"- `find_project_files` searches project paths by substring.\n"
			"- `read_project_file` reads safe text files including `project.godot`, scenes, scripts, and resources.\n"
			"- `get_project_settings` reads key project settings such as main scene and project name.\n"
			"- `get_input_actions` reads current input actions and bindings.\n"
			"- `get_open_scenes`, `get_current_scene`, `get_selected_nodes`, `get_scene_tree`, `get_node_details`, and `get_node_api` inspect editor state and scene capabilities.\n"
			"- `get_unsaved_scenes` lists scenes with unsaved edits.\n"
			"- `file_exists` checks whether a res:// path exists as a file or directory and returns size/type for files.\n"
			"- `list_directory` lists one directory level under res:// (optional extension filter).\n"
			"- `get_editor_log` returns recent editor Output / error / warning lines (newest first).\n"
			"- `capture_editor_viewport` captures the 2D or 3D editor viewport as PNG (base64); use after focusing the right tab.\n"
			"- `get_debug_snapshot` bundles log tail, current scene, selection, unsaved scenes, and play state for quick diagnosis.\n"
			"- `grep_project_files` searches file contents under res:// (substring; optional extensions and case sensitivity).\n"
			"- `get_autoloads` lists project autoload singletons and script paths.\n"
			"- `get_node_groups` and `get_node_collision_layers` read groups and physics layers/masks for a node.\n"
			"- `get_animation_player_state`, `get_tilemap_info`, and `get_navigation_region_info` summarize those node types.\n"
			"Write tools:\n"
			"- `create_scene_file`, `open_scene`, `save_current_scene`, `save_all_scenes`, `reload_scene`, `add_node`, `reparent_node`, `rename_node`, `duplicate_node`, `move_child`, `instantiate_scene`, `add_primitive_mesh`, `add_collision_shape`, `set_node_property`, `remove_node`, `create_standard_material`, `assign_resource_to_property`, `attach_script`, `create_gdscript_file`, `update_gdscript_file`, `write_project_file`, `create_project_folder`, `delete_project_file` (requires confirm:true), `connect_signal`, `create_input_action`, and `set_main_scene` mutate the project.\n"
			"- In `batch_tool_calls`, `create_gdscript_file` / `update_gdscript_file` cannot appear alongside any other tool in the same batch (use a dedicated GDScript-only batch).\n"
			"- `set_editor_main_screen` switches the main editor tab (e.g. 2D, 3D, Script, Game, AssetLib).\n"
			"- `select_file` focuses a file in the FileSystem dock; `edit_script` opens a .gd script in the script editor at an optional line.\n"
			"- `play_current_scene`, `play_main_scene`, and `stop_playing_scene` control editor play mode.\n"
			"- `move_project_file` renames/moves a file under res:// (safe extensions only); destination must not exist.\n"
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
