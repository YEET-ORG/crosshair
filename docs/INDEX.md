# Crosshair Documentation Index

Last verified: 2026-07-09

High-signal map for agents. Progressive disclosure: start here, open only what the task needs.
Mechanical check: `python misc/scripts/check_docs_index.py` (also via `check_harness.py`).

| Document | Status | Last verified | Purpose |
| --- | --- | --- | --- |
| `AGENTS.md` | active | 2026-07-09 | Root table of contents for coding agents. |
| `docs/ARCHITECTURE.md` | active | 2026-07-09 | Domain map and yeet_ai layering rules. |
| `docs/AGENT_LOOP.md` | active | 2026-07-09 | Plan→Act→Verify loop; no hardcoded recipes. |
| `docs/design-docs/core-beliefs.md` | active | 2026-07-09 | Agent-first golden principles. |
| `docs/exec-plans/README.md` | active | 2026-07-09 | Multi-session execution plan system. |
| `README.md` | active | 2026-06-30 | Project overview and upstream Godot context. |
| `docs/CLAUDB_CODE_PROMPT.md` | active | 2026-06-30 | Deep `yeet_ai` implementation prompt and patterns. |
| `docs/CORE_ENGINE_IMPROVEMENTS.md` | active | 2026-06-30 | Engine/editor changes needed for robust AI editing. |
| `docs/HARNESS_ENGINEERING_RESEARCH.md` | active | 2026-07-09 | Research sources and roadmap for harness work. |
| `docs/YEET_AI_SCORECARD.md` | active | 2026-07-09 | Mechanical scorecard for `yeet_ai` maturity. |
| `modules/yeet_ai/IMPLEMENTATION_SUMMARY.md` | active | 2026-06-30 | Summary of reliability changes (parser, retry, cache). |
| `modules/yeet_ai/dataset/README.md` | active | 2026-06-30 | Dataset, fine-tuning, and tool-calling protocol. |
| `2D_UI_ROADMAP.md` | draft | 2026-06-30 | Roadmap for 2D and UI tool coverage. |

## Validation

From repository root:

```sh
python misc/scripts/check_harness.py
```

Individual gates:

```sh
python misc/scripts/check_docs_index.py
python misc/scripts/check_agent_map.py
python misc/scripts/check_yeet_ai_tools.py
python misc/scripts/check_yeet_ai_scorecard.py
```
