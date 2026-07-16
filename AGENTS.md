# Crosshair Agent Guide

Last verified: 2026-07-09

Short map only. Deep truth lives under `docs/` and is enforced by scripts.
Loop: **Orient → Plan → Act → Verify → Handoff** (`docs/AGENT_LOOP.md`).

## Read first (progressive disclosure)

| Order | Doc | When |
| --- | --- | --- |
| 1 | `docs/INDEX.md` | Always — knowledge catalog |
| 2 | `docs/design-docs/core-beliefs.md` | Always — non-negotiable principles |
| 3 | `docs/ARCHITECTURE.md` | Structure / where to edit |
| 4 | `docs/AGENT_LOOP.md` | Multi-step work, budgets, stop conditions |
| 5 | `docs/CLAUDB_CODE_PROMPT.md` | Any `modules/yeet_ai/` code |
| 6 | `docs/CORE_ENGINE_IMPROVEMENTS.md` | UndoRedo / editor platform gaps |
| 7 | `docs/exec-plans/active/` | Resume multi-session work |

## Product surface

Primary fork delta: `modules/yeet_ai/` (AI dock + 400+ editor tools).

| Piece | Path |
| --- | --- |
| Dock / state | `modules/yeet_ai/editor/yeet_ai_dock.h` |
| Dispatch SSOT | `modules/yeet_ai/editor/yeet_ai_tools.cpp` |
| Tool impls | `modules/yeet_ai/editor/yeet_ai_tools_*.cpp` |
| Schemas | `modules/yeet_ai/editor/yeet_ai_tool_schema.cpp` |
| Composite tools | `yeet_ai_tools_scaffold.cpp` (and other composites) |
| Dataset protocol | `modules/yeet_ai/dataset/` |

## How agents compose work (not hardcoded recipes)

Do **not** add fixed multi-tool workflow JSON/YAML scripts. Composition is dynamic:

1. **Plan** with `update_plan` (in-editor) or an exec-plan (repo work).
2. **Disclose tools** via packs / context — advertise only what the task needs.
3. **Prefer composite tools** (`scaffold_*`, batch helpers) when one capability wraps a stable multi-step pattern.
4. **Otherwise call atomic tools** driven by live scene/project state.
5. **Verify** with inspect/audit/play/trace — not by replaying a canned sequence.

Hardcoded step lists drift from the 400+ tool surface and fight real agent loops.

## Tool change checklist

1. Declare `_tool_<name>` in `yeet_ai_dock.h`.
2. Implement in the matching `yeet_ai_tools_*.cpp`.
3. Register in `tool_dispatch_table()` (`yeet_ai_tools.cpp`).
4. Explicit schema when inference is too vague.
5. Update dataset defs only if the protocol should expose it.
6. If many callers always do the same 5 steps, consider a **composite tool** — not a workflow file.
7. Run `python misc/scripts/check_harness.py`.

## Verify before "done"

```sh
python misc/scripts/check_harness.py
```

Windows build (deps often need flags):

```sh
py -m SCons -j12 accesskit=no d3d12=no
```

## Harness priorities

Encode rules as scripts, schemas, composite tools, and scorecards — not prompt walls or canned recipes:

- registry + schema sync;
- UndoRedo / refresh via helpers for mutators;
- run traces under `user://yeet_ai/runs/`;
- scorecard visibility;
- session handoff via `docs/exec-plans/`.

## Anti-patterns

- Growing this file past ~150 lines — split into `docs/`.
- Hardcoded multi-tool workflow files that restate the dispatch table.
- Claiming done without harness output.
