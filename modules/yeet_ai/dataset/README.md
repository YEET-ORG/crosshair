# Crosshair AI Fine-Tuning Dataset

Last verified: 2026-06-30

Dataset for fine-tuning LLMs on Crosshair AI tool-calling in the Godot editor.
Built from 24 open-source Godot repos (9.4k+ stars) containing 2,152 .gd, 1,431 .tscn, 632 .tres, 190 project.godot files.

## Directory Structure

```
dataset/
├── README.md
├── tool_schemas/
│   ├── all_tools.json                     # 65+ tool definitions (name, category, args, returns)
│   └── system_prompts.json               # 3 system prompt variants (full, compact, tool-list-only)
├── tool_calling_examples/
│   └── examples.json                      # 30 curated intent→tool-call examples with reasoning
├── finetune_openai/
│   ├── crosshair_ultimate.jsonl           # ★ ALL OpenAI-format examples merged (387)
│   ├── crosshair_all_combined.jsonl       # 317 examples (synthetic + real-world + patterns)
│   ├── crosshair_full_chains.jsonl        # 80 full multi-turn end-to-end workflows
│   ├── crosshair_negative_and_recovery.jsonl # 90 error-recovery & negative examples
│   ├── crosshair_real_world.jsonl         # 128 real-world multi-turn conversations
│   └── crosshair_tool_calls.jsonl         # 89 synthetic tool-calling examples
├── finetune_sharegpt/
│   ├── crosshair_all_combined_sharegpt.jsonl # 441 ShareGPT-format examples
│   └── crosshair_tool_calls_sharegpt.jsonl    # 126 synthetic ShareGPT examples
└── godot_projects/
    ├── extracted/
    │   ├── gdscript_patterns.json          # 80 real GDScript patterns from open-source repos
    │   ├── scene_patterns.json             # 50 parsed .tscn node trees with tool_calls_to_recreate
    │   ├── project_configs.json            # 40 real project.godot configurations
    │   ├── conversations_from_projects.jsonl # 128 multi-turn conversations from real projects
    │   ├── gdscript_code_corpus.jsonl      # 300 real .gd files as code corpus (712 KB)
    │   └── scene_corpus.jsonl              # 300 real .tscn files parsed (2.8 MB)
    └── <24 cloned repos>/                  # Full git clones of open-source Godot projects
```

## Dataset Summary

| Dataset | Format | Examples | Size | Source |
|---------|--------|----------|------|--------|
| `crosshair_ultimate.jsonl` | OpenAI | 387 | 1.1 MB | All merged |
| `crosshair_all_combined.jsonl` | OpenAI | 317 | 758 KB | Synthetic + real + patterns |
| `crosshair_full_chains.jsonl` | OpenAI | 80 | 583 KB | Complex multi-turn workflows |
| `crosshair_negative_and_recovery.jsonl` | OpenAI | 90 | 184 KB | Error recovery & violations |
| `crosshair_real_world.jsonl` | OpenAI | 128 | 332 KB | Real project conversations |
| `crosshair_all_combined_sharegpt.jsonl` | ShareGPT | 441 | 913 KB | All ShareGPT merged |
| `gdscript_patterns.json` | JSON | 80 | 357 KB | Real GDScript from repos |
| `scene_patterns.json` | JSON | 50 | 49 KB | Real .tscn node trees |
| `project_configs.json` | JSON | 40 | 253 KB | Real project.godot |
| `gdscript_code_corpus.jsonl` | JSONL | 300 | 712 KB | Raw .gd files |
| `scene_corpus.jsonl` | JSONL | 300 | 2.8 MB | Raw .tscn files |
| **TOTAL** | | **2,295** | **8.3 MB** | |

## Open-Source Repos Used

24 repos cloned via git, containing 2,152 .gd files and 1,431 scenes:

| Repo | Stars | Type |
|------|-------|------|
| godotengine/godot-demo-projects | 8.6k | Official demos (2D, 3D, GUI, audio, networking, XR) |
| godotengine/tps-demo | — | Official third-person shooter demo |
| GDQuest/godot-shaders | — | 2D/3D shader library |
| GDQuest/godot-visual-effects | — | Particle systems and VFX |
| GDQuest/godot-procedural-generation | — | PCG algorithms |
| GDQuest/godot-2d-tactical-space-combat | — | FTL-like tactical combat |
| GDQuest/godot-2d-jrpg-combat | — | JRPG battle system |
| GDQuest/godot-2d-builder | — | Isometric builder/simulation |
| GDQuest/godot-2d-rhythm | — | Rhythm game |
| GDQuest/godot-2d-tower-defense | — | Tower defense |
| GDQuest/godot-2d-tactical-rpg-movement | — | Grid movement/pathfinding |
| GDQuest/godot-2d-action-platformer | — | Side-scrolling shooter |
| GDQuest/godot-2d-visual-novel | — | Visual novel/dialogue |
| GDQuest/godot-2d-space-game | — | Steering AI / space game |
| GDQuest/godot-steering-ai-framework | — | Steering behaviors library |
| GDQuest/godot-design-patterns | — | GDScript design patterns |
| GDQuest/godot-open-rpg | — | JRPG template |
| GDQuest/godot-make-pro-2d-games | — | A-RPG demo |
| GDQuest/godot-mini-tuts-demos | — | Small focused demos |
| bitbrain/beehave | 3.0k | Behavior tree AI |
| ramokz/phantom-camera | 3.3k | Camera system |
| nathanhoad/godot_dialogue_manager | 3.5k | Dialogue system |
| HungryProton/scatter | 2.8k | Procedural scatter |
| MrEliptik/godot_experiments | — | 2D/3D/VR experiments |

## Tool-Calling Protocol

Crosshair AI uses a custom JSON protocol (NOT OpenAI function_calling):

```
Tool call:  {"type":"tool_call","tool":"TOOL_NAME","arguments":{...}}
Final answer: {"type":"final","message":"text response"}
```

### Critical Rules (encoded in training data)

1. **GDScript batch isolation**: `create_gdscript_file` and `update_gdscript_file` MUST be in their own `batch_tool_calls`, never mixed with other tools
2. **batch_tool_calls** supports `shared_arguments` merged into every call
3. **Physics floors**: StaticBody3D → add_primitive_mesh + add_collision_shape as children
4. **Playable characters**: CharacterBody3D + collision + input actions + mesh + script
5. **No markdown wrapping** of JSON responses
6. **set_node_property** position uses `{"property":"position","value":{"x":0,"y":5,"z":0}}`

## Training Data Categories

### Synthetic Examples (crosshair_tool_calls.jsonl)
Single-shot tool calls for all 65+ tools, basic argument patterns, final responses.

### Real-World Multi-Turn (crosshair_real_world.jsonl)
Full conversations derived from real open-source Godot projects. 2-8 turns per conversation with realistic tool results.

### Full Chain Workflows (crosshair_full_chains.jsonl)
Complex end-to-end workflows (4-10 turns): 3D platformer, 2D top-down, FPS, tower defense, RPG, racing, puzzle, survival, multiplayer, rhythm game.

### Negative & Error Recovery (crosshair_negative_and_recovery.jsonl)
- GDScript batch isolation violations → error → correct split
- Wrong node type → error → correct type
- Missing required args → error → retry
- Stub tool fallbacks (create_particle_emitter → add_node GPUParticles3D)
- Path validation (missing res://, file not found)
- Scene state errors (no scene open, node not found)
- User misconceptions (C# support, .fbx import, visual scripting)
- Undo/recovery workflows

### Code Corpus (gdscript_code_corpus.jsonl, scene_corpus.jsonl)
Raw .gd and .tscn files from open-source repos for code completion pre-training.

## Fine-Tuning Formats

### OpenAI Format
```json
{"messages": [
  {"role": "system", "content": "<system prompt>"},
  {"role": "user", "content": "Create a 3D level"},
  {"role": "assistant", "content": "{\"type\":\"tool_call\",\"tool\":\"create_scene_file\",\"arguments\":{...}}"},
  {"role": "user", "content": "{\"scene_path\":\"res://levels/main.tscn\",...}"},
  {"role": "assistant", "content": "{\"type\":\"final\",\"message\":\"Created the level.\"}"}
]}
```

### ShareGPT Format
```json
{"conversations": [
  {"from": "system", "value": "<system prompt>"},
  {"from": "human", "value": "Create a 3D level"},
  {"from": "gpt", "value": "{\"type\":\"tool_call\",\"tool\":\"create_scene_file\",\"arguments\":{...}}"},
  {"from": "human", "value": "{\"scene_path\":\"res://levels/main.tscn\",...}"},
  {"from": "gpt", "value": "{\"type\":\"final\",\"message\":\"Created the level.\"}"}
]}
```

## Recommended Fine-Tuning Strategy

1. **Pre-train** on `gdscript_code_corpus.jsonl` + `scene_corpus.jsonl` for Godot 4 code familiarity
2. **Supervised fine-tune** on `crosshair_ultimate.jsonl` (387 examples) for tool-calling
3. **Augment** with `crosshair_negative_and_recovery.jsonl` (90 examples) for error handling
4. **Validate** on held-out examples from `crosshair_full_chains.jsonl`

## Source Code Reference

- Tool dispatch: `modules/yeet_ai/editor/yeet_ai_tools.cpp`
- Tool implementations: `modules/yeet_ai/editor/yeet_ai_tools_*.cpp`
- System prompt: `modules/yeet_ai/editor/yeet_ai_system_prompt.cpp`
- Dock (UI + streaming): `modules/yeet_ai/editor/yeet_ai_dock.cpp`
- Editor plugin: `modules/yeet_ai/editor/yeet_ai_editor_plugin.cpp`
