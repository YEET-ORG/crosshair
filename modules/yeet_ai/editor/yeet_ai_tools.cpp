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

// Defined later in this file; used by catalog tools above the definition site.
Vector<String> yeet_ai_get_all_tool_names();

// ═══════════════════════════════════════════════════════════════════════════
// _execute_tool — dispatch table
// ═══════════════════════════════════════════════════════════════════════════

// Single source of truth for every callable tool. Defined as a static member so
// it can take the address of the protected _tool_* handlers; exposed via the
// header so both dispatch and advertising read the same list.
const YeetAIDock::ToolEntry *YeetAIDock::tool_dispatch_table(int &r_count) {
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
		{ "create_sprite_frames",                 &YeetAIDock::_tool_create_sprite_frames },
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
		{ "create_light_occluder_2d",            &YeetAIDock::_tool_create_light_occluder_2d },
		{ "create_canvas_modulate",              &YeetAIDock::_tool_create_canvas_modulate },
		{ "create_skeleton_2d",                  &YeetAIDock::_tool_create_skeleton_2d },
		{ "create_bone_2d",                      &YeetAIDock::_tool_create_bone_2d },
		{ "create_pin_joint_2d",                 &YeetAIDock::_tool_create_pin_joint_2d },
		{ "create_damped_spring_joint_2d",       &YeetAIDock::_tool_create_damped_spring_joint_2d },
		{ "create_multimesh_instance_2d",        &YeetAIDock::_tool_create_multimesh_instance_2d },
		{ "create_touch_screen_button",          &YeetAIDock::_tool_create_touch_screen_button },
		{ "create_subviewport",                  &YeetAIDock::_tool_create_subviewport },
		{ "create_mesh_instance_2d",             &YeetAIDock::_tool_create_mesh_instance_2d },
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
		{ "scaffold_platformer_player_2d",       &YeetAIDock::_tool_scaffold_platformer_player_2d },
		{ "scaffold_patrol_enemy_2d",            &YeetAIDock::_tool_scaffold_patrol_enemy_2d },
		{ "scaffold_collectible_2d",             &YeetAIDock::_tool_scaffold_collectible_2d },
		{ "scaffold_moving_platform_2d",         &YeetAIDock::_tool_scaffold_moving_platform_2d },
		{ "scaffold_game_hud_2d",                &YeetAIDock::_tool_scaffold_game_hud_2d },
		{ "scaffold_main_menu_2d",               &YeetAIDock::_tool_scaffold_main_menu_2d },
		{ "scaffold_pause_menu_2d",              &YeetAIDock::_tool_scaffold_pause_menu_2d },
		{ "scaffold_lighting_rig_2d",            &YeetAIDock::_tool_scaffold_lighting_rig_2d },
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
		{ "create_texture_rect",                 &YeetAIDock::_tool_create_texture_rect },
		{ "create_text_edit",                    &YeetAIDock::_tool_create_text_edit },
		{ "create_theme",                        &YeetAIDock::_tool_create_theme },
		{ "create_tile_map",                     &YeetAIDock::_tool_create_tile_map },
		{ "create_tilemap_layer",                &YeetAIDock::_tool_create_tilemap_layer },
		{ "create_tileset",                      &YeetAIDock::_tool_create_tileset },
		{ "create_timer",                        &YeetAIDock::_tool_create_timer },
		{ "create_tween",                        &YeetAIDock::_tool_create_tween },
		{ "create_tree_widget",                  &YeetAIDock::_tool_create_tree_widget },
		{ "create_ui_element",                   &YeetAIDock::_tool_create_ui_element },
		{ "create_vehicle_body_3d",              &YeetAIDock::_tool_create_vehicle_body_3d },
		{ "create_v_separator",                  &YeetAIDock::_tool_create_v_separator },
		{ "create_visible_on_screen_notifier_2d", &YeetAIDock::_tool_create_visible_on_screen_notifier_2d },
		{ "debugger_continue",                   &YeetAIDock::_tool_debugger_continue },
		{ "debugger_assert_condition",           &YeetAIDock::_tool_debugger_assert_condition },
		{ "debugger_await_condition",            &YeetAIDock::_tool_debugger_await_condition },
		{ "debugger_evaluate",                   &YeetAIDock::_tool_debugger_evaluate },
		{ "debugger_get_errors",                 &YeetAIDock::_tool_debugger_get_errors },
		{ "debugger_get_memory_info",            &YeetAIDock::_tool_debugger_get_memory_info },
		{ "debugger_get_performance_snapshot",   &YeetAIDock::_tool_debugger_get_performance_snapshot },
		{ "debugger_get_sessions",               &YeetAIDock::_tool_debugger_get_sessions },
		{ "debugger_get_stack",                  &YeetAIDock::_tool_debugger_get_stack },
		{ "debugger_get_state",                  &YeetAIDock::_tool_debugger_get_state },
		{ "debugger_get_variables",              &YeetAIDock::_tool_debugger_get_variables },
		{ "debugger_reload_scripts",             &YeetAIDock::_tool_debugger_reload_scripts },
		{ "debugger_send_custom_message",        &YeetAIDock::_tool_debugger_send_custom_message },
		{ "debugger_step_out",                   &YeetAIDock::_tool_debugger_step_out },
		{ "debugger_toggle_profiler",            &YeetAIDock::_tool_debugger_toggle_profiler },
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
		{ "memory_record_entity",                &YeetAIDock::_tool_memory_record_entity },
		{ "memory_query_entities",               &YeetAIDock::_tool_memory_query_entities },
		{ "memory_get_entity",                   &YeetAIDock::_tool_memory_get_entity },
		{ "memory_update_entity",                &YeetAIDock::_tool_memory_update_entity },
		{ "memory_delete_entity",                &YeetAIDock::_tool_memory_delete_entity },
		{ "memory_get_summary",                  &YeetAIDock::_tool_memory_get_summary },
		{ "manage_autoloads",                    &YeetAIDock::_tool_manage_autoloads },
		{ "manage_editor_plugins",               &YeetAIDock::_tool_manage_editor_plugins },
		{ "manage_export_presets",                &YeetAIDock::_tool_manage_export_presets },
		{ "index_project_assets",                &YeetAIDock::_tool_index_project_assets },
		{ "index_asset",                         &YeetAIDock::_tool_index_asset },
		{ "search_assets",                       &YeetAIDock::_tool_search_assets },
		{ "get_asset_manifest",                  &YeetAIDock::_tool_get_asset_manifest },
		{ "update_asset_manifest",               &YeetAIDock::_tool_update_asset_manifest },
		{ "confirm_asset_manifest",              &YeetAIDock::_tool_confirm_asset_manifest },
		{ "list_asset_index_issues",             &YeetAIDock::_tool_list_asset_index_issues },
		{ "create_sprite_frames_from_manifest",  &YeetAIDock::_tool_create_sprite_frames_from_manifest },
		{ "create_tileset_from_manifest",        &YeetAIDock::_tool_create_tileset_from_manifest },
		{ "index_project_context",               &YeetAIDock::_tool_index_project_context },
		{ "search_project_context",              &YeetAIDock::_tool_search_project_context },
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
		{ "project_audit_health",                &YeetAIDock::_tool_project_audit_health },
		{ "project_detect_broken_scripts",       &YeetAIDock::_tool_project_detect_broken_scripts },
		{ "project_get_class_api",               &YeetAIDock::_tool_project_get_class_api },
		{ "project_scan_cyclic_deps",            &YeetAIDock::_tool_project_scan_cyclic_deps },
		{ "project_scan_missing_deps",           &YeetAIDock::_tool_project_scan_missing_deps },
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
		{ "resource_create",                     &YeetAIDock::_tool_resource_create },
		{ "resource_modify",                     &YeetAIDock::_tool_resource_modify },
		{ "resource_read",                       &YeetAIDock::_tool_resource_read },
		{ "runtime_break",                       &YeetAIDock::_tool_runtime_break },
		{ "runtime_call_method",                 &YeetAIDock::_tool_runtime_call_method },
		{ "runtime_change_scene",                &YeetAIDock::_tool_runtime_change_scene },
		{ "runtime_connect_signal",              &YeetAIDock::_tool_runtime_connect_signal },
		{ "runtime_create_node",                 &YeetAIDock::_tool_runtime_create_node },
		{ "runtime_disconnect_signal",           &YeetAIDock::_tool_runtime_disconnect_signal },
		{ "runtime_audio_bus",                   &YeetAIDock::_tool_runtime_audio_bus },
		{ "runtime_audio_effect",                &YeetAIDock::_tool_runtime_audio_effect },
		{ "runtime_audio_play",                  &YeetAIDock::_tool_runtime_audio_play },
		{ "runtime_canvas_draw",                 &YeetAIDock::_tool_runtime_canvas_draw },
		{ "runtime_debug_draw",                  &YeetAIDock::_tool_runtime_debug_draw },
		{ "runtime_duplicate_node",              &YeetAIDock::_tool_runtime_duplicate_node },
		{ "runtime_emit_signal",                 &YeetAIDock::_tool_runtime_emit_signal },
		{ "runtime_environment",                 &YeetAIDock::_tool_runtime_environment },
		{ "runtime_eval",                        &YeetAIDock::_tool_runtime_eval },
		{ "runtime_get_camera",                  &YeetAIDock::_tool_runtime_get_camera },
		{ "runtime_get_node_property",           &YeetAIDock::_tool_runtime_get_node_property },
		{ "runtime_get_nodes_in_group",          &YeetAIDock::_tool_runtime_get_nodes_in_group },
		{ "runtime_get_performance",             &YeetAIDock::_tool_runtime_get_performance },
		{ "runtime_get_scene_tree",              &YeetAIDock::_tool_runtime_get_scene_tree },
		{ "runtime_gridmap",                     &YeetAIDock::_tool_runtime_gridmap },
		{ "runtime_inspect_object",              &YeetAIDock::_tool_runtime_inspect_object },
		{ "runtime_instantiate_scene",           &YeetAIDock::_tool_runtime_instantiate_scene },
		{ "runtime_key_hold",                    &YeetAIDock::_tool_runtime_key_hold },
		{ "runtime_key_press",                   &YeetAIDock::_tool_runtime_key_press },
		{ "runtime_key_release",                 &YeetAIDock::_tool_runtime_key_release },
		{ "runtime_light_3d",                    &YeetAIDock::_tool_runtime_light_3d },
		{ "runtime_manage_group",                &YeetAIDock::_tool_runtime_manage_group },
		{ "runtime_mesh_instance",               &YeetAIDock::_tool_runtime_mesh_instance },
		{ "runtime_mouse_click",                 &YeetAIDock::_tool_runtime_mouse_click },
		{ "runtime_mouse_move",                  &YeetAIDock::_tool_runtime_mouse_move },
		{ "runtime_parallax",                    &YeetAIDock::_tool_runtime_parallax },
		{ "runtime_pause",                       &YeetAIDock::_tool_runtime_pause },
		{ "runtime_physics_body",                &YeetAIDock::_tool_runtime_physics_body },
		{ "runtime_play_animation",              &YeetAIDock::_tool_runtime_play_animation },
		{ "runtime_raycast",                     &YeetAIDock::_tool_runtime_raycast },
		{ "runtime_reparent_node",               &YeetAIDock::_tool_runtime_reparent_node },
		{ "runtime_remove_node",                 &YeetAIDock::_tool_runtime_remove_node },
		{ "runtime_send_message",                &YeetAIDock::_tool_runtime_send_message },
		{ "runtime_serialize_state",             &YeetAIDock::_tool_runtime_serialize_state },
		{ "runtime_set_camera",                  &YeetAIDock::_tool_runtime_set_camera },
		{ "runtime_set_node_property",           &YeetAIDock::_tool_runtime_set_node_property },
		{ "runtime_set_property",                &YeetAIDock::_tool_runtime_set_property },
		{ "runtime_shader_param",                &YeetAIDock::_tool_runtime_shader_param },
		{ "runtime_sky",                         &YeetAIDock::_tool_runtime_sky },
		{ "runtime_step",                        &YeetAIDock::_tool_runtime_step },
		{ "runtime_theme_override",              &YeetAIDock::_tool_runtime_theme_override },
		{ "runtime_time_scale",                  &YeetAIDock::_tool_runtime_time_scale },
		{ "runtime_tween_property",              &YeetAIDock::_tool_runtime_tween_property },
		{ "runtime_ui_control",                  &YeetAIDock::_tool_runtime_ui_control },
		{ "runtime_ui_popup",                    &YeetAIDock::_tool_runtime_ui_popup },
		{ "runtime_ui_range",                    &YeetAIDock::_tool_runtime_ui_range },
		{ "runtime_ui_text",                     &YeetAIDock::_tool_runtime_ui_text },
		{ "runtime_window",                      &YeetAIDock::_tool_runtime_window },
		{ "runtime_3d_effects",                  &YeetAIDock::_tool_runtime_3d_effects },
		{ "runtime_animation_control",           &YeetAIDock::_tool_runtime_animation_control },
		{ "runtime_animation_tree",              &YeetAIDock::_tool_runtime_animation_tree },
		{ "runtime_bone_pose",                   &YeetAIDock::_tool_runtime_bone_pose },
		{ "runtime_camera_attributes",           &YeetAIDock::_tool_runtime_camera_attributes },
		{ "runtime_create_joint",                &YeetAIDock::_tool_runtime_create_joint },
		{ "runtime_create_timer",                &YeetAIDock::_tool_runtime_create_timer },
		{ "runtime_csg",                         &YeetAIDock::_tool_runtime_csg },
		{ "runtime_gamepad",                     &YeetAIDock::_tool_runtime_gamepad },
		{ "runtime_gi",                          &YeetAIDock::_tool_runtime_gi },
		{ "runtime_http_request",                &YeetAIDock::_tool_runtime_http_request },
		{ "runtime_input_state",                 &YeetAIDock::_tool_runtime_input_state },
		{ "runtime_light_2d",                    &YeetAIDock::_tool_runtime_light_2d },
		{ "runtime_locale",                      &YeetAIDock::_tool_runtime_locale },
		{ "runtime_mouse_drag",                  &YeetAIDock::_tool_runtime_mouse_drag },
		{ "runtime_multimesh",                   &YeetAIDock::_tool_runtime_multimesh },
		{ "runtime_multiplayer",                 &YeetAIDock::_tool_runtime_multiplayer },
		{ "runtime_navigate_path",               &YeetAIDock::_tool_runtime_navigate_path },
		{ "runtime_navigation_3d",               &YeetAIDock::_tool_runtime_navigation_3d },
		{ "runtime_os_info",                     &YeetAIDock::_tool_runtime_os_info },
		{ "runtime_particles",                   &YeetAIDock::_tool_runtime_particles },
		{ "runtime_path_3d",                     &YeetAIDock::_tool_runtime_path_3d },
		{ "runtime_physics_2d_query",            &YeetAIDock::_tool_runtime_physics_2d_query },
		{ "runtime_physics_3d_query",            &YeetAIDock::_tool_runtime_physics_3d_query },
		{ "runtime_procedural_mesh",             &YeetAIDock::_tool_runtime_procedural_mesh },
		{ "runtime_process_mode",                &YeetAIDock::_tool_runtime_process_mode },
		{ "runtime_render_settings",             &YeetAIDock::_tool_runtime_render_settings },
		{ "runtime_resource_load",               &YeetAIDock::_tool_runtime_resource_load },
		{ "runtime_rpc",                         &YeetAIDock::_tool_runtime_rpc },
		{ "runtime_scroll",                      &YeetAIDock::_tool_runtime_scroll },
		{ "runtime_script_attach",               &YeetAIDock::_tool_runtime_script_attach },
		{ "runtime_shape_2d",                    &YeetAIDock::_tool_runtime_shape_2d },
		{ "runtime_skeleton_ik",                 &YeetAIDock::_tool_runtime_skeleton_ik },
		{ "runtime_tilemap_cells",               &YeetAIDock::_tool_runtime_tilemap_cells },
		{ "runtime_touch",                       &YeetAIDock::_tool_runtime_touch },
		{ "runtime_ui_item_list",                &YeetAIDock::_tool_runtime_ui_item_list },
		{ "runtime_ui_menu",                     &YeetAIDock::_tool_runtime_ui_menu },
		{ "runtime_ui_tabs",                     &YeetAIDock::_tool_runtime_ui_tabs },
		{ "runtime_ui_tree",                     &YeetAIDock::_tool_runtime_ui_tree },
		{ "runtime_viewport",                    &YeetAIDock::_tool_runtime_viewport },
		{ "runtime_websocket",                   &YeetAIDock::_tool_runtime_websocket },
		{ "runtime_world_settings",              &YeetAIDock::_tool_runtime_world_settings },
		{ "runtime_audio_bus_layout",            &YeetAIDock::_tool_runtime_audio_bus_layout },
		{ "runtime_audio_spatial",               &YeetAIDock::_tool_runtime_audio_spatial },
		{ "manage_layers",                       &YeetAIDock::_tool_manage_layers },
		{ "manage_translations",                 &YeetAIDock::_tool_manage_translations },
		{ "resolve_resource_uid",                &YeetAIDock::_tool_resolve_resource_uid },
		{ "run_gdscript_expression",             &YeetAIDock::_tool_run_gdscript_expression },
		{ "run_gdscript_test",                   &YeetAIDock::_tool_run_gdscript_test },
		{ "run_scene_script",                    &YeetAIDock::_tool_run_scene_script },
		{ "save_all_scenes",                     &YeetAIDock::_tool_save_all_scenes },
		{ "save_current_scene",                  &YeetAIDock::_tool_save_current_scene },
		{ "save_resource",                       &YeetAIDock::_tool_save_resource },
		{ "scan_project_filesystem",             &YeetAIDock::_tool_scan_project_filesystem },
		{ "scene_get_signals",                   &YeetAIDock::_tool_scene_get_signals },
		{ "scene_modify_node",                   &YeetAIDock::_tool_scene_modify_node },
		{ "scene_read",                          &YeetAIDock::_tool_scene_read },
		{ "scene_remove_node",                   &YeetAIDock::_tool_scene_remove_node },
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
		{ "fill_tilemap_rect",                    &YeetAIDock::_tool_fill_tilemap_rect },
		{ "set_world_environment",               &YeetAIDock::_tool_set_world_environment },
		{ "shape_cast_query",                     &YeetAIDock::_tool_shape_cast_query },
		{ "stop_audio",                          &YeetAIDock::_tool_stop_audio },
		{ "stop_playing_scene",                  &YeetAIDock::_tool_stop_playing_scene },
		{ "update_gdscript_file",                &YeetAIDock::_tool_update_gdscript_file },
		{ "update_shader_code",                  &YeetAIDock::_tool_update_shader_code },
		{ "validate_scene",                      &YeetAIDock::_tool_validate_scene },
		{ "write_project_file",                  &YeetAIDock::_tool_write_project_file },
		{ "update_plan",                         &YeetAIDock::_tool_update_plan },
		{ "search_tool_catalog",                 &YeetAIDock::_tool_search_tool_catalog },
		{ "request_tool_pack",                   &YeetAIDock::_tool_request_tool_pack },
	};
	r_count = (int)(sizeof(table) / sizeof(table[0]));
	return table;
}

Vector<String> YeetAIDock::get_registered_tool_names() {
	int count = 0;
	const ToolEntry *table = tool_dispatch_table(count);
	Vector<String> names;
	names.resize(count);
	for (int i = 0; i < count; i++) {
		names.write[i] = String::utf8(table[i].name);
	}
	return names;
}

Dictionary YeetAIDock::_tool_update_plan(const Dictionary &p_args) const {
	Dictionary result;
	// Accept either "plan" or "steps"; each item may be an object {step,status} or
	// a bare string (treated as a pending step). Full overwrite, like TodoWrite.
	Variant steps_v = p_args.has("plan") ? p_args.get("plan", Variant()) : p_args.get("steps", Variant());
	if (steps_v.get_type() != Variant::ARRAY) {
		result["ok"] = false;
		result["error"] = "update_plan requires a 'plan' (or 'steps') array of {step, status} objects (status: pending|in_progress|done).";
		return result;
	}

	const Array in = steps_v;
	Array plan;
	int done_count = 0;
	for (int i = 0; i < in.size(); i++) {
		Dictionary item;
		if (in[i].get_type() == Variant::DICTIONARY) {
			const Dictionary d = in[i];
			String step = String(d.get("step", d.get("title", d.get("text", ""))));
			if (step.strip_edges().is_empty()) {
				continue;
			}
			String status = String(d.get("status", "pending")).to_lower().strip_edges();
			if (status == "completed" || status == "complete") {
				status = "done";
			} else if (status == "active" || status == "doing" || status == "in-progress") {
				status = "in_progress";
			}
			if (status != "in_progress" && status != "done") {
				status = "pending";
			}
			item["step"] = step.strip_edges();
			item["status"] = status;
		} else if (in[i].get_type() == Variant::STRING) {
			const String step = String(in[i]).strip_edges();
			if (step.is_empty()) {
				continue;
			}
			item["step"] = step;
			item["status"] = "pending";
		} else {
			continue;
		}
		if (String(item["status"]) == "done") {
			done_count++;
		}
		plan.push_back(item);
	}

	_current_plan = plan; // mutable

	result["ok"] = true;
	result["plan"] = plan;
	result["steps_total"] = plan.size();
	result["steps_done"] = done_count;
	result["note"] = "Plan recorded. Keep exactly one step in_progress and mark steps done as you complete them. Call update_plan again whenever the plan changes.";
	return result;
}

Dictionary YeetAIDock::_tool_search_tool_catalog(const Dictionary &p_args) const {
	Dictionary result;
	const String query = String(p_args.get("query", "")).strip_edges().to_lower();
	const String pack_filter = String(p_args.get("pack", "")).strip_edges().to_lower();
	const int max_results = CLAMP(int(p_args.get("max_results", 40)), 1, 200);

	const Vector<String> all = yeet_ai_get_all_tool_names();
	Array matches;
	for (const String &name : all) {
		if (yeet_ai_is_disabled_advertised_tool(name)) {
			continue;
		}
		const ToolPack pack = _tool_pack_for(name);
		String pack_name = "core";
		switch (pack) {
			case PACK_2D:
				pack_name = "2d";
				break;
			case PACK_3D:
				pack_name = "3d";
				break;
			case PACK_UI:
				pack_name = "ui";
				break;
			case PACK_ANIM:
				pack_name = "anim";
				break;
			case PACK_AUDIO:
				pack_name = "audio";
				break;
			case PACK_SHADER:
				pack_name = "shader";
				break;
			case PACK_PHYSICS:
				pack_name = "physics";
				break;
			case PACK_DEBUG:
				pack_name = "debug";
				break;
			case PACK_MULTIPLAYER:
				pack_name = "multiplayer";
				break;
			default:
				pack_name = "core";
				break;
		}
		if (!pack_filter.is_empty() && pack_name != pack_filter && !(pack_filter == "core" && pack == PACK_CORE)) {
			continue;
		}
		if (!query.is_empty() && !name.to_lower().contains(query) && !pack_name.contains(query)) {
			continue;
		}
		Dictionary row;
		row["name"] = name;
		row["pack"] = pack_name;
		matches.push_back(row);
		if (matches.size() >= max_results) {
			break;
		}
	}

	result["ok"] = true;
	result["query"] = query;
	result["pack"] = pack_filter;
	result["count"] = matches.size();
	result["tools"] = matches;
	result["note"] = "Catalog search only. To advertise a domain pack on the next model turn, call request_tool_pack with packs like [\"2d\",\"debug\"].";
	Array pack_names;
	pack_names.push_back("core");
	pack_names.push_back("2d");
	pack_names.push_back("3d");
	pack_names.push_back("ui");
	pack_names.push_back("anim");
	pack_names.push_back("audio");
	pack_names.push_back("shader");
	pack_names.push_back("physics");
	pack_names.push_back("debug");
	pack_names.push_back("multiplayer");
	result["packs"] = pack_names;
	return result;
}

Dictionary YeetAIDock::_tool_request_tool_pack(const Dictionary &p_args) const {
	Dictionary result;
	Array packs = p_args.get("packs", Array());
	if (packs.is_empty() && p_args.has("pack")) {
		packs.push_back(p_args.get("pack", ""));
	}
	if (packs.is_empty()) {
		result["ok"] = false;
		result["error"] = "request_tool_pack requires 'packs' (array of pack names: 2d,3d,ui,anim,audio,shader,physics,debug,multiplayer).";
		return result;
	}

	Array accepted;
	Array unknown;
	for (int i = 0; i < packs.size(); i++) {
		const String p = String(packs[i]).strip_edges().to_lower();
		ToolPack pack = PACK_CORE;
		bool ok = true;
		if (p == "2d") {
			pack = PACK_2D;
		} else if (p == "3d") {
			pack = PACK_3D;
		} else if (p == "ui") {
			pack = PACK_UI;
		} else if (p == "anim" || p == "animation") {
			pack = PACK_ANIM;
		} else if (p == "audio") {
			pack = PACK_AUDIO;
		} else if (p == "shader" || p == "material") {
			pack = PACK_SHADER;
		} else if (p == "physics" || p == "collision") {
			pack = PACK_PHYSICS;
		} else if (p == "debug" || p == "runtime") {
			pack = PACK_DEBUG;
		} else if (p == "multiplayer" || p == "network") {
			pack = PACK_MULTIPLAYER;
		} else if (p == "core") {
			pack = PACK_CORE;
		} else {
			ok = false;
			unknown.push_back(p);
		}
		if (ok) {
			_turn_extra_pack_mask |= (1u << int(pack));
			accepted.push_back(p);
		}
	}

	// Expand advertised tools for the next model request in this turn.
	_turn_active_tools_valid = false;
	const Vector<String> expanded = _build_active_tool_names();

	result["ok"] = !accepted.is_empty();
	result["accepted_packs"] = accepted;
	if (!unknown.is_empty()) {
		result["unknown_packs"] = unknown;
	}
	result["advertised_tool_count"] = expanded.size();
	result["max_advertised_tools"] = _resolve_max_advertised_tools();
	result["extra_pack_mask"] = int(_turn_extra_pack_mask);
	result["note"] = "Pack(s) unlocked for this turn. The expanded tool list is sent on the next model round-trip. Prefer search_tool_catalog first if unsure which pack you need.";
	if (accepted.is_empty()) {
		result["error"] = "No valid packs requested.";
	}
	return result;
}

YeetAIDock::ToolExecutionResult YeetAIDock::_execute_tool(const String &p_tool_name, const Dictionary &p_args) {
	int table_size = 0;
	const ToolEntry *table = tool_dispatch_table(table_size);

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
	_current_tool_name_for_undo = p_tool_name;
	int64_t tool_start_time = Time::get_singleton()->get_ticks_msec();
	Dictionary payload = (this->*found->handler)(effective_args);
	_current_tool_name_for_undo.clear();
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

	// ── Auto-verification ────────────────────────────────────────────────────
	// After a successful script write, surface GDScript parse errors in the SAME
	// tool result so the model reacts in this turn instead of relying on it to
	// remember to ask. get_gdscript_errors reads the editor log (no path needed).
	if (_get_editor_setting_bool("yeet_ai/chat/auto_verify", true) &&
			(p_tool_name == "create_gdscript_file" || p_tool_name == "update_gdscript_file" ||
					p_tool_name == "edit_script" || p_tool_name == "attach_script")) {
		const Dictionary diag = _tool_get_gdscript_errors(Dictionary());
		if (int(diag.get("error_count", 0)) > 0) {
			result.payload["auto_diagnostics"] = diag;
			result.payload["auto_diagnostics_hint"] = "GDScript errors were detected after this edit. Fix them before continuing.";
		}
	}

	result.display_text = JSON::stringify(result.payload, "\t", false, true);
	result.duration_ms = tool_duration;

	// Record successful tool execution metric
	((YeetAIDock*)this)->_record_tool_execution(p_tool_name, tool_duration, true, false);

	// Auto-record to project memory for context persistence across sessions.
	((YeetAIDock*)this)->_auto_record_tool_result(p_tool_name, effective_args, payload);

	// Refresh editor UI (SceneTree, Inspector, FileSystem) after mutating tools.
	((YeetAIDock*)this)->_refresh_editor_after_tool(p_tool_name, payload);

	return result;
}

// ── Tool name enumeration ───────────────────────────────────────────────────

Vector<String> yeet_ai_get_all_tool_names() {
	// Derived from the single source of truth (tool_dispatch_table) so the
	// advertised tool list can never drift from what is actually executable.
	// Previously a hand-maintained list that had silently fallen 127 tools behind.
	return YeetAIDock::get_registered_tool_names();
}

/* Legacy hand-maintained tool-name list — superseded by tool_dispatch_table().
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
		"create_sprite_frames",
		"create_area_3d", "create_aspect_ratio_container", "create_atlas_texture", "create_audio_player",
		"create_audio_stream_player_2d", "create_blend_tree", "create_button", 		"create_camera_2d", "create_camera_3d", "create_canvas_layer", "create_character_body_2d",
		"create_character_body_3d", "create_check_box", "create_color_rect", "create_collision_polygon_2d",
		"create_light_occluder_2d", "create_canvas_modulate", "create_skeleton_2d", "create_bone_2d",
		"create_pin_joint_2d", "create_damped_spring_joint_2d", "create_multimesh_instance_2d",
		"create_touch_screen_button", "create_subviewport", "create_mesh_instance_2d",
		"create_confirmation_dialog", "create_container_layout", "create_control_node",
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
		"create_sky", "scaffold_platformer_player_2d", "scaffold_patrol_enemy_2d", "scaffold_collectible_2d", "scaffold_moving_platform_2d",
		"scaffold_game_hud_2d", "scaffold_main_menu_2d", "scaffold_pause_menu_2d", "scaffold_lighting_rig_2d",
		"create_slider", "create_spin_box", "create_sprite_2d", 		"create_standard_material",
		"create_static_body_2d", "create_static_body_3d", "create_stylebox", "create_subviewport_container", "create_tab_container",
		"create_texture_2d", "create_texture_progress_bar", "create_text_edit", 		"create_theme", "create_tile_map", "create_tilemap_layer", "create_tileset", "create_timer", "create_tween", "create_tree_widget",
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
		"inspect_runtime_variable", "instantiate_scene", 		"lint_gdscript",
		"list_directory", "manage_autoloads", "manage_editor_plugins",
		"index_project_assets", "index_asset", "search_assets", "get_asset_manifest",
		"update_asset_manifest", "confirm_asset_manifest", "list_asset_index_issues",
		"create_sprite_frames_from_manifest", "create_tileset_from_manifest",
		"memory_record_entity", "memory_query_entities", "memory_get_entity",
		"memory_update_entity", "memory_delete_entity", "memory_get_summary",
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
		"set_tilemap_cells", "fill_tilemap_rect", "set_world_environment", "shape_cast_query",
		"stop_audio", "stop_playing_scene", "update_gdscript_file",
		"update_shader_code", "validate_scene", "write_project_file",
*/
