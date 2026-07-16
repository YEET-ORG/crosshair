# Plan: Agent harness upgrade (dynamic loop, no recipe files)

Status: active
Created: 2026-07-09
Owner: agent
Last verified: 2026-07-09

## Goal

Make Crosshair agent-first: progressive disclosure, Plan→Act→Verify loop, mechanical gates — **without** hardcoded multi-tool workflow recipes.

## Acceptance criteria

- [x] Docs: AGENT_LOOP, ARCHITECTURE, core-beliefs, exec-plans
- [x] `check_harness.py` umbrella + agent map + tools + scorecard
- [x] No `modules/yeet_ai/workflows/` recipe catalog
- [x] System prompt steers dynamic composition + composites
- [x] `python misc/scripts/check_harness.py` passes

## Non-goals

- Fixed JSON/YAML tool sequences
- `_tool_run_workflow` recipe executor

## Decision log

| Date | Decision | Why |
| --- | --- | --- |
| 2026-07-09 | Reject hardcoded workflows | Second SSOT; drifts; fights agent loops |
| 2026-07-09 | Composites + packs + plan | Stable multi-step = tools; rest is dynamic |

## Handoff notes

Next leverage: tool-pack consistency checks, UndoRedo coverage, smoke tests — not workflow files.
