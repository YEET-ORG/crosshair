/**************************************************************************/
/*  yeet_ai_dock.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "yeet_ai_tool_schema.h"

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
#include "scene/gui/texture_rect.h"
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

static String _clean_json_string_field(const Variant &p_value) {
	if (p_value.get_type() != Variant::STRING && p_value.get_type() != Variant::STRING_NAME) {
		return String();
	}
	const String value = String(p_value).strip_edges();
	const String lower = value.to_lower();
	if (value.is_empty() || lower == "<null>" || lower == "null") {
		return String();
	}
	return value;
}

static String _clean_json_string_field(const Dictionary &p_dict, const StringName &p_key) {
	if (!p_dict.has(p_key)) {
		return String();
	}
	return _clean_json_string_field(p_dict[p_key]);
}

// Strip non-ASCII / non-printable artifacts from LLM keep-alive and streaming noise.
// Keeps ASCII printable (32-126), tabs, and newlines only.
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
	String t = _clean_json_string_field(d, SNAME("tool"));
	if (!t.is_empty()) {
		return t;
	}
	return _clean_json_string_field(d, SNAME("name"));
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
	const String typ = _clean_json_string_field(d, SNAME("type"));
	return _json_frag_type_is_allowed_on_args_fragment(typ);
}

static bool _json_frag_is_tool_header(const Dictionary &d) {
	const String tool = _json_frag_effective_tool_name(d);
	if (tool.is_empty()) {
		return false;
	}
	const String typ = _clean_json_string_field(d, SNAME("type"));
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
	const String t = _clean_json_string_field(p_norm, SNAME("type"));
	if (t == "final") {
		return true;
	}
	return t == "tool_call" && !_clean_json_string_field(p_norm, SNAME("tool")).is_empty();
}

// Lift batch_tool_calls top-level keys ("calls", "tool_calls", "shared_arguments",
// "stop_on_error", "dry_run") into the inner arguments dict. Models often emit
// the calls list at the envelope root rather than nested under "arguments";
// without this lift the dispatcher receives an empty args dict and bails with
// "calls is required and must contain at least one tool call."
static void _lift_batch_top_level_into_args(const Dictionary &p_envelope, Dictionary &io_args) {
	if (!io_args.has("calls") && !io_args.has("tool_calls")) {
		if (p_envelope.has("calls")) {
			io_args["calls"] = p_envelope["calls"];
		} else if (p_envelope.has("tool_calls")) {
			io_args["calls"] = p_envelope["tool_calls"];
		}
	}
	static const char *passthrough_keys[] = { "shared_arguments", "stop_on_error", "dry_run", nullptr };
	for (int i = 0; passthrough_keys[i] != nullptr; i++) {
		const String k(passthrough_keys[i]);
		if (!io_args.has(k) && p_envelope.has(k)) {
			io_args[k] = p_envelope[k];
		}
	}
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
	ClassDB::bind_method(D_METHOD("_init_conversation_window"), &YeetAIDock::_init_conversation_window);
	ClassDB::bind_method(D_METHOD("_add_message_to_window", "role", "text"), &YeetAIDock::_add_message_to_window);
	ClassDB::bind_method(D_METHOD("_trim_conversation_window"), &YeetAIDock::_trim_conversation_window);
	ClassDB::bind_method(D_METHOD("_trim_to_fit", "new_message_tokens"), &YeetAIDock::_trim_to_fit);
	ClassDB::bind_method(D_METHOD("_get_windowed_messages_count"), &YeetAIDock::_get_windowed_messages_count);

	ClassDB::bind_method(D_METHOD("_record_tool_execution", "tool_name", "duration_ms", "success", "was_retry"), &YeetAIDock::_record_tool_execution);
	ClassDB::bind_method(D_METHOD("_export_metrics_summary"), &YeetAIDock::_export_metrics_summary);
	ClassDB::bind_method(D_METHOD("_reset_metrics"), &YeetAIDock::_reset_metrics);

	ClassDB::bind_method(D_METHOD("_evict_expired_cache_entries"), &YeetAIDock::_evict_expired_cache_entries);
	ClassDB::bind_method(D_METHOD("_evict_least_recently_used", "max_entries"), &YeetAIDock::_evict_least_recently_used);
	ClassDB::bind_method(D_METHOD("_evict_cache_if_needed"), &YeetAIDock::_evict_cache_if_needed);

	ClassDB::bind_method(D_METHOD("_calculate_backoff_delay", "attempt"), &YeetAIDock::_calculate_backoff_delay);

	ClassDB::bind_method(D_METHOD("_pause_streaming"), &YeetAIDock::_pause_streaming);
	ClassDB::bind_method(D_METHOD("_resume_streaming"), &YeetAIDock::_resume_streaming);
	ClassDB::bind_method(D_METHOD("_save_stream_snapshot"), &YeetAIDock::_save_stream_snapshot);
	ClassDB::bind_method(D_METHOD("_restore_stream_snapshot"), &YeetAIDock::_restore_stream_snapshot);
	ClassDB::bind_method(D_METHOD("_has_pending_tool_call"), &YeetAIDock::_has_pending_tool_call);

	ClassDB::bind_method(D_METHOD("_build_default_prompt_sections"), &YeetAIDock::_build_default_prompt_sections);
	ClassDB::bind_method(D_METHOD("_add_prompt_section", "id", "name", "content"), &YeetAIDock::_add_prompt_section);
	ClassDB::bind_method(D_METHOD("_remove_prompt_section", "id"), &YeetAIDock::_remove_prompt_section);
	ClassDB::bind_method(D_METHOD("_enable_prompt_section", "id", "enabled"), &YeetAIDock::_enable_prompt_section);
	ClassDB::bind_method(D_METHOD("_build_modular_system_prompt"), &YeetAIDock::_build_modular_system_prompt);

	ClassDB::bind_method(D_METHOD("_can_optimize_to_batch", "tool_name", "args"), &YeetAIDock::_can_optimize_to_batch);

	// Enhanced toolcalling methods
	ClassDB::bind_method(D_METHOD("_get_tool_call_timeout", "tool_name"), &YeetAIDock::_get_tool_call_timeout);
	ClassDB::bind_method(D_METHOD("_start_tool_call_timeout", "tool_name"), &YeetAIDock::_start_tool_call_timeout);
	ClassDB::bind_method(D_METHOD("_check_tool_call_timeout"), &YeetAIDock::_check_tool_call_timeout);
	ClassDB::bind_method(D_METHOD("_cancel_tool_call_timeout"), &YeetAIDock::_cancel_tool_call_timeout);

	ClassDB::bind_method(D_METHOD("_normalize_tool_arguments", "tool_name", "args"), &YeetAIDock::_normalize_tool_arguments);

 	ClassDB::bind_method(D_METHOD("_get_rate_limit_for_tool", "tool_name"), &YeetAIDock::_get_rate_limit_for_tool);
 	ClassDB::bind_method(D_METHOD("_check_rate_limit", "tool_name"), &YeetAIDock::_check_rate_limit);
 	ClassDB::bind_method(D_METHOD("_record_tool_call", "tool_name"), &YeetAIDock::_record_tool_call);

 	ClassDB::bind_method(D_METHOD("_classify_error", "error_msg"), &YeetAIDock::_classify_error);
 	ClassDB::bind_method(D_METHOD("_generate_recovery_context", "tool_name", "args", "error"), &YeetAIDock::_generate_recovery_context);
 }

 void YeetAIDock::_apply_dock_theme() {
	if (!is_inside_tree()) {
		return; // Theme is not available before the dock enters the tree.
	}
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

	const int font_size = get_theme_font_size(SNAME("main_size"), EditorStringName(EditorFonts));
	const Color font_color = get_theme_color(SNAME("font_color"), EditorStringName(Editor));
	const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
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
		log->add_theme_color_override(SNAME("link_color"), accent);
		// Tight chat density: tighter baseline rhythm.
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

	// Title row: icon + title on the left, controls on the right
	HBoxContainer *title_row = memnew(HBoxContainer);
	title_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	title_row->add_theme_constant_override("separation", int(8.0f * EDSCALE));
	header_mc->add_child(title_row);

	_header_icon_rect = memnew(TextureRect);
	_header_icon_rect->set_custom_minimum_size(Size2(18, 18) * EDSCALE);
	_header_icon_rect->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	_header_icon_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	title_row->add_child(_header_icon_rect);

	// Title label
	Label *title_label = memnew(Label);
	title_label->set_text(TTR("Crosshair AI"));
	title_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	title_label->add_theme_font_size_override("font_size", int(14 * EDSCALE));
	title_row->add_child(title_label);

	// Spacer
	Node *title_spacer = memnew(Node);
	title_spacer->set_name("TitleSpacer");
	title_row->add_child(title_spacer);

	// Right controls
	HBoxContainer *controls_row = memnew(HBoxContainer);
	controls_row->add_theme_constant_override("separation", int(4.0f * EDSCALE));
	header_mc->add_child(controls_row);

	// Chat selector dropdown
	_chat_selector = memnew(OptionButton);
	_chat_selector->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	_chat_selector->set_custom_minimum_size(Size2(110, 0) * EDSCALE);
	_chat_selector->set_clip_text(true);
	_chat_selector->connect("item_selected", callable_mp(this, &YeetAIDock::_on_chat_selected));
	controls_row->add_child(_chat_selector);

	// New chat button
	_new_chat_button = memnew(Button);
	_new_chat_button->set_theme_type_variation("FlatButton");
	_new_chat_button->set_tooltip_text(TTR("New Chat (Ctrl+Shift+N)"));
	_new_chat_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_on_new_chat_pressed));
	controls_row->add_child(_new_chat_button);

	// Delete chat button
	_delete_chat_button = memnew(Button);
	_delete_chat_button->set_theme_type_variation("FlatButton");
	_delete_chat_button->set_tooltip_text(TTR("Delete Chat"));
	_delete_chat_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_on_delete_chat_pressed));
	controls_row->add_child(_delete_chat_button);

	// Agent selector
	_agent_selector = memnew(OptionButton);
	_agent_selector->set_custom_minimum_size(Size2(90, 0) * EDSCALE);
	_agent_selector->set_clip_text(true);
	_agent_selector->connect("item_selected", callable_mp(this, &YeetAIDock::_on_agent_selected));
	controls_row->add_child(_agent_selector);

	// Model selector button
	_model_selector_button = memnew(Button);
	_model_selector_button->set_theme_type_variation("FlatButton");
	_model_selector_button->set_tooltip_text(TTR("Select Model"));
	_model_selector_button->set_clip_text(true);
	_model_selector_button->set_custom_minimum_size(Size2(70, 0) * EDSCALE);
	_model_selector_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_on_model_selector_pressed));
	controls_row->add_child(_model_selector_button);

	// Status dot + label on the right
	_status_dot = memnew(ColorRect);
	_status_dot->set_custom_minimum_size(Size2(8, 8) * EDSCALE);
	_status_dot->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	_status_dot->set_color(Color(0.5f, 0.5f, 0.5f, 0.35f));
	controls_row->add_child(_status_dot);

	status_label = memnew(Label);
	status_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	status_label->set_clip_text(true);
	status_label->set_text(TTR("Ready"));
	controls_row->add_child(status_label);

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

	// Input area with bordered text box
	MarginContainer *input_inner_mc = memnew(MarginContainer);
	input_inner_mc->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input_inner_mc->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	input_inner_mc->add_theme_constant_override("margin_left", int(4.0f * EDSCALE));
	input_inner_mc->add_theme_constant_override("margin_right", int(4.0f * EDSCALE));
	input_inner_mc->add_theme_constant_override("margin_top", int(4.0f * EDSCALE));
	input_inner_mc->add_theme_constant_override("margin_bottom", int(4.0f * EDSCALE));
	input_column->add_child(input_inner_mc);

	VBoxContainer *input_vb = memnew(VBoxContainer);
	input_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input_vb->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	input_inner_mc->add_child(input_vb);

	prompt_input = memnew(TextEdit);
	prompt_input->set_custom_minimum_size(Size2(0, 96) * EDSCALE);
	prompt_input->set_placeholder(TTR("Ask Crosshair AI..."));
	prompt_input->connect("gui_input", callable_mp(this, &YeetAIDock::_on_prompt_gui_input));
	prompt_input->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	input_vb->add_child(prompt_input);

	// Button row with prominent send button
	HBoxContainer *button_row = memnew(HBoxContainer);
	button_row->add_theme_constant_override("separation", int(6.0f * EDSCALE));
	button_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input_vb->add_child(button_row);

	_token_count_label = memnew(Label);
	_token_count_label->set_h_size_flags(Control::SIZE_SHRINK_END);
	_token_count_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	_token_count_label->set_visible(false);
	button_row->add_child(_token_count_label);

	// Send button with icon
	send_button = memnew(Button);
	send_button->set_text(TTR("Send"));
	send_button->set_h_size_flags(Control::SIZE_SHRINK_END);
	send_button->set_theme_type_variation("FlatButton");
	send_button->set_tooltip_text(TTR("Send prompt (Ctrl+Enter)"));
	send_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_send_prompt));
	button_row->add_child(send_button);

	// Stop button — visible only while the AI is generating
	stop_button = memnew(Button);
	stop_button->set_text(TTR("Stop"));
	stop_button->set_h_size_flags(Control::SIZE_SHRINK_END);
	stop_button->set_theme_type_variation("FlatButton");
	stop_button->set_tooltip_text(TTR("Stop generation (Escape)"));
	stop_button->set_visible(false);
	stop_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_on_stop_pressed));
	button_row->add_child(stop_button);

	// Spacer to push clear button to the right
	Node *btn_spacer = memnew(Node);
	btn_spacer->set_name("BtnSpacer");
	button_row->add_child(btn_spacer);

	clear_button = memnew(Button);
	clear_button->set_theme_type_variation("FlatButton");
	clear_button->set_text(TTR("Clear"));
	clear_button->set_tooltip_text(TTR("Clear conversation (Ctrl+L)"));
	clear_button->set_h_size_flags(Control::SIZE_SHRINK_END);
	clear_button->connect(SceneStringName(pressed), callable_mp(this, &YeetAIDock::_clear_chat));
	button_row->add_child(clear_button);

	_model_tags_request = memnew(HTTPRequest);
	_model_tags_request->set_use_threads(true);
	_model_tags_request->set_timeout(15.0);
	_model_tags_request->connect("request_completed", callable_mp(this, &YeetAIDock::_on_model_tags_request_completed));
	add_child(_model_tags_request);

	// Must run after chat_log / stream_label exist — _create_new_chat → _switch_to_chat touches them.
	_build_agent_presets();
	_create_new_chat();
	_update_chat_selector();

	// Initialize new architecture features
	_init_conversation_window();
	_build_default_prompt_sections();
	_reset_metrics();
}

YeetAIDock::~YeetAIDock() {
	_cancel_streaming();
	_save_all_chats();
}

// ── Conversation Window Management ────────────────────────────────────────────

int YeetAIDock::ConversationWindow::estimate_message_tokens(const String &text) const {
	return text.length() / 4;
}

void YeetAIDock::_init_conversation_window() {
	_conversation_window.max_tokens = _get_editor_setting_int("yeet_ai/chat/context_token_budget", 150000);
	_conversation_window.current_tokens = 0;
	_conversation_window.trim_threshold = MAX(2000, int(_conversation_window.max_tokens * 0.8f));
	_conversation_window.sliding_enabled = true;
	_conversation_window.min_keep_messages = 3;
}

void YeetAIDock::_add_message_to_window(const String &role, const String &text) {
	if (!_conversation_window.sliding_enabled) {
		return;
	}
	int tokens = _conversation_window.estimate_message_tokens(text);
	_conversation_window.current_tokens += tokens;
}

void YeetAIDock::_trim_conversation_window() {
	if (!_conversation_window.sliding_enabled) {
		return;
	}
	if (_conversation_window.current_tokens <= _conversation_window.trim_threshold) {
		return;
	}
	int removed = 0;
	while (removed < conversation_messages.size() && _conversation_window.current_tokens > _conversation_window.trim_threshold) {
		if (removed < _conversation_window.min_keep_messages) {
			break;
		}
		const Variant &msg = conversation_messages[removed];
		if (msg.get_type() == Variant::DICTIONARY) {
			const Dictionary d = msg;
			String content = d.get("content", "");
			_conversation_window.current_tokens -= _conversation_window.estimate_message_tokens(content);
		}
		removed++;
	}
	if (removed > 0) {
		Array remaining;
		for (int i = removed; i < conversation_messages.size(); i++) {
			remaining.append(conversation_messages[i]);
		}
		conversation_messages = remaining;
	}
}

void YeetAIDock::_trim_to_fit(int new_message_tokens) {
	int total = _conversation_window.current_tokens + new_message_tokens;
	if (total > _conversation_window.max_tokens) {
		_trim_conversation_window();
	}
}

int YeetAIDock::_get_windowed_messages_count() const {
	return conversation_messages.size();
}

String YeetAIDock::_message_content_to_text(const Variant &p_content) const {
	if (p_content.get_type() == Variant::NIL) {
		return String();
	}
	if (p_content.get_type() == Variant::STRING || p_content.get_type() == Variant::STRING_NAME) {
		return String(p_content);
	}
	if (p_content.get_type() == Variant::ARRAY) {
		const Array parts = p_content;
		String out;
		for (int i = 0; i < parts.size(); i++) {
			if (i > 0 && !out.ends_with("\n")) {
				out += "\n";
			}
			const Variant part = parts[i];
			if (part.get_type() == Variant::DICTIONARY) {
				const Dictionary d = part;
				const String type = _clean_json_string_field(d, SNAME("type"));
				if (type == "text") {
					out += String(d.get("text", ""));
				} else if (type == "image_url") {
					out += "[image omitted]";
				} else {
					out += JSON::stringify(d, "", false, true);
				}
			} else {
				out += String(part);
			}
		}
		return out;
	}
	if (p_content.get_type() == Variant::DICTIONARY) {
		return JSON::stringify(p_content, "", false, true);
	}
	return String(p_content);
}

int YeetAIDock::_estimate_message_tokens(const Variant &p_message) const {
	String text;
	if (p_message.get_type() == Variant::DICTIONARY) {
		const Dictionary d = p_message;
		text = _message_content_to_text(d.get("content", Variant()));
		if (d.has("tool_calls")) {
			text += JSON::stringify(d.get("tool_calls", Array()), "", false, true);
		}
	} else {
		text = _message_content_to_text(p_message);
	}
	return MAX(1, text.length() / 4);
}

int YeetAIDock::_estimate_messages_tokens(const Array &p_messages) const {
	int tokens = 0;
	for (const Variant &message : p_messages) {
		tokens += _estimate_message_tokens(message);
	}
	return tokens;
}

bool YeetAIDock::_is_context_summarization_enabled() const {
	return _get_editor_setting_bool("yeet_ai/chat/context_summarization_enabled", true);
}

int YeetAIDock::_get_context_summary_trigger_tokens() const {
	return CLAMP(_get_editor_setting_int("yeet_ai/chat/context_summary_trigger_tokens", 24000), 1000, 500000);
}

int YeetAIDock::_get_context_summary_keep_recent_messages() const {
	return CLAMP(_get_editor_setting_int("yeet_ai/chat/context_summary_keep_recent_messages", 16), 2, 200);
}

int YeetAIDock::_get_context_summary_max_chars() const {
	return CLAMP(_get_editor_setting_int("yeet_ai/chat/context_summary_max_chars", 12000), 2000, 100000);
}

String YeetAIDock::_truncate_context_summary(const String &p_summary) const {
	const int max_chars = _get_context_summary_max_chars();
	if (p_summary.length() <= max_chars) {
		return p_summary;
	}
	String tail = p_summary.substr(p_summary.length() - max_chars, max_chars);
	const int first_newline = tail.find("\n");
	if (first_newline > 0 && first_newline < tail.length() - 1) {
		tail = tail.substr(first_newline + 1);
	}
	return "[Earlier summary entries were compacted.]\n" + tail;
}

String YeetAIDock::_summarize_message_for_context(const Dictionary &p_message) const {
	const String role = _clean_json_string_field(p_message, SNAME("role"));
	String content = _clean_content_artifacts(_message_content_to_text(p_message.get("content", Variant()))).strip_edges();
	content = content.replace("\r", " ").replace("\n", " ");

	if (role == "assistant" && content.begins_with("{")) {
		const Variant parsed = JSON::parse_string(content);
		if (parsed.get_type() == Variant::DICTIONARY) {
			const Dictionary d = parsed;
			const String type = _clean_json_string_field(d, SNAME("type"));
			if (type == "tool_call") {
				const String tool_name = _clean_json_string_field(d, SNAME("tool"));
				String args = JSON::stringify(d.get("arguments", Dictionary()), "", false, true);
				if (args.length() > 360) {
					args = args.substr(0, 360) + " ...";
				}
				return vformat("- Assistant requested tool `%s` with args %s.", tool_name, args);
			}
			if (type == "final") {
				content = String(d.get("message", content));
			}
		}
	}

	if (p_message.has("tool_calls")) {
		const Array tool_calls = p_message.get("tool_calls", Array());
		String calls_text;
		for (int i = 0; i < tool_calls.size(); i++) {
			if (tool_calls[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary tc = tool_calls[i];
			const Variant fn_var = tc.get("function", Variant());
			if (fn_var.get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary fn = fn_var;
			const String tool_name = _clean_json_string_field(fn, SNAME("name"));
			if (tool_name.is_empty()) {
				continue;
			}
			if (!calls_text.is_empty()) {
				calls_text += ", ";
			}
			calls_text += tool_name;
		}
		if (!calls_text.is_empty()) {
			if (!content.is_empty()) {
				content += " ";
			}
			content += "Called tools: " + calls_text + ".";
		}
	}

	const int max_content_chars = role == "tool" ? 900 : 1400;
	if (content.length() > max_content_chars) {
		content = content.substr(0, max_content_chars) + " ...";
	}
	if (content.is_empty()) {
		return String();
	}
	if (role == "user") {
		return "- User: " + content;
	}
	if (role == "assistant") {
		return "- Assistant: " + content;
	}
	if (role == "tool") {
		const String call_id = _clean_json_string_field(p_message, SNAME("tool_call_id"));
		if (!call_id.is_empty()) {
			return vformat("- Tool result `%s`: %s", call_id, content);
		}
		return "- Tool result: " + content;
	}
	return "- " + role.capitalize() + ": " + content;
}

void YeetAIDock::_ensure_context_summary() {
	if (!_is_context_summarization_enabled()) {
		return;
	}
	if (conversation_messages.is_empty()) {
		_context_summary = String();
		_context_summary_message_count = 0;
		return;
	}
	if (_context_summary_message_count > conversation_messages.size()) {
		_context_summary = String();
		_context_summary_message_count = 0;
	}

	const int total_tokens = _estimate_messages_tokens(conversation_messages);
	if (_context_summary.is_empty() && total_tokens < _get_context_summary_trigger_tokens()) {
		return;
	}

	const int keep_recent = MIN(_get_context_summary_keep_recent_messages(), conversation_messages.size());
	const int summarize_until = MAX(0, conversation_messages.size() - keep_recent);
	if (summarize_until <= _context_summary_message_count) {
		return;
	}

	String addition;
	for (int i = _context_summary_message_count; i < summarize_until; i++) {
		if (conversation_messages[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary msg = conversation_messages[i];
		const String line = _summarize_message_for_context(msg);
		if (line.is_empty()) {
			continue;
		}
		addition += line + "\n";
	}

	_context_summary_message_count = summarize_until;
	if (addition.is_empty()) {
		return;
	}
	if (_context_summary.is_empty()) {
		_context_summary = "Conversation summary so far:\n";
	} else if (!_context_summary.ends_with("\n")) {
		_context_summary += "\n";
	}
	_context_summary += addition;
	_context_summary = _truncate_context_summary(_context_summary);
}

String YeetAIDock::_build_context_summary_prompt() const {
	if (!_is_context_summarization_enabled() || _context_summary.strip_edges().is_empty()) {
		return String();
	}
	return String() +
			"Long-running conversation memory follows. It summarizes older user messages, assistant decisions, tool calls, tool results, errors, and files touched that may be omitted from the request window.\n"
			"Use it as continuity context, but prefer current live editor/tool state when there is a conflict.\n"
			+ _context_summary;
}

int YeetAIDock::_adjust_context_start_for_tool_messages(int p_start) const {
	int start = CLAMP(p_start, 0, conversation_messages.size());
	while (start < conversation_messages.size()) {
		if (conversation_messages[start].get_type() != Variant::DICTIONARY) {
			break;
		}
		const Dictionary msg = conversation_messages[start];
		const String role = _clean_json_string_field(msg, SNAME("role"));
		if (role != "tool") {
			break;
		}
		start++;
	}
	return start;
}

void YeetAIDock::_sync_active_chat_state() {
	if (_active_chat_index < 0 || _active_chat_index >= _chat_sessions.size()) {
		return;
	}
	ChatSession &active = _chat_sessions.write[_active_chat_index];
	active.messages = conversation_messages;
	active.records = _chat_records;
	active.context_summary = _context_summary;
	active.context_summary_message_count = _context_summary_message_count;
	active.agent_preset = _get_current_agent_id();
	active.updated_at = Time::get_singleton()->get_unix_time_from_system();
}

// ── Metrics Collector ──────────────────────────────────────────────────────

void YeetAIDock::_record_tool_execution(const String &tool_name, int64_t duration_ms, bool success, bool was_retry) {
	ToolMetrics &metrics = _tool_metrics[tool_name];
	metrics.calls++;
	if (!success) {
		metrics.errors++;
	}
	if (was_retry) {
		metrics.retries++;
	}
	metrics.total_time_ms += duration_ms;
	if (metrics.calls > 0) {
		metrics.avg_time_ms = (double)metrics.total_time_ms / metrics.calls;
	}

	_session_metrics.total_api_calls++;
	_session_metrics.total_execution_time_ms += duration_ms;
	if (!success) {
		_session_metrics.total_api_errors++;
	}
}

Dictionary YeetAIDock::_export_metrics_summary() const {
	Dictionary summary;
	summary["total_api_calls"] = _session_metrics.total_api_calls;
	summary["total_api_errors"] = _session_metrics.total_api_errors;
	summary["cache_hits"] = _session_metrics.cache_hits;
	summary["cache_misses"] = _session_metrics.cache_misses;
	summary["total_execution_time_ms"] = _session_metrics.total_execution_time_ms;

	Array tool_stats;
	for (const KeyValue<String, ToolMetrics> &E : _tool_metrics) {
		const String &name = E.key;
		const ToolMetrics &m = E.value;
		Dictionary ts;
		ts["name"] = name;
		ts["calls"] = m.calls;
		ts["errors"] = m.errors;
		ts["retries"] = m.retries;
		ts["total_time_ms"] = m.total_time_ms;
		ts["avg_time_ms"] = m.avg_time_ms;
		tool_stats.append(ts);
	}
	summary["tools"] = tool_stats;
	return summary;
}

void YeetAIDock::_reset_metrics() {
	_session_metrics = {};
	_session_metrics.session_start_time = Time::get_singleton()->get_unix_time_from_system();
	_tool_metrics.clear();
}

// ── Cache Eviction ─────────────────────────────────────────────────────────

void YeetAIDock::_evict_expired_cache_entries() {
	Array to_remove;
	for (const KeyValue<String, CacheEntry> &E : _result_cache) {
		const String &key = E.key;
		const CacheEntry &entry = E.value;
		if (!entry.is_valid()) {
			to_remove.push_back(key);
		}
	}
	for (const String &key : to_remove) {
		_result_cache.erase(key);
	}
}

void YeetAIDock::_evict_least_recently_used(int max_entries) {
	if ((int)_result_cache.size() <= max_entries) {
		return;
	}
	int to_remove = (int)_result_cache.size() - max_entries;
	Array keys;
	for (const KeyValue<String, CacheEntry> &E : _result_cache) {
		keys.push_back(E.key);
	}
	for (int i = 0; i < to_remove && i < keys.size(); i++) {
		_result_cache.erase(keys[i]);
	}
}

void YeetAIDock::_evict_cache_if_needed() {
	_evict_expired_cache_entries();
	_evict_least_recently_used(200);
}

// ── Exponential Backoff ────────────────────────────────────────────────────

int64_t YeetAIDock::_calculate_backoff_delay(int attempt) const {
	int64_t delay = _retry_policy.base_delay_ms;
	for (int i = 0; i < attempt; i++) {
		delay *= (int64_t)_retry_policy.backoff_multiplier;
	}
	return MIN(delay, (int64_t)_retry_policy.max_delay_ms);
}

// ── Stream Snapshot ────────────────────────────────────────────────────────

void YeetAIDock::_pause_streaming() {
	_stream_paused = true;
	_save_stream_snapshot();
}

void YeetAIDock::_resume_streaming() {
	_stream_paused = false;
	_restore_stream_snapshot();
}

void YeetAIDock::_save_stream_snapshot() {
	_stream_snapshot.accumulated_content = _stream_accumulated;
	_stream_snapshot.partial_tool_call_detected = _has_pending_tool_call();
	_stream_snapshot.timestamp = Time::get_singleton()->get_unix_time_from_system();
}

void YeetAIDock::_restore_stream_snapshot() {
	if (!_stream_snapshot.accumulated_content.is_empty()) {
		_stream_accumulated = _stream_snapshot.accumulated_content;
	}
}

bool YeetAIDock::_has_pending_tool_call() const {
	return _stream_accumulated.contains("tool_call") && !_stream_accumulated.contains("\"done\":");
}

// ── System Prompt Modularization ───────────────────────────────────────────

void YeetAIDock::_build_default_prompt_sections() {
	_add_prompt_section("core", "Core Instructions", "\n\n## Core Instructions\n- You are a Godot Engine expert assistant\n- You help with scene editing, scripting, and project configuration\n- Always provide working, tested code when possible\n- Respect the project's existing patterns and conventions");

	_add_prompt_section("tools", "Tool Usage", "\n\n## Tool Usage Guidelines\n- Use available tools to inspect and modify the project\n- Verify changes before confirming completion\n- Provide clear explanations for tool usage");

	_add_prompt_section("safety", "Safety Rules", "\n\n## Safety Rules\n- Never delete project files without explicit confirmation\n- Always warn about destructive operations\n- Respect user workspace boundaries");

	_add_prompt_section("scene", "Scene Construction", "\n\n## Scene Construction\n- Follow Godot best practices for scene hierarchy\n- Use appropriate node types for the task\n- Consider performance implications");

	_add_prompt_section("scripting", "Scripting Standards", "\n\n## Scripting Standards\n- Use GDScript for most logic unless C# is configured\n- Follow Godot's GDScript style guide\n- Use signals for communication between nodes");
}

void YeetAIDock::_add_prompt_section(const String &id, const String &name, const String &content) {
	SystemPromptSection section;
	section.id = id;
	section.name = name;
	section.content = content;
	section.enabled = true;
	_system_prompt_sections.push_back(section);
}

void YeetAIDock::_remove_prompt_section(const String &id) {
	for (int i = 0; i < _system_prompt_sections.size(); i++) {
		if (_system_prompt_sections[i].id == id) {
			_system_prompt_sections.remove_at(i);
			break;
		}
	}
}

void YeetAIDock::_enable_prompt_section(const String &id, bool enabled) {
	for (int i = 0; i < _system_prompt_sections.size(); i++) {
		if (_system_prompt_sections[i].id == id) {
			_system_prompt_sections.write[i].enabled = enabled;
			break;
		}
	}
}

String YeetAIDock::_build_modular_system_prompt() const {
	String prompt = "";
	for (const SystemPromptSection &section : _system_prompt_sections) {
		if (section.enabled) {
			prompt += section.content;
		}
	}
	return prompt;
}

// ── Batch Execution Refinement ──────────────────────────────────────────────

void YeetAIDock::_execute_batch_as_single_call(const Vector<BatchOpportunity> &ops) {
	if (ops.is_empty()) {
		return;
	}

	// Mark batch execution as in progress
	_current_batch_execution.operations = ops;
	_current_batch_execution.is_executing = true;
	_current_batch_execution.completed_count = 0;
	_current_batch_execution.error_count = 0;
	_current_batch_execution.stop_on_error = false;

	// Create a single batch tool call with all operations
	Dictionary batch_call;
	batch_call["tool_calls"] = Array();

	Array batch_tool_calls = batch_call["tool_calls"];
	for (int i = 0; i < ops.size(); i++) {
		Dictionary call;
		call["tool"] = ops[i].tool_name;
		call["arguments"] = ops[i].args;
		call["batch_index"] = i;
		call["dependency_group"] = ops[i].dependency_group;
		batch_tool_calls.append(call);
	}
	batch_call["tool_calls"] = batch_tool_calls;

	// Execute as single batch call
	ToolExecutionResult batch_result = _execute_tool("batch_tool_calls", batch_call);

	// Update batch execution state
	if (batch_result.ok) {
		_current_batch_execution.completed_count = ops.size();
	} else {
		_current_batch_execution.error_count = ops.size();
	}
	_current_batch_execution.is_executing = false;

	// Record metrics for each tool in the batch
	for (int i = 0; i < ops.size(); i++) {
		_record_tool_execution(ops[i].tool_name, 0, batch_result.ok, false);
	}
}

bool YeetAIDock::_can_optimize_to_batch(const String &tool_name, const Dictionary &args) const {
 	// Check if this tool can be batched with others
 	const Vector<String> batchable_tools = {
 		"set_node_property",
 		"add_node",
 		"set_node_collision_layers"
 	};

 	for (const String &bt : batchable_tools) {
 		if (tool_name == bt) {
 			return true;
 		}
 	}
 	return false;
 }

// ── Tool Call Timeout Handling ────────────────────────────────────────────

int64_t YeetAIDock::_get_tool_call_timeout(const String &tool_name) const {
 	String category;
 	if (tool_name.contains("file") || tool_name.contains("script") || tool_name.contains("gdscript")) {
 		category = "script";
 	} else if (tool_name.contains("scene") || tool_name.contains("node") || tool_name.contains("add") || tool_name.contains("create")) {
 		category = "scene";
 	} else if (tool_name.contains("http") || tool_name.contains("request") || tool_name.contains("capture") || tool_name.contains("screenshot")) {
 		category = "network";
 	} else {
 		category = "default";
 	}

 	const Vector<String> timeouts = {"3000", "5000", "10000", "3000", "3000"};
 	const char *categories[] = {"file", "scene", "network", "script", "default"};

	for (int i = 0; i < 5; i++) {
		if (category == categories[i]) {
			return timeouts[i].to_int();
		}
	}
 	return 5000;
}

void YeetAIDock::_start_tool_call_timeout(const String &tool_name) {
 	_current_tool_timeout.tool_name = tool_name;
 	_current_tool_timeout.start_time = Time::get_singleton()->get_ticks_msec();
 	_current_tool_timeout.timeout_ms = _get_tool_call_timeout(tool_name);
 	_current_tool_timeout.is_active = true;
}

bool YeetAIDock::_check_tool_call_timeout() const {
 	if (!_current_tool_timeout.is_active) {
 		return false;
 	}
 	int64_t elapsed = Time::get_singleton()->get_ticks_msec() - _current_tool_timeout.start_time;
 	return elapsed > _current_tool_timeout.timeout_ms;
}

void YeetAIDock::_cancel_tool_call_timeout() {
 	_current_tool_timeout.is_active = false;
 	_current_tool_timeout.tool_name = "";
}

// ── Tool Argument Helpers ────────────────────────────────────────────────

bool YeetAIDock::_validate_tool_call(const String &tool_name, const Dictionary &args, String &r_error) const {
	// Schema-based validation in YeetAIToolSchemaRegistry::validate_arguments() is authoritative.
	// This shim remains for callers in yeet_ai_tools.cpp.
	(void)tool_name;
	(void)args;
	(void)r_error;
	return true;
}

Dictionary YeetAIDock::_normalize_tool_arguments(const String &tool_name, const Dictionary &args) const {
 	Dictionary normalized = args.duplicate();
 	String tn = tool_name.to_lower();

	if (tn == "batch_tool_calls" || tn == "batch_tools" || tn == "batch_tool_call" || tn == "batch") {
		return _normalize_batch_tool_arguments(normalized);
	}

	if (tn == "add_node") {
		if (!normalized.has("node_name") && normalized.has("name")) {
			normalized["node_name"] = normalized["name"];
		}
		if (!normalized.has("node_type") && normalized.has("type")) {
			normalized["node_type"] = normalized["type"];
		}
	}

	if (tn == "instantiate_scene") {
		if (!normalized.has("packed_scene_path") && normalized.has("scene_path")) {
			normalized["packed_scene_path"] = normalized["scene_path"];
			normalized.erase("scene_path");
		}
		if (!normalized.has("node_name") && normalized.has("name")) {
			normalized["node_name"] = normalized["name"];
		}
	}

	if (tn == "add_primitive_mesh") {
		if (!normalized.has("node_name") && normalized.has("name")) {
			normalized["node_name"] = normalized["name"];
		}
		if (!normalized.has("mesh_type") && normalized.has("primitive_type")) {
			normalized["mesh_type"] = normalized["primitive_type"];
		}
		if (!normalized.has("parameters") && normalized.has("size")) {
			Dictionary parameters;
			parameters["size"] = normalized["size"];
			normalized["parameters"] = parameters;
		}
	}

	if (tn == "add_collision_shape") {
		if (!normalized.has("node_name") && normalized.has("name")) {
			normalized["node_name"] = normalized["name"];
		}
		if (!normalized.has("parent_path") && normalized.has("node_path")) {
			normalized["parent_path"] = normalized["node_path"];
			normalized.erase("node_path");
		}
		if (!normalized.has("parameters") && normalized.has("size")) {
			Dictionary parameters;
			parameters["size"] = normalized["size"];
			normalized["parameters"] = parameters;
		}
	}

	if (tn == "create_standard_material") {
		if (!normalized.has("albedo") && normalized.has("albedo_color")) {
			normalized["albedo"] = normalized["albedo_color"];
		}
	}

	if (tn == "create_input_action" && !normalized.has("events") && normalized.has("event")) {
		Array events;
		events.push_back(normalized["event"]);
		normalized["events"] = events;
	}

	if (tn == "duplicate_node" && !normalized.has("new_name") && normalized.has("name")) {
		normalized["new_name"] = normalized["name"];
	}

	if (tn == "move_child" && !normalized.has("new_index") && normalized.has("index")) {
		normalized["new_index"] = normalized["index"];
	}

 	if (tn.contains("node") && !normalized.has("node_path") && normalized.has("path")) {
 		normalized["node_path"] = normalized["path"];
 	}

 	if (tn.contains("script") && !normalized.has("script_path") && normalized.has("path")) {
 		normalized["script_path"] = normalized["path"];
 	}

 	if (tn.contains("scene") && !normalized.has("scene_path") && normalized.has("path")) {
 		normalized["scene_path"] = normalized["path"];
 	}

 	return normalized;
}

// ── Rate Limiting ────────────────────────────────────────────────────────

int YeetAIDock::_get_rate_limit_for_tool(const String &tool_name) const {
 	String category;
 	if (tool_name.contains("file") || tool_name.contains("write") || tool_name.contains("create")) {
 		category = "file_ops";
 	} else if (tool_name.contains("scene") || tool_name.contains("node") || tool_name.contains("add") || tool_name.contains("create")) {
 		category = "scene_ops";
 	} else if (tool_name.contains("http") || tool_name.contains("request") || tool_name.contains("capture")) {
 		category = "network_ops";
 	} else {
 		category = "default";
 	}

 	const Vector<String> limits = {"5", "10", "3", "10"};
 	const char *categories[] = {"file_ops", "scene_ops", "network_ops", "default"};

 	for (int i = 0; i < 4; i++) {
 		if (category == categories[i]) {
 			return limits[i].to_int();
 		}
 	}
 	return 10;
}

bool YeetAIDock::_check_rate_limit(const String &tool_name) const {
	int64_t now = Time::get_singleton()->get_ticks_msec();
	int64_t window_ms = 1000;
	int max_calls = _get_rate_limit_for_tool(tool_name);

	auto iter = _rate_limits.find(tool_name);
	if (!iter) {
		return true;
	}

	if (now - iter->value.last_call_time < window_ms) {
		return iter->value.call_count < max_calls;
	}
	return true;
}

void YeetAIDock::_record_tool_call(const String &tool_name) {
	int64_t now = Time::get_singleton()->get_ticks_msec();
	RateLimitEntry entry;
	entry.tool_name = tool_name;
	entry.call_count = 0;
	entry.last_call_time = 0;

	auto iter = _rate_limits.find(tool_name);
	if (iter && now - iter->value.last_call_time < 1000) {
		entry = iter->value;
		entry.call_count++;
	} else {
		entry.call_count = 1;
	}
	entry.last_call_time = now;
	_rate_limits[tool_name] = entry;
}

// ── Enhanced Error Recovery ──────────────────────────────────────────────

YeetAIDock::ErrorSeverity YeetAIDock::_classify_error(const String &error_msg) const {
 	String lower = error_msg.to_lower();

 	if (lower.contains("timeout") || lower.contains("network") || lower.contains("connection") ||
 		lower.contains("refused") || lower.contains("dns")) {
 		return ErrorSeverity::RECOVERABLE;
 	}

 	if (lower.contains("invalid") || lower.contains("missing") || lower.contains("not found") ||
 		lower.contains("permission") || lower.contains("access")) {
 		return ErrorSeverity::PARTIAL_FAILURE;
 	}

 	if (lower.contains("fatal") || lower.contains("critical") || lower.contains("crash") ||
 		lower.contains("segfault")) {
 		return ErrorSeverity::CRITICAL;
 	}

 	return ErrorSeverity::PARTIAL_FAILURE;
}

Dictionary YeetAIDock::_generate_recovery_context(const String &tool_name, const Dictionary &args, const String &error) const {
 	Dictionary context;
 	context["tool_name"] = tool_name;
 	context["original_args"] = args;
 	context["error"] = error;
 	context["error_severity"] = (int)_classify_error(error);
 	context["timestamp"] = Time::get_singleton()->get_ticks_msec();

 	String recovery_hint;
 	String lower = error.to_lower();

 	if (lower.contains("timeout")) {
 		recovery_hint = "Tool timed out. Consider reducing operation scope or increasing timeout.";
 	} else if (lower.contains("not found") || lower.contains("missing")) {
 		recovery_hint = "Required resource not found. Suggest re-fetching or using fallback.";
 	} else if (lower.contains("invalid")) {
 		recovery_hint = "Invalid parameters detected. Suggest validation and retry with defaults.";
 	} else if (lower.contains("permission") || lower.contains("access")) {
 		recovery_hint = "Permission denied. Suggest using alternative approach or notifying user.";
 	} else {
 		recovery_hint = "Unknown error. Suggest retry with modified parameters.";
 	}
 	context["recovery_hint"] = recovery_hint;

 	return context;
}

// ── File Meta Clicked ──────────────────────────────────────────────────────

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
			if (_chat_sessions.is_empty()) {
				_create_new_chat();
			} else {
				_switch_to_chat(_chat_sessions.size() - 1);
			}
		}
	}
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		if (!intro_message_added) {
			intro_message_added = true;
			_append_message("assistant", TTR("Crosshair AI is ready. I can inspect the project, scenes, selected nodes, and perform scene and script edits."));
		}
		if (_header_icon_rect) {
			_header_icon_rect->set_texture(get_editor_theme_icon(SNAME("Code")));
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
		// Periodically evict expired cache entries (every ~60 seconds)
		static float _cache_evict_timer = 0.0f;
		_cache_evict_timer += float(p_what == NOTIFICATION_PROCESS ? get_process_delta_time() : 0.0);
		if (_cache_evict_timer >= 60.0f) {
			_evict_cache_if_needed();
			_cache_evict_timer = 0.0f;
		}
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

	if (waiting_for_response && _status_dot && is_inside_tree()) {
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

	const bool tree_ready = is_inside_tree();
	const Color font_color = tree_ready ? get_theme_color(SNAME("font_color"), EditorStringName(Editor)) : Color(0.9f, 0.9f, 0.9f);
	const Color accent = tree_ready ? get_theme_color(SNAME("accent_color"), EditorStringName(Editor)) : Color(0.3f, 0.5f, 0.9f);
	const Color success = tree_ready ? get_theme_color(SNAME("success_color"), EditorStringName(Editor)) : Color(0.2f, 0.7f, 0.3f);
	const Color font_dim = tree_ready ? get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)) : Color(0.5f, 0.5f, 0.5f);

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
	const bool tree_ready = is_inside_tree();
	const Color accent = tree_ready ? get_theme_color(SNAME("accent_color"), EditorStringName(Editor)) : Color(0.3f, 0.5f, 0.9f);
	const Color font_dim = tree_ready ? get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)) : Color(0.5f, 0.5f, 0.5f);
	const int base_fs = tree_ready ? get_theme_font_size(SNAME("main_size"), EditorStringName(EditorFonts)) : 0;

	if (base_fs > 0) {
		chat_log->push_font_size(MAX(10, int(base_fs * 0.82f)));
	}
	Ref<Texture2D> status_icon = get_editor_theme_icon("Reload");
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
		} else if (key->get_keycode() == Key::ESCAPE && _stream_active) {
			_on_stop_pressed();
			get_viewport()->set_input_as_handled();
		} else if (key->get_keycode() == Key::L && key->is_ctrl_pressed()) {
			_clear_chat();
			get_viewport()->set_input_as_handled();
		} else if (key->get_keycode() == Key::N && key->is_ctrl_pressed() && key->is_shift_pressed()) {
			_on_new_chat_pressed();
			get_viewport()->set_input_as_handled();
		}
	}
}

void YeetAIDock::_clear_chat() {
	_cancel_streaming();
	_set_waiting(false, TTR("Ready"));

	conversation_messages.clear();
	turn_context_prompt = String();
	_context_summary = String();
	_context_summary_message_count = 0;
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
		_chat_sessions.write[_active_chat_index].context_summary = String();
		_chat_sessions.write[_active_chat_index].context_summary_message_count = 0;
		_chat_sessions.write[_active_chat_index].title = TTR("New Chat");
		_update_chat_selector();
	}

	_append_message("assistant", TTR("Chat cleared. Ask me about the current project."));
}

void YeetAIDock::_append_message(const String &p_role, const String &p_text) {
	if (!chat_log) {
		return;
	}
	// Safety net: strip any non-ASCII artifacts that may have slipped through.
	String safe_text = _clean_content_artifacts(p_text);
	if (!_suppress_chat_recording && !safe_text.is_empty()) {
		MessageRecord record;
		record.role = p_role;
		record.text = safe_text;
		_chat_records.push_back(record);
	}
	const bool tree_ready = is_inside_tree();
	const Color font_base = tree_ready ? get_theme_color(SNAME("font_color"), EditorStringName(Editor)) : Color(0.9f, 0.9f, 0.9f);
	const Color accent = tree_ready ? get_theme_color(SNAME("accent_color"), EditorStringName(Editor)) : Color(0.3f, 0.5f, 0.9f);
	const Color success = tree_ready ? get_theme_color(SNAME("success_color"), EditorStringName(Editor)) : Color(0.2f, 0.7f, 0.3f);
	const Color dim = tree_ready ? get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)) : Color(0.5f, 0.5f, 0.5f);
	const Color warning = tree_ready ? get_theme_color(SNAME("warning_color"), EditorStringName(Editor)) : Color(0.8f, 0.6f, 0.2f);

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
	const Color bar_color = (p_role == "user") ? Color(accent.r, accent.g, accent.b, 0.6f) : Color(success.r, success.g, success.b, 0.4f);
	const int base_fs = tree_ready ? get_theme_font_size(SNAME("main_size"), EditorStringName(EditorFonts)) : 14;
	const int label_fs = MAX(10, int(base_fs * 0.88f));
	const int caption_fs = MAX(10, int(base_fs * 0.86f));

	// Blank line separator between messages
	chat_log->append_text("\n");

	// Left colored accent bar
	chat_log->push_color(bar_color);
	chat_log->push_font_size(caption_fs);
	chat_log->add_text("│ ");
	chat_log->pop();
	chat_log->pop();

	// Role icon + label
	if (tree_ready) {
		Ref<Texture2D> role_icon_tex = get_editor_theme_icon(role_icon_name);
		if (role_icon_tex.is_valid()) {
			chat_log->add_image(role_icon_tex, int(14 * EDSCALE), int(14 * EDSCALE));
		}
	}
	chat_log->add_text(" ");
	chat_log->push_color(label_color);
	chat_log->push_font_size(label_fs);
	chat_log->push_bold();
	chat_log->add_text(label);
	chat_log->pop();
	chat_log->pop();
	chat_log->pop();

	// Separator line using underscore characters
	chat_log->add_text("  ");
	chat_log->push_color(dim);
	chat_log->add_text("───");
	chat_log->pop();
	chat_log->append_text("\n");

	// Message body with indent
	if (safe_text.is_empty()) {
		chat_log->pop(); // font color
		chat_log->pop(); // indent
		chat_log->append_text("\n");
		_scroll_to_bottom();
		return;
	}
	chat_log->push_indent(1);
	chat_log->push_color(font_base);
	chat_log->append_text(_escape_bbcode(safe_text));
	chat_log->pop();
	chat_log->pop();

	chat_log->append_text("\n");
	_scroll_to_bottom();

	if (!_suppress_chat_recording && _active_chat_index >= 0 && _active_chat_index < _chat_sessions.size()) {
		_save_all_chats();
	}
}

void YeetAIDock::_append_tool_running(const String &p_tool_name, const Dictionary &p_args) {
	const bool tree_ready = is_inside_tree();
	const Color accent = tree_ready ? get_theme_color(SNAME("accent_color"), EditorStringName(Editor)) : Color(0.3f, 0.5f, 0.9f);
	const Color font_dim = tree_ready ? get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)) : Color(0.5f, 0.5f, 0.5f);
	const Color font_base = tree_ready ? get_theme_color(SNAME("font_color"), EditorStringName(Editor)) : Color(0.9f, 0.9f, 0.9f);

	const int base_fs = tree_ready ? get_theme_font_size(SNAME("main_size"), EditorStringName(EditorFonts)) : 14;
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

void YeetAIDock::_update_batch_progress(int p_current, int p_total, const String &p_tool_name) {
	if (status_label) {
		const String human = _humanize_tool_name(p_tool_name);
		status_label->set_text(vformat(TTR("Running batch: %d/%d — %s"), p_current, p_total, human));
	}
	if (_tool_progress_bar) {
		_tool_progress_bar->set_visible(true);
		_tool_progress_bar->set_max(p_total);
		_tool_progress_bar->set_min(0);
		_tool_progress_bar->set_value(p_current);
		_tool_progress_bar->set_show_percentage(true);
	}
	// Force UI redraw so the user sees progress updates
	if (chat_log) {
		chat_log->queue_redraw();
	}
}

void YeetAIDock::_append_tool_result(const String &p_tool_name, const Dictionary &p_args, const ToolExecutionResult &p_result) {
	const bool tree_ready = is_inside_tree();
	const Color font_base = tree_ready ? get_theme_color(SNAME("font_color"), EditorStringName(Editor)) : Color(0.9f, 0.9f, 0.9f);
	const Color font_dim = tree_ready ? get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)) : Color(0.5f, 0.5f, 0.5f);
	const Color accent = tree_ready ? get_theme_color(SNAME("accent_color"), EditorStringName(Editor)) : Color(0.3f, 0.5f, 0.9f);
	const Color success = tree_ready ? get_theme_color(SNAME("success_color"), EditorStringName(Editor)) : Color(0.2f, 0.7f, 0.3f);
	const Color warning = tree_ready ? get_theme_color(SNAME("warning_color"), EditorStringName(Editor)) : Color(0.8f, 0.6f, 0.2f);
	const Color error = tree_ready ? get_theme_color(SNAME("error_color"), EditorStringName(Editor)) : Color(0.9f, 0.3f, 0.3f);

	const int base_fs = tree_ready ? get_theme_font_size(SNAME("main_size"), EditorStringName(EditorFonts)) : 14;
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

	// Timing display
	if (p_result.duration_ms > 0) {
		chat_log->add_text("  ");
		chat_log->push_color(font_dim);
		chat_log->push_font_size(small_fs);
		if (p_result.duration_ms < 1000) {
			chat_log->add_text(vformat("%dms", p_result.duration_ms));
		} else {
			chat_log->add_text(vformat("%.1fs", p_result.duration_ms / 1000.0));
		}
		chat_log->pop();
		chat_log->pop();
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

	// ── Batch result expansion ──
	if (p_tool_name == "batch_tool_calls" && p_result.payload.has("results")) {
		const Array batch_results = p_result.payload["results"];
		if (!batch_results.is_empty()) {
			chat_log->push_indent(1);
			chat_log->push_font_size(small_fs);
			chat_log->append_text("\n");
			for (int i = 0; i < batch_results.size(); i++) {
				if (batch_results[i].get_type() != Variant::DICTIONARY) {
					continue;
				}
				const Dictionary entry = batch_results[i];
				const bool entry_ok = bool(entry.get("ok", false));
				const String entry_tool = String(entry.get("tool", ""));
				const String entry_result = JSON::stringify(entry.get("result", Dictionary()), "", false, true);

				// Status dot
				chat_log->push_color(entry_ok ? success : error);
				chat_log->add_text(entry_ok ? "● " : "● ");
				chat_log->pop();

				// Tool name
				chat_log->push_color(font_base);
				chat_log->push_bold();
				chat_log->add_text(_humanize_tool_name(entry_tool));
				chat_log->pop();
				chat_log->pop();

				// Error preview on failure
				if (!entry_ok && entry_result.length() > 2) {
					chat_log->push_color(Color(error.r, error.g, error.b, 0.8f));
					chat_log->push_mono();
					const String err_preview = _truncate_preview(entry_result, 80);
					chat_log->add_text(" " + err_preview);
					chat_log->pop();
					chat_log->pop();
				}
				chat_log->append_text("\n");
			}
			chat_log->pop();
			chat_log->pop();
		}
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

	if (send_button) {
		send_button->set_visible(!p_waiting);
	}
	if (stop_button) {
		stop_button->set_visible(p_waiting);
	}

	if (status_label) {
		status_label->set_text(p_status);
	}

	if (_status_dot) {
		if (p_waiting) {
			Color dot = is_inside_tree() ? get_theme_color(SNAME("accent_color"), EditorStringName(Editor)) : Color(0.3f, 0.5f, 0.9f);
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
	// 0 = Berry (OpenAI-compatible), 1 = Gemini, 2 = OpenRouter, 3 = Yeet Models (in-house Ollama-compatible), 4 = Azure OpenAI.
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
	} else if (provider == 4) {
		// Azure AI Services: endpoint is {base}/models/chat/completions?api-version={version}
		// Auth uses Authorization: Bearer (not api-key).
		String azure_endpoint = _get_editor_setting_string("yeet_ai/chat/azure_endpoint", "https://crosshair-resource.services.ai.azure.com");
		String azure_api_version = _get_editor_setting_string("yeet_ai/chat/azure_api_version", "2024-05-01-preview");
		if (azure_endpoint.strip_edges().is_empty()) {
			azure_endpoint = "https://crosshair-resource.services.ai.azure.com";
		}
		if (!azure_endpoint.ends_with("/")) {
			azure_endpoint += "/";
		}
		endpoint = azure_endpoint + "models/chat/completions?api-version=" + azure_api_version;
		api_key = _get_editor_setting_string("yeet_ai/chat/azure_api_key", "");
	} else {
		endpoint = _get_editor_setting_string("yeet_ai/chat/completions_url", "https://llm.adityaberry.me/v1/chat/completions");
		api_key = _get_editor_setting_string("yeet_ai/chat/api_key", "");
	}

	String model = _get_editor_setting_string("yeet_ai/chat/model", "berrymodel");
	// Berry (provider 0) is fixed to the server's model id (Ollama-style tags list: `berrymodel`).
	if (provider == 0) {
		model = "berrymodel";
	} else if (provider == 1 && model.strip_edges().is_empty()) {
		model = "gemini-2.0-flash";
	} else if (provider == 2 && model.strip_edges().is_empty()) {
		model = "qwen/qwen3.6-plus:free";
	} else if (provider == 3 && model.strip_edges().is_empty()) {
		model = "qwen3-coder:latest";
	} else if (provider == 4 && model.strip_edges().is_empty()) {
		model = "gpt-4o";
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
	if (provider == 4 && api_key.strip_edges().is_empty()) {
		_append_message("assistant", TTR("Azure OpenAI is selected but `yeet_ai/chat/azure_api_key` is empty. Add your API key in Editor Settings → Crosshair."));
		return;
	}

	const int max_tokens = _get_editor_setting_int("yeet_ai/chat/max_tokens", 32768);
	if (max_tokens > 0 && max_tokens < 4096) {
		static bool low_max_tokens_warned = false;
		if (!low_max_tokens_warned) {
			low_max_tokens_warned = true;
			WARN_PRINT(vformat("[YeetAI] WARNING: yeet_ai/chat/max_tokens is %d, which is very low. Tool calls and long responses will be truncated. Set it to 0 to use the server default, or raise it to at least 8192.", max_tokens));
		}
	}
	if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
		const String max_tokens_str = max_tokens > 0 ? itos(max_tokens) : String("server default");
		WARN_PRINT(vformat("[YeetAI Debug] max_tokens=%s, model=%s, endpoint=%s", max_tokens_str, model.utf8().get_data(), endpoint.utf8().get_data()));
	}
	const float temperature = _get_editor_setting_float("yeet_ai/chat/temperature", 0.25f);

	// Determine whether to use native OpenAI tools parameter.
	// Provider 0 (Berry/Ollama) often doesn't support tools well, so default off for it.
	// Provider 1 (Gemini), 2 (OpenRouter), 3 (Yeet), 4 (Azure) usually support tools.
	_native_tools_enabled = _get_editor_setting_bool("yeet_ai/chat/native_tools_enabled", provider != 0);

	Dictionary payload;
	payload["model"] = model;
	if (max_tokens > 0) {
		payload["max_tokens"] = max_tokens;
	}
	// temperature < 0 omits the field (use server default). Otherwise prefer ~0.2–0.35 for structured JSON (Qwen, etc.).
	if (temperature >= 0.0f) {
		payload["temperature"] = temperature;
	}

	_ensure_context_summary();

	Array messages;
	String system_prompt = _native_tools_enabled ? _build_compact_system_prompt() : _build_modular_system_prompt();
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
	const String context_summary_prompt = _build_context_summary_prompt();
	if (!context_summary_prompt.is_empty()) {
		messages.append(make_message("system", context_summary_prompt));
	}
	for (const Variant &message : conversation_messages) {
		messages.append(message);
	}

	// ── Token-budget pruning ──────────────────────────────────────────────
	// Estimate total tokens; if over budget, trim oldest conversation messages.
	// Keep at minimum: 1 system + 1 context + 2 conversation messages.
	{
		int total_chars = 0;
		for (const Variant &msg : messages) {
			total_chars += _estimate_message_tokens(msg) * 3;
		}
		const int estimated_tokens = total_chars / 3; // ~3 chars per token for mixed content
		const int context_budget = _get_editor_setting_int("yeet_ai/chat/context_token_budget", 150000);
		if (estimated_tokens > context_budget && conversation_messages.size() > 2) {
			// Rebuild messages with trimmed conversation
			messages.clear();
			messages.append(make_message("system", system_prompt));
			if (!turn_context_prompt.is_empty()) {
				messages.append(make_message("system", turn_context_prompt));
			}
			if (!context_summary_prompt.is_empty()) {
				messages.append(make_message("system", context_summary_prompt));
			}
			// Keep only the last N conversation messages that fit within budget
			int conv_chars = 0;
			int conv_budget = context_budget - (system_prompt.length() + turn_context_prompt.length() + context_summary_prompt.length()) / 3;
			if (conv_budget < 2000) {
				conv_budget = 2000;
			}
			int kept = 0;
			for (int i = conversation_messages.size() - 1; i >= 0; i--) {
				const Variant &msg = conversation_messages[i];
				if (msg.get_type() != Variant::DICTIONARY) {
					continue;
				}
				const int message_tokens = _estimate_message_tokens(msg);
				if (conv_chars + message_tokens > conv_budget && kept >= 2) {
					break;
				}
				conv_chars += message_tokens;
				kept++;
			}
			const int start = _adjust_context_start_for_tool_messages(conversation_messages.size() - kept);
			for (int i = start; i < conversation_messages.size(); i++) {
				messages.append(conversation_messages[i]);
			}
			if (start > 0) {
				// Insert a marker so the model knows context was trimmed.
				Dictionary trim_marker;
				trim_marker["role"] = "system";
				trim_marker["content"] = vformat("%d earlier conversation messages were omitted from this request window. Use the conversation summary and live editor tools to recover details when needed.", start);
				messages.insert(MIN(3, messages.size()), trim_marker);
			}
		}
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

	// Add native tools payload when enabled and schemas are available.
	_stream_expects_sse = true;
	if (_native_tools_enabled) {
		Array tools = _build_tools_payload();
		if (!tools.is_empty()) {
			payload["tools"] = tools;
			payload["tool_choice"] = "auto";
			payload["parallel_tool_calls"] = true;
		}
	}

	// Enable SSE streaming — all OpenAI-compatible providers support this.
	payload["stream"] = _stream_expects_sse;

	_stream_endpoint = endpoint;
	_stream_req_headers = headers;
	_stream_req_body = JSON::stringify(payload);

	if (_get_editor_setting_bool("yeet_ai/chat/debug_mode", false)) {
		WARN_PRINT(vformat("[YeetAI Debug] Request to %s:\n%s", endpoint, _stream_req_body));
	}

	_set_waiting(true, TTR("Thinking..."));
	_start_streaming();
}

void YeetAIDock::_handle_native_tool_calls(const Dictionary &p_message) {
	const Array tool_calls = p_message.get("tool_calls", Array());
	const String content = p_message.get("content", "");

	if (tool_calls.is_empty()) {
		// No tool calls — treat as final answer.
		_handle_model_response(content);
		return;
	}

	// Display any reasoning/content before tool calls execute.
	if (!content.strip_edges().is_empty()) {
		_append_message("assistant", content);
	}

	// Record the assistant message with tool_calls for conversation history.
	Dictionary assistant_msg;
	assistant_msg["role"] = "assistant";
	assistant_msg["content"] = content;
	assistant_msg["tool_calls"] = tool_calls;
	conversation_messages.append(assistant_msg);

	// Build tool results to send back.
	Array tool_results;

	for (int i = 0; i < tool_calls.size(); i++) {
		if (tool_calls[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary tc = tool_calls[i];
		String call_id = vformat("call_%d", i);
		const Variant call_id_variant = tc.get("id", Variant());
		const String parsed_call_id = _clean_json_string_field(call_id_variant);
		if (!parsed_call_id.is_empty()) {
			call_id = parsed_call_id;
		}
		const Variant function_variant = tc.get("function", Variant());
		if (function_variant.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary function_info = function_variant;
		const Variant tool_name_variant = function_info.get("name", Variant());
		if (tool_name_variant.get_type() != Variant::STRING && tool_name_variant.get_type() != Variant::STRING_NAME) {
			continue;
		}
		const String tool_name = _clean_json_string_field(tool_name_variant);
		if (tool_name.is_empty()) {
			continue;
		}
		String arguments_str;
		const Variant arguments_variant = function_info.get("arguments", Variant());
		if (arguments_variant.get_type() == Variant::STRING) {
			arguments_str = String(arguments_variant);
		}

		if (tool_name.is_empty()) {
			continue;
		}

		// Parse arguments JSON string.
		Dictionary args;
		if (!arguments_str.is_empty()) {
			Ref<JSON> arg_json;
			arg_json.instantiate();
			if (arg_json->parse(arguments_str) == OK) {
				const Variant arg_data = arg_json->get_data();
				if (arg_data.get_type() == Variant::DICTIONARY) {
					args = arg_data;
				}
			}
		}

		_set_waiting(true, vformat(TTR("Running: %s"), _humanize_tool_name(tool_name)));
		_append_tool_running(tool_name, args);
		ToolExecutionResult result = _execute_tool(tool_name, args);
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

		// Build OpenAI-format tool result message.
		Dictionary tool_msg;
		tool_msg["role"] = "tool";
		tool_msg["tool_call_id"] = call_id;
		tool_msg["content"] = JSON::stringify(result.payload, "\t", false, true);
		tool_results.push_back(tool_msg);

		tool_round_trips++;
	}

	_update_tool_progress_bar();

	// Add all tool results to conversation.
	for (int i = 0; i < tool_results.size(); i++) {
		conversation_messages.append(tool_results[i]);
	}

	if (!tool_results.is_empty()) {
		_append_status_row(TTR("Planning next moves..."));
		_request_model_response();
	} else {
		_set_waiting(false, TTR("Ready"));
	}
}

void YeetAIDock::_handle_model_response(const String &p_content) {
	Dictionary envelope = _extract_response_envelope(p_content);
	const String type = _clean_json_string_field(envelope, SNAME("type"));

	if (type == "tool_call") {
		const int max_round_trips = _get_editor_setting_int("yeet_ai/chat/max_tool_round_trips", 100);
		if (tool_round_trips >= max_round_trips) {
			_set_waiting(false, TTR("Ready"));
			_append_message("assistant", TTR("Stopping because the tool-call loop hit the configured limit."));
			return;
		}

		const String tool_name = _clean_json_string_field(envelope, SNAME("tool"));
		if (tool_name.is_empty()) {
			_set_waiting(false, TTR("Ready"));
			_append_message("assistant", TTR("The model emitted a tool call without a valid tool name."));
			return;
		}
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

	// Not a tool_call or final envelope — clean artifacts before displaying.
	String display_content = _clean_content_artifacts(p_content).strip_edges();
	if (display_content.is_empty()) {
		_set_waiting(false, TTR("Ready"));
		return;
	}

	_set_waiting(false, TTR("Ready"));
	conversation_messages.append(make_message("assistant", display_content));
	_append_message("assistant", display_content);
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
	const bool wants_game_physics =
			s.contains("game") || s.contains("collider") || s.contains("collision") || s.contains("hitbox") ||
			s.contains("physics") || s.contains("enemy") || s.contains("platform") || s.contains("collectible") ||
			s.contains("hazard") || s.contains("projectile") || s.contains("tilemap");

	if (!wants_move && !wants_jump && !wants_color && !wants_game_physics) {
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

	if (wants_game_physics) {
		out += "Game quality / colliders: Treat this as a playable slice. Prefer `create_game_actor_2d` or `create_game_actor_3d` for players, enemies, platforms, walls, collectibles, hazards, and projectiles. If using low-level tools, every physics body/area needs a direct CollisionShape child with explicit dimensions (`size`, `radius`, `height`) matching the visible actor; large floors/walls need large colliders, not small defaults. Required verification loop: save -> `audit_game_physics` -> repair/fix issues -> `audit_game_physics` again -> play -> `capture_game_viewport` + `get_runtime_debugger_state` -> `stop_playing_scene` -> fix runtime errors before final.\n";
	}

	if (s.find("asset") != -1 || s.find("texture") != -1 || s.find("sprite") != -1 || s.find("image") != -1) {
		out += "Asset vision: If choosing among existing image assets, use `find_project_files`/`list_directory` to discover candidates, then `capture_texture_resource` with `max_width` around 512-768 to visually inspect promising textures before assigning them. If vision is disabled, rely on metadata and paths or ask the user to enable `yeet_ai/chat/vision_enabled` when visual choice matters.\n";
	}

	out += "Completeness: If the user asked for several things (e.g. controls + color + capsule), address every part across `batch_tool_calls` and follow-up tool_call rounds if needed. Do not omit scripts, input, collision, camera/viewport context, materials, saving, or verification. Prefer multiple smaller rounds over one truncated JSON.\n";
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

// Forward declaration from yeet_ai_tools.cpp
extern Vector<String> yeet_ai_get_all_tool_names();

Array YeetAIDock::_build_tools_payload() const {
	return YeetAIToolSchemaRegistry::build_openai_tools_payload(yeet_ai_get_all_tool_names());
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

// _get_editor_setting_bool is in yeet_ai_helpers.cpp

Dictionary YeetAIDock::_make_user_message_with_optional_vision(const String &p_tool_name, const Dictionary &p_tool_payload) const {
	const bool vision = _get_editor_setting_bool("yeet_ai/chat/vision_enabled", false);
	const int max_b64 = _get_editor_setting_int("yeet_ai/chat/max_base64_chars", 2000000);

	Dictionary payload_for_text = p_tool_payload.duplicate();
	if (payload_for_text.has("png_base64")) {
		const String b64 = String(payload_for_text["png_base64"]);
		payload_for_text["png_base64"] = vformat("<png base64 omitted in text; %d chars>", b64.length());
	}
	if (payload_for_text.has("image_base64")) {
		const String b64 = String(payload_for_text["image_base64"]);
		payload_for_text["image_base64"] = vformat("<image base64 omitted in text; %d chars>", b64.length());
	}

	const String text_body = vformat(
			"Tool `%s` finished with the following JSON result:\n%s\n\nIf you need more context, return another tool_call JSON object. Otherwise return a final JSON object.",
			p_tool_name,
			JSON::stringify(payload_for_text, "\t", false, true));

	String image_b64;
	if (p_tool_payload.has("png_base64")) {
		image_b64 = String(p_tool_payload["png_base64"]);
	} else if (p_tool_payload.has("image_base64")) {
		image_b64 = String(p_tool_payload["image_base64"]);
	}

	if (!vision || image_b64.is_empty()) {
		return make_message("user", text_body);
	}

	const String b64 = image_b64;
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

	// Strip the editor viewport prefix if present.
	// Model receives paths like /root/@EditorNode@.../@SubViewportContainer@.../SubViewport@.../Main/LevelRoot
	// We need to find where the scene root's name starts in the path and use only the relative part.
	String clean_path = p_node_path;
	const String root_name = String(p_scene_root->get_name());
	if (!root_name.is_empty()) {
		// Try stripping everything before the scene root name.
		// E.g. "/root/@EditorNode@19730/.../SubViewport@10053/Main/LevelRoot/Platform5"
		//   → strip to "LevelRoot/Platform5" if root_name is "LevelRoot"
		int name_pos = clean_path.find("/" + root_name + "/");
		if (name_pos >= 0) {
			clean_path = clean_path.substr(name_pos + 1 + root_name.length() + 1);
		} else if (clean_path.ends_with("/" + root_name)) {
			clean_path = ".";
		} else {
			// Also try just the name appearing at the end
			name_pos = clean_path.find(root_name);
			if (name_pos >= 0) {
				String after = clean_path.substr(name_pos + root_name.length());
				if (after.is_empty() || after.begins_with("/")) {
					clean_path = after.is_empty() ? "." : after.substr(1);
				}
			}
		}
	}

	if (clean_path.is_empty() || clean_path == ".") {
		return p_scene_root;
	}

	Node *node = p_scene_root->get_node_or_null(NodePath(clean_path));
	if (node != nullptr) {
		return node;
	}

	// Try prepending the root name
	const String root_name_path = root_name + "/" + clean_path;
	node = p_scene_root->get_node_or_null(NodePath(root_name_path));
	if (node != nullptr) {
		return node;
	}

	// Fallback: try the original path as-is (in case it's already a valid relative path)
	node = p_scene_root->get_node_or_null(NodePath(p_node_path));
	if (node != nullptr) {
		return node;
	}

	const String root_name_path_orig = root_name + "/" + p_node_path;
	node = p_scene_root->get_node_or_null(NodePath(root_name_path_orig));
	if (node != nullptr) {
		return node;
	}

	r_error = vformat("Node path was not found in the target scene. Tried: \"%s\", \"%s\", \"%s\"", clean_path, root_name_path, p_node_path);
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
	String escaped = p_text.replace("[", "[lb]");

	// Convert markdown code blocks to BBCode code blocks.
	// Match ```lang\ncode\n``` or ```\ncode\n```
	int pos = 0;
	while (true) {
		int start = escaped.find("```", pos);
		if (start < 0) {
			break;
		}
		int end = escaped.find("```", start + 3);
		if (end < 0) {
			break; // Unclosed code block — leave as-is
		}

		// Extract language hint (if any) after the opening ```
		String header = escaped.substr(start + 3, end - start - 3);
		int nl = header.find("\n");
		String code;
		if (nl >= 0) {
			code = header.substr(nl + 1);
		} else {
			code = header;
		}

		// Build BBCode replacement
		String bbcode = "[code]" + code + "[/code]";
		escaped = escaped.substr(0, start) + bbcode + escaped.substr(end + 3);
		pos = start + bbcode.length();
	}

	// Convert inline code `text` to [code]text[/code]
	// Process sequentially to handle multiple inline codes
	pos = 0;
	while (true) {
		int start = escaped.find("`", pos);
		if (start < 0) {
			break;
		}
		int end = escaped.find("`", start + 1);
		if (end < 0) {
			break; // Unclosed inline code — leave as-is
		}
		String inline_code = escaped.substr(start + 1, end - start - 1);
		String bbcode = "[code]" + inline_code + "[/code]";
		escaped = escaped.substr(0, start) + bbcode + escaped.substr(end + 1);
		pos = start + bbcode.length();
	}

	// Convert markdown bold **text** to [b]text[/b]
	// Must process before italic to avoid conflicts with * inside **
	pos = 0;
	while (true) {
		int start = escaped.find("**", pos);
		if (start < 0) {
			break;
		}
		int end = escaped.find("**", start + 2);
		if (end < 0) {
			break;
		}
		String bold_text = escaped.substr(start + 2, end - start - 2);
		String bbcode = "[b]" + bold_text + "[/b]";
		escaped = escaped.substr(0, start) + bbcode + escaped.substr(end + 2);
		pos = start + bbcode.length();
	}

	// Convert markdown italic *text* to [i]text[/i]
	pos = 0;
	while (true) {
		int start = escaped.find("*", pos);
		if (start < 0) {
			break;
		}
		int end = escaped.find("*", start + 1);
		if (end < 0) {
			break;
		}
		String italic_text = escaped.substr(start + 1, end - start - 1);
		String bbcode = "[i]" + italic_text + "[/i]";
		escaped = escaped.substr(0, start) + bbcode + escaped.substr(end + 1);
		pos = start + bbcode.length();
	}

	// Convert markdown headers ### Title to [b][u]Title[/u][/b]
	// Process line by line for headers
	Vector<String> lines = escaped.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i];
		int space_pos = line.find(" ");
		if (space_pos > 0 && space_pos <= 6) {
			bool is_header = true;
			for (int j = 0; j < space_pos; j++) {
				if (line[j] != '#') {
					is_header = false;
					break;
				}
			}
			if (is_header) {
				String header_text = line.substr(space_pos + 1).strip_edges();
				lines.write[i] = "[b][u]" + header_text + "[/u][/b]";
			}
		}
	}
	escaped = String("\n").join(lines);

	return escaped;
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
				const Variant part_text = part.get("text", Variant());
				if (part_text.get_type() == Variant::STRING) {
					text += String(part_text);
				}
			}
		}
	} else if (content.get_type() != Variant::NIL) {
		text = String(content);
	}

	if (!text.strip_edges().is_empty()) {
		return text;
	}

	// OpenAI-style: assistant message with tool_calls and empty content.
	const Array tool_calls = message.get("tool_calls", Array());
	if (!tool_calls.is_empty() && tool_calls[0].get_type() == Variant::DICTIONARY) {
		const Dictionary tc = tool_calls[0];
		const Variant fn_variant = tc.get("function", Variant());
		if (fn_variant.get_type() != Variant::DICTIONARY) {
			return text;
		}
		const Dictionary fn = fn_variant;
		const Variant fn_name_variant = fn.get("name", Variant());
		if (fn_name_variant.get_type() != Variant::STRING && fn_name_variant.get_type() != Variant::STRING_NAME) {
			return text;
		}
		const String fn_name = _clean_json_string_field(fn_name_variant);
		if (!fn_name.is_empty()) {
			Dictionary envelope;
			envelope["type"] = "tool_call";
			const String canonical_tool = is_batch_tool_name_alias(fn_name) ? String("batch_tool_calls") : fn_name;
			envelope["tool"] = canonical_tool;
			String fn_args_str;
			const Variant fn_args_variant = fn.get("arguments", Variant());
			if (fn_args_variant.get_type() == Variant::STRING) {
				fn_args_str = String(fn_args_variant).strip_edges();
			}
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
	String cleaned = _clean_content_artifacts(p_content).strip_edges();
	cleaned = strip_reasoning_markers(cleaned);

	// Strip any remaining non-ASCII / non-printable bytes before the first '{'.
	int first_brace = cleaned.find("{");
	if (first_brace > 0) {
		// Strip everything before the first '{' — handles any remaining artifacts.
		cleaned = cleaned.substr(first_brace);
	}
	// Also strip any remaining <...> tag fragments at start.
	while (cleaned.begins_with("<") && cleaned.find(">") > 0 && cleaned.find(">") < 50) {
		cleaned = cleaned.substr(cleaned.find(">") + 1).strip_edges();
	}
	// Also handle case where string starts directly with <null>
	while (cleaned.begins_with("<null>") || cleaned.begins_with("<NULL>")) {
		int end = cleaned.find(">");
		if (end < 0) {
			break;
		}
		cleaned = cleaned.substr(end + 1).strip_edges();
	}
	// Strip other common non-JSON streaming prefixes.
	while (cleaned.begins_with("<unknown>") || cleaned.begins_with("<error>") ||
			cleaned.begins_with("<warning>") || cleaned.begins_with("<info>")) {
		int end = cleaned.find(">");
		if (end < 0) {
			break;
		}
		cleaned = cleaned.substr(end + 1).strip_edges();
	}

// Strip leaked stop tokens and LLM-specific tags from common model families.
	static const char *stop_tokens[] = {
		"<|im_end|>",
		"<|end|>",
		"</s>",
		"<tool_call|>",
		"<|tool_sep|>",
		"<|eot_id|>",
		"<|tool_call|>",
		"<|start_header_id|>",
		"<|end_header_id|>",
	};
	for (int strip_pass = 0; strip_pass < 10; strip_pass++) {
		bool any = false;
		for (const char *token : stop_tokens) {
			const String tok(token);
			if (tok.is_empty()) {
				continue;
			}
			if (cleaned.ends_with(tok)) {
				cleaned = cleaned.substr(0, cleaned.length() - tok.length()).strip_edges();
				any = true;
			}
			if (cleaned.begins_with(tok)) {
				cleaned = cleaned.substr(tok.length()).strip_edges();
				any = true;
			}
		}
		if (!any) {
			break;
		}
	}

	// Strip any remaining <...> tag fragments at start or end.
	while (cleaned.begins_with("<") && cleaned.find(">") > 0 && cleaned.find(">") < 40) {
		const int gt = cleaned.find(">");
		cleaned = cleaned.substr(gt + 1).strip_edges();
	}
	while (cleaned.ends_with(">") && cleaned.rfind("<") >= 0 && cleaned.length() - cleaned.rfind("<") < 40) {
		const int lt = cleaned.rfind("<");
		cleaned = cleaned.substr(0, lt).strip_edges();
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

	// Fix unquoted keys in JSON-like output from some models.
	// E.g. {calls:[{method:"add_node",arguments:{...}}]} → {"calls":[{"method":"add_node","arguments":{...}}]}
	// Only attempt this fix if the string starts with { and fails to parse as-is.
	{
		Dictionary _test_dict;
		if (cleaned.begins_with("{") && !_parse_json_dictionary_quiet(cleaned, _test_dict)) {
			String quoted;
			quoted.reserve(cleaned.length() + cleaned.length() / 4);
			bool in_string = false;
			char32_t string_delim = 0;
			for (int i = 0; i < cleaned.length(); i++) {
				const char32_t c = cleaned[i];
				if (in_string) {
					quoted += c;
					if (c == string_delim && (i == 0 || cleaned[i - 1] != '\\')) {
						in_string = false;
					}
					continue;
				}
				if (c == '"' || c == '\'') {
					in_string = true;
					string_delim = c;
					quoted += c;
					continue;
				}
				// Check for a bare identifier followed by ':'
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
					int start = i;
					while (i < cleaned.length() && ((cleaned[i] >= 'a' && cleaned[i] <= 'z') || (cleaned[i] >= 'A' && cleaned[i] <= 'Z') || cleaned[i] == '_' || (cleaned[i] >= '0' && cleaned[i] <= '9'))) {
						i++;
					}
					// Look ahead past whitespace for colon
					int k = i;
					while (k < cleaned.length() && (cleaned[k] == ' ' || cleaned[k] == '\t')) {
						k++;
					}
					// Skip booleans and null — they are values, not keys
					const String ident = cleaned.substr(start, i - start);
					if (k < cleaned.length() && cleaned[k] == ':' &&
							ident != "true" && ident != "false" && ident != "null") {
						quoted += '"';
						quoted += ident;
						quoted += '"';
					} else {
						quoted += ident;
					}
					i--; // will be incremented by loop
					continue;
				}
				quoted += c;
			}
			Dictionary _test2;
			if (_parse_json_dictionary_quiet(quoted, _test2)) {
				cleaned = quoted;
			}
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
		"{\"calls\":",
		"{\"calls\": ",
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
	// Try to recover truncated tool calls by closing braces.
	{
		String repaired = cleaned;
		// Add missing closing braces.
		int brace_depth = 0;
		bool in_string = false;
		bool escape = false;
		for (int i = 0; i < repaired.length(); i++) {
			const char32_t c = repaired[i];
			if (in_string) {
				if (escape) {
					escape = false;
				} else if (c == '\\') {
					escape = true;
				} else if (c == '"') {
					in_string = false;
				}
				continue;
			}
			if (c == '"') {
				in_string = true;
			} else if (c == '{') {
				brace_depth++;
			} else if (c == '}') {
				brace_depth--;
			}
		}
		while (brace_depth > 0) {
			repaired += "}";
			brace_depth--;
		}
		Dictionary repaired_parsed;
		if (_parse_json_dictionary_quiet(repaired, repaired_parsed)) {
			Dictionary norm = _normalize_envelope(repaired_parsed);
			if (_envelope_normalized_is_dispatchable(norm)) {
				return norm;
			}
		}
	}
	// Detect truncated tool call: has "arguments" but missing "tool"/"type".
	if (cleaned.contains("\"arguments\"") && !cleaned.contains("\"type\"") && !cleaned.contains("\"tool\":")) {
		fallback["message"] = TTR("The model output appears truncated — it sent tool arguments but no tool name. This usually means max_tokens is too low.\n\n"
				"Fix: increase `yeet_ai/chat/max_tokens` in Editor Settings AND on your inference server. Then try again.") +
				"\n\n--- Partial output (preview) ---\n" +
				_truncate_preview(cleaned, 2000);
	} else if (cleaned.contains("\"type\":\"tool_call\"") || cleaned.contains("\"batch_tool_calls\"") || cleaned.contains("batch_tool_calls") || cleaned.contains("\"method\":")) {
		fallback["message"] = TTR("The model output was not valid JSON. Common causes:\n"
				"(1) Response truncated by max_tokens — raise `yeet_ai/chat/max_tokens` in Editor Settings AND on the inference server.\n"
				"(2) Reasoning/commentary before the JSON block.\n"
				"(3) Malformed JSON from the model.\n\n"
				"Tip: Try asking the model to use single tool_calls instead of batch_tool_calls, or increase max_output_tokens on the server.") +
				"\n\n--- Raw output (preview) ---\n" +
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
		String tn = _clean_json_string_field(call, SNAME("tool"));
		if (tn.is_empty()) {
			tn = _clean_json_string_field(call, SNAME("name"));
		}
		if (tn.is_empty()) {
			tn = _clean_json_string_field(call, SNAME("method"));
		}
		if (tn.is_empty()) {
			const Dictionary fn = call.get("function", Dictionary());
			tn = _clean_json_string_field(fn, SNAME("name"));
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
	const String type = _clean_json_string_field(p_envelope, SNAME("type"));
	const String tool = _clean_json_string_field(p_envelope, SNAME("tool"));

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
		String t = _clean_json_string_field(out, SNAME("tool"));
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
		if (t == "batch_tool_calls") {
			Dictionary args_dict;
			if (out.has("arguments") && out["arguments"].get_type() == Variant::DICTIONARY) {
				args_dict = out["arguments"];
			}
			// Lift envelope-level calls/tool_calls/shared_arguments/etc.
			// into args before normalization. Without this, an envelope like
			// {"type":"tool_call","tool":"batch_tool_calls","calls":[...]}
			// reaches the dispatcher with empty args and bails immediately.
			_lift_batch_top_level_into_args(p_envelope, args_dict);
			out["arguments"] = _normalize_batch_tool_arguments(args_dict);
		}
		return out;
	}

	// ── Shape 1 ──────────────────────────────────────────────────────────────
	// Model put the tool name in "type":
	//   {"type":"batch_tool_calls","arguments":{...}}
	//   {"type":"batch_tool_calls","calls":[...]}            (calls at root)
	if (!type.is_empty() && (p_envelope.has("arguments") || p_envelope.has("calls") || p_envelope.has("tool_calls"))) {
		Dictionary fixed;
		fixed["type"] = "tool_call";
		fixed["tool"] = is_batch_tool_name_alias(type) ? String("batch_tool_calls") : type;
		Dictionary args_dict;
		if (p_envelope.has("arguments")) {
			const Variant args_var = p_envelope["arguments"];
			if (args_var.get_type() == Variant::STRING) {
				if (!_parse_json_dictionary_quiet(String(args_var).strip_edges(), args_dict)) {
					args_dict = Dictionary();
				}
			} else if (args_var.get_type() == Variant::DICTIONARY) {
				args_dict = args_var;
			}
		}
		if (String(fixed["tool"]) == "batch_tool_calls") {
			_lift_batch_top_level_into_args(p_envelope, args_dict);
			fixed["arguments"] = _normalize_batch_tool_arguments(args_dict);
		} else {
			fixed["arguments"] = args_dict;
		}
		return fixed;
	}

	// ── Shape 2 ──────────────────────────────────────────────────────────────
	// Model included "tool" but omitted "type":
	//   {"tool":"batch_tool_calls","arguments":{...}}
	//   {"tool":"batch_tool_calls","calls":[...]}            (calls at root)
	if (!tool.is_empty() && (p_envelope.has("arguments") || p_envelope.has("calls") || p_envelope.has("tool_calls"))) {
		Dictionary fixed;
		fixed["type"] = "tool_call";
		fixed["tool"] = is_batch_tool_name_alias(tool) ? String("batch_tool_calls") : tool;
		Dictionary args_dict;
		if (p_envelope.has("arguments")) {
			const Variant args_var = p_envelope["arguments"];
			if (args_var.get_type() == Variant::STRING) {
				if (!_parse_json_dictionary_quiet(String(args_var).strip_edges(), args_dict)) {
					args_dict = Dictionary();
				}
			} else if (args_var.get_type() == Variant::DICTIONARY) {
				args_dict = args_var;
			}
		}
		if (String(fixed["tool"]) == "batch_tool_calls") {
			_lift_batch_top_level_into_args(p_envelope, args_dict);
			fixed["arguments"] = _normalize_batch_tool_arguments(args_dict);
		} else {
			fixed["arguments"] = args_dict;
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

	AgentPreset game_builder;
	game_builder.id = "game_builder";
	game_builder.name = TTR("Game");
	game_builder.system_suffix = "\nYou are in game-building mode. Treat requests as playable slices, not decorative scenes. Build complete actors with input, scripts, camera/viewport context, visuals, collision layers/masks, and direct CollisionShape children sized to match visuals. Prefer create_game_actor_2d/create_game_actor_3d for gameplay entities. Required loop before final: save, audit_game_physics, repair/fix and audit again, play, capture_game_viewport, get_runtime_debugger_state, stop_playing_scene.";
	_agent_presets.push_back(game_builder);

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
	_switch_to_chat(_chat_sessions.size() - 1);
	_update_chat_selector();
}

void YeetAIDock::_switch_to_chat(int p_index) {
	if (p_index < 0 || p_index >= _chat_sessions.size()) {
		return;
	}

	// Save current chat state
	if (_active_chat_index >= 0 && _active_chat_index < _chat_sessions.size()) {
		_sync_active_chat_state();
	}

	_cancel_streaming();
	_set_waiting(false, TTR("Ready"));

	_active_chat_index = p_index;
	const ChatSession &session = _chat_sessions[p_index];
	conversation_messages = session.messages;
	_chat_records = session.records;
	_context_summary = session.context_summary;
	_context_summary_message_count = session.context_summary_message_count;
	tool_round_trips = 0;
	turn_context_prompt = String();
	_session_modified_files.clear();

	// Initialize conversation window for this chat session
	_conversation_window.current_tokens = 0;
	for (int i = 0; i < conversation_messages.size(); i++) {
		if (conversation_messages[i].get_type() == Variant::DICTIONARY) {
			_conversation_window.current_tokens += _estimate_message_tokens(conversation_messages[i]);
		}
	}

	// Rebuild the chat log (constructor may call before nodes exist; guarded for safety)
	if (chat_log) {
		chat_log->clear();
	}
	if (stream_label) {
		stream_label->set_visible(false);
		stream_label->clear();
	}
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
	if (_chat_records.is_empty() && !conversation_messages.is_empty()) {
		for (int i = 0; i < conversation_messages.size(); i++) {
			if (conversation_messages[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary msg = conversation_messages[i];
			MessageRecord record;
			record.role = _clean_json_string_field(msg, SNAME("role"));
			record.text = _message_content_to_text(msg.get("content", Variant())).strip_edges();
			if (record.role.is_empty() || record.text.is_empty()) {
				continue;
			}
			if (record.text.length() > 4000) {
				record.text = record.text.substr(0, 4000) + " ...";
			}
			_chat_records.push_back(record);
		}
	}
	_suppress_chat_recording = true;
	if (_chat_records.is_empty()) {
		intro_message_added = true;
		_append_message("assistant", TTR("Crosshair AI is ready. I can inspect the project, scenes, selected nodes, and perform scene and script edits."));
	} else {
		for (int i = 0; i < _chat_records.size(); i++) {
			_append_message(_chat_records[i].role, _chat_records[i].text);
		}
	}
	_suppress_chat_recording = false;

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

void YeetAIDock::_save_all_chats() {
	_sync_active_chat_state();

	const String dir = _get_chats_dir();
	Ref<DirAccess> da = DirAccess::open("res://");
	if (da.is_null()) {
		return;
	}
	da->make_dir_recursive(dir);

	for (int i = 0; i < _chat_sessions.size(); i++) {
		const ChatSession &s = _chat_sessions[i];
		Dictionary session_data;
		session_data["id"] = s.id;
		session_data["title"] = s.title;
		session_data["agent_preset"] = s.agent_preset;
		session_data["created_at"] = s.created_at;
		session_data["updated_at"] = s.updated_at;
		session_data["messages"] = s.messages;
		session_data["context_summary"] = s.context_summary;
		session_data["context_summary_message_count"] = s.context_summary_message_count;

		Array records;
		for (int j = 0; j < s.records.size(); j++) {
			Dictionary rec;
			rec["role"] = s.records[j].role;
			rec["text"] = s.records[j].text;
			records.push_back(rec);
		}
		session_data["records"] = records;

		const String path = dir.path_join(s.id + ".json");
		const String json_str = JSON::stringify(session_data, "\t");
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
					const Dictionary json_data = parsed;
					ChatSession session;
					session.id = json_data.get("id", "");
					session.title = json_data.get("title", TTR("Chat"));
					session.agent_preset = json_data.get("agent_preset", "general");
					session.created_at = json_data.get("created_at", 0);
					session.updated_at = json_data.get("updated_at", 0);
					session.messages = json_data.get("messages", Array());
					session.context_summary = json_data.get("context_summary", "");
					session.context_summary_message_count = json_data.get("context_summary_message_count", 0);

					const Array rec_arr = json_data.get("records", Array());
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

		MarginContainer *outer_mc = memnew(MarginContainer);
		outer_mc->add_theme_constant_override("margin_left", int(6.0f * EDSCALE));
		outer_mc->add_theme_constant_override("margin_right", int(6.0f * EDSCALE));
		outer_mc->add_theme_constant_override("margin_top", int(8.0f * EDSCALE));
		outer_mc->add_theme_constant_override("margin_bottom", int(8.0f * EDSCALE));
		_model_popup->add_child(outer_mc);

		VBoxContainer *vb = memnew(VBoxContainer);
		vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		vb->add_theme_constant_override("separation", int(6.0f * EDSCALE));
		outer_mc->add_child(vb);

		_model_search_edit = memnew(LineEdit);
		_model_search_edit->set_placeholder(TTR("Search models..."));
		_model_search_edit->set_clear_button_enabled(true);
		_model_search_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		_model_search_edit->connect("text_changed", callable_mp(this, &YeetAIDock::_on_model_search_changed));
		vb->add_child(_model_search_edit);

		_model_item_list = memnew(ItemList);
		_model_item_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		_model_item_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		_model_item_list->set_custom_minimum_size(Size2(280, 300) * EDSCALE);
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
	// Azure OpenAI (provider 4) doesn't have a models/tags endpoint; deployments are fixed.
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
	} else if (provider == 4) {
		// Azure OpenAI models are fixed by the deployment name.
		models.push_back("gpt-4o");
		models.push_back("gpt-4o-mini");
		models.push_back("gpt-4-turbo");
		models.push_back("gpt-4");
		models.push_back("gpt-35-turbo");
		String azure_deployment = _get_editor_setting_string("yeet_ai/chat/azure_deployment", "");
		if (!azure_deployment.is_empty() && !models.has(azure_deployment)) {
			models.push_back(azure_deployment);
		}
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
	const Variant json_result = json->get_data();
	if (json_result.get_type() != Variant::DICTIONARY) {
		return;
	}
	const Dictionary d = json_result;
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
		approx_tokens += _estimate_message_tokens(msg);
	}
	if (!_context_summary.is_empty()) {
		approx_tokens += _context_summary.length() / 4;
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

	const bool tree_ready = is_inside_tree();
	const Color accent = tree_ready ? get_theme_color(SNAME("accent_color"), EditorStringName(Editor)) : Color(0.3f, 0.5f, 0.9f);
	const Color font_dim = tree_ready ? get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)) : Color(0.5f, 0.5f, 0.5f);
	const int base_fs = tree_ready ? get_theme_font_size(SNAME("main_size"), EditorStringName(EditorFonts)) : 14;
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

// ═══════════════════════════════════════════════════════════════════════════
// STRUCTURED RESPONSE PARSER INTEGRATION
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_extract_structured_response(const String &p_content) const {
	// Use structured parser for reliable extraction
	YeetAIResponseEnvelope envelope = YeetAIResponseParser::parse(p_content);

	Dictionary result;

	if (envelope.is_tool_call()) {
		result["type"] = "tool_call";
		result["tool"] = envelope.tool_call.tool_name;
		result["arguments"] = envelope.tool_call.arguments;
	} else if (envelope.is_final_answer()) {
		result["type"] = "final";
		result["message"] = envelope.message;
	} else if (envelope.is_error()) {
		result["type"] = "error";
		result["error_code"] = envelope.error_code;
		result["error_message"] = envelope.error_message;
		result["raw"] = envelope.raw_data;
	} else if (envelope.type == YeetAIResponseType::THINKING) {
		// Thinking responses are not dispatchable
		result.clear();
		return result;
	} else {
		// Unknown - return empty
		result.clear();
		return result;
	}

	return result;
}

YeetAIResponseType YeetAIDock::_parse_response_type(const String &p_content) const {
	// Quick type detection without full parsing
	String cleaned = p_content.strip_edges();
	cleaned = _clean_content_artifacts(cleaned);

	// Check for thinking
	if (YeetAIResponseParser::is_thinking_response(cleaned)) {
		return YeetAIResponseType::THINKING;
	}

	// Find JSON object
	int json_start = cleaned.find("{");
	if (json_start == -1) {
		return YeetAIResponseType::UNKNOWN;
	}

	int json_end = find_json_object_end(cleaned, json_start);
	if (json_end == -1) {
		return YeetAIResponseType::UNKNOWN;
	}

	String json = cleaned.substr(json_start, json_end - json_start + 1);

	// Check for type field
	if (json.contains("\"type\":\"tool_call\"") || json.contains("\"type\": \"tool_call\"")) {
		return YeetAIResponseType::TOOL_CALL;
	}
	if (json.contains("\"type\":\"final\"") || json.contains("\"type\": \"final\"")) {
		return YeetAIResponseType::FINAL_ANSWER;
	}

	return YeetAIResponseType::UNKNOWN;
}

// ═══════════════════════════════════════════════════════════════════════════
// RETRY LOGIC WITH CONTEXT PRESERVATION
// ═══════════════════════════════════════════════════════════════════════════

void YeetAIDock::_start_retry(const String &tool_name, const Dictionary &args, const String &error) {
	_cancel_retry();

	_current_retry.tool_name = tool_name;
	_current_retry.original_args = args;
	_current_retry.attempt_count = 0;
	_current_retry.max_attempts = 3;
	_current_retry.last_error = error;

	// Keep last 3 tool results for context
	if (!_chat_records.is_empty()) {
		int recent = MIN(3, _chat_records.size());
		for (int i = 0; i < recent; i++) {
			Dictionary record_dict;
			record_dict["role"] = _chat_records[_chat_records.size() - 1 - i].role;
			record_dict["text"] = _chat_records[_chat_records.size() - 1 - i].text;
			_current_retry.last_tool_results.push_back(record_dict);
		}
	}

	_append_status_row(TTR("Retrying tool call..."));
}

void YeetAIDock::_execute_retry() {
	if (_current_retry.tool_name.is_empty()) {
		return;
	}

	_current_retry.attempt_count++;

	if (_current_retry.attempt_count > _current_retry.max_attempts) {
		_append_status_row(TTR("Retry limit reached for tool: ") + _current_retry.tool_name);
		_cancel_retry();
		return;
	}

	// Apply exponential backoff delay before retry
	int64_t backoff_delay = _calculate_backoff_delay(_current_retry.attempt_count - 1);
	if (backoff_delay > 0) {
		// Note: In threaded context, actual delay would be implemented via Thread.sleep()
		// For now, we log the intended delay
		WARN_PRINT(vformat("[YeetAI] Retry backoff: %lld ms before attempt %d", backoff_delay, _current_retry.attempt_count));
	}

	// Prepare retry context with error information
	Dictionary retry_args = _prepare_retry_context(_current_retry.tool_name, _current_retry.original_args);

	// Execute tool with retry context
	ToolExecutionResult result = _execute_tool(_current_retry.tool_name, retry_args);

	// Record retry metric
	_record_tool_execution(_current_retry.tool_name, 0, result.ok, true);

	if (result.ok) {
		_append_status_row(vformat(TTR("Retry successful on attempt %d"), _current_retry.attempt_count));
		_cancel_retry();

		// Continue with normal flow
		Dictionary tool_payload = result.payload;
		tool_payload["ok"] = true;
		conversation_messages.append(_make_user_message_with_optional_vision(_current_retry.tool_name, tool_payload));
		_request_model_response();
		return;
	}

	// Retry failed - try again or give up
	String error_msg = String(result.payload.get("error", "Unknown error"));
	if (_should_retry(_current_retry.tool_name, error_msg)) {
		_append_status_row(vformat(TTR("Retry %d/%d failed: %s"), _current_retry.attempt_count, _current_retry.max_attempts, error_msg));
		_current_retry.last_error = error_msg;
		_execute_retry(); // Try again
	} else {
		_append_status_row(vformat(TTR("Giving up on retry: %s"), error_msg));
		_cancel_retry();
	}
}

void YeetAIDock::_cancel_retry() {
	_current_retry.tool_name = "";
	_current_retry.original_args.clear();
	_current_retry.last_tool_results.clear();
	_current_retry.attempt_count = 0;
	_current_retry.last_error = "";
}

void YeetAIDock::_handle_tool_failure(const String &tool_name, const Dictionary &args, const ToolExecutionResult &result) {
	String error_msg = String(result.payload.get("error", "Unknown error"));

	if (!_should_retry(tool_name, error_msg)) {
		return;
	}

	_start_retry(tool_name, args, error_msg);
	_execute_retry();
}

bool YeetAIDock::_should_retry(const String &tool_name, const String &error) {
	// Retryable errors
	static const char *retryable_errors[] = {
		"not found", "timeout", "connection", "network",
		"invalid", "missing", "permission", "access denied",
	};

	for (const char *pattern : retryable_errors) {
		if (error.to_lower().contains(pattern)) {
			return true;
		}
	}

	// Retry for most tool errors except permanent failures
	return !error.to_lower().contains("permanent") && !error.to_lower().contains("fatal");
}

Dictionary YeetAIDock::_prepare_retry_context(const String &tool_name, const Dictionary &original_args) {
	Dictionary retry_args = original_args.duplicate();

	// Add retry context
	retry_args["retry_attempt"] = _current_retry.attempt_count;
	retry_args["last_error"] = _current_retry.last_error;

	// Add recent tool results for context
	Array context_results;
	for (int i = 0; i < _current_retry.last_tool_results.size(); i++) {
		Dictionary d = _current_retry.last_tool_results[i];
		context_results.push_back(d.get("text", ""));
	}
	retry_args["recent_context"] = context_results;

	return retry_args;
}

// ═══════════════════════════════════════════════════════════════════════════
// RESULT CACHING
// ═══════════════════════════════════════════════════════════════════════════

String YeetAIDock::_generate_cache_key(const String &tool_name, const Dictionary &args) const {
	// Simple hash-based cache key
	String key = tool_name;

	// Include relevant argument values
	Vector<String> arg_values;
	Array sorted_keys = args.keys();
	for (int i = 0; i < sorted_keys.size(); i++) {
		Variant val = args[sorted_keys[i]];
		if (val.get_type() == Variant::STRING) {
			arg_values.push_back(String(sorted_keys[i]) + "=" + String(val));
		}
	}

	key += ":" + String(", ").join(arg_values);

	// Simple hash
	uint32_t hash = 5381;
	for (int i = 0; i < key.length(); i++) {
		hash = hash * 33 + key[i];
	}

	return tool_name + "_" + String::num_uint64(hash);
}

void YeetAIDock::_cache_result(const String &cache_key, const Dictionary &result) {
	CacheEntry entry;
	entry.result = result;
	entry.expires_at = Time::get_singleton()->get_unix_time_from_system() + CACHE_TTL_SECONDS;
	_result_cache[cache_key] = entry;
}

Dictionary YeetAIDock::_get_cached_result(const String &cache_key) const {
	auto iter = _result_cache.find(cache_key);
	if (iter == _result_cache.end()) {
		_session_metrics.cache_misses++;
		return Dictionary();
	}

	const CacheEntry &entry = iter->value;
	if (!entry.is_valid()) {
		_session_metrics.cache_misses++;
		return Dictionary();
	}

	_session_metrics.cache_hits++;
	return entry.result;
}

void YeetAIDock::_invalidate_cache(const String &pattern) {
	if (pattern.is_empty()) {
		_result_cache.clear();
		return;
	}

	Vector<String> keys_to_remove;
	for (const KeyValue<String, CacheEntry> &E : _result_cache) {
		const String &key = E.key;
		if (key.contains(pattern)) {
			keys_to_remove.push_back(key);
		}
	}

	for (const String &key : keys_to_remove) {
		_result_cache.erase(key);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// AUTOMATIC TOOL BATCHING
// ═══════════════════════════════════════════════════════════════════════════

void YeetAIDock::_analyze_batch_opportunity(const String &tool_name, const Dictionary &args) {
	// Skip if already using batch_tool_calls
	if (tool_name == "batch_tool_calls") {
		return;
	}

	// Determine dependency group
	String dep_group;
	if (tool_name.contains("create_") || tool_name.contains("add_")) {
		dep_group = "scene_construction";
	} else if (tool_name == "set_node_property") {
		dep_group = "property_setting";
	} else if (tool_name == "connect_signal") {
		dep_group = "signal_connection";
	} else {
		dep_group = "other";
	}

	// Check if we can batch with previous operation
	if (!_pending_batch_ops.is_empty() && _can_batch_with_previous(_pending_batch_ops[_pending_batch_ops.size() - 1].tool_name, tool_name)) {
		// Can batch - add to pending
		BatchOpportunity op;
		op.tool_name = tool_name;
		op.args = args;
		op.dependency_group = dep_group;
		_pending_batch_ops.push_back(op);
	} else {
		// Execute pending batch first if any
		if (!_pending_batch_ops.is_empty()) {
			_execute_pending_batch();
		}

		// Start new batch group
		BatchOpportunity op;
		op.tool_name = tool_name;
		op.args = args;
		op.dependency_group = dep_group;
		_pending_batch_ops.push_back(op);
	}
}

bool YeetAIDock::_can_batch_with_previous(const String &current_tool, const String &prev_tool) {
	// Operations that can be batched together
	if (prev_tool == "batch_tool_calls" || current_tool == "batch_tool_calls") {
		return false;
	}

	// Script operations must not be mixed with scene operations
	bool prev_is_script = prev_tool.contains("gdscript") || prev_tool.contains("script");
	bool curr_is_script = current_tool.contains("gdscript") || current_tool.contains("script");
	if (prev_is_script != curr_is_script) {
		return false;
	}

	// Scene construction operations can be batched
	if (prev_tool.contains("create_") || prev_tool.contains("add_")) {
		if (current_tool.contains("create_") || current_tool.contains("add_")) {
			return true;
		}
	}

	// Property setting operations can be batched
	if (prev_tool == "set_node_property" && current_tool == "set_node_property") {
		return true;
	}

	return false;
}

Dictionary YeetAIDock::_prepare_batch_request(const Vector<BatchOpportunity> &ops) {
	Dictionary batch_call;

	Array calls;
	for (int i = 0; i < ops.size(); i++) {
		Dictionary call;
		call["tool"] = ops[i].tool_name;
		call["arguments"] = ops[i].args;
		calls.push_back(call);
	}

	batch_call["calls"] = calls;
	batch_call["stop_on_error"] = false;
	batch_call["dry_run"] = false;

	return batch_call;
}

void YeetAIDock::_execute_pending_batch() {
	if (_pending_batch_ops.is_empty()) {
		return;
	}

	// Group by dependency to avoid conflicts
	HashMap<String, Vector<BatchOpportunity>> groups;
	for (int i = 0; i < _pending_batch_ops.size(); i++) {
		groups[_pending_batch_ops[i].dependency_group].push_back(_pending_batch_ops[i]);
	}

	// Execute each group as a batch
	for (const KeyValue<String, Vector<BatchOpportunity>> &E : groups) {
		const String &group = E.key;
		const Vector<BatchOpportunity> &group_ops = E.value;

		if (group_ops.size() == 1) {
			// Single operation - execute directly
			ToolExecutionResult result = _execute_tool(group_ops[0].tool_name, group_ops[0].args);
			if (!result.ok) {
				_handle_tool_failure(group_ops[0].tool_name, group_ops[0].args, result);
			}
		} else {
			// Multiple operations - batch them
			Dictionary batch = _prepare_batch_request(group_ops);
			ToolExecutionResult result = _execute_tool("batch_tool_calls", batch);
			if (!result.ok) {
				_handle_tool_failure("batch_tool_calls", batch, result);
			}
		}
	}

	_clear_pending_batch();
}

void YeetAIDock::_clear_pending_batch() {
	_pending_batch_ops.clear();
}

// Update tool execution to integrate batching
