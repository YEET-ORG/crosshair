# Crosshair Agent Loop

Last verified: 2026-07-09

Standard control loop for agents working **on** Crosshair (coding agents) and **inside** Crosshair (`yeet_ai` dock).

Inspired by OpenAI harness engineering (repo as system of record, progressive disclosure, mechanical enforcement), Anthropic long-running agents (session handoff + progress artifacts), and Plan → Execute → Verify loops.

**Not used:** hardcoded multi-tool workflow recipes. Paths are planned each turn from live context, packs, and composite tools.

## Loop overview

```text
                 ┌──────────────┐
                 │  ORIENT      │  AGENTS.md → docs/INDEX.md → domain doc
                 └──────┬───────┘
                        ▼
                 ┌──────────────┐
                 │  PLAN        │  goals, budgets, stop conditions (dynamic)
                 └──────┬───────┘
                        ▼
                 ┌──────────────┐
                 │  ACT         │  composite tools and/or atomic tools from state
                 └──────┬───────┘
                        ▼
                 ┌──────────────┐
                 │  VERIFY      │  harness checks, inspect, audit, play, traces
                 └──────┬───────┘
                        ▼
              pass? ──no──► diagnose capability gap ──► fix tool/doc/check ──► loop
                │
               yes
                ▼
                 ┌──────────────┐
                 │  HANDOFF     │  progress notes / close plan
                 └──────────────┘
```

## Why not hardcoded workflows

| Hardcoded recipes | Dynamic composition |
| --- | --- |
| Drift when tools rename or improve | Dispatch table + schemas stay SSOT |
| Same steps ignore live scene state | Plan from `get_scene_tree` / project context |
| Second source of truth to maintain | Composites only when a pattern is truly stable |
| Agents skip or fight the script | Agents own the plan (`update_plan`) |

Stable multi-step **product** behavior belongs in **composite tools** (`scaffold_*`, batch ops), implemented in C++ with undo/refresh — not in JSON step lists.

## Budgets (default)

| Budget | Default | Purpose |
| --- | --- | --- |
| Max plan steps before re-plan | 12 | Prevents thrashing without a new plan |
| Max tool failures per step | 3 | Aligns with dock retry |
| Max consecutive verify failures | 2 | Force capability diagnosis |
| Context compaction trigger | when task loses the goal | Re-read AGENTS.md + active plan only |

## Stop conditions

Stop and report when:

1. Acceptance criteria are met **and** verify evidence exists.
2. A required capability is missing and out of scope.
3. Budget exhausted without progress on the primary goal.
4. Destructive action needs human confirmation.

## Coding-agent loop (this repository)

### Orient

1. Read `AGENTS.md`.
2. Read `docs/INDEX.md` → domain doc.
3. For `yeet_ai`: `docs/CLAUDB_CODE_PROMPT.md`.
4. Check `docs/exec-plans/active/` for in-flight work.

### Plan

- Small task: short plan in the reply.
- Multi-session work: `docs/exec-plans/active/<slug>.md` from the template.
- Do not invent a repo-local workflow file for tool sequences.

### Act

- Prefer existing patterns under `modules/yeet_ai/editor/`.
- Tool changes follow `AGENTS.md` checklist.

### Verify

```sh
python misc/scripts/check_harness.py
```

C++ changes:

```sh
py -m SCons -j12 accesskit=no d3d12=no
```

### Handoff

- Move finished plans to `docs/exec-plans/completed/`.
- Update `Last verified:` when you proved a doc current.

## In-editor loop (`yeet_ai`)

1. **Observe** — scene tree, selection, project context, last tool_trace.
2. **Plan** — `update_plan` with task-specific steps (not a canned path).
3. **Select pack** — only tools relevant to 2D/UI/debug/etc.
4. **Compose** — use `scaffold_*` / composites when they match the intent; otherwise atomics.
5. **Mutate** via UndoRedo helpers + central refresh.
6. **Verify** — re-inspect, audit, play/capture, read errors.
7. **Trace** — `user://yeet_ai/runs/<run_id>/tool_trace.jsonl`.

## Anti-patterns

| Anti-pattern | Do instead |
| --- | --- |
| Fixed JSON/YAML multi-tool scripts | Dynamic plan + composite tools |
| Paste a 2k-line rule prompt | Docs + checks |
| "Fixed" without verify | Run harness / show inspect evidence |
| Silent UndoRedo skip on writes | `_commit_ai_*` / undo-aware `_add_to_scene` |
