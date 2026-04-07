/**************************************************************************/
/*  yeet_ai_dock.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#pragma once

#include "core/math/rect2i.h"
#include "core/object/property_info.h"
#include "scene/gui/box_container.h"

#include "core/templates/vector.h"

class Button;
class HTTPRequest;
class InputEvent;
class Label;
class MarginContainer;
class Node;
class PanelContainer;
class Resource;
class RichTextLabel;
class TextEdit;

class YeetAIDock : public VBoxContainer {
	GDCLASS(YeetAIDock, VBoxContainer);

	struct ToolExecutionResult {
		bool ok = false;
		Dictionary payload;
		String display_text;
	};

	Label *status_label = nullptr;
	RichTextLabel *chat_log = nullptr;
	TextEdit *prompt_input = nullptr;
	Button *send_button = nullptr;
	Button *clear_button = nullptr;
	HTTPRequest *request = nullptr;

	PanelContainer *header_panel = nullptr;
	PanelContainer *chat_panel = nullptr;
	PanelContainer *input_panel = nullptr;
	MarginContainer *outer_margin = nullptr;

	void _apply_dock_theme();

	Array conversation_messages;
	bool waiting_for_response = false;
	int tool_round_trips = 0;
	bool intro_message_added = false;
	String turn_context_prompt;

	// Async game screenshot callback (EditorRun); only touched on the main thread.
	bool game_screenshot_done = false;
	int64_t game_screenshot_w = 0;
	int64_t game_screenshot_h = 0;
	String game_screenshot_path;

	void _on_game_screenshot_cb(int64_t p_w, int64_t p_h, const String &p_path, Rect2i p_rect);
	void _send_prompt();
	void _clear_chat();
	void _on_prompt_gui_input(const Ref<InputEvent> &p_event);
	void _append_message(const String &p_role, const String &p_text);
	void _append_tool_result(const String &p_tool_name, const Dictionary &p_args, const ToolExecutionResult &p_result);
	String _humanize_tool_name(const String &p_tool) const;
	Vector<String> _collect_relevant_paths(const Dictionary &p_args, const Dictionary &p_payload) const;
	String _truncate_preview(const String &p_text, int p_max_chars) const;
	void _set_waiting(bool p_waiting, const String &p_status);
	void _request_model_response();
	void _handle_model_response(const String &p_content);
	ToolExecutionResult _execute_tool(const String &p_tool_name, const Dictionary &p_args) const;
	String _build_runtime_context_prompt() const;
	String _build_task_hints_for_user_prompt(const String &p_user_prompt) const;
	Node *_resolve_scene_root(const String &p_scene_path, String &r_error) const;
	Node *_resolve_node_target(Node *p_scene_root, const String &p_node_path, String &r_error) const;
	void _set_owner_recursive(Node *p_node, Node *p_owner) const;
	Dictionary _tool_get_project_tree(const Dictionary &p_args) const;
	Dictionary _tool_read_project_file(const Dictionary &p_args) const;
	Dictionary _tool_get_open_scenes() const;
	Dictionary _tool_get_current_scene() const;
	Dictionary _tool_get_selected_nodes() const;
	Dictionary _tool_get_project_settings(const Dictionary &p_args) const;
	Dictionary _tool_get_input_actions(const Dictionary &p_args) const;
	Dictionary _tool_find_project_files(const Dictionary &p_args) const;
	Dictionary _tool_get_scene_tree(const Dictionary &p_args) const;
	Dictionary _tool_get_node_details(const Dictionary &p_args) const;
	Dictionary _tool_get_node_api(const Dictionary &p_args) const;
	Dictionary _tool_batch_tool_calls(const Dictionary &p_args) const;
	Dictionary _tool_open_scene(const Dictionary &p_args) const;
	Dictionary _tool_save_current_scene() const;
	Dictionary _tool_create_scene_file(const Dictionary &p_args) const;
	Dictionary _tool_create_gdscript_file(const Dictionary &p_args) const;
	Dictionary _tool_update_gdscript_file(const Dictionary &p_args) const;
	Dictionary _tool_attach_script(const Dictionary &p_args) const;
	Dictionary _tool_add_node(const Dictionary &p_args) const;
	Dictionary _tool_instantiate_scene(const Dictionary &p_args) const;
	Dictionary _tool_add_primitive_mesh(const Dictionary &p_args) const;
	Dictionary _tool_add_collision_shape(const Dictionary &p_args) const;
	Dictionary _tool_create_standard_material(const Dictionary &p_args) const;
	Dictionary _tool_assign_resource_to_property(const Dictionary &p_args) const;
	Dictionary _tool_connect_signal(const Dictionary &p_args) const;
	Dictionary _tool_create_input_action(const Dictionary &p_args) const;
	Dictionary _tool_set_main_scene(const Dictionary &p_args) const;
	Dictionary _tool_play_current_scene() const;
	Dictionary _tool_play_main_scene() const;
	Dictionary _tool_stop_playing_scene() const;
	Dictionary _tool_remove_node(const Dictionary &p_args) const;
	Dictionary _tool_set_node_property(const Dictionary &p_args) const;
	Dictionary _tool_write_project_file(const Dictionary &p_args) const;
	Dictionary _tool_save_all_scenes() const;
	Dictionary _tool_reload_scene(const Dictionary &p_args) const;
	Dictionary _tool_set_editor_main_screen(const Dictionary &p_args) const;
	Dictionary _tool_select_file(const Dictionary &p_args) const;
	Dictionary _tool_get_unsaved_scenes() const;
	Dictionary _tool_reparent_node(const Dictionary &p_args) const;
	Dictionary _tool_rename_node(const Dictionary &p_args) const;
	Dictionary _tool_file_exists(const Dictionary &p_args) const;
	Dictionary _tool_list_directory(const Dictionary &p_args) const;
	Dictionary _tool_duplicate_node(const Dictionary &p_args) const;
	Dictionary _tool_edit_script(const Dictionary &p_args) const;
	Dictionary _tool_move_child(const Dictionary &p_args) const;
	Dictionary _tool_create_project_folder(const Dictionary &p_args) const;
	Dictionary _tool_delete_project_file(const Dictionary &p_args) const;
	Dictionary _tool_get_editor_log(const Dictionary &p_args) const;
	Dictionary _tool_capture_editor_viewport(const Dictionary &p_args) const;
	Dictionary _tool_get_debug_snapshot(const Dictionary &p_args) const;
	Dictionary _tool_grep_project_files(const Dictionary &p_args) const;
	Dictionary _tool_get_autoloads() const;
	Dictionary _tool_get_node_groups(const Dictionary &p_args) const;
	Dictionary _tool_get_node_collision_layers(const Dictionary &p_args) const;
	Dictionary _tool_move_project_file(const Dictionary &p_args) const;
	Dictionary _tool_get_animation_player_state(const Dictionary &p_args) const;
	Dictionary _tool_get_tilemap_info(const Dictionary &p_args) const;
	Dictionary _tool_get_navigation_region_info(const Dictionary &p_args) const;
	Dictionary _tool_capture_game_viewport(const Dictionary &p_args) const;
	Dictionary _tool_get_runtime_debugger_state() const;
	Dictionary _tool_editor_undo(const Dictionary &p_args) const;
	void _grep_project_files_recursive(const String &p_dir, const String &p_query, bool p_case_sensitive, const Vector<String> &p_extensions, int p_max_bytes, int p_max_matches, int &r_match_count, Array &r_matches) const;
	void _collect_project_entries(const String &p_dir_path, int p_depth, int p_max_depth, const Vector<String> &p_include_extensions, Array &r_entries, int &r_entry_count) const;
	void _find_project_entries(const String &p_dir_path, const String &p_query, int p_depth, int p_max_depth, const Vector<String> &p_include_extensions, Array &r_entries, int &r_entry_count, int p_max_results) const;
	Dictionary _serialize_node(Node *p_node, int p_depth, int p_max_depth, const Vector<String> &p_include_properties, int &r_node_count) const;
	Dictionary _serialize_node_properties(Node *p_node, const Vector<String> &p_include_properties) const;
	bool _node_has_property(Node *p_node, const StringName &p_property) const;
	bool _get_node_property_info(Node *p_node, const StringName &p_property, PropertyInfo &r_info) const;
	Ref<Resource> _load_resource_for_property(const String &p_resource_path, const String &p_expected_type, String &r_error) const;
	Variant _json_safe_variant(const Variant &p_value, int p_depth = 0) const;
	Variant _variant_from_json(const Variant &p_input, Variant::Type p_hint_type = Variant::NIL) const;
	bool _parse_json_dictionary_quiet(const String &p_text, Dictionary &r_result) const;
	String _get_editor_setting_string(const String &p_setting, const String &p_default) const;
	int _get_editor_setting_int(const String &p_setting, int p_default) const;
	float _get_editor_setting_float(const String &p_setting, float p_default) const;
	bool _get_editor_setting_bool(const String &p_setting, bool p_default) const;
	Dictionary _make_user_message_with_optional_vision(const String &p_tool_name, const Dictionary &p_tool_payload) const;
	String _build_system_prompt() const;
	String _escape_bbcode(const String &p_text) const;
	String _extract_message_content(const Dictionary &p_response_json) const;
	Dictionary _extract_response_envelope(const String &p_content) const;
	bool _try_merge_adjacent_tool_call_json(const String &p_cleaned, Dictionary &r_envelope) const;
	Dictionary _normalize_envelope(const Dictionary &p_envelope) const;
	Dictionary _normalize_batch_tool_arguments(const Dictionary &p_args) const;
	Dictionary _parse_one_batch_call(const Variant &p_call_var, int p_index, const Dictionary &p_shared_arguments) const;
	Vector<String> _variant_array_to_string_vector(const Array &p_values) const;
	bool _is_allowed_text_file(const String &p_path) const;

	void _on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _notification(int p_what);

protected:
	static void _bind_methods();

public:
	YeetAIDock();
};
