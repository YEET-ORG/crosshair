# Crosshair Architecture Map

Last verified: 2026-07-09

Short domain map for agents. Deep detail lives under `docs/` and `modules/yeet_ai/`.

## Product

**Crosshair** = Godot Engine 4.7-dev fork + AI-native editor module.

| Domain | Path | Notes |
| --- | --- | --- |
| Engine core | `core/`, `scene/`, `servers/` | Prefer upstream patterns; avoid drive-by refactors |
| Editor shell | `editor/` | Host for docks, undo, filesystem |
| Platforms | `platform/` | OS backends |
| **AI product** | `modules/yeet_ai/` | Primary Crosshair delta |
| Agent harness docs | `docs/`, `AGENTS.md` | Progressive disclosure knowledge base |
| Mechanical checks | `misc/scripts/check_*.py` | Enforce invariants in CI |
| Fine-tune data | `modules/yeet_ai/dataset/` | Protocol + examples (may lag runtime tools) |

## yeet_ai layering

Depend only **forward**:

```text
Types/Schemas  →  Dispatch  →  Tool impls (atomic + composite)  →  Helpers (undo/refresh/trace)
       ↑                ↑
 System prompt     Streaming / providers
       ↑
     Dock UI / settings / tool packs
```

| Layer | Files | Rule |
| --- | --- | --- |
| Schemas | `yeet_ai_tool_schema.*` | Explicit or inferred; no silent arg contracts |
| Dispatch | `yeet_ai_tools.cpp` | **Single** source of tool names |
| Atomic tools | `yeet_ai_tools_*.cpp` | One category per file when practical |
| Composite tools | e.g. `yeet_ai_tools_scaffold.cpp` | Multi-step only when pattern is stable product behavior |
| Helpers | `yeet_ai_helpers.cpp` | UndoRedo, refresh, arg parse, scene resolve |
| Providers | `*_provider.cpp`, streaming | HTTP/CLI adapters only; no scene mutation |
| Context | project/asset index, memory | Read models for prompts; write via tools |
| UI | dock, settings, packs | Progressive disclosure of tools by task |

## Composition model (dynamic)

```text
User goal
   → plan (update_plan / exec-plan)
   → tool pack selection (context)
   → composite tool if intent matches
   → else atomic tools from live state
   → verify (inspect / audit / play / harness)
```

There is **no** parallel workflow-file runtime. That would be a second dispatch graph that drifts.

## Runtime data (editor session)

| Artifact | Location | Purpose |
| --- | --- | --- |
| Tool trace | `user://yeet_ai/runs/<run_id>/tool_trace.jsonl` | Append-only execution log |
| Memory | `.crosshair/memory.json` (project) | Cross-session project memory |
| Exec plans | `docs/exec-plans/active/` | Multi-session **coding** plans (not tool recipes) |

## Dependency rules

1. Tools return `Dictionary` with `ok` or use `_make_error`.
2. Mutators use undo/refresh helpers where applicable.
3. Dataset tool names must not reference missing runtime tools.
4. Do not add second dispatch tables or hardcoded multi-tool recipe catalogs.
5. Prefer extending composite tools over documenting fixed step lists.

## Related docs

- Loop: `docs/AGENT_LOOP.md`
- Beliefs: `docs/design-docs/core-beliefs.md`
- Tool depth: `docs/CLAUDB_CODE_PROMPT.md`
- Engine gaps: `docs/CORE_ENGINE_IMPROVEMENTS.md`
- Maturity: `docs/YEET_AI_SCORECARD.md`
