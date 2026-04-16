/**************************************************************************/
/*  yeet_ai_tools.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/
/*                                                                        */
/*  Tool dispatch: _execute_tool replaces the old if/else chain with a    */
/*  sorted binary-search lookup over tool name → handler pairs.           */
/*                                                                        */
/*  New tool implementations should be added to yeet_ai_dock.cpp and     */
/*  registered here (and in the system prompt) to complete the loop.      */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/json.h"
#include "core/string/translation.h"

#include <algorithm>

// ═══════════════════════════════════════════════════════════════════════════
// _execute_tool — sorted dispatch table (binary search)
// ═══════════════════════════════════════════════════════════════════════════

YeetAIDock::ToolExecutionResult YeetAIDock::_execute_tool(const String &p_tool_name, const Dictionary &p_args) const {
	using ToolHandler = Dictionary (YeetAIDock::*)(const Dictionary &) const;

	struct ToolEntry {
		const char *name;
		ToolHandler handler;
		bool operator<(const ToolEntry &o) const { return strcmp(name, o.name) < 0; }
	};

	// Static, sorted once at first call. No file-scope member pointer access.
	static const ToolEntry table[] = {
		// ═══════════════════════════════════════════════════════════════════════
		// SORTED ALPHABETICALLY — binary search requires this order.
		// ═══════════════════════════════════════════════════════════════════════
		{ "add_2d_collision_shape",              &YeetAIDock::_tool_add_2d_collision_shape },
		{ "add_animation_track",                 &YeetAIDock::_tool_add_animation_track },
		{ "add_animation_transition",             &YeetAIDock::_tool_add_animation_transition },
		{ "add_audio_bus_effect",                &YeetAIDock::_tool_add_audio_bus_effect },
		{ "add_custom_class",                    &YeetAIDock::_tool_add_custom_class },
		{ "add_csg_primitive",                   &YeetAIDock::_tool_add_csg_primitive },
		{ "add_collision_exception",             &YeetAIDock::_tool_add_collision_exception },
		{ "add_collision_shape",                 &YeetAIDock::_tool_add_collision_shape },
		{ "add_node",                            &YeetAIDock::_tool_add_node },
		{ "add_primitive_mesh",                  &YeetAIDock::_tool_add_primitive_mesh },
		{ "add_tileset_atlas_source",            &YeetAIDock::_tool_add_tileset_atlas_source },
		{ "assign_resource_to_property",         &YeetAIDock::_tool_assign_resource_to_property },
		{ "attach_script",                       &YeetAIDock::_tool_attach_script },
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
		{ "copy_project_file",                   &YeetAIDock::_tool_copy_project_file },
		{ "create_animated_sprite_2d",            &YeetAIDock::_tool_create_animated_sprite_2d },
		{ "create_animation",                    &YeetAIDock::_tool_create_animation },
		{ "create_area_2d",                      &YeetAIDock::_tool_create_area_2d },
		{ "create_area_3d",                      &YeetAIDock::_tool_create_area_3d },
		{ "create_atlas_texture",                &YeetAIDock::_tool_create_atlas_texture },
		{ "create_audio_player",                 &YeetAIDock::_tool_create_audio_player },
		{ "create_blend_tree",                   &YeetAIDock::_tool_create_blend_tree },
		{ "create_camera_3d",                    &YeetAIDock::_tool_create_camera_3d },
		{ "create_character_body_2d",            &YeetAIDock::_tool_create_character_body_2d },
		{ "create_character_body_3d",            &YeetAIDock::_tool_create_character_body_3d },
		{ "create_container_layout",             &YeetAIDock::_tool_create_container_layout },
		{ "create_control_node",                 &YeetAIDock::_tool_create_control_node },
		{ "create_curve",                        &YeetAIDock::_tool_create_curve },
		{ "create_fog_volume",                   &YeetAIDock::_tool_create_fog_volume },
		{ "create_font",                         &YeetAIDock::_tool_create_font },
		{ "create_gdscript_file",                &YeetAIDock::_tool_create_gdscript_file },
		{ "create_gi_probe",                     &YeetAIDock::_tool_create_gi_probe },
		{ "create_gradient",                     &YeetAIDock::_tool_create_gradient },
		{ "create_graph_node",                   &YeetAIDock::_tool_create_graph_node },
		{ "create_input_action",                 &YeetAIDock::_tool_create_input_action },
		{ "create_item_list",                    &YeetAIDock::_tool_create_item_list },
		{ "create_light",                        &YeetAIDock::_tool_create_light },
		{ "create_light_2d",                     &YeetAIDock::_tool_create_light_2d },
		{ "create_line_2d",                      &YeetAIDock::_tool_create_line_2d },
		{ "create_multiplayer_spawner",          &YeetAIDock::_tool_create_multiplayer_spawner },
		{ "create_multiplayer_synchronizer",      &YeetAIDock::_tool_create_multiplayer_synchronizer },
		{ "create_navigation_link",              &YeetAIDock::_tool_create_navigation_link },
		{ "create_navigation_obstacle",          &YeetAIDock::_tool_create_navigation_obstacle },
		{ "create_noise_texture",                &YeetAIDock::_tool_create_noise_texture },
		{ "create_option_button",                &YeetAIDock::_tool_create_option_button },
		{ "create_path_2d",                      &YeetAIDock::_tool_create_path_2d },
		{ "create_path_3d",                      &YeetAIDock::_tool_create_path_3d },
		{ "create_particle_emitter",             &YeetAIDock::_tool_create_particle_emitter },
		{ "create_polygon_2d",                   &YeetAIDock::_tool_create_polygon_2d },
		{ "create_progress_bar",                 &YeetAIDock::_tool_create_progress_bar },
		{ "create_project_folder",               &YeetAIDock::_tool_create_project_folder },
		{ "create_ray_cast_2d",                  &YeetAIDock::_tool_create_ray_cast_2d },
		{ "create_ray_cast_3d",                  &YeetAIDock::_tool_create_ray_cast_3d },
		{ "create_reflection_probe",             &YeetAIDock::_tool_create_reflection_probe },
		{ "create_rigid_body_2d",                &YeetAIDock::_tool_create_rigid_body_2d },
		{ "create_rigid_body_3d",                &YeetAIDock::_tool_create_rigid_body_3d },
		{ "create_scene_file",                   &YeetAIDock::_tool_create_scene_file },
		{ "create_scroll_container",             &YeetAIDock::_tool_create_scroll_container },
		{ "create_shape_cast_3d",                &YeetAIDock::_tool_create_shape_cast_3d },
		{ "create_shader_material",              &YeetAIDock::_tool_create_shader_material },
		{ "create_slider",                       &YeetAIDock::_tool_create_slider },
		{ "create_sky",                          &YeetAIDock::_tool_create_sky },
		{ "create_sprite_2d",                    &YeetAIDock::_tool_create_sprite_2d },
		{ "create_static_body_3d",                &YeetAIDock::_tool_create_static_body_3d },
		{ "create_standard_material",            &YeetAIDock::_tool_create_standard_material },
		{ "create_stylebox",                     &YeetAIDock::_tool_create_stylebox },
		{ "create_tab_container",                &YeetAIDock::_tool_create_tab_container },
		{ "create_texture_2d",                   &YeetAIDock::_tool_create_texture_2d },
		{ "create_theme",                        &YeetAIDock::_tool_create_theme },
		{ "create_tileset",                      &YeetAIDock::_tool_create_tileset },
		{ "create_tree_widget",                  &YeetAIDock::_tool_create_tree_widget },
		{ "create_ui_element",                   &YeetAIDock::_tool_create_ui_element },
		{ "create_vehicle_body_3d",              &YeetAIDock::_tool_create_vehicle_body_3d },
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
		{ "raycast_query",                       &YeetAIDock::_tool_raycast_query },
		{ "read_project_file",                   &YeetAIDock::_tool_read_project_file },
		{ "remove_animation_track",              &YeetAIDock::_tool_remove_animation_track },
		{ "remove_collision_exception",          &YeetAIDock::_tool_remove_collision_exception },
		{ "remove_input_action",                  &YeetAIDock::_tool_remove_input_action },
		{ "remove_node",                         &YeetAIDock::_tool_remove_node },
		{ "rename_node",                         &YeetAIDock::_tool_rename_node },
		{ "rename_resource_references",          &YeetAIDock::_tool_rename_resource_references },
		{ "reparent_node",                       &YeetAIDock::_tool_reparent_node },
		{ "replace_in_project_files",            &YeetAIDock::_tool_replace_in_project_files },
		{ "replace_node_with_scene",              &YeetAIDock::_tool_replace_node_with_scene },
		{ "reimport_project_files",              &YeetAIDock::_tool_reimport_project_files },
		{ "reload_scene",                        &YeetAIDock::_tool_reload_scene },
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
		{ "set_editor_3d_camera",                &YeetAIDock::_tool_set_editor_3d_camera },
		{ "set_editor_main_screen",              &YeetAIDock::_tool_set_editor_main_screen },
		{ "set_default_import_presets",          &YeetAIDock::_tool_set_default_import_presets },
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

	// Binary search over the sorted table
	const ToolEntry key = { p_tool_name.utf8().get_data(), nullptr };
	const ToolEntry *found = std::lower_bound(table, table + table_size, key,
			[](const ToolEntry &a, const ToolEntry &b) {
				return strcmp(a.name, b.name) < 0;
			});

	ToolExecutionResult result;
	if (found != table + table_size && strcmp(found->name, p_tool_name.utf8().get_data()) == 0) {
		result.ok = true;
		result.payload = (this->*found->handler)(p_args);
		result.display_text = JSON::stringify(result.payload, "\t", false, true);
		return result;
	}

	result.payload["error"] = "Unknown tool";
	result.display_text = vformat("Unknown tool: %s", p_tool_name);
	return result;
}
