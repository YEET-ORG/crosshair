# Crosshair AI Fine-Tuning Dataset

Dataset for fine-tuning LLMs on Crosshair AI tool-calling in the Godot editor.

## Directory Structure

```
dataset/
├── README.md                          # This file
├── tool_schemas/
│   ├── all_tools.json                 # Complete tool definitions (name, category, args, returns)
│   └── system_prompts.json            # System prompt variants (full, compact, tool-list-only)
├── tool_calling_examples/
│   └── examples.json                  # Curated intent→tool-call examples with reasoning
├── finetune_openai/
│   └── crosshair_tool_calls.jsonl     # OpenAI fine-tuning format (messages array)
└── finetune_sharegpt/
    └── crosshair_tool_calls_sharegpt.jsonl  # ShareGPT conversation format
```

## Tool-Calling Protocol

Crosshair AI uses a custom JSON protocol (NOT OpenAI function_calling):

```
Tool call:  {"type":"tool_call","tool":"TOOL_NAME","arguments":{...}}
Final answer: {"type":"final","message":"text response"}
```

### Critical Rules

1. **GDScript batch isolation**: `create_gdscript_file` and `update_gdscript_file` MUST be in their own `batch_tool_calls`, never mixed with other tools
2. **batch_tool_calls** supports `shared_arguments` merged into every call
3. **Physics floors**: StaticBody3D → add_primitive_mesh + add_collision_shape as children
4. **Playable characters**: CharacterBody3D + collision + input actions + mesh + script
5. **No markdown wrapping** of JSON responses
6. **set_node_property** position uses `{"property":"position","value":{"x":0,"y":5,"z":0}}`

## Tool Categories (65+ tools)

| Category | Tools | Count |
|----------|-------|-------|
| Context/Inspection | get_project_tree, find_project_files, read_project_file, get_project_settings, get_input_actions, get_open_scenes, get_current_scene, get_selected_nodes, get_scene_tree, get_node_details, get_node_api, file_exists, list_directory, grep_project_files, get_unsaved_scenes, get_autoloads, get_node_groups, get_global_classes, search_project_settings_keys, get_editor_settings, get_binary_file_metadata, get_editor_version | 21 |
| Scene Management | create_scene_file, open_scene, save_current_scene, save_all_scenes, reload_scene | 5 |
| Node Operations | add_node, remove_node, set_node_property, reparent_node, rename_node, duplicate_node, move_child, instantiate_scene | 8 |
| 3D Scene | add_primitive_mesh, add_collision_shape, create_light, create_camera_3d, create_character_body_3d, create_static_body_3d, create_rigid_body_3d, create_area_3d, create_ray_cast_3d, create_shape_cast_3d, add_csg_primitive, create_path_3d, create_vehicle_body_3d | 13 |
| 2D Scene | create_sprite_2d, create_animated_sprite_2d, create_character_body_2d, create_rigid_body_2d, create_area_2d, create_ray_cast_2d, create_line_2d, create_path_2d, create_polygon_2d, create_light_2d, add_2d_collision_shape | 11 |
| Scripting | create_gdscript_file, update_gdscript_file, attach_script, edit_script | 4 |
| Materials/Resources | create_standard_material, assign_resource_to_property | 2 |
| Signals | connect_signal, disconnect_signal | 2 |
| Input | create_input_action, set_input_action_bindings, remove_input_action | 3 |
| Animation | create_animation, add_animation_track, remove_animation_track, set_animation_track_key, create_blend_tree, add_animation_transition, set_animation_blend_amount, get_animation_player_state | 8 |
| Physics | set_collision_layer_mask, add_collision_exception, remove_collision_exception, get_collision_exceptions, set_physics_material, raycast_query, shape_cast_query, get_node_collision_layers, query_physics | 9 |
| TileMap | set_tilemap_cells, clear_tilemap_cells, get_tilemap_info, get_tileset_sources, create_tileset, add_tileset_atlas_source, set_tile_collision_polygon, paint_terrain | 8 |
| Navigation | bake_navigation_mesh, create_navigation_link, create_navigation_obstacle, get_navigation_path, get_navigation_region_info, set_navigation_agent_params | 6 |
| Multiplayer | create_multiplayer_spawner, create_multiplayer_synchronizer, get_network_state | 3 |
| UI/HUD | create_control_node, set_control_layout, set_control_theme_override, create_container_layout, create_scroll_container, create_progress_bar, create_slider, create_item_list, create_option_button, create_tab_container, create_graph_node, create_tree_widget | 12 |
| Resources | create_gradient, create_curve, create_stylebox, create_theme, create_font, create_texture_2d, create_noise_texture, create_atlas_texture, import_asset, set_import_setting, create_shader_material, set_shader_uniform, get_shader_uniforms | 13 |
| Audio | create_audio_player, play_audio, stop_audio, get_audio_buses, add_audio_bus_effect | 5 |
| Environment | create_sky, set_environment_fog, set_environment_tonemap, set_environment_ss_effects, create_fog_volume, create_reflection_probe, create_gi_probe, get_world_environment, set_world_environment | 9 |
| Debug | get_debug_snapshot, get_runtime_debugger_state, get_console_output, get_editor_log, inspect_runtime_variable, inspect_runtime_node, set_breakpoint, debugger_continue, profile_frame, monitor_runtime_performance | 10 |
| Scripting Intelligence | get_gdscript_symbols, get_gdscript_docs, get_class_reference, search_class_db, get_method_signature, get_enum_values, lint_gdscript, run_gdscript_test, run_gdscript_expression, get_gdscript_errors | 10 |
| Editor | set_editor_main_screen, select_file, editor_undo, manage_editor_plugins, run_scene_script, play_current_scene, play_main_scene, stop_playing_scene | 8 |
| File Ops | write_project_file, create_project_folder, delete_project_file, move_project_file, copy_project_file | 5 |
| Project Config | set_main_scene, manage_export_presets, export_project, add_custom_class, set_default_import_presets, patch_project_settings, manage_autoloads | 7 |
| Git | git_status, git_diff_file, git_log, git_branch | 4 |
| Vision | capture_editor_viewport, capture_game_viewport, capture_dual_view, capture_subviewport, capture_texture_resource | 5 |
| Workflow | batch_tool_calls, batch_set_node_property, batch_reparent_nodes | 3 |

## Fine-Tuning Formats

### OpenAI Format (`finetune_openai/`)
```json
{"messages": [
  {"role": "system", "content": "<system prompt>"},
  {"role": "user", "content": "Create a 3D level"},
  {"role": "assistant", "content": "{\"type\":\"tool_call\",\"tool\":\"create_scene_file\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"root_type\":\"Node3D\"}}"}
]}
```

### ShareGPT Format (`finetune_sharegpt/`)
```json
{"conversations": [
  {"from": "system", "value": "<system prompt>"},
  {"from": "human", "value": "Create a 3D level"},
  {"from": "gpt", "value": "{\"type\":\"tool_call\",\"tool\":\"create_scene_file\",\"arguments\":{\"scene_path\":\"res://levels/main.tscn\",\"root_type\":\"Node3D\"}}"}
]}
```

## Tool Implementation Status

- **Implemented**: All tools listed in `yeet_ai_tools.cpp` dispatch table (65+ tools)
- **Stub (returns error)**: batch_set_node_property, capture_dual_view, capture_subviewport, capture_texture_resource, copy_project_file, create_particle_emitter, create_ui_element, disconnect_signal, focus_scene_tree_node, get_editor_3d_camera_transform, get_editor_inspector_subject, get_export_presets, get_gdscript_errors, get_remote_scene_tree, get_resource_dependencies, get_scene_dependency_closure, get_shader_code, get_signal_connections, get_translation_overview, get_world_environment, manage_autoloads, patch_editor_settings, patch_project_settings, play_animation, query_physics, reimport_project_files, rename_resource_references, replace_in_project_files, resolve_resource_uid, run_gdscript_expression, save_resource, scan_project_filesystem, set_editor_3d_camera, set_node_collision_layers, set_world_environment, update_shader_code, validate_scene

## Source Code Reference

- Tool dispatch: `modules/yeet_ai/editor/yeet_ai_tools.cpp`
- Tool implementations: `modules/yeet_ai/editor/yeet_ai_tools_*.cpp`
- System prompt: `modules/yeet_ai/editor/yeet_ai_system_prompt.cpp`
- Dock (UI + streaming): `modules/yeet_ai/editor/yeet_ai_dock.cpp`
- Editor plugin: `modules/yeet_ai/editor/yeet_ai_editor_plugin.cpp`
