# Core Engine Changes Needed for Crosshair AI

Last verified: 2026-06-30

> This document catalogs architectural improvements needed in the Godot 4 editor core (not just the yeet_ai module) to make AI-assisted development truly first-class.

---

## 1. UndoRedo Integration (Critical)

**Problem:** The `yeet_ai` module currently has zero UndoRedo support. When the AI creates a node, changes a property, or writes a file, there is no way for the user to undo that action with Ctrl+Z. This breaks the fundamental contract of the Godot editor.

**Current workaround:** None. The AI dock calls `_mark_unsaved()` but that only dirties the scene, it doesn't create undo history.

**What needs to change:**

### 1A. Module-level: Wrap every mutating tool
Every tool that modifies the editor state should use `EditorUndoRedoManager`:

```cpp
// Pattern for node creation
EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
ur->create_action("AI: Create Sprite2D");
ur->add_do_method(parent, "add_child", sprite);
ur->add_do_method(sprite, "set_owner", scene_root);
ur->add_undo_method(parent, "remove_child", sprite);
ur->add_do_reference(sprite); // Prevents memleak if undo never happens
ur->commit_action();
```

**Complexity:** HIGH. There are ~120 mutating tools across 15 files. Each needs custom do/undo pairs.

### 1B. Core-level: Batch undo for tool chains
When the AI executes 5 tools in a row, the user should be able to undo all 5 as a single "AI action" or individually. Godot's UndoRedo doesn't natively support action groups.

**Proposed core change:** Add `UndoRedo::begin_merge_group(String p_name)` / `end_merge_group()` to the editor core, allowing multiple actions to be collapsed into a single undo step or expanded on demand.

---

## 2. Editor Auto-Refresh After External/Programmatic Changes (Critical)

**Problem:** When `yeet_ai` tools create nodes, modify properties, or write files, the editor UI often doesn't refresh. The Scene dock shows stale data, the Inspector doesn't update, the FileSystem dock doesn't show new files, and the script editor doesn't reload changed scripts.

**Current workarounds:** The user must manually click around or save/reload the scene.

**What needs to change:**

### 2A. Scene Tree Dock Refresh
After adding/removing/reparenting nodes, the Scene dock needs to refresh its tree view.

**Current Godot limitation:** `EditorInterface::get_singleton()->get_selection()->clear()` works but is a blunt instrument. There's no `refresh_scene_tree_dock()` API.

**Proposed core change:** Expose `SceneTreeDock::instance()->refresh_tree()` or add `EditorInterface::refresh_scene_tree()`.

### 2B. Inspector Refresh
After `set_node_property`, the Inspector panel doesn't update.

**Current workaround:** `node->notify_property_list_changed()` sometimes works but often doesn't refresh existing property values.

**Proposed core change:** Add `EditorInterface::refresh_inspector_for_node(Node *p_node)` that forces a full property re-read.

### 2C. FileSystem Dock Refresh
After file creation/deletion, the FileSystem dock is stale.

**Current workaround:** `EditorFileSystem::get_singleton()->scan_changes()` is async and slow.

**Proposed core change:** Add `EditorFileSystem::notify_file_changed(String p_path)` for single-file incremental updates without a full scan.

### 2D. Script Editor Reload
After `update_gdscript_file`, the script editor shows stale content.

**Current workaround:** `EditorInterface::get_singleton()->edit_script(ref)` works but resets cursor position.

**Proposed core change:** Add `ScriptEditor::reload_script_keep_state(Ref<Script> p_script)` that reloads from disk without losing cursor position, breakpoints, or bookmarks.

---

## 3. Safe Async Tool Execution (High Priority)

**Problem:** All tool execution currently happens on the main thread. If a tool is slow (e.g., baking a navigation mesh, exporting a project), the entire editor freezes.

**What needs to change:**

### 3A. Thread-safe tool dispatch
Move tool execution to a worker thread with proper synchronization:

```cpp
// Proposed API
class EditorToolWorker : public RefCounted {
    void execute_async(String p_tool_name, Dictionary p_args, Callable p_callback);
    void cancel();
};
```

### 3B. Thread-safe scene tree access
Godot's SceneTree is NOT thread-safe. Any tool that modifies nodes must either:
- Execute on the main thread (current approach — blocks)
- Or use deferred calls (safe but limited)

**Proposed core change:** Add a `SceneTree::queue_node_mutation(Callable p_callable)` that batches mutations and applies them at the end of the frame, allowing worker threads to queue changes safely.

---

## 4. Context-Aware Tool Validation (High Priority)

**Problem:** The AI often calls tools with invalid arguments because it doesn't have full context. For example:
- Setting a property that doesn't exist on a node
- Connecting a signal to a method that doesn't exist
- Creating a node with a name that already exists
- Referencing a scene path that doesn't exist

**Current workaround:** Schema validation catches some issues, but runtime validation is ad-hoc per tool.

**What needs to change:**

### 4A. Declarative tool validation framework
Instead of each tool doing its own validation, define constraints declaratively:

```cpp
struct ToolConstraint {
    String arg_name;
    enum Type { EXISTS, UNIQUE, VALID_PATH, HAS_METHOD, HAS_SIGNAL };
    Type type;
    String context_arg; // e.g., "node_path" for "HAS_METHOD" checks
};
```

The engine pre-validates all constraints before executing any tool, returning a structured error the LLM can understand.

### 4B. Live context introspection API
Add editor APIs that the module can query:
- `EditorInterface::get_all_node_names_in_scene(String p_scene_path)` → Array
- `EditorInterface::get_all_properties(String p_node_path)` → Dictionary
- `EditorInterface::get_all_signals(String p_node_path)` → Array
- `EditorInterface::get_all_methods(String p_script_path)` → Array

These would let the LLM discover valid values instead of guessing.

---

## 5. Performance: Scene Tree Serialization (Medium Priority)

**Problem:** `_tool_get_scene_tree` recursively serializes the entire scene tree every time the AI sends a message. For scenes with 300+ nodes, this takes 100-500ms and freezes the editor.

**Current workaround:** Hard limit of 300 nodes, truncation at depth 4.

**What needs to change:**

### 5A. Incremental scene tree caching
Cache the serialized scene tree and only invalidate nodes that changed:

```cpp
// Proposed core change: Node change tracking
class Node : public Object {
    uint64_t get_structure_version() const; // Increments on add_child, remove_child, reparent
    uint64_t get_property_version() const;  // Increments on set() for exported properties
};
```

The module would cache per-node serialized dictionaries and only re-serialize nodes whose version changed.

### 5B. Lazy property collection
Don't collect all properties upfront. Only collect:
- Name, type, path
- Script reference
- Position/transform
- The properties the AI explicitly asks for

**Proposed core change:** Add `Node::get_property_names_filtered(PackedStringArray p_include, PackedStringArray p_exclude)` for efficient filtering.

---

## 6. Theme System Robustness (Medium Priority)

**Problem:** `get_theme_color()` / `get_theme_font_size()` crash or warn when called before the dock enters the scene tree or during theme changes.

**Current workaround:** We added `is_inside_tree()` guards everywhere, but this is fragile.

**What needs to change:**

### 6A. Safe theme access API
Add a `Control::has_valid_theme()` method that returns true only when theme resources are fully loaded and accessible.

### 6B. Theme change batching
When the editor theme changes, dozens of widgets update simultaneously. Add a `NOTIFICATION_THEME_ABOUT_TO_CHANGE` / `NOTIFICATION_THEME_CHANGED` pair so widgets can batch their updates instead of reacting individually.

---

## 7. File Watcher Integration (Medium Priority)

**Problem:** When the AI writes a file (e.g., `create_gdscript_file`), the editor doesn't know about it until the user manually triggers a scan or saves the scene.

**What needs to change:**

### 7A. EditorFileSystem event emission
Currently `EditorFileSystem` scans on a timer. Add OS-level file watching (using `FileAccess` with watch capability or OS APIs):

```cpp
// Proposed API
EditorFileSystem::watch_directory(String p_dir);
EditorFileSystem::unwatch_directory(String p_dir);
```

When a watched file changes, emit `filesystem_file_changed(String p_path)` that the module can listen to.

### 7B. Auto-import on file creation
When the AI creates a `.png`, `.wav`, or `.gltf`, the editor should auto-import it immediately:

```cpp
// Proposed API
EditorFileSystem::import_file_now(String p_path);
```

---

## 8. Inspector Integration for AI-Driven Editing (Medium Priority)

**Problem:** The AI sets properties via `_tool_set_node_property`, but the user has no visibility into what the AI changed. There's no "AI Changes" panel.

**What needs to change:**

### 8A. Property change tracking
Add a mechanism to track which properties were changed by the AI vs. the user:

```cpp
// Proposed API in Object
void Object::set_meta("_ai_modified", true);
void Object::set_meta("_ai_modified_properties", PackedStringArray(["position", "script"]));
```

### 8B. Visual indicators in Inspector
Highlight AI-modified properties in the Inspector with a colored border or badge. This requires changes to `EditorInspector` and `EditorProperty` classes.

---

## 9. Script Editor Integration (Low-Medium Priority)

**Problem:** The AI edits scripts but the script editor experience is poor:
- Cursor position is lost after reload
- No diff view showing what the AI changed
- No inline comments explaining AI decisions

**What needs to change:**

### 9A. Non-destructive script updates
Instead of overwriting the entire file, support patch-based updates:

```cpp
// Proposed API
ScriptEditor::apply_patch(Ref<Script> p_script, String p_old_text, String p_new_text);
```

This would create an undoable diff and preserve cursor position if the cursor line wasn't modified.

### 9B. AI comment preservation
Add a convention where AI-generated code blocks are wrapped in comments:
```gdscript
# --- AI GENERATED: player_movement ---
# Generated by Crosshair AI v4.7
func _physics_process(delta):
    velocity.y += gravity * delta
# --- END AI GENERATED ---
```

The editor could collapse these blocks or color them differently.

---

## 10. Multiplayer & Runtime Debugging (Low Priority)

**Problem:** Testing multiplayer features requires manually exporting and running multiple instances.

**What needs to change:**

### 10A. Headless client spawning
```cpp
// Proposed API
EditorInterface::spawn_headless_client(String p_scene_path);
EditorInterface::get_headless_client_state(int p_client_id);
```

### 10B. Runtime RPC inspection
```cpp
// Proposed API
EditorDebugger::get_rpc_log(NodePath p_node);
```

---

## 11. Memory Management for Long Sessions (Low Priority)

**Problem:** Chat sessions with hundreds of messages accumulate memory. Images (base64 screenshots) are never freed.

**What needs to change:**

### 11A. Image cache eviction
Add an LRU cache for vision images with configurable size limits.

### 11B. Message archive
Auto-archive messages older than N turns to disk, keeping only summaries in memory.

---

## Summary: Implementation Priority

| Priority | Feature | Files to Touch | Effort |
|----------|---------|----------------|--------|
| **P0** | UndoRedo for all mutating tools | `yeet_ai_tools_*.cpp` (all 15 files) | 2-3 weeks |
| **P0** | Editor auto-refresh after tool execution | `yeet_ai_dock.cpp`, core: `editor_interface.cpp` | 1 week |
| **P1** | Async tool execution | `yeet_ai_dock.cpp`, `yeet_ai_streaming.cpp` | 2 weeks |
| **P1** | Context-aware validation framework | `yeet_ai_tool_schema.cpp`, new file | 1 week |
| **P2** | Scene tree serialization caching | `yeet_ai_tools_inspect.cpp`, core: `node.cpp` | 1-2 weeks |
| **P2** | File watcher integration | core: `editor_file_system.cpp` | 3-5 days |
| **P2** | Inspector AI-change indicators | core: `editor_inspector.cpp` | 1 week |
| **P3** | Multiplayer headless clients | core: `editor_run_bar.cpp` | 2 weeks |
| **P3** | Memory management / archiving | `yeet_ai_dock.cpp` | 3-5 days |

---

## What Can Be Done Without Core Changes

These improvements stay entirely within the `modules/yeet_ai/` boundary:

1. **Complete stub implementations** — `yeet_ai_tools_stubs.cpp`
2. **2D/3D game workflow templates** — `yeet_ai_tools_scene2d.cpp`, `yeet_ai_tools_game_physics.cpp`
3. **Animation workflow enhancement** — `yeet_ai_tools_animation.cpp`
4. **Shader workflow** — new `yeet_ai_tools_shader.cpp`
5. **Performance optimization** — caching in `yeet_ai_helpers.cpp`
6. **Error recovery** — retry logic in `yeet_ai_dock.cpp`
7. **Testing infrastructure** — new `tests/yeet_ai/` directory
8. **Schema completeness** — `yeet_ai_tool_schema.cpp`

---

*End of document. Use this as a roadmap for prioritizing work between module-level and core-level changes.*
