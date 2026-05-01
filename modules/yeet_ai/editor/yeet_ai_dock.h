/**************************************************************************/
/*  yeet_ai_dock.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#pragma once

#include "core/math/rect2i.h"
#include "core/object/property_info.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/os/time.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "scene/gui/box_container.h"
#include "scene/resources/material.h"
#include "yeet_ai_response_parser.h"

class Button;
class ColorRect;
class HSeparator;
class HTTPRequest;
class InputEvent;
class ItemList;
class Label;
class LineEdit;
class MarginContainer;
class Node;
class OptionButton;
class PanelContainer;
class PopupPanel;
class ProgressBar;
class Resource;
class RichTextLabel;
class ScrollContainer;
class TextEdit;

class Image;
class EditorInterface;

Array coerce_json_array_from_variant(const Variant &p_v);
Dictionary coerce_json_dictionary_from_variant(const Variant &p_v);
bool contains_string(const Vector<String> &p_values, const String &p_value);

class YeetAIDock : public VBoxContainer {
	GDCLASS(YeetAIDock, VBoxContainer);

	struct ToolExecutionResult {
		bool ok = false;
		Dictionary payload;
		String display_text;
	};

	// Parallel record used for chat rebuild (retry, session load)
	struct MessageRecord {
		String role;
		String text;
	};

	struct ChatSession {
		String id;
		String title;
		Array messages;
		Vector<MessageRecord> records;
		String agent_preset;
		int64_t created_at = 0;
		int64_t updated_at = 0;
	};

	// ── Core UI ──────────────────────────────────────────────────────────────
	ColorRect *_status_dot = nullptr;
	Label *status_label = nullptr;
	RichTextLabel *chat_log = nullptr;
	RichTextLabel *stream_label = nullptr;
	ScrollContainer *chat_scroll = nullptr;
	TextEdit *prompt_input = nullptr;
	Button *send_button = nullptr;
	Button *stop_button = nullptr;
	Button *clear_button = nullptr;

	PanelContainer *header_panel = nullptr;
	PanelContainer *chat_panel = nullptr;
	PanelContainer *input_panel = nullptr;
	MarginContainer *outer_margin = nullptr;

	void _apply_dock_theme();

	// ── Token counter ─────────────────────────────────────────────────────────
	Label *_token_count_label = nullptr;
	void _update_token_counter();

	// ── Tool progress bar ─────────────────────────────────────────────────────
	ProgressBar *_tool_progress_bar = nullptr;
	void _update_tool_progress_bar();

	// ── Session file tracker ──────────────────────────────────────────────────
	HashSet<String> _session_modified_files;
	RichTextLabel *_files_modified_label = nullptr; // RichTextLabel for meta links
	void _update_files_modified_label();
	void _on_files_meta_clicked(const Variant &p_meta);

	// ── Model selector ────────────────────────────────────────────────────────
	Button *_model_selector_button = nullptr;
	PopupPanel *_model_popup = nullptr;
	LineEdit *_model_search_edit = nullptr;
	ItemList *_model_item_list = nullptr;
	HTTPRequest *_model_tags_request = nullptr;
	Vector<String> _fetched_model_tags;

	void _on_model_selector_pressed();
	void _populate_model_list(const String &p_filter = String());
	void _on_model_search_changed(const String &p_text);
	void _on_model_item_activated(int p_index);
	void _on_model_tags_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _update_model_button_label();

	// ── Agent selector ────────────────────────────────────────────────────────
	OptionButton *_agent_selector = nullptr;
	struct AgentPreset {
		String id;
		String name;
		String system_suffix;
	};
	Vector<AgentPreset> _agent_presets;
	void _build_agent_presets();
	void _on_agent_selected(int p_index);
	String _get_current_agent_id() const;

	// ── Chat management ──────────────────────────────────────────────────────
	OptionButton *_chat_selector = nullptr;
	Button *_new_chat_button = nullptr;
	Button *_delete_chat_button = nullptr;
	Vector<ChatSession> _chat_sessions;
	int _active_chat_index = -1;
	void _on_chat_selected(int p_index);
	void _on_new_chat_pressed();
	void _on_delete_chat_pressed();
	void _create_new_chat();
	void _switch_to_chat(int p_index);
	void _update_chat_selector();
	void _update_chat_title();
	void _save_all_chats() const;
	void _load_chats();
	String _get_chats_dir() const;

	// ── Conversation state ────────────────────────────────────────────────────
	Array conversation_messages;
	Vector<MessageRecord> _chat_records; // parallel to chat_log content for rebuild
	bool waiting_for_response = false;
	int tool_round_trips = 0;
	bool intro_message_added = false;
	bool _session_loaded = false;
	String turn_context_prompt;
	
	// ── Conversation Window Management ─────────────────────────────────────────
	struct ConversationWindow {
		int max_tokens;
		int current_tokens;
		int trim_threshold; // Start trimming when exceeding this
		bool sliding_enabled;
		int min_keep_messages; // Always keep at least N messages (system, errors)
		
		int estimate_message_tokens(const String &text) const;
	};
	ConversationWindow _conversation_window;
	
	void _init_conversation_window();
	void _add_message_to_window(const String &role, const String &text);
	void _trim_conversation_window();
	void _trim_to_fit(int new_message_tokens);
	int _get_windowed_messages_count() const;
	
	// ── Metrics Collector ──────────────────────────────────────────────────────
	struct ToolMetrics {
		int calls = 0;
		int errors = 0;
		int retries = 0;
		int64_t total_time_ms = 0;
		double avg_time_ms = 0.0;
	};
	
	struct SessionMetrics {
		int total_api_calls = 0;
		int total_api_errors = 0;
		int cache_hits = 0;
		int cache_misses = 0;
		int64_t total_execution_time_ms = 0;
		int64_t session_start_time = 0;
	};
	
	mutable HashMap<String, ToolMetrics> _tool_metrics;
	mutable SessionMetrics _session_metrics;
	
	void _record_tool_execution(const String &tool_name, int64_t duration_ms, bool success, bool was_retry);
	Dictionary _export_metrics_summary() const;
	void _reset_metrics();
	
	// ── Cache Eviction ─────────────────────────────────────────────────────────
	void _evict_expired_cache_entries();
	void _evict_least_recently_used(int max_entries = 100);
	void _evict_cache_if_needed();
	
	// ── Exponential Backoff ────────────────────────────────────────────────────
	struct RetryPolicy {
		int max_attempts = 3;
		int base_delay_ms = 1000;
		int max_delay_ms = 30000;
		double backoff_multiplier = 2.0;
	};
	RetryPolicy _retry_policy;
	
	int64_t _calculate_backoff_delay(int attempt) const;
	
	// ── Stream Snapshot ────────────────────────────────────────────────────────
	struct StreamSnapshot {
		String accumulated_content;
		bool partial_tool_call_detected;
		String partial_tool_name;
		Dictionary partial_tool_args;
		int64_t timestamp;
	};
	StreamSnapshot _stream_snapshot;
	bool _stream_paused = false;
	
	void _pause_streaming();
	void _resume_streaming();
	void _save_stream_snapshot();
	void _restore_stream_snapshot();
	bool _has_pending_tool_call() const;
	
	// ── System Prompt Modularization ───────────────────────────────────────────
	struct SystemPromptSection {
		String id;
		String name;
		String content;
		bool enabled = true;
	};
	
	Vector<SystemPromptSection> _system_prompt_sections;
	
	void _build_default_prompt_sections();
	void _add_prompt_section(const String &id, const String &name, const String &content);
	void _remove_prompt_section(const String &id);
	void _enable_prompt_section(const String &id, bool enabled);
	String _build_modular_system_prompt() const;
	
	// ── Native tool calling (OpenAI tools parameter) ──────────────────────────
	bool _native_tools_enabled = true;
	Array _build_tools_payload() const;
	Dictionary _convert_native_tool_call_to_envelope(const Dictionary &p_tool_call) const;
	bool _response_has_native_tool_calls(const Dictionary &p_response) const;
	Array _extract_native_tool_calls(const Dictionary &p_response) const;
	void _handle_native_tool_calls(const Dictionary &p_message);
	
	// ── Retry logic ───────────────────────────────────────────────────────────
	struct RetryState {
		String tool_name;
		Dictionary original_args;
		Array last_tool_results; // Last N results for context
		int attempt_count = 0;
		int max_attempts = 3;
		String last_error;
	};
	RetryState _current_retry;
	void _start_retry(const String &tool_name, const Dictionary &args, const String &error);
	void _execute_retry();
	void _cancel_retry();
	
	// ── Result caching ────────────────────────────────────────────────────────
	struct CacheEntry {
		Dictionary result;
		int64_t expires_at; // Timestamp when cache expires
		bool is_valid() const { return Time::get_singleton()->get_unix_time_from_system() < expires_at; }
	};
	HashMap<String, CacheEntry> _result_cache;
	static constexpr int CACHE_TTL_SECONDS = 30; // 30 second TTL
	void _cache_result(const String &cache_key, const Dictionary &result);
	Dictionary _get_cached_result(const String &cache_key) const;
	void _invalidate_cache(const String &pattern = "");
	
	// ── Automatic batching ────────────────────────────────────────────────────
	struct BatchOpportunity {
		String tool_name;
		Dictionary args;
		String dependency_group; // Related operations
	};
	Vector<BatchOpportunity> _pending_batch_ops;
	void _analyze_batch_opportunity(const String &tool_name, const Dictionary &args);
	void _execute_pending_batch();
	void _clear_pending_batch();

	// ── Prompt history ────────────────────────────────────────────────────────
	Vector<String> _prompt_history;
	int _prompt_history_index = -1;
	String _prompt_history_draft;
	void _history_navigate(int p_direction);

	// ── SSE Streaming ─────────────────────────────────────────────────────────
	Thread _stream_thread;
	Mutex _stream_mutex;
	bool _stream_active = false;
	volatile bool _stream_should_stop = false;
	bool _stream_done_flag = false;
	bool _stream_error_flag = false;
	String _stream_error_msg;
	Vector<String> _pending_chunks;
	String _stream_accumulated;
	String _stream_endpoint;
	Vector<String> _stream_req_headers;
	String _stream_req_body;
	bool _stream_expects_sse = true;

	// Streaming tool call accumulation (delta.tool_calls SSE parsing)
	Array _stream_tool_call_accumulator; // Accumulates partial tool_calls by index
	bool _stream_has_tool_calls = false;

	void _start_streaming();
	void _cancel_streaming();
	static void _stream_thread_trampoline(void *p_user);
	void _stream_thread_body();
	void _drain_stream_queue();
	void _finalize_stream();

	// ── Typing indicator ──────────────────────────────────────────────────────
	bool _typing_indicator_active = false;
	float _typing_dot_time = 0.0f;

	// ── Animation ─────────────────────────────────────────────────────────────
	float _anim_time = 0.0f;

	void _update_animation(double p_delta);
	void _update_stream_label();
	void _scroll_to_bottom();

	// ── Async game screenshot ──────────────────────────────────────────────────
	mutable bool game_screenshot_done = false;
	mutable int64_t game_screenshot_w = 0;
	mutable int64_t game_screenshot_h = 0;
	mutable String game_screenshot_path;

	void _on_game_screenshot_cb(int64_t p_w, int64_t p_h, const String &p_path, Rect2i p_rect);

	// ── User actions ──────────────────────────────────────────────────────────
	void _send_prompt();
	void _clear_chat();
	void _on_stop_pressed();
	void _on_prompt_gui_input(const Ref<InputEvent> &p_event);
	void _on_chat_meta_clicked(const Variant &p_meta);

	// ── Chat rendering ────────────────────────────────────────────────────────
	void _append_message(const String &p_role, const String &p_text);
	void _rebuild_chat_log();
	void _append_tool_result(const String &p_tool_name, const Dictionary &p_args, const ToolExecutionResult &p_result);
	void _append_tool_running(const String &p_tool_name, const Dictionary &p_args);
	void _update_batch_progress(int p_current, int p_total, const String &p_tool_name);
	void _append_status_row(const String &p_text);
	String _humanize_tool_name(const String &p_tool) const;
	String _icon_for_tool(const String &p_tool_name) const;
	Vector<String> _collect_relevant_paths(const Dictionary &p_args, const Dictionary &p_payload) const;
	String _truncate_preview(const String &p_text, int p_max_chars) const;
	String _apply_gdscript_highlighting(const String &p_text) const;

	// ── Session persistence ───────────────────────────────────────────────────
	void _save_session() const;
	void _load_session();

	// ── Request / response cycle ──────────────────────────────────────────────
	void _set_waiting(bool p_waiting, const String &p_status);
	void _request_model_response();
	void _handle_model_response(const String &p_content);

protected:
	// Tool methods are protected so yeet_ai_tools.cpp can take their
	// addresses for the dispatch table.
	ToolExecutionResult _execute_tool(const String &p_tool_name, const Dictionary &p_args);
	String _build_runtime_context_prompt() const;
	String _build_task_hints_for_user_prompt(const String &p_user_prompt) const;
	Node *_resolve_scene_root(const String &p_scene_path, String &r_error) const;
	Node *_resolve_node_target(Node *p_scene_root, const String &p_node_path, String &r_error) const;
	void _set_owner_recursive(Node *p_node, Node *p_owner) const;
	void _mark_unsaved() const;
	void _add_to_scene(Node *p_parent, Node *p_child, Node *p_owner) const;
	Dictionary _tool_get_project_tree(const Dictionary &p_args) const;
	Dictionary _tool_read_project_file(const Dictionary &p_args) const;
	Dictionary _tool_get_open_scenes(const Dictionary &p_args) const;
	Dictionary _tool_get_current_scene(const Dictionary &p_args) const;
	Dictionary _tool_get_selected_nodes(const Dictionary &p_args) const;
	Dictionary _tool_get_project_settings(const Dictionary &p_args) const;
	Dictionary _tool_get_input_actions(const Dictionary &p_args) const;
	Dictionary _tool_find_project_files(const Dictionary &p_args) const;
	Dictionary _tool_get_scene_tree(const Dictionary &p_args) const;
	Dictionary _tool_get_node_details(const Dictionary &p_args) const;
	Dictionary _tool_get_node_api(const Dictionary &p_args) const;
	Dictionary _tool_batch_tool_calls(const Dictionary &p_args) const;
	Dictionary _tool_open_scene(const Dictionary &p_args) const;
	Dictionary _tool_save_current_scene(const Dictionary &p_args) const;
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
	Dictionary _tool_play_current_scene(const Dictionary &p_args) const;
	Dictionary _tool_play_main_scene(const Dictionary &p_args) const;
	Dictionary _tool_stop_playing_scene(const Dictionary &p_args) const;
	Dictionary _tool_remove_node(const Dictionary &p_args) const;
	Dictionary _tool_set_node_property(const Dictionary &p_args) const;
	Dictionary _tool_write_project_file(const Dictionary &p_args) const;
	Dictionary _tool_save_all_scenes(const Dictionary &p_args) const;
	Dictionary _tool_reload_scene(const Dictionary &p_args) const;
	Dictionary _tool_set_editor_main_screen(const Dictionary &p_args) const;
	Dictionary _tool_select_file(const Dictionary &p_args) const;
	Dictionary _tool_get_unsaved_scenes(const Dictionary &p_args) const;
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
	Dictionary _tool_get_autoloads(const Dictionary &p_args) const;
	Dictionary _tool_get_node_groups(const Dictionary &p_args) const;
	Dictionary _tool_get_node_collision_layers(const Dictionary &p_args) const;
	Dictionary _tool_move_project_file(const Dictionary &p_args) const;
	Dictionary _tool_copy_project_file(const Dictionary &p_args) const;
	Dictionary _tool_get_resource_dependencies(const Dictionary &p_args) const;
	Dictionary _tool_get_signal_connections(const Dictionary &p_args) const;
	Dictionary _tool_get_animation_player_state(const Dictionary &p_args) const;
	Dictionary _tool_get_tilemap_info(const Dictionary &p_args) const;
	Dictionary _tool_get_navigation_region_info(const Dictionary &p_args) const;
	Dictionary _tool_capture_game_viewport(const Dictionary &p_args) const;
	Dictionary _tool_capture_dual_view(const Dictionary &p_args) const;
	Dictionary _tool_capture_texture_resource(const Dictionary &p_args) const;
	Dictionary _tool_capture_subviewport(const Dictionary &p_args) const;
	Dictionary _tool_get_runtime_debugger_state(const Dictionary &p_args) const;
	Dictionary _tool_get_remote_scene_tree(const Dictionary &p_args) const;
	Dictionary _tool_editor_undo(const Dictionary &p_args) const;
	Dictionary _tool_get_editor_settings(const Dictionary &p_args) const;
	Dictionary _tool_patch_project_settings(const Dictionary &p_args) const;
	Dictionary _tool_patch_editor_settings(const Dictionary &p_args) const;
	Dictionary _tool_get_global_classes(const Dictionary &p_args) const;
	Dictionary _tool_disconnect_signal(const Dictionary &p_args) const;
	Dictionary _tool_validate_scene(const Dictionary &p_args) const;
	Dictionary _tool_get_gdscript_errors(const Dictionary &p_args) const;
	Dictionary _tool_get_scene_dependency_closure(const Dictionary &p_args) const;
	Dictionary _tool_focus_scene_tree_node(const Dictionary &p_args) const;
	Dictionary _tool_get_editor_3d_camera_transform(const Dictionary &p_args) const;
	Dictionary _tool_search_project_settings_keys(const Dictionary &p_args) const;
	Dictionary _tool_get_export_presets(const Dictionary &p_args) const;
	Dictionary _tool_resolve_resource_uid(const Dictionary &p_args) const;
	Dictionary _tool_get_translation_overview(const Dictionary &p_args) const;
	Dictionary _tool_manage_autoloads(const Dictionary &p_args) const;
	Dictionary _tool_rename_resource_references(const Dictionary &p_args) const;
	Dictionary _tool_get_binary_file_metadata(const Dictionary &p_args) const;
	Dictionary _tool_get_editor_inspector_subject(const Dictionary &p_args) const;
	Dictionary _tool_reimport_project_files(const Dictionary &p_args) const;
	Dictionary _tool_scan_project_filesystem(const Dictionary &p_args) const;
	Dictionary _tool_save_resource(const Dictionary &p_args) const;
	Dictionary _tool_replace_in_project_files(const Dictionary &p_args) const;
	// New tools
	Dictionary _tool_run_gdscript_expression(const Dictionary &p_args) const;
	Dictionary _tool_get_shader_code(const Dictionary &p_args) const;
	Dictionary _tool_update_shader_code(const Dictionary &p_args) const;
	// New tools v2
	Dictionary _tool_set_editor_3d_camera(const Dictionary &p_args) const;
	Dictionary _tool_get_world_environment(const Dictionary &p_args) const;
	Dictionary _tool_set_world_environment(const Dictionary &p_args) const;
	Dictionary _tool_play_animation(const Dictionary &p_args) const;
	Dictionary _tool_batch_set_node_property(const Dictionary &p_args) const;
	Dictionary _tool_set_node_collision_layers(const Dictionary &p_args) const;
	Dictionary _tool_query_physics(const Dictionary &p_args) const;
	Dictionary _tool_create_particle_emitter(const Dictionary &p_args) const;
	Dictionary _tool_create_ui_element(const Dictionary &p_args) const;

	// ── A. 3D Scene Construction ──────────────────────────────────────────────
	Dictionary _tool_create_light(const Dictionary &p_args) const;
	Dictionary _tool_create_camera_3d(const Dictionary &p_args) const;
	Dictionary _tool_add_2d_collision_shape(const Dictionary &p_args) const;
	Dictionary _tool_create_rigid_body_3d(const Dictionary &p_args) const;
	Dictionary _tool_create_static_body_3d(const Dictionary &p_args) const;
	Dictionary _tool_create_character_body_3d(const Dictionary &p_args) const;
	Dictionary _tool_create_area_3d(const Dictionary &p_args) const;
	Dictionary _tool_create_ray_cast_3d(const Dictionary &p_args) const;
	Dictionary _tool_create_shape_cast_3d(const Dictionary &p_args) const;
	Dictionary _tool_add_csg_primitive(const Dictionary &p_args) const;
	Dictionary _tool_create_path_3d(const Dictionary &p_args) const;
	Dictionary _tool_create_vehicle_body_3d(const Dictionary &p_args) const;

	// ── B. 2D Scene Construction ──────────────────────────────────────────────
	Dictionary _tool_create_sprite_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_animated_sprite_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_rigid_body_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_character_body_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_static_body_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_area_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_ray_cast_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_shape_cast_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_line_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_path_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_polygon_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_collision_polygon_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_light_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_camera_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_marker_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_tile_map(const Dictionary &p_args) const;
	Dictionary _tool_create_cpu_particles_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_gpu_particles_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_animatable_body_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_audio_stream_player_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_canvas_layer(const Dictionary &p_args) const;
	Dictionary _tool_create_parallax_background(const Dictionary &p_args) const;
	Dictionary _tool_create_parallax_layer(const Dictionary &p_args) const;
	Dictionary _tool_create_visible_on_screen_notifier_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_remote_transform_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_navigation_agent_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_navigation_region_2d(const Dictionary &p_args) const;
	Dictionary _tool_add_collision_shape_2d(const Dictionary &p_args) const;

	// ── C. Animation ──────────────────────────────────────────────────────────
	Dictionary _tool_create_animation(const Dictionary &p_args) const;
	Dictionary _tool_add_animation_track(const Dictionary &p_args) const;
	Dictionary _tool_remove_animation_track(const Dictionary &p_args) const;
	Dictionary _tool_set_animation_track_key(const Dictionary &p_args) const;
	Dictionary _tool_create_blend_tree(const Dictionary &p_args) const;
	Dictionary _tool_add_animation_transition(const Dictionary &p_args) const;
	Dictionary _tool_set_animation_blend_amount(const Dictionary &p_args) const;

	// ── D. TileMap / 2D Level Design ──────────────────────────────────────────
	Dictionary _tool_set_tilemap_cells(const Dictionary &p_args) const;
	Dictionary _tool_clear_tilemap_cells(const Dictionary &p_args) const;
	Dictionary _tool_get_tileset_sources(const Dictionary &p_args) const;
	Dictionary _tool_create_tileset(const Dictionary &p_args) const;
	Dictionary _tool_add_tileset_atlas_source(const Dictionary &p_args) const;
	Dictionary _tool_set_tile_collision_polygon(const Dictionary &p_args) const;
	Dictionary _tool_paint_terrain(const Dictionary &p_args) const;

	// ── E. Physics ────────────────────────────────────────────────────────────
	Dictionary _tool_set_collision_layer_mask(const Dictionary &p_args) const;
	Dictionary _tool_add_collision_exception(const Dictionary &p_args) const;
	Dictionary _tool_remove_collision_exception(const Dictionary &p_args) const;
	Dictionary _tool_get_collision_exceptions(const Dictionary &p_args) const;
	Dictionary _tool_set_physics_material(const Dictionary &p_args) const;
	Dictionary _tool_raycast_query(const Dictionary &p_args) const;
	Dictionary _tool_shape_cast_query(const Dictionary &p_args) const;

	// ── F. Scripting & Code Intelligence ──────────────────────────────────────
	Dictionary _tool_get_gdscript_symbols(const Dictionary &p_args) const;
	Dictionary _tool_get_gdscript_docs(const Dictionary &p_args) const;
	Dictionary _tool_set_input_action_bindings(const Dictionary &p_args) const;
	Dictionary _tool_remove_input_action(const Dictionary &p_args) const;
	Dictionary _tool_run_gdscript_test(const Dictionary &p_args) const;
	Dictionary _tool_get_class_reference(const Dictionary &p_args) const;
	Dictionary _tool_search_class_db(const Dictionary &p_args) const;
	Dictionary _tool_get_method_signature(const Dictionary &p_args) const;
	Dictionary _tool_get_enum_values(const Dictionary &p_args) const;
	Dictionary _tool_lint_gdscript(const Dictionary &p_args) const;

	// ── G. Shader Authoring ──────────────────────────────────────────────────
	Dictionary _tool_create_shader_material(const Dictionary &p_args) const;
	Dictionary _tool_set_shader_uniform(const Dictionary &p_args) const;
	Dictionary _tool_get_shader_uniforms(const Dictionary &p_args) const;

	// ── H. Audio ──────────────────────────────────────────────────────────────
	Dictionary _tool_create_audio_player(const Dictionary &p_args) const;
	Dictionary _tool_play_audio(const Dictionary &p_args) const;
	Dictionary _tool_stop_audio(const Dictionary &p_args) const;
	Dictionary _tool_get_audio_buses(const Dictionary &p_args) const;
	Dictionary _tool_add_audio_bus_effect(const Dictionary &p_args) const;

	// ── I. UI / HUD Construction ─────────────────────────────────────────────
	Dictionary _tool_create_control_node(const Dictionary &p_args) const;
	Dictionary _tool_set_control_layout(const Dictionary &p_args) const;
	Dictionary _tool_set_control_theme_override(const Dictionary &p_args) const;
	Dictionary _tool_create_container_layout(const Dictionary &p_args) const;
	Dictionary _tool_create_scroll_container(const Dictionary &p_args) const;
	Dictionary _tool_create_progress_bar(const Dictionary &p_args) const;
	Dictionary _tool_create_slider(const Dictionary &p_args) const;
	Dictionary _tool_create_item_list(const Dictionary &p_args) const;
	Dictionary _tool_create_option_button(const Dictionary &p_args) const;
	Dictionary _tool_create_tab_container(const Dictionary &p_args) const;
	Dictionary _tool_create_graph_node(const Dictionary &p_args) const;
	Dictionary _tool_create_tree_widget(const Dictionary &p_args) const;

	// ── J. Resources & Assets ─────────────────────────────────────────────────
	Dictionary _tool_create_gradient(const Dictionary &p_args) const;
	Dictionary _tool_create_curve(const Dictionary &p_args) const;
	Dictionary _tool_create_stylebox(const Dictionary &p_args) const;
	Dictionary _tool_create_theme(const Dictionary &p_args) const;
	Dictionary _tool_create_font(const Dictionary &p_args) const;
	Dictionary _tool_create_texture_2d(const Dictionary &p_args) const;
	Dictionary _tool_create_noise_texture(const Dictionary &p_args) const;
	Dictionary _tool_create_atlas_texture(const Dictionary &p_args) const;
	Dictionary _tool_import_asset(const Dictionary &p_args) const;
	Dictionary _tool_set_import_setting(const Dictionary &p_args) const;

	// ── K. Navigation & AI ────────────────────────────────────────────────────
	Dictionary _tool_bake_navigation_mesh(const Dictionary &p_args) const;
	Dictionary _tool_create_navigation_link(const Dictionary &p_args) const;
	Dictionary _tool_create_navigation_obstacle(const Dictionary &p_args) const;
	Dictionary _tool_get_navigation_path(const Dictionary &p_args) const;
	Dictionary _tool_set_navigation_agent_params(const Dictionary &p_args) const;

	// ── L. Multiplayer & Networking ───────────────────────────────────────────
	Dictionary _tool_create_multiplayer_spawner(const Dictionary &p_args) const;
	Dictionary _tool_create_multiplayer_synchronizer(const Dictionary &p_args) const;
	Dictionary _tool_get_network_state(const Dictionary &p_args) const;

	// ── M. Debugging & Runtime Inspection ─────────────────────────────────────
	Dictionary _tool_inspect_runtime_variable(const Dictionary &p_args) const;
	Dictionary _tool_set_breakpoint(const Dictionary &p_args) const;
	Dictionary _tool_debugger_continue(const Dictionary &p_args) const;
	Dictionary _tool_get_console_output(const Dictionary &p_args) const;
	Dictionary _tool_profile_frame(const Dictionary &p_args) const;
	Dictionary _tool_monitor_runtime_performance(const Dictionary &p_args) const;
	Dictionary _tool_inspect_runtime_node(const Dictionary &p_args) const;

	// ── N. Editor Workflow ────────────────────────────────────────────────────
	Dictionary _tool_manage_editor_plugins(const Dictionary &p_args) const;
	Dictionary _tool_run_scene_script(const Dictionary &p_args) const;
	Dictionary _tool_get_editor_version(const Dictionary &p_args) const;

	// ── O. Project Configuration ─────────────────────────────────────────────
	Dictionary _tool_manage_export_presets(const Dictionary &p_args) const;
	Dictionary _tool_export_project(const Dictionary &p_args) const;
	Dictionary _tool_add_custom_class(const Dictionary &p_args) const;
	Dictionary _tool_set_default_import_presets(const Dictionary &p_args) const;

	// ── P. Version Control ────────────────────────────────────────────────────
	Dictionary _tool_git_status(const Dictionary &p_args) const;
	Dictionary _tool_git_diff_file(const Dictionary &p_args) const;
	Dictionary _tool_git_log(const Dictionary &p_args) const;
	Dictionary _tool_git_branch(const Dictionary &p_args) const;

	// ── Q. Scene Refactoring ──────────────────────────────────────────────────
	Dictionary _tool_merge_scenes(const Dictionary &p_args) const;
	Dictionary _tool_extract_sub_scene(const Dictionary &p_args) const;
	Dictionary _tool_replace_node_with_scene(const Dictionary &p_args) const;
	Dictionary _tool_batch_reparent_nodes(const Dictionary &p_args) const;
	Dictionary _tool_set_node_meta(const Dictionary &p_args) const;

	// ── R. Environment & Rendering ────────────────────────────────────────────
	Dictionary _tool_create_sky(const Dictionary &p_args) const;
	Dictionary _tool_set_environment_fog(const Dictionary &p_args) const;
	Dictionary _tool_set_environment_tonemap(const Dictionary &p_args) const;
	Dictionary _tool_set_environment_ss_effects(const Dictionary &p_args) const;
	Dictionary _tool_create_fog_volume(const Dictionary &p_args) const;
	Dictionary _tool_create_reflection_probe(const Dictionary &p_args) const;
	Dictionary _tool_create_gi_probe(const Dictionary &p_args) const;

	// ── Tool helpers — reduce per-tool boilerplate ──────────────────────────
	static Dictionary _make_error(const String &p_message);
	static Dictionary _make_ok();
	static Dictionary _make_ok(const Dictionary &p_extra);

	// Typed argument accessors — return defaults on missing/wrong-type.
	static String _arg_string(const Dictionary &p_args, const String &p_key, const String &p_default = "");
	static int _arg_int(const Dictionary &p_args, const String &p_key, int p_default = 0);
	static double _arg_float(const Dictionary &p_args, const String &p_key, double p_default = 0.0);
	static bool _arg_bool(const Dictionary &p_args, const String &p_key, bool p_default = false);
	static Array _arg_array(const Dictionary &p_args, const String &p_key);
	static Dictionary _arg_dict(const Dictionary &p_args, const String &p_key);
	static Vector3 _arg_vector3(const Dictionary &p_args, const String &p_key, const Vector3 &p_default = Vector3());
	static Vector2 _arg_vector2(const Dictionary &p_args, const String &p_key, const Vector2 &p_default = Vector2());
	static Color _arg_color(const Dictionary &p_args, const String &p_key, const Color &p_default = Color());

	// Common resolution patterns — return error Dictionary on failure, empty on success.
	// Use: String err; if (!_resolve_scene(args, &root, err)) return _make_error(err);
	bool _resolve_scene(const Dictionary &p_args, Node **r_root, String &r_error) const;
	bool _resolve_scene_and_node(const Dictionary &p_args, const String &p_node_key, Node **r_scene_root, Node **r_node, String &r_error) const;
	bool _require_editor(EditorInterface **r_editor, String &r_error) const;

	void _encode_viewport_image_for_vision(const Ref<Image> &p_img, const Dictionary &p_args, Dictionary &r_result) const;
	void _grep_project_files_recursive(const String &p_dir, const String &p_query, bool p_case_sensitive, const Vector<String> &p_extensions, int p_max_bytes, int p_max_matches, int &r_match_count, Array &r_matches) const;
	void _collect_project_entries(const String &p_dir_path, int p_depth, int p_max_depth, const Vector<String> &p_include_extensions, Array &r_entries, int &r_entry_count) const;
	void _find_project_entries(const String &p_dir_path, const String &p_query, int p_depth, int p_max_depth, const Vector<String> &p_include_extensions, Array &r_entries, int &r_entry_count, int p_max_results) const;
	Dictionary _serialize_node(Node *p_node, int p_depth, int p_max_depth, const Vector<String> &p_include_properties, int &r_node_count) const;
	Dictionary _serialize_node_properties(Node *p_node, const Vector<String> &p_include_properties) const;
	bool _node_has_property(Node *p_node, const StringName &p_property) const;
	bool _get_node_property_info(Object *p_object, const StringName &p_property, PropertyInfo &r_info) const;
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
	String _build_compact_system_prompt() const;
	String _escape_bbcode(const String &p_text) const;
	String _extract_message_content(const Dictionary &p_response_json) const;
	Dictionary _extract_response_envelope(const String &p_content) const;
	bool _try_merge_adjacent_tool_call_json(const String &p_cleaned, Dictionary &r_envelope) const;
	Dictionary _normalize_envelope(const Dictionary &p_envelope) const;
	Dictionary _normalize_batch_tool_arguments(const Dictionary &p_args) const;
	Dictionary _parse_one_batch_call(const Variant &p_call_var, int p_index, const Dictionary &p_shared_arguments) const;
	
	// ── Structured parser integration ─────────────────────────────────────────
	Dictionary _extract_structured_response(const String &p_content) const;
	YeetAIResponseType _parse_response_type(const String &p_content) const;
	
	// ── Retry methods ─────────────────────────────────────────────────────────
	void _handle_tool_failure(const String &tool_name, const Dictionary &args, const ToolExecutionResult &result);
	bool _should_retry(const String &tool_name, const String &error);
	Dictionary _prepare_retry_context(const String &tool_name, const Dictionary &original_args);
	
	// ── Cache methods ─────────────────────────────────────────────────────────
	String _generate_cache_key(const String &tool_name, const Dictionary &args) const;
	
	// ── Batching methods ──────────────────────────────────────────────────────
	bool _can_batch_with_previous(const String &current_tool, const String &prev_tool);
	Dictionary _prepare_batch_request(const Vector<BatchOpportunity> &ops);
	Vector<String> _variant_array_to_string_vector(const Array &p_values) const;
	bool _is_allowed_text_file(const String &p_path) const;
	
	// ── Batch execution refinement ────────────────────────────────────────────
	struct BatchExecutionState {
		Vector<BatchOpportunity> operations;
		bool is_executing = false;
		int completed_count = 0;
		int error_count = 0;
		bool stop_on_error = false;
	};
	BatchExecutionState _current_batch_execution;
	
	void _execute_batch_as_single_call(const Vector<BatchOpportunity> &ops);
	bool _can_optimize_to_batch(const String &tool_name, const Dictionary &args) const;
	
	// ── Tool Call Timeout Handling ────────────────────────────────────────────
	struct ToolCallTimeout {
		String tool_name;
		int64_t start_time;
		int64_t timeout_ms;
		bool is_active;
	};
	ToolCallTimeout _current_tool_timeout;
	
	int64_t _get_tool_call_timeout(const String &tool_name) const;
	void _start_tool_call_timeout(const String &tool_name);
	bool _check_tool_call_timeout() const;
	void _cancel_tool_call_timeout();
	
	// ── Tool Argument Helpers ────────────────────────────────────────────────
	bool _validate_tool_call(const String &tool_name, const Dictionary &args, String &r_error) const;
	Dictionary _normalize_tool_arguments(const String &tool_name, const Dictionary &args) const;
	
	// ── Rate Limiting ────────────────────────────────────────────────────────
	struct RateLimitEntry {
		String tool_name;
		int64_t last_call_time;
		int call_count;
	};
	HashMap<String, RateLimitEntry> _rate_limits;
	int _get_rate_limit_for_tool(const String &tool_name) const;
	bool _check_rate_limit(const String &tool_name) const;
	void _record_tool_call(const String &tool_name);
	
	// ── Enhanced Error Recovery ──────────────────────────────────────────────
	enum class ErrorSeverity {
		RECOVERABLE,
		PARTIAL_FAILURE,
		CRITICAL
	};
	ErrorSeverity _classify_error(const String &error_msg) const;
	Dictionary _generate_recovery_context(const String &tool_name, const Dictionary &args, const String &error) const;
	
	void _notification(int p_what);

protected:
	static void _bind_methods();

public:
	YeetAIDock();
	~YeetAIDock();

	// Used by yeet_ai_tools_*.cpp translation units.
	static BaseMaterial3D::Transparency parse_transparency_mode(const String &p_value, bool &r_ok);
};

VARIANT_ENUM_CAST(YeetAIDock::ErrorSeverity)
