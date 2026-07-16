# Yeet AI 2D & UI Tool Roadmap

## Phase 1: Core Gameplay Loop (Essential)
**Goal: Build a complete playable 2D game from scratch with AI**

### 2D Nodes
| Tool | Priority | Why |
|------|----------|-----|
| `create_timer` | 🔴 Critical | Spawn delays, cooldowns, UI timeouts, turn-based timers |
| `create_tween` | 🔴 Critical | Smooth property animations (move, fade, scale, rotate) without AnimationPlayer |
| `create_path_follow_2d` | 🔴 Critical | Patrolling enemies, moving platforms, camera rails |
| `create_light_occluder_2d` | 🟡 High | Shadow casting for atmospheric lighting |
| `create_animation_player` | 🟡 High | Keyframe animations for sprites, UI, cameras |
| `create_animation_tree` | 🟡 High | State machine blending (idle→walk→jump transitions) |
| `create_joint_2d` | 🟡 High | Physics joints (pin, groove, damped spring) for chains, bridges |

### Query/Interaction Tools
| Tool | Priority | Why |
|------|----------|-----|
| `query_raycast_2d` | 🔴 Critical | Check what's under cursor, line-of-sight, shooting |
| `query_shapecast_2d` | 🟡 High | Ground detection, area sweep, hitbox overlap |
| `query_physics_2d` | 🟡 High | Ray/shape cast in one call with filters |
| `set_path_follow_progress` | 🟡 High | Move along path programmatically |

---

## Phase 2: UI System (Complete HUD/Menus)
**Goal: Build full game UI without manual node creation**

### Basic UI Nodes
| Tool | Priority | Why |
|------|----------|-----|
| `create_button` | 🔴 Critical | Interactive UI (menus, in-game buttons, inventory) |
| `create_label` | 🔴 Critical | Text display (score, health, dialog) |
| `create_texture_rect` | 🟡 High | Display images, icons, portraits |
| `create_nine_patch_rect` | 🟡 High | Scalable UI borders and backgrounds |
| `create_color_rect` | 🟡 High | Solid color backgrounds, health bars |
| `create_panel` / `create_panel_container` | 🟡 High | Grouped UI sections with styling |

### Input/Text Nodes
| Tool | Priority | Why |
|------|----------|-----|
| `create_line_edit` | 🟡 High | Name entry, search boxes, console input |
| `create_text_edit` | 🟢 Medium | Multi-line text, code editing, notes |
| `create_rich_text_label` | 🟢 Medium | Dialog with colors, bold, inline images |

### Layout Containers
| Tool | Priority | Why |
|------|----------|-----|
| `create_hbox_container` | 🟡 High | Horizontal layout (health bar + icon + text) |
| `create_vbox_container` | 🟡 High | Vertical layout (menu buttons stacked) |
| `create_grid_container` | 🟡 High | Inventory grid, skill trees |
| `create_margin_container` | 🟢 Medium | Padding around UI sections |
| `create_center_container` | 🟢 Medium | Center dialogs, pause menus |
| `create_aspect_ratio_container` | 🟢 Medium | Keep minimap or portrait aspect |

### UI State/Interaction
| Tool | Priority | Why |
|------|----------|-----|
| `set_button_properties` | 🟡 High | Text, icon, disabled state, toggle mode |
| `set_label_properties` | 🟡 High | Text, alignment, autowrap, clip |
| `set_progress_bar_value` | 🟡 High | Health bars, loading bars, XP bars |
| `set_slider_value` | 🟢 Medium | Volume controls, settings |
| `set_container_theme` | 🟢 Medium | Apply theme to entire UI branch |

---

## Phase 3: Visual Polish & Effects
**Goal: Make games look professional**

### Rendering
| Tool | Priority | Why |
|------|----------|-----|
| `create_shader_material_2d` | 🟡 High | Sprite shaders (hit flash, dissolve, water) |
| `set_canvas_item_material` | 🟡 High | Apply shader to any 2D node |
| `create_world_environment_2d` | 🟢 Medium | Glow, background color, canvas max layers |
| `create_sub_viewport` | 🟢 Medium | Render-to-texture (minimaps, split screen) |
| `create_back_buffer_copy` | 🟢 Medium | Post-processing effects |

### Particles & Effects
| Tool | Priority | Why |
|------|----------|-----|
| `set_particles_2d_properties` | 🟡 High | Configure emission, lifetime, color, trails |
| `create_multi_mesh_2d` | 🟢 Medium | Grass, crowds, asteroids efficiently |

---

## Phase 4: AI & Logic Systems
**Goal: Smart NPCs and game mechanics**

### State Management
| Tool | Priority | Why |
|------|----------|-----|
| `create_state_machine_2d` | 🟡 High | Enemy AI states (patrol→chase→attack) |
| `set_state_transitions` | 🟡 High | Define transitions between states |
| `trigger_state_change` | 🟡 High | Force state from script/GDScript |

### Navigation (expand existing)
| Tool | Priority | Why |
|------|----------|-----|
| `set_navigation_agent_target` | 🟡 High | Move NPC to destination |
| `get_navigation_path_2d` | 🟡 High | Get path for manual following |
| `bake_navigation_region_2d` | 🟢 Medium | Rebuild navmesh after terrain changes |

---

## Phase 5: Audio & Polish
**Goal: Professional audio integration**

| Tool | Priority | Why |
|------|----------|-----|
| `set_audio_stream_properties` | 🟡 High | Volume, pitch, loop, bus routing |
| `create_audio_bus` | 🟢 Medium | Separate SFX/music/voice channels |
| `add_audio_bus_effect` | 🟢 Medium | Reverb, EQ, compression |
| `play_audio_one_shot` | 🟡 High | Fire-and-forget sound effects |

---

## Implementation Priority Summary

### Do First (Biggest Impact)
1. **`create_timer`** — Unblocks every game mechanic
2. **`create_tween`** — Replaces 90% of simple AnimationPlayer use
3. **`create_button`** + **`create_label`** — Unblocks UI construction
4. **`create_path_follow_2d`** — Unlocks movement patterns
5. **`query_raycast_2d`** / **`query_shapecast_2d`** — Enables interaction

### Do Second
6. **`create_animation_player`** + **`create_animation_tree`** — Full animation system
7. **`create_hbox/vbox/grid_container`** — Proper UI layouts
8. **`create_light_occluder_2d`** — Visual polish
9. **`set_button_properties`** + **`set_label_properties`** — Interactive UI
10. **`create_shader_material_2d`** — Visual effects

### Do Third
11. **`create_line_edit`** / **`create_text_edit`** — Input-heavy games
12. **`create_state_machine_2d`** — Complex AI
13. **`create_sub_viewport`** — Advanced rendering
14. **`set_progress_bar_value`** — HUD updates
15. **`create_joint_2d`** — Physics puzzles

---

## New Features (Not Tools)

### Smart Defaults
- When `create_character_body_2d` is called, auto-suggest adding `Sprite2D` + `CollisionShape2D`
- When `create_button` is called, auto-connect a pressed signal stub

### Template Workflows
- `build_platformer_character` — One tool that creates CharacterBody2D + Sprite2D + CollisionShape2D + AnimationPlayer + input actions
- `build_top_down_character` — Same but for top-down movement
- `build_hud` — CanvasLayer + health bar + score + minimap container
- `build_main_menu` — VBoxContainer + title + buttons + background

### Scene Templates
- `create_game_scene_2d` — Camera2D + CanvasLayer + WorldEnvironment + TileMap + Player spawn point
- `create_level_scene_2d` — TileMap + parallax backgrounds + boundaries

---

## Recommended Next Action

Implement **Phase 1** (Timer, Tween, PathFollow2D, RayCast query) — these 4 tools unlock:
- Timed game mechanics (waves, cooldowns)
- Smooth animations without code
- Enemy patrol routes
- Shooting / interaction systems

This gives the AI enough building blocks to construct a complete 2D game with minimal tool calls.
