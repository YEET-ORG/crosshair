# YEET AI — Comprehensive Development Prompt for Claude Code
Last verified: 2026-06-30


> Use this prompt when asking Claude Code to work on the Crosshair Engine's `yeet_ai` module. It contains full context on architecture, patterns, current state, and prioritized work items.

---

## 1. Project Overview

**Crosshair Engine** is a Godot 4.3+ fork with an AI-native editor module (`modules/yeet_ai/`). The `yeet_ai` module adds a dock panel to the editor that lets developers chat with LLMs (OpenAI, Azure, Gemini, OpenRouter, Ollama) and invoke **250+ editor tools** via structured JSON.

**Key design principle:** The LLM never edits files directly. It requests tool calls (e.g., `create_sprite_2d`, `update_gdscript_file`, `set_node_property`) which the engine executes atomically with full undo support and validation.

**GitHub:** `https://github.com/YEET-ORG/crosshair` — branch `v0`

---

## 2. Architecture

### 2.1 File Organization
```
modules/yeet_ai/
├── SCsub                              # Build config — auto-picks up editor/*.cpp
├── editor/
│   ├── yeet_ai_dock.h / .cpp          # Main dock widget, chat UI, request building, tool dispatch
│   ├── yeet_ai_streaming.cpp          # HTTPClient-based SSE streaming with retry logic
│   ├── yeet_ai_system_prompt.cpp      # System prompt builders (compact + modular modes)
│   ├── yeet_ai_tool_schema.h/.cpp     # JSON Schema registry for native OpenAI tools
│   ├── yeet_ai_response_parser.cpp    # Structured response parsing (JSON-in-text fallback)
│   ├── yeet_ai_settings_panel.cpp     # Editor Settings UI for all provider configs
│   ├── yeet_ai_editor_plugin.cpp      # EditorPlugin registration, setting hints
│   ├── yeet_ai_helpers.cpp            # Shared helpers (_arg_string, _resolve_scene, etc.)
│   ├── yeet_ai_tools.cpp              # Tool dispatch table (~250 entries)
│   ├── yeet_ai_tools_*.cpp            # Tool implementations by category:
│   │   ├── _scene2d.cpp               # 2D nodes (Sprite2D, CharacterBody2D, TileMap, etc.)
│   │   ├── _scene3d.cpp               # 3D nodes (MeshInstance3D, lights, CSG, etc.)
│   │   ├── _ui.cpp                    # UI nodes (Button, Label, containers, dialogs)
│   │   ├── _shader_audio_ui.cpp       # Shaders, audio buses, generic UI
│   │   ├── _animation.cpp             # AnimationPlayer, AnimationTree, tracks
│   │   ├── _tilemap_physics.cpp       # TileMap/TileSet manipulation
│   │   ├── _inspect.cpp               # Scene inspection, viewport capture
│   │   ├── _scripting.cpp             # GDScript file create/update/run
│   │   ├── _file_ops.cpp              # File read/write/delete/list
│   │   ├── _node_create.cpp           # Generic node creation, primitive mesh
│   │   ├── _node_ops.cpp              # Reparent, duplicate, rename, remove
│   │   ├── _signal_project.cpp        # Signal connect/disconnect, input actions
│   │   ├── _capture.cpp               # Editor/game viewport screenshot
│   │   ├── _batch_capture.cpp         # Batch screenshot operations
│   │   ├── _editor_project.cpp        # Editor settings, export, project settings
│   │   ├── _resource_nav_debug.cpp    # Resources, navigation, debugger
│   │   ├── _multiplayer.cpp           # MultiplayerSpawner, Synchronizer
│   │   ├── _stubs.cpp                 # Placeholder tools not yet implemented
│   │   └── _game_physics.cpp          # Game actor creation, physics audit/repair
```

### 2.2 Tool Implementation Pattern
Every tool follows this exact pattern:

```cpp
Dictionary YeetAIDock::_tool_create_sprite_2d(const Dictionary &p_args) const {
    Dictionary result;
    String err;
    Node *scene_root;
    if (!_resolve_scene(p_args, &scene_root, err)) {
        return _make_error(err);
    }
    const String node_name = _arg_string(p_args, "name", "Sprite2D");

    Sprite2D *sprite = memnew(Sprite2D);
    sprite->set_name(node_name);

    const String texture_path = _arg_string(p_args, "texture_path", "");
    if (!texture_path.is_empty()) {
        Ref<Texture2D> tex = ResourceLoader::load(texture_path);
        if (tex.is_valid()) {
            sprite->set_texture(tex);
        }
    }

    Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
    if (parent == nullptr) {
        parent = scene_root;
    }
    _add_to_scene(parent, sprite, scene_root);
    sprite->set_position(_arg_vector2(p_args, "position", Vector2()));

    _mark_unsaved();
    result["ok"] = true;
    result["node_path"] = String(sprite->get_path());
    return result;
}
```

**Rules:**
- Return `_make_error("message")` on any failure.
- Return `result["ok"] = true` + useful data on success.
- Always call `_mark_unsaved()` after mutations.
- Use `_arg_string`, `_arg_int`, `_arg_float`, `_arg_bool`, `_arg_vector2`, `_arg_vector3`, `_arg_color`, `_arg_dict`, `_arg_array` for arg extraction.
- Use `_resolve_scene()` to get the current editable scene root.
- Use `_resolve_node_target()` to find a node by path within a scene.
- Use `_add_to_scene(parent, child, owner)` to properly set ownership for saved scenes.
- For UI tools, call `_apply_anchor_preset_arg(control, p_args)` after positioning if the control type supports it.

### 2.3 Adding a New Tool (3-step process)
1. **Declare** in `yeet_ai_dock.h` under the appropriate section comment.
2. **Implement** in the appropriate `yeet_ai_tools_*.cpp` file.
3. **Register** in `yeet_ai_tools.cpp` dispatch table AND `yeet_ai_get_all_tool_names()`.

### 2.4 Schema Registration
For tools to work with native OpenAI `tools` parameter, they need JSON Schemas. Two options:
- **Explicit:** Add to `yeet_ai_tool_schema.cpp` in the big schema array.
- **Inferred:** The `get_schema()` function auto-generates schemas based on tool name patterns (`create_*`, `set_*`, `get_*`, etc.) with thread-local caching.

If a tool has complex args beyond the standard inferred patterns, add it explicitly.

### 2.5 Native Tools vs Fallback Mode
- **Native tools enabled** (default for providers 1-4): Uses OpenAI `tools` array with `tool_choice: "auto"`. System prompt is compact (~2K chars). Supports streaming `delta.tool_calls`.
- **Fallback mode** (default for provider 0/Berry): LLM emits JSON-in-text. System prompt is modular (~15K chars with full tool docs).
- Toggle: `yeet_ai/chat/native_tools_enabled` (bool setting).

---

## 3. Current State — What's Working

### 3.1 Solid
- ✅ SSE streaming with `delta.tool_calls` accumulation and mutex protection
- ✅ Retry logic with exponential backoff for HTTP 429/502/503/504
- ✅ Auto-schema inference for 250+ tools
- ✅ Azure OpenAI provider (provider 4) with full settings UI
- ✅ Token budget pruning (auto-trims old conversation messages)
- ✅ Chat session persistence (auto-saves after every message)
- ✅ Keyboard shortcuts (Esc, Ctrl+L, Ctrl+Shift+N, Ctrl+Enter, Alt+Up/Down)
- ✅ Markdown→BBCode formatting in chat log
- ✅ Per-tool execution timing display
- ✅ Batch tool execution progress UI
- ✅ 2D scene tools: Sprite2D, AnimatedSprite2D, CharacterBody2D, StaticBody2D, RigidBody2D, Area2D, RayCast2D, ShapeCast2D, Camera2D, TileMap, CPUParticles2D, GPUParticles2D, Path2D, Line2D, Polygon2D, CollisionPolygon2D, Marker2D, CanvasLayer, ParallaxBackground, ParallaxLayer, Timer, Tween, PathFollow2D, etc.
- ✅ UI tools: Button, Label, TextureRect, NinePatchRect, ColorRect, RichTextLabel, TextureProgressBar, LineEdit, TextEdit, CheckBox, SpinBox, PanelContainer, AcceptDialog, ConfirmationDialog, HSeparator, VSeparator, AspectRatioContainer, SubViewportContainer, MarginContainer, ReferenceRect
- ✅ Signal wiring: `connect_signal` (generic) and `connect_ui_signal` (UI-specific with auto-mapping)
- ✅ Smart anchor presets on all UI creation tools

### 3.2 Partially Working / Needs Polish
- ⚠️ Theme access during early initialization (we added `is_inside_tree()` guards but there may be more edge cases)
- ⚠️ Settings panel signal connections (recently fixed — verify no regressions)
- ⚠️ `open_scene` has retry logic but could be more robust
- ⚠️ Game physics audit tools exist but need more validation rules
- ⚠️ Streaming tool call validation can fail on malformed partial JSON

### 3.3 Known Broken / Missing
- ❌ **No UndoRedo integration.** Tools mutate scenes directly. There's no `UndoRedo::get_singleton()->create_action()` wrapping. This is a major gap.
- ❌ **Scene dock doesn't auto-refresh** after `_add_to_scene` in some cases. Need `EditorInterface::get_singleton()->get_selection()->clear()` or `EditorInterface::get_singleton()->edit_node()` calls.
- ❌ **Inspector doesn't update** after `_tool_set_node_property`. Need `notify_property_list_changed()` or inspector refresh.
- ❌ **FileSystem dock doesn't refresh** after file creation. Need `EditorFileSystem::get_singleton()->scan()` or `EditorFileSystem::get_singleton()->scan_changes()`.
- ❌ **Many stub tools** in `yeet_ai_tools_stubs.cpp` return `_make_error("Not implemented yet")`.
- ❌ **No automated tests** for tool execution. We rely on manual testing.
- ❌ **3D workflows are thin.** We have basic node creation but no advanced 3D tooling (procedural mesh generation, advanced material setups, lighting rigs, etc.).
- ❌ **Animation workflows are basic.** We can create AnimationPlayers and add tracks, but no keyframe manipulation, no animation blending setup, no state machine authoring.
- ❌ **Shader authoring is minimal.** `get_shader_code` and `update_shader_code` exist but are crude.
- ❌ **No GDScript linting integration.** `_tool_lint_gdscript` is a stub.
- ❌ **No project template system.** Can't scaffold a full game project from a prompt.
- ❌ **No version control integration beyond git status.** Can't commit, branch, or diff meaningfully.
- ❌ **No multiplayer testing tools.** Can't simulate clients or test RPCs.
- ❌ **Performance:** Large scene trees (>300 nodes) can freeze the editor during `_tool_get_scene_tree`.

---

## 4. Priority Work Items

### P0 — Critical (Do First)

#### 4.1 UndoRedo Integration
**Every mutating tool must support undo.**

Add an `UndoRedo *_undo_redo` member to `YeetAIDock` (or use `EditorUndoRedoManager`). Wrap all scene mutations:

```cpp
// Pattern for node creation
UndoRedo *ur = EditorUndoRedoManager::get_singleton();
ur->create_action("Create Sprite2D");
ur->add_do_method(this, "_do_add_node", parent_path, node_type, properties);
ur->add_undo_method(this, "_do_remove_node", node_path);
ur->commit_action();
```

Start with these high-impact tools:
- `_tool_add_node`
- `_tool_remove_node`
- `_tool_reparent_node`
- `_tool_set_node_property`
- `_tool_create_gdscript_file`
- `_tool_update_gdscript_file`
- `_tool_write_project_file`

#### 4.2 Editor Refresh After Mutations
After any tool that modifies:
- **Scene tree:** Call `EditorInterface::get_singleton()->get_selection()->clear()` and potentially `EditorInterface::get_singleton()->edit_node(new_node)`
- **FileSystem:** Call `EditorFileSystem::get_singleton()->scan_changes()` after file creation/deletion
- **Inspector:** Call `p_node->notify_property_list_changed()` after property changes
- **Script editor:** Call `EditorInterface::get_singleton()->edit_script(ref)` after script updates

Add a helper: `void _refresh_editor_after_tool(const String &p_tool_name, const Dictionary &p_result)`

#### 4.3 Complete All Stubs
`yeet_ai_tools_stubs.cpp` has ~40 stub tools. Implement the most commonly requested:
- `_tool_lint_gdscript`
- `_tool_run_gdscript_test`
- `_tool_get_method_signature`
- `_tool_search_class_db`
- `_tool_merge_scenes`
- `_tool_replace_node_with_scene`
- `_tool_import_asset`
- `_tool_set_import_setting`
- `_tool_manage_editor_plugins`
- `_tool_profile_frame`

### P1 — High Impact

#### 4.4 2D Game Workflow Toolkit
Create a cohesive set of tools specifically for 2D game development that work together:

**A. Character Controller Template**
- `_tool_create_platformer_character_2d(args)` — Creates a complete CharacterBody2D with:
  - CollisionShape2D (capsule or rectangle)
  - AnimatedSprite2D or Sprite2D
  - Camera2D (optional)
  - Attached GDScript with `_physics_process` implementing:
    - Gravity, jump, coyote time, jump buffering
    - WASD / arrow key movement
    - Sprite flipping based on direction
  - Input actions: `move_left`, `move_right`, `jump`

**B. Enemy / NPC Template**
- `_tool_create_patrol_enemy_2d(args)` — CharacterBody2D with:
  - Patrol path (using Path2D + PathFollow2D or simple A↔B)
  - RayCast2D for edge detection
  - Area2D for player detection (aggro range)
  - Basic state machine script (idle, patrol, chase)

**C. Interactive Object Templates**
- `_tool_create_collectible_2d(args)` — Area2D with bobbing animation, sound on pickup
- `_tool_create_moving_platform_2d(args)` — AnimatableBody2D with path movement
- `_tool_create_one_way_platform_2d(args)` — StaticBody2D with one-way collision
- `_tool_create_hazard_2d(args)` — Area2D that damages player on contact
- `_tool_create_trigger_zone_2d(args)` — Area2D for level events (spawn, win, cutscene)

**D. Level Building Helpers**
- `_tool_create_tilemap_from_image(args)` — Auto-generate TileMap from a reference image
- `_tool_place_tile_rectangle(args)` — Fill a rect with a specific tile
- `_tool_create_parallax_background_from_layers(args)` — Multi-layer parallax setup
- `_tool_add_scene_decorations(args)` — Scatter sprites/elements along a path or area

**E. UI/HUD Templates**
- `_tool_create_game_hud(args)` — Complete HUD with:
  - Health bar (TextureProgressBar)
  - Score label
  - Pause button
  - Minimap (SubViewport)
  - Anchored to screen corners using anchor_preset
- `_tool_create_main_menu(args)` — Title, buttons, options, credits
- `_tool_create_pause_menu(args)` — Overlay with resume, restart, quit

#### 4.5 3D Game Workflow Toolkit
Mirror the 2D toolkit for 3D:
- `_tool_create_fps_character_3d(args)` — CharacterBody3D + Camera3D + collision
- `_tool_create_third_person_character_3d(args)` — With spring-arm camera rig
- `_tool_create_3d_platform(args)` — StaticBody3D + CSG or mesh
- `_tool_create_3d_collectible(args)` — RigidBody3D or Area3D with spin animation
- `_tool_create_3d_trigger(args)` — Area3D for level transitions
- `_tool_create_lighting_rig(args)` — DirectionalLight3D + WorldEnvironment setup
- `_tool_create_3d_terrain(args)` — HeightMapShape3D or GridMap-based terrain

#### 4.6 Animation Workflow Enhancement
- `_tool_create_animation_keyframe(args)` — Add a keyframe at a specific time
- `_tool_delete_animation_keyframe(args)` — Remove a keyframe
- `_tool_set_animation_loop(args)` — Toggle loop mode, set loop points
- `_tool_create_animation_blend_tree_2d(args)` — Build a 2D blend space for character locomotion
- `_tool_create_animation_state_machine(args)` — Build an AnimationNodeStateMachine with transitions
- `_tool_set_animation_callback(args)` — Add method call track keyframes
- `_tool_bake_animation(args)` — Bake procedural animation to keyframes

#### 4.7 Shader Workflow
- `_tool_create_visual_shader(args)` — Create a VisualShader graph programmatically
- `_tool_add_visual_shader_node(args)` — Add nodes to an existing VisualShader
- `_tool_create_canvas_item_shader(args)` — 2D shader with common effects (outline, glow, dissolve)
- `_tool_create_sky_shader(args)` — Procedural sky shader
- `_tool_create_post_process_shader(args)` — Screen-space effect via CompositorEffect

### P2 — Medium Priority

#### 4.8 Performance Optimization
- **Scene tree serialization:** `_serialize_node` is recursive and slow. Add:
  - Caching for unchanged scenes
  - Lazy property collection
  - Configurable max nodes (currently 300 hardcoded)
- **Context building:** `_build_runtime_context_prompt` rebuilds everything every request. Cache:
  - Project tree (invalidate on file changes)
  - Scene tree (invalidate on scene changes)
  - Input actions (invalidate on project settings changes)
- **Image encoding:** `_encode_viewport_image_for_vision` base64 encodes every time. Cache recent captures.

#### 4.9 Error Recovery & Robustness
- **Tool call retry with repair:** If `_tool_set_node_property` fails because the node was deleted, auto-retry after re-resolving the node.
- **Batch rollback:** If one tool in a `batch_tool_calls` fails, provide an option to undo all previous tools in the batch.
- **Schema validation feedback:** When native tool schema validation fails, return the specific validation error to the LLM so it can correct the call.
- **Graceful degradation:** If `get_scene_tree` returns too many nodes, automatically switch to a truncated mode instead of failing.

#### 4.10 Testing Infrastructure
- **Tool unit tests:** Create a test harness in `tests/yeet_ai/` that:
  - Creates a temporary scene
  - Executes a tool
  - Asserts on the result Dictionary
  - Cleans up
- **Schema validation tests:** Verify every registered tool has a valid schema (explicit or inferred).
- **Integration tests:** End-to-end tests that mock HTTP responses and verify tool chains.

### P3 — Nice to Have

#### 4.11 Advanced Features
- **Project scaffolding:** `_tool_scaffold_project(args)` — Create a complete project structure from a template (platformer, RPG, FPS, etc.)
- **Asset pipeline:** Integration with asset stores or local asset libraries for drag-and-drop placement
- **AI-assisted debugging:** When a GDScript error occurs, automatically capture the error context and offer the LLM a chance to fix it
- **Multiplayer simulation:** `_tool_simulate_client(args)` — Run a headless client instance for testing multiplayer logic
- **CI/CD integration:** Export presets that trigger GitHub Actions or similar
- **Localization workflow:** `_tool_extract_translations(args)` — Scan project for `tr()` calls and generate POT files

---

## 5. Code Style & Constraints

### 5.1 Naming
- Tools: `_tool_<verb>_<noun>` (e.g., `_tool_create_sprite_2d`)
- Private helpers: `_<verb>_<noun>`
- Static helpers: no prefix, in anonymous namespace if file-local
- Arg keys: `snake_case` in schemas, matching Godot property names where possible

### 5.2 Error Handling
- Use `_make_error("message")` for all failures.
- Error messages should be actionable: `"Node 'Player' not found in scene 'res://levels/level_1.tscn'. Available nodes: [Enemy, Coin, Ground]"`
- Never crash the editor. Always validate pointers before dereferencing.

### 5.3 File Size
- Keep files under 500 lines where possible.
- If a category grows beyond that, split it (e.g., `yeet_ai_tools_ui.cpp` was split from the main file).

### 5.4 Godot Version
- Target Godot 4.3+ API.
- Never use deprecated APIs (e.g., no `set_sync_to_physics`, no `set_follow_viewport_enabled`).
- Use `SceneStringName(...)` and `EditorStringName(...)` for string constants where available.

### 5.5 Build & Test
- Build with: `py -m SCons -j12`
- After every change, verify build succeeds.
- Run the engine and test at least one tool path manually.

---

## 6. Communication Patterns

When working on this codebase, use these patterns:

### For new tool implementation:
```
1. Read the header to find the declaration section
2. Add declaration under appropriate comment block
3. Create/modify the implementation .cpp file
4. Register in yeet_ai_tools.cpp dispatch table
5. Register in yeet_ai_get_all_tool_names()
6. Add explicit schema in yeet_ai_tool_schema.cpp if args are complex
7. Build and test
```

### For bug fixes:
```
1. Identify the specific tool or system
2. Read the implementation
3. Apply minimal fix
4. Verify no regressions in related tools
5. Build and test
```

### For architecture changes:
```
1. Propose the change in a comment first
2. Update the header
3. Update all call sites
4. Build and fix compilation errors iteratively
```

---

## 7. Example: Implementing a New 2D Tool

**Goal:** Add `_tool_create_bouncy_ball_2d` — a RigidBody2D ball with CircleShape2D, Sprite2D, and bouncy physics material.

**Step 1:** Add to `yeet_ai_dock.h` under `// ── B. 2D Scene Construction ──────────────────────────────────────────────`:
```cpp
Dictionary _tool_create_bouncy_ball_2d(const Dictionary &p_args) const;
```

**Step 2:** Add to `yeet_ai_tools_scene2d.cpp`:
```cpp
Dictionary YeetAIDock::_tool_create_bouncy_ball_2d(const Dictionary &p_args) const {
    Dictionary result;
    String err;
    Node *scene_root;
    if (!_resolve_scene(p_args, &scene_root, err)) {
        return _make_error(err);
    }

    const String node_name = _arg_string(p_args, "name", "BouncyBall");
    const float radius = _arg_float(p_args, "radius", 16.0);
    const float bounce = _arg_float(p_args, "bounce", 0.7);
    const float friction = _arg_float(p_args, "friction", 0.3);

    RigidBody2D *body = memnew(RigidBody2D);
    body->set_name(node_name);

    CircleShape2D *shape = memnew(CircleShape2D);
    shape->set_radius(radius);
    CollisionShape2D *collision = memnew(CollisionShape2D);
    collision->set_shape(shape);
    body->add_child(collision);
    collision->set_owner(scene_root);

    Ref<PhysicsMaterial> phys_mat;
    phys_mat.instantiate();
    phys_mat->set_bounce(bounce);
    phys_mat->set_friction(friction);
    body->set_physics_material_override(phys_mat);

    Sprite2D *sprite = memnew(Sprite2D);
    body->add_child(sprite);
    sprite->set_owner(scene_root);
    // Optional: set texture, scale sprite to match radius

    Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
    if (parent == nullptr) {
        parent = scene_root;
    }
    _add_to_scene(parent, body, scene_root);
    body->set_position(_arg_vector2(p_args, "position", Vector2()));

    _mark_unsaved();
    result["ok"] = true;
    result["node_path"] = String(body->get_path());
    result["radius"] = radius;
    return result;
}
```

**Step 3:** Register in `yeet_ai_tools.cpp`:
```cpp
{ "create_bouncy_ball_2d", &YeetAIDock::_tool_create_bouncy_ball_2d },
```

**Step 4:** Add to `yeet_ai_get_all_tool_names()`:
```cpp
"create_bouncy_ball_2d",
```

**Step 5:** Build: `py -m SCons -j12`

**Step 6:** Test by asking the AI: "Create a bouncy ball at position (100, 100) with radius 32"

---

## 8. Debugging Tips

- **Enable debug mode:** Set `yeet_ai/chat/debug_mode = true` in Editor Settings to see full request/response bodies in the output log.
- **Check tool schemas:** Run `check_schemas.py` (if available) to validate all registered tools have schemas.
- **Stream errors:** If you see "Streaming error: HTTP 429", the retry logic should handle it. If it persists, check your Azure/OpenAI quota.
- **Missing tools in LLM calls:** If the LLM doesn't use a tool you just added, verify it's in `yeet_ai_get_all_tool_names()` AND the schema registry.
- **Compilation errors:** The most common are undeclared identifiers (missing header includes) or const-correctness issues with tool handlers.

---

## 9. Context for This Session

The current branch is `v0`. Recent commits include:
- Native OpenAI tools parameter support with streaming `delta.tool_calls`
- Azure OpenAI provider
- 8 new UI tools + smart anchor presets
- `connect_ui_signal` for simplified UI wiring
- Theme access guards to prevent early initialization warnings
- Retry logic for HTTP 429 rate limits
- `max_tokens=0` for server default

The most impactful next work is **UndoRedo integration** and **editor refresh after mutations**, followed by **2D game workflow templates**.

---

*End of prompt. Copy this entire document and paste it into Claude Code's context when starting work.*
