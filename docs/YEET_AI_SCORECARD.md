# Yeet AI Scorecard

Last verified: 2026-07-09

Mechanical maturity board for `modules/yeet_ai/` and the repo agent harness. Prefer script output over subjective status.

```sh
python misc/scripts/check_harness.py
# or:
python misc/scripts/check_yeet_ai_scorecard.py
```

## Current gates

| Gate | Target | Enforcer | Why |
| --- | --- | --- | --- |
| Tool registry sync | Pass | `check_yeet_ai_tools.py` | Declare = implement = dispatch |
| Schema route coverage | Pass | tools + scorecard | Explicit or inferred schema for every tool |
| Dataset → runtime | Pass | tools check | Dataset must not name missing tools |
| Agent map / progressive disclosure | Pass | `check_agent_map.py` | Short AGENTS.md + required docs |
| Stub visibility | Reported | scorecard | Dead stubs stay countable |
| UndoRedo coverage | Improve | scorecard heuristic | Mutators must be undoable |
| Editor refresh | Pass | scorecard | Central post-tool refresh |
| Run trace artifact | Present | scorecard | Inspectable tool_trace.jsonl |
| Large tool files | Reported | scorecard | Legibility debt |

## Interpretation

- `pass` — script can prove the gate holds.
- `warn` — drift/debt; do not ignore forever.
- `fail` — fix before adding more tools.

## Run artifacts (in-editor)

```text
user://yeet_ai/runs/<run_id>/tool_trace.jsonl
```

## Agent loop artifacts (repo)

| Artifact | Path |
| --- | --- |
| Active plans | `docs/exec-plans/active/` |
| Completed plans | `docs/exec-plans/completed/` |
| Core beliefs | `docs/design-docs/core-beliefs.md` |

No hardcoded multi-tool workflow catalog — composition is planned dynamically; stable multi-step behavior is composite tools.

## Next scorecard improvements

- Per-tool UndoRedo annotations (not keyword heuristics).
- Per-tool refresh metadata (not name-prefix heuristics).
- Smoke test: one read tool + one write tool in a temp project.
- Tool-pack coverage / advertise-set consistency checks.
- Dataset provenance / freshness.
