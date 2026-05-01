/**************************************************************************/
/*  yeet_ai_tools.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/
/*                                                                        */
/*  Tool dispatch: _execute_tool replaces the old if/else chain with a    */
/*  table lookup over tool name → handler pairs.                          */
/*                                                                        */
/*  New tool implementations should be added to yeet_ai_dock.cpp and     */
/*  registered here (and in the system prompt) to complete the loop.      */
/**************************************************************************/

#include "yeet_ai_dock.h"
#include "yeet_ai_tool_schema.h"

#include "core/io/json.h"
#include "core/string/translation.h"

// ═══════════════════════════════════════════════════════════════════════════
// _execute_tool — dispatch table
// ═══════════════════════════════════════════════════════════════════════════

YeetAIDock::ToolExecutionResult YeetAIDock::_execute_tool(const String &p_tool_name, const Dictionary &p_args) {
	using ToolHandler = Dictionary (YeetAIDock::*)(const Dictionary &) const;

	struct ToolEntry {
		const char *name;
		ToolHandler handler;
	};

	// Static lookup table. No file-scope member pointer access.
	static const ToolEntry table[] = {
		// ═══════════════════════════════════════════════════════════════════════
		// Tool name → handler pairs.
		// ═══════════════════════════════════════════════════════════════════════
		{ "add_2d_collision_shape",              &YeetAIDock::_tool_add_2d_collision_shape },
		{ "add_animation_track",                 &YeetAIDock::_tool_add_animation_track },
		{ "add_animation_transition",             &YeetAIDock::_tool_add_animation_transition },
		{ "add_audio_bus_effect",                &YeetAIDock::_tool_add_audio_bus_effect },
		{ "add_collision_exception",             &YeetAIDock::_tool_add_collision_exception },
		{ "add_collision_shape",                 &YeetAIDock::_tool_add_collision_shape },
		{ "add_collision_shape_2d",              &YeetAIDock::_tool_add_collision_shape_2d },
		{ "add_csg_primitive",                   &YeetAIDock::_tool_add_csg_primitive },
		{ "add_custom_class",                    &YeetAIDock::_tool_add_custom_class },
		{ "add_node",                            &YeetAIDock::_tool_add_node },
		{ "add_primitive_mesh",                  &YeetAIDock::_tool_add_primitive_mesh },
		{ "add_tileset_atlas_source",            &YeetAIDock::_tool_add_tileset_atlas_source },
		{ "assign_resource_to_property",         &YeetAIDock::_tool_assign_resource_to_property },
		{ "attach_script",                       &YeetAIDock::_tool_attach_script },
		{ "audit_game_physics",                  &YeetAIDock::_tool_audit_game_physics },
		{ "bake_navigation_mesh",                &YeetAIDock::_tool_bake_navigation_mesh },
		{ "batch_reparent_nodes",                &YeetAIDock::_tool_batch_reparent_nodes },
		{ "batch_set_node_property",             &YeetAIDock::_tool_batch_set_node_property },
		{ "batch_tool_calls",                    &YeetAIDock::_tool_batch_tool_calls },
		{ "capture_dual_view",                   &YeetAIDock::_tool_capture_dual_view },
		{ "capture_editor_viewport",             &YeetAIDock::_tool_capture_editor_viewport },
		{ "capture_game_viewport",               &YeetAIDock::_tool_capture_game_viewport },
		{ "capture_subviewport",                 &YeetAIDock::_tool_capture_subviewport },
		{ "capture_texture_resource",            &YeetAIDock::_tool_capture_texture_resource },
		{ "clear_tilemap_cells",                 &YeetAIDock::_tool_clear_tilemap_cells },
		{ "connect_signal",                      &YeetAIDock::_tool_connect_signal },
		{ "connect_ui_signal",                   &YeetAIDock::_tool_connect_ui_signal },
		{ "copy_project_file",                   &YeetAIDock::_tool_copy_project_file },
		{ "create_animated_sprite_2d",            &YeetAIDock::_tool_create_animated_sprite_2d },
		{ "create_animatable_body_2d",           &YeetAIDock::_tool_create_animatable_body_2d },
		{ "create_animation",                    &YeetAIDock::_tool_create_animation },
		{ "create_accept_dialog",                &YeetAIDock::_tool_create_accept_dialog },
		{ "create_area_2d",                      &YeetAIDock::_tool_create_area_2d },
		{ "create_area_3d",                      &YeetAIDock::_tool_create_area_3d },
		{ "create_aspect_ratio_container",       &YeetAIDock::_tool_create_aspect_ratio_container },
		{ "create_atlas_texture",                &YeetAIDock::_tool_create_atlas_texture },
		{ "create_audio_player",                 &YeetAIDock::_tool_create_audio_player },
		{ "create_audio_stream_player_2d",       &YeetAIDock::_tool_create_audio_stream_player_2d },
		{ "create_blend_tree",                   &YeetAIDock::_tool_create_blend_tree },
		{ "create_button",                       &YeetAIDock::_tool_create_button },
		{ "create_camera_2d",                    &YeetAIDock::_tool_create_camera_2d },
		{ "create_camera_3d",                    &YeetAIDock::_tool_create_camera_3d },
		{ "create_canvas_layer",                 &YeetAIDock::_tool_create_canvas_layer },
		{ "create_character_body_2d",            &YeetAIDock::_tool_create_character_body_2d },
		{ "create_character_body_3d",            &YeetAIDock::_tool_create_character_body_3d },
		{ "create_check_box",                    &YeetAIDock::_tool_create_check_box },
		{ "create_color_rect",                   &YeetAIDock::_tool_create_color_rect },
		{ "create_collision_polygon_2d",         &YeetAIDock::_tool_create_collision_polygon_2d },
		{ "create_confirmation_dialog",          &YeetAIDock::_tool_create_confirmation_dialog },
		{ "create_container_layout",             &YeetAIDock::_tool_create_container_layout },
		{ "create_control_node",                 &YeetAIDock::_tool_create_control_node },
		{ "create_cpu_particles_2d",             &YeetAIDock::_tool_create_cpu_particles_2d },
		{ "create_curve",                        &YeetAIDock::_tool_create_curve },
		{ "create_fog_volume",                   &YeetAIDock::_tool_create_fog_volume },
		{ "create_font",                         &YeetAIDock::_tool_create_font },
		{ "create_game_actor_2d",                &YeetAIDock::_tool_create_game_actor_2d },
		{ "create_game_actor_3d",                &YeetAIDock::_tool_create_game_actor_3d },
		{ "create_gdscript_file",                &YeetAIDock::_tool_create_gdscript_file },
		{ "create_gi_probe",                     &YeetAIDock::_tool_create_gi_probe },
		{ "create_gpu_particles_2d",             &YeetAIDock::_tool_create_gpu_particles_2d },
		{ "create_gradient",                     &YeetAIDock::_tool_create_gradient },
		{ "create_graph_node",                   &YeetAIDock::_tool_create_graph_node },
		{ "create_h_separator",                  &YeetAIDock::_tool_create_h_separator },
		{ "create_input_action",                 &YeetAIDock::_tool_create_input_action },
		{ "create_item_list",                    &YeetAIDock::_tool_create_item_list },
		{ "create_label",                        &YeetAIDock::_tool_create_label },
		{ "create_line_edit",                    &YeetAIDock::_tool_create_line_edit },
		{ "create_light",                        &YeetAIDock::_tool_create_light },
		{ "create_light_2d",                     &YeetAIDock::_tool_create_light_2d },
		{ "create_line_2d",                      &YeetAIDock::_tool_create_line_2d },
		{ "create_marker_2d",                    &YeetAIDock::_tool_create_marker_2d },
		{ "create_margin_container",             &YeetAIDock::_tool_create_margin_container },
		{ "create_multiplayer_spawner",          &YeetAIDock::_tool_create_multiplayer_spawner },
		{ "create_multiplayer_synchronizer",      &YeetAIDock::_tool_create_multiplayer_synchronizer },
		{ "create_navigation_agent_2d",          &YeetAIDock::_tool_create_navigation_agent_2d },
		{ "create_navigation_link",              &YeetAIDock::_tool_create_navigation_link },
		{ "create_navigation_obstacle",          &YeetAIDock::_tool_create_navigation_obstacle },
		{ "create_navigation_region_2d",         &YeetAIDock::_tool_create_navigation_region_2d },
		{ "create_nine_patch_rect",              &YeetAIDock::_tool_create_nine_patch_rect },
		{ "create_noise_texture",                &YeetAIDock::_tool_create_noise_texture },
		{ "create_option_button",                &YeetAIDock::_tool_create_option_button },
		{ "create_panel_container",              &YeetAIDock::_tool_create_panel_container },
		{ "create_parallax_background",          &YeetAIDock::_tool_create_parallax_background },
		{ "create_parallax_layer",               &YeetAIDock::_tool_create_parallax_layer },
		{ "create_particle_emitter",             &YeetAIDock::_tool_create_particle_emitter },
		{ "create_path_follow_2d",               &YeetAIDock::_tool_create_path_follow_2d },
		{ "create_path_2d",                      &YeetAIDock::_tool_create_path_2d },
		{ "create_path_3d",                      &YeetAIDock::_tool_create_path_3d },
		{ "create_polygon_2d",                   &YeetAIDock::_tool_create_polygon_2d },
		{ "create_progress_bar",                 &YeetAIDock::_tool_create_progress_bar },
		{ "create_project_folder",               &YeetAIDock::_tool_create_project_folder },
		{ "create_ray_cast_2d",                  &YeetAIDock::_tool_create_ray_cast_2d },
		{ "create_ray_cast_3d",                  &YeetAIDock::_tool_create_ray_cast_3d },
		{ "create_reference_rect",               &YeetAIDock::_tool_create_reference_rect },
		{ "create_rich_text_label",              &YeetAIDock::_tool_create_rich_text_label },
		{ "create_remote_transform_2d",          &YeetAIDock::_tool_create_remote_transform_2d },
		{ "create_reflection_probe",             &YeetAIDock::_tool_create_reflection_probe },
		{ "create_rigid_body_2d",                &YeetAIDock::_tool_create_rigid_body_2d },
		{ "create_rigid_body_3d",                &YeetAIDock::_tool_create_rigid_body_3d },
		{ "create_scene_file",                   &YeetAIDock::_tool_create_scene_file },
		{ "create_scroll_container",             &YeetAIDock::_tool_create_scroll_container },
		{ "create_shader_material",              &YeetAIDock::_tool_create_shader_material },
		{ "create_shape_cast_2d",                &YeetAIDock::_tool_create_shape_cast_2d },
		{ "create_shape_cast_3d",                &YeetAIDock::_tool_create_shape_cast_3d },
		{ "create_sky",                          &YeetAIDock::_tool_create_sky },
		{ "create_slider",                       &YeetAIDock::_tool_create_slider },
		{ "create_spin_box",                     &YeetAIDock::_tool_create_spin_box },
		{ "create_sprite_2d",                    &YeetAIDock::_tool_create_sprite_2d },
		{ "create_standard_material",            &YeetAIDock::_tool_create_standard_material },
		{ "create_static_body_2d",               &YeetAIDock::_tool_create_static_body_2d },
		{ "create_static_body_3d",                &YeetAIDock::_tool_create_static_body_3d },
		{ "create_subviewport_container",        &YeetAIDock::_tool_create_subviewport_container },
		{ "create_stylebox",                     &YeetAIDock::_tool_create_stylebox },
		{ "create_tab_container",                &YeetAIDock::_tool_create_tab_container },
		{ "create_texture_2d",                   &YeetAIDock::_tool_create_texture_2d },
		{ "create_texture_progress_bar",         &YeetAIDock::_tool_create_texture_progress_bar },
		{ "create_text_edit",                    &YeetAIDock::_tool_create_text_edit },
		{ "create_theme",                        &YeetAIDock::_tool_create_theme },
		{ "create_tile_map",                     &YeetAIDock::_tool_create_tile_map },
		{ "create_tileset",                      &YeetAIDock::_tool_create_tileset },
		{ "create_timer",                        &YeetAIDock::_tool_create_timer },
		{ "create_tween",                        &YeetAIDock::_tool_create_tween },
		{ "create_tree_widget",                  &YeetAIDock::_tool_create_tree_widget },
		{ "create_ui_element",                   &YeetAIDock::_tool_create_ui_element },
		{ "create_vehicle_body_3d",              &YeetAIDock::_tool_create_vehicle_body_3d },
		{ "create_v_separator",                  &YeetAIDock::_tool_create_v_separator },
		{ "create_visible_on_screen_notifier_2d", &YeetAIDock::_tool_create_visible_on_screen_notifier_2d },
		{ "debugger_continue",                   &YeetAIDock::_tool_debugger_continue },
		{ "delete_project_file",                 &YeetAIDock::_tool_delete_project_file },
		{ "disconnect_signal",                   &YeetAIDock::_tool_disconnect_signal },
		{ "duplicate_node",                      &YeetAIDock::_tool_duplicate_node },
		{ "edit_script",                         &YeetAIDock::_tool_edit_script },
		{ "editor_undo",                         &YeetAIDock::_tool_editor_undo },
		{ "export_project",                      &YeetAIDock::_tool_export_project },
		{ "extract_sub_scene",                    &YeetAIDock::_tool_extract_sub_scene },
		{ "file_exists",                         &YeetAIDock::_tool_file_exists },
		{ "find_project_files",                  &YeetAIDock::_tool_find_project_files },
		{ "focus_scene_tree_node",               &YeetAIDock::_tool_focus_scene_tree_node },
		{ "get_animation_player_state",          &YeetAIDock::_tool_get_animation_player_state },
		{ "get_audio_buses",                     &YeetAIDock::_tool_get_audio_buses },
		{ "get_autoloads",                       &YeetAIDock::_tool_get_autoloads },
		{ "get_binary_file_metadata",            &YeetAIDock::_tool_get_binary_file_metadata },
		{ "get_class_reference",                 &YeetAIDock::_tool_get_class_reference },
		{ "get_collision_exceptions",             &YeetAIDock::_tool_get_collision_exceptions },
		{ "get_console_output",                  &YeetAIDock::_tool_get_console_output },
		{ "get_current_scene",                   &YeetAIDock::_tool_get_current_scene },
		{ "get_debug_snapshot",                  &YeetAIDock::_tool_get_debug_snapshot },
		{ "get_editor_3d_camera_transform",      &YeetAIDock::_tool_get_editor_3d_camera_transform },
		{ "get_editor_inspector_subject",        &YeetAIDock::_tool_get_editor_inspector_subject },
		{ "get_editor_log",                      &YeetAIDock::_tool_get_editor_log },
		{ "get_editor_settings",                 &YeetAIDock::_tool_get_editor_settings },
		{ "get_editor_version",                  &YeetAIDock::_tool_get_editor_version },
		{ "get_enum_values",                     &YeetAIDock::_tool_get_enum_values },
		{ "get_export_presets",                  &YeetAIDock::_tool_get_export_presets },
		{ "get_gdscript_docs",                   &YeetAIDock::_tool_get_gdscript_docs },
		{ "get_gdscript_errors",                 &YeetAIDock::_tool_get_gdscript_errors },
		{ "get_gdscript_symbols",                &YeetAIDock::_tool_get_gdscript_symbols },
		{ "get_global_classes",                  &YeetAIDock::_tool_get_global_classes },
		{ "get_input_actions",                   &YeetAIDock::_tool_get_input_actions },
		{ "get_method_signature",                 &YeetAIDock::_tool_get_method_signature },
		{ "get_navigation_path",                  &YeetAIDock::_tool_get_navigation_path },
		{ "get_navigation_region_info",          &YeetAIDock::_tool_get_navigation_region_info },
		{ "get_network_state",                   &YeetAIDock::_tool_get_network_state },
		{ "get_node_api",                        &YeetAIDock::_tool_get_node_api },
		{ "get_node_collision_layers",           &YeetAIDock::_tool_get_node_collision_layers },
		{ "get_node_details",                    &YeetAIDock::_tool_get_node_details },
		{ "get_node_groups",                     &YeetAIDock::_tool_get_node_groups },
		{ "get_open_scenes",                     &YeetAIDock::_tool_get_open_scenes },
		{ "get_project_settings",                &YeetAIDock::_tool_get_project_settings },
		{ "get_project_tree",                    &YeetAIDock::_tool_get_project_tree },
		{ "get_remote_scene_tree",               &YeetAIDock::_tool_get_remote_scene_tree },
		{ "get_resource_dependencies",           &YeetAIDock::_tool_get_resource_dependencies },
		{ "get_runtime_debugger_state",          &YeetAIDock::_tool_get_runtime_debugger_state },
		{ "get_scene_dependency_closure",        &YeetAIDock::_tool_get_scene_dependency_closure },
		{ "get_scene_tree",                      &YeetAIDock::_tool_get_scene_tree },
		{ "get_selected_nodes",                  &YeetAIDock::_tool_get_selected_nodes },
		{ "get_shader_code",                     &YeetAIDock::_tool_get_shader_code },
		{ "get_shader_uniforms",                  &YeetAIDock::_tool_get_shader_uniforms },
		{ "get_signal_connections",              &YeetAIDock::_tool_get_signal_connections },
		{ "get_tilemap_info",                    &YeetAIDock::_tool_get_tilemap_info },
		{ "get_tileset_sources",                  &YeetAIDock::_tool_get_tileset_sources },
		{ "get_translation_overview",            &YeetAIDock::_tool_get_translation_overview },
		{ "get_unsaved_scenes",                  &YeetAIDock::_tool_get_unsaved_scenes },
		{ "get_world_environment",               &YeetAIDock::_tool_get_world_environment },
		{ "git_branch",                          &YeetAIDock::_tool_git_branch },
		{ "git_diff_file",                        &YeetAIDock::_tool_git_diff_file },
		{ "git_log",                             &YeetAIDock::_tool_git_log },
		{ "git_status",                          &YeetAIDock::_tool_git_status },
		{ "grep_project_files",                  &YeetAIDock::_tool_grep_project_files },
		{ "import_asset",                        &YeetAIDock::_tool_import_asset },
		{ "inspect_runtime_node",                 &YeetAIDock::_tool_inspect_runtime_node },
		{ "inspect_runtime_variable",             &YeetAIDock::_tool_inspect_runtime_variable },
		{ "instantiate_scene",                   &YeetAIDock::_tool_instantiate_scene },
		{ "lint_gdscript",                       &YeetAIDock::_tool_lint_gdscript },
		{ "list_directory",                      &YeetAIDock::_tool_list_directory },
		{ "manage_autoloads",                    &YeetAIDock::_tool_manage_autoloads },
		{ "manage_editor_plugins",               &YeetAIDock::_tool_manage_editor_plugins },
		{ "manage_export_presets",                &YeetAIDock::_tool_manage_export_presets },
		{ "merge_scenes",                        &YeetAIDock::_tool_merge_scenes },
		{ "monitor_runtime_performance",         &YeetAIDock::_tool_monitor_runtime_performance },
		{ "move_child",                          &YeetAIDock::_tool_move_child },
		{ "move_project_file",                   &YeetAIDock::_tool_move_project_file },
		{ "open_scene",                          &YeetAIDock::_tool_open_scene },
		{ "paint_terrain",                       &YeetAIDock::_tool_paint_terrain },
		{ "patch_editor_settings",               &YeetAIDock::_tool_patch_editor_settings },
		{ "patch_project_settings",              &YeetAIDock::_tool_patch_project_settings },
		{ "play_animation",                      &YeetAIDock::_tool_play_animation },
		{ "play_audio",                          &YeetAIDock::_tool_play_audio },
		{ "play_current_scene",                  &YeetAIDock::_tool_play_current_scene },
		{ "play_main_scene",                     &YeetAIDock::_tool_play_main_scene },
		{ "profile_frame",                       &YeetAIDock::_tool_profile_frame },
		{ "query_physics",                       &YeetAIDock::_tool_query_physics },
		{ "query_raycast_2d",                    &YeetAIDock::_tool_query_raycast_2d },
		{ "raycast_query",                       &YeetAIDock::_tool_raycast_query },
		{ "read_project_file",                   &YeetAIDock::_tool_read_project_file },
		{ "reimport_project_files",              &YeetAIDock::_tool_reimport_project_files },
		{ "reload_scene",                        &YeetAIDock::_tool_reload_scene },
		{ "remove_animation_track",              &YeetAIDock::_tool_remove_animation_track },
		{ "remove_collision_exception",          &YeetAIDock::_tool_remove_collision_exception },
		{ "remove_input_action",                  &YeetAIDock::_tool_remove_input_action },
		{ "remove_node",                         &YeetAIDock::_tool_remove_node },
		{ "rename_node",                         &YeetAIDock::_tool_rename_node },
		{ "rename_resource_references",          &YeetAIDock::_tool_rename_resource_references },
		{ "reparent_node",                       &YeetAIDock::_tool_reparent_node },
		{ "repair_game_physics",                 &YeetAIDock::_tool_repair_game_physics },
		{ "replace_in_project_files",            &YeetAIDock::_tool_replace_in_project_files },
		{ "replace_node_with_scene",              &YeetAIDock::_tool_replace_node_with_scene },
		{ "resolve_resource_uid",                &YeetAIDock::_tool_resolve_resource_uid },
		{ "run_gdscript_expression",             &YeetAIDock::_tool_run_gdscript_expression },
		{ "run_gdscript_test",                   &YeetAIDock::_tool_run_gdscript_test },
		{ "run_scene_script",                    &YeetAIDock::_tool_run_scene_script },
		{ "save_all_scenes",                     &YeetAIDock::_tool_save_all_scenes },
		{ "save_current_scene",                  &YeetAIDock::_tool_save_current_scene },
		{ "save_resource",                       &YeetAIDock::_tool_save_resource },
		{ "scan_project_filesystem",             &YeetAIDock::_tool_scan_project_filesystem },
		{ "search_class_db",                     &YeetAIDock::_tool_search_class_db },
		{ "search_project_settings_keys",        &YeetAIDock::_tool_search_project_settings_keys },
		{ "select_file",                         &YeetAIDock::_tool_select_file },
		{ "set_animation_blend_amount",           &YeetAIDock::_tool_set_animation_blend_amount },
		{ "set_animation_track_key",              &YeetAIDock::_tool_set_animation_track_key },
		{ "set_breakpoint",                      &YeetAIDock::_tool_set_breakpoint },
		{ "set_collision_layer_mask",            &YeetAIDock::_tool_set_collision_layer_mask },
		{ "set_control_layout",                  &YeetAIDock::_tool_set_control_layout },
		{ "set_control_theme_override",          &YeetAIDock::_tool_set_control_theme_override },
		{ "set_default_import_presets",          &YeetAIDock::_tool_set_default_import_presets },
		{ "set_editor_3d_camera",                &YeetAIDock::_tool_set_editor_3d_camera },
		{ "set_editor_main_screen",              &YeetAIDock::_tool_set_editor_main_screen },
		{ "set_environment_fog",                  &YeetAIDock::_tool_set_environment_fog },
		{ "set_environment_ss_effects",           &YeetAIDock::_tool_set_environment_ss_effects },
		{ "set_environment_tonemap",              &YeetAIDock::_tool_set_environment_tonemap },
		{ "set_import_setting",                   &YeetAIDock::_tool_set_import_setting },
		{ "set_input_action_bindings",            &YeetAIDock::_tool_set_input_action_bindings },
		{ "set_main_scene",                      &YeetAIDock::_tool_set_main_scene },
		{ "set_navigation_agent_params",          &YeetAIDock::_tool_set_navigation_agent_params },
		{ "set_node_collision_layers",           &YeetAIDock::_tool_set_node_collision_layers },
		{ "set_node_meta",                       &YeetAIDock::_tool_set_node_meta },
		{ "set_node_property",                   &YeetAIDock::_tool_set_node_property },
		{ "set_physics_material",                 &YeetAIDock::_tool_set_physics_material },
		{ "set_shader_uniform",                   &YeetAIDock::_tool_set_shader_uniform },
		{ "set_tile_collision_polygon",           &YeetAIDock::_tool_set_tile_collision_polygon },
		{ "set_tilemap_cells",                    &YeetAIDock::_tool_set_tilemap_cells },
		{ "set_world_environment",               &YeetAIDock::_tool_set_world_environment },
		{ "shape_cast_query",                     &YeetAIDock::_tool_shape_cast_query },
		{ "stop_audio",                          &YeetAIDock::_tool_stop_audio },
		{ "stop_playing_scene",                  &YeetAIDock::_tool_stop_playing_scene },
		{ "update_gdscript_file",                &YeetAIDock::_tool_update_gdscript_file },
		{ "update_shader_code",                  &YeetAIDock::_tool_update_shader_code },
		{ "validate_scene",                      &YeetAIDock::_tool_validate_scene },
		{ "write_project_file",                  &YeetAIDock::_tool_write_project_file },
	};
	static const int table_size = sizeof(table) / sizeof(table[0]);

	const CharString tool_name_utf8 = p_tool_name.utf8();
	const ToolEntry *found = nullptr;
	for (int i = 0; i < table_size; i++) {
		if (strcmp(table[i].name, tool_name_utf8.get_data()) == 0) {
			found = &table[i];
			break;
		}
	}

	ToolExecutionResult result;
	if (found == nullptr) {
		result.payload["error"] = "Unknown tool";
		result.display_text = vformat("Unknown tool: %s", p_tool_name);
		// Record failed tool execution metric
		int64_t start_time = Time::get_singleton()->get_ticks_msec();
		((YeetAIDock*)this)->_record_tool_execution(p_tool_name, Time::get_singleton()->get_ticks_msec() - start_time, false, false);
		return result;
	}

// ── Enhanced Toolcalling: Validation & Normalization ─────────────────────────
 	// Validate arguments before schema pre-flight for early error detection.
 	// Normalize field names (e.g., "path" → "node_path") for consistency.
 	Dictionary effective_args = p_args;
 	String validation_error;
 	if (!_validate_tool_call(p_tool_name, effective_args, validation_error)) {
 		result.ok = false;
 		result.payload["error"] = validation_error;
 		result.payload["tool"] = p_tool_name;
 		result.payload["validation_failed"] = true;
 		result.display_text = vformat("Validation failed: %s", validation_error);
 		int64_t start_time = Time::get_singleton()->get_ticks_msec();
 		((YeetAIDock*)this)->_record_tool_execution(p_tool_name, Time::get_singleton()->get_ticks_msec() - start_time, false, false);
 		return result;
 	}
 	effective_args = _normalize_tool_arguments(p_tool_name, effective_args);
	const ToolSchema *schema = YeetAIToolSchemaRegistry::get_schema(p_tool_name);
	if (schema != nullptr) {
		// Auto-resolve any node-path arguments flagged in the schema.
		// Pulling the scene root once and reusing it keeps this O(args), not O(args*tree).
		Node *resolve_root = nullptr;
		bool resolve_root_attempted = false;
		for (int i = 0; i < schema->arguments.size(); i++) {
			const ToolArgSchema &arg_schema = schema->arguments[i];
			if (!arg_schema.auto_resolve_node_path || !effective_args.has(arg_schema.name)) {
				continue;
			}
			const Variant raw = effective_args[arg_schema.name];
			if (raw.get_type() != Variant::STRING) {
				continue;
			}
			const String raw_path = String(raw).strip_edges();
			if (raw_path.is_empty() || YeetAIToolSchemaRegistry::is_valid_node_path(raw_path)) {
				continue;
			}
			if (!resolve_root_attempted) {
				resolve_root_attempted = true;
				String _err;
				resolve_root = _resolve_scene_root(effective_args.get("scene_path", ""), _err);
			}
			if (resolve_root == nullptr) {
				continue;
			}
			String resolve_err;
			const String resolved = YeetAIToolSchemaRegistry::auto_resolve_node_path(raw_path, resolve_root, resolve_err);
			if (!resolved.is_empty()) {
				effective_args[arg_schema.name] = resolved;
			}
		}

		// Validate. On failure, return a structured error WITHOUT dispatching —
		// this is what saves the model from spamming garbage tool calls.
		Array errors;
		if (!YeetAIToolSchemaRegistry::validate_arguments(*schema, effective_args, errors)) {
			result.ok = false;
			result.payload["error"] = "Invalid tool arguments";
			result.payload["tool"] = p_tool_name;
			result.payload["validation_errors"] = errors;
			result.display_text = JSON::stringify(result.payload, "\t", false, true);
			// Record failed tool execution metric
			((YeetAIDock*)this)->_record_tool_execution(p_tool_name, 0, false, false);
			return result;
		}
	}

	result.ok = true;

	// ── Cache check for read-only tools ──────────────────────────────────────
	// Read-only tools (get_*, validate_*) can be cached to avoid redundant calls
	bool is_read_only = (schema == nullptr || !schema->is_write_operation) && (p_tool_name.begins_with("get_") || p_tool_name.begins_with("validate_"));
	if (is_read_only) {
		String cache_key = _generate_cache_key(p_tool_name, effective_args);
		Dictionary cached = _get_cached_result(cache_key);
		if (!cached.is_empty()) {
			result.payload = cached;
			result.display_text = JSON::stringify(result.payload, "\t", false, true);
			// Record cache hit metric
			((YeetAIDock*)this)->_record_tool_execution(p_tool_name, 0, true, false);
			return result;
		}
	}

// ── Timeout & Rate Limiting ──────────────────────────────────────────────
 	// Start timeout tracking to detect hanging tool calls.
 	_start_tool_call_timeout(p_tool_name);
 	// Check rate limit before execution.
 	if (!_check_rate_limit(p_tool_name)) {
 		_cancel_tool_call_timeout();
 		result.ok = false;
 		result.payload["error"] = "Rate limit exceeded for tool";
 		result.payload["tool"] = p_tool_name;
 		result.payload["rate_limited"] = true;
 		result.display_text = "Rate limit exceeded. Please wait before retrying.";
 		int64_t start_time = Time::get_singleton()->get_ticks_msec();
 		((YeetAIDock*)this)->_record_tool_execution(p_tool_name, Time::get_singleton()->get_ticks_msec() - start_time, false, false);
 		return result;
 	}
 	// Execute tool with timing.
 	int64_t tool_start_time = Time::get_singleton()->get_ticks_msec();
 	Dictionary payload = (this->*found->handler)(effective_args);
 	int64_t tool_duration = Time::get_singleton()->get_ticks_msec() - tool_start_time;
	// Check for timeout after execution.
	if (_check_tool_call_timeout()) {
		result.ok = false;
		result.payload["error"] = "Tool call timed out";
		result.payload["tool"] = p_tool_name;
		result.payload["timeout"] = true;
		result.display_text = "Tool call timed out after " + String::num_int64(tool_duration) + "ms";
		result.duration_ms = tool_duration;
		_cancel_tool_call_timeout();
		_record_tool_call(p_tool_name);
		((YeetAIDock*)this)->_record_tool_execution(p_tool_name, tool_duration, false, false);
		return result;
	}
	// Cancel timeout and record rate limit usage.
	_cancel_tool_call_timeout();
	_record_tool_call(p_tool_name);

	// ── Cache write for read-only tools ──────────────────────────────────────
	if (is_read_only && (p_tool_name.begins_with("get_") || p_tool_name.begins_with("validate_"))) {
		String cache_key = _generate_cache_key(p_tool_name, effective_args);
		_cache_result(cache_key, payload);
	}

	// ── Handle failures with retry ───────────────────────────────────────────
	if (!payload.get("ok", true) && payload.has("error")) {
		String error_msg = String(payload.get("error", "Unknown error"));
		result.ok = false;
		result.payload = payload;
		result.display_text = JSON::stringify(payload, "\t", false, true);
		result.duration_ms = tool_duration;

		// Record tool execution metric
		((YeetAIDock*)this)->_record_tool_execution(p_tool_name, tool_duration, false, false);

		// Handle retry
		_handle_tool_failure(p_tool_name, effective_args, result);

		return result;
	}

	result.payload = payload;
	result.display_text = JSON::stringify(payload, "\t", false, true);
	result.duration_ms = tool_duration;

	// Record successful tool execution metric
	((YeetAIDock*)this)->_record_tool_execution(p_tool_name, tool_duration, true, false);

	return result;
}

// ── Tool name enumeration ───────────────────────────────────────────────────

Vector<String> yeet_ai_get_all_tool_names() {
	static const char *names[] = {
		"add_2d_collision_shape", "add_animation_track", "add_animation_transition",
		"add_audio_bus_effect", "add_collision_exception", "add_collision_shape", "add_collision_shape_2d",
		"add_csg_primitive", "add_custom_class", "add_node", "add_primitive_mesh",
		"add_tileset_atlas_source", "assign_resource_to_property", "attach_script",
		"audit_game_physics",
		"bake_navigation_mesh", "batch_reparent_nodes", "batch_set_node_property",
		"batch_tool_calls", "capture_dual_view", "capture_editor_viewport",
		"capture_game_viewport", "capture_subviewport", "capture_texture_resource",
		"clear_tilemap_cells", "connect_signal", "connect_ui_signal", "copy_project_file",
		"create_accept_dialog", "create_animated_sprite_2d", "create_animatable_body_2d", "create_animation", "create_area_2d",
		"create_area_3d", "create_aspect_ratio_container", "create_atlas_texture", "create_audio_player",
		"create_audio_stream_player_2d", "create_blend_tree", "create_button", 		"create_camera_2d", "create_camera_3d", "create_canvas_layer", "create_character_body_2d",
		"create_character_body_3d", "create_check_box", "create_color_rect", "create_collision_polygon_2d", "create_confirmation_dialog", "create_container_layout", "create_control_node",
		"create_curve", "create_fog_volume", "create_font", "create_game_actor_2d", "create_game_actor_3d", "create_gdscript_file",
		"create_gi_probe", "create_gpu_particles_2d", "create_gradient", "create_graph_node",
		"create_h_separator", "create_input_action", "create_item_list", "create_label", "create_line_edit", "create_light",
		"create_light_2d", "create_line_2d", "create_margin_container", "create_marker_2d", "create_multiplayer_spawner",
		"create_multiplayer_synchronizer", "create_navigation_agent_2d", "create_navigation_link",
		"create_navigation_obstacle", "create_navigation_region_2d", "create_nine_patch_rect", "create_noise_texture", 		"create_option_button", "create_panel_container", "create_parallax_background", "create_parallax_layer",
		"create_particle_emitter", "create_path_2d", "create_path_3d", "create_path_follow_2d",
		"create_polygon_2d", "create_progress_bar", "create_project_folder",
		"create_ray_cast_2d", "create_ray_cast_3d", "create_reference_rect", "create_rich_text_label", "create_reflection_probe", "create_remote_transform_2d",
		"create_rigid_body_2d", "create_rigid_body_3d", "create_scene_file",
		"create_scroll_container", "create_shader_material", "create_shape_cast_2d", "create_shape_cast_3d",
		"create_sky", "create_slider", "create_spin_box", "create_sprite_2d", 		"create_standard_material",
		"create_static_body_2d", "create_static_body_3d", "create_stylebox", "create_subviewport_container", "create_tab_container",
		"create_texture_2d", "create_texture_progress_bar", "create_text_edit", "create_theme", "create_tile_map", "create_tileset", "create_timer", "create_tween", "create_tree_widget",
		"create_ui_element", "create_vehicle_body_3d", "create_v_separator",
		"create_visible_on_screen_notifier_2d", "debugger_continue",
		"delete_project_file", "disconnect_signal", "duplicate_node", "edit_script",
		"editor_undo", "export_project", "extract_sub_scene", "file_exists",
		"find_project_files", "focus_scene_tree_node", "get_animation_player_state",
		"get_audio_buses", "get_autoloads", "get_binary_file_metadata",
		"get_class_reference", "get_collision_exceptions", "get_console_output",
		"get_current_scene", "get_debug_snapshot", "get_editor_3d_camera_transform",
		"get_editor_inspector_subject", "get_editor_log", "get_editor_settings",
		"get_editor_version", "get_enum_values", "get_export_presets",
		"get_gdscript_docs", "get_gdscript_errors", "get_gdscript_symbols",
		"get_global_classes", "get_input_actions", "get_method_signature",
		"get_navigation_path", "get_navigation_region_info", "get_network_state",
		"get_node_api", "get_node_collision_layers", "get_node_details",
		"get_node_groups", "get_open_scenes", "get_project_settings",
		"get_project_tree", "get_remote_scene_tree", "get_resource_dependencies",
		"get_runtime_debugger_state", "get_scene_dependency_closure",
		"get_scene_tree", "get_selected_nodes", "get_shader_code",
		"get_shader_uniforms", "get_signal_connections", "get_tilemap_info",
		"get_tileset_sources", "get_translation_overview", "get_unsaved_scenes",
		"get_world_environment", "git_branch", "git_diff_file", "git_log",
		"git_status", "grep_project_files", "import_asset", "inspect_runtime_node",
		"inspect_runtime_variable", "instantiate_scene", "lint_gdscript",
		"list_directory", "manage_autoloads", "manage_editor_plugins",
		"manage_export_presets", "merge_scenes", "monitor_runtime_performance",
		"move_child", "move_project_file", "open_scene", "paint_terrain",
		"patch_editor_settings", "patch_project_settings", "play_animation",
		"play_audio", "play_current_scene", "play_main_scene", "profile_frame",
		"query_physics", "query_raycast_2d", "raycast_query", "read_project_file",
		"reimport_project_files", "reload_scene", "remove_animation_track",
		"remove_collision_exception", "remove_input_action", "remove_node",
		"rename_node", "rename_resource_references", "reparent_node", "repair_game_physics",
		"replace_in_project_files", "replace_node_with_scene", "resolve_resource_uid",
		"run_gdscript_expression", "run_gdscript_test", "run_scene_script",
		"save_all_scenes", "save_current_scene", "save_resource",
		"scan_project_filesystem", "search_class_db", "search_project_settings_keys",
		"select_file", "set_animation_blend_amount", "set_animation_track_key",
		"set_breakpoint", "set_collision_layer_mask", "set_control_layout",
		"set_control_theme_override", "set_default_import_presets",
		"set_editor_3d_camera", "set_editor_main_screen", "set_environment_fog",
		"set_environment_ss_effects", "set_environment_tonemap", "set_import_setting",
		"set_input_action_bindings", "set_main_scene", "set_navigation_agent_params",
		"set_node_collision_layers", "set_node_meta", "set_node_property",
		"set_physics_material", "set_shader_uniform", "set_tile_collision_polygon",
		"set_tilemap_cells", "set_world_environment", "shape_cast_query",
		"stop_audio", "stop_playing_scene", "update_gdscript_file",
		"update_shader_code", "validate_scene", "write_project_file",
	};
	static const int count = sizeof(names) / sizeof(names[0]);
	Vector<String> out;
	out.resize(count);
	for (int i = 0; i < count; i++) {
		out.write[i] = String(names[i]);
	}
	return out;
}
