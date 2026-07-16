# Harness Engineering Research for Crosshair

Last verified: 2026-07-09

Research date: 2026-06-30 (updated 2026-07-09 with implementation status)

This note covers two related but different meanings of "harness engineering":

- OpenAI's harness engineering: an agent-first software engineering operating model where the repository, tools, validation loops, and feedback systems are designed so coding agents can do reliable work.
- Harness.io's platform-engineering product patterns: internal developer portals, self-service workflows, scorecards, CI/CD, feature flags, supply-chain security, and reliability/cost governance.

For Crosshair, the OpenAI meaning is the better primary model because Crosshair is already building an AI-native Godot editor module in `modules/yeet_ai/`. The Harness.io ideas are useful as product patterns to borrow, not as the core architecture.

## Sources Checked

- OpenAI: "Harness engineering: leveraging Codex in an agent-first world" (2026-02-11): https://openai.com/index/harness-engineering/
- Anthropic: "Effective harnesses for long-running agents" (2025-11): https://www.anthropic.com/engineering/effective-harnesses-for-long-running-agents
- Anthropic: Agent Skills / progressive disclosure: https://www.anthropic.com/engineering/equipping-agents-for-the-real-world-with-agent-skills
- Harness Developer Hub documentation overview: https://developer.harness.io/docs/
- Harness IDP overview: https://developer.harness.io/docs/internal-developer-portal/
- Harness IDP scorecards overview: https://developer.harness.io/docs/internal-developer-portal/scorecards/scorecard/
- Harness IDP self-service workflows overview: https://developer.harness.io/docs/internal-developer-portal/flows/overview/
- Harness Feature Flags docs: https://developer.harness.io/docs/feature-flags/
- Harness Supply Chain Security docs: https://developer.harness.io/docs/software-supply-chain-assurance/

## Implementation status (2026-07-09)

| Research item | Status | Location |
| --- | --- | --- |
| Short AGENTS.md map | Done | `AGENTS.md` |
| Doc index + freshness check | Done | `docs/INDEX.md`, `check_docs_index.py` |
| Tool registry check | Done | `check_yeet_ai_tools.py` |
| Scorecard | Done | `docs/YEET_AI_SCORECARD.md`, `check_yeet_ai_scorecard.py` |
| Core beliefs / golden principles | Done | `docs/design-docs/core-beliefs.md` |
| Agent loop (PEV + budgets) | Done | `docs/AGENT_LOOP.md` |
| Architecture map | Done | `docs/ARCHITECTURE.md` |
| Exec plans (session handoff) | Done | `docs/exec-plans/` |
| Dynamic agent loop (no recipe files) | Done | `docs/AGENT_LOOP.md` |
| Composite tools as multi-step product APIs | Partial | `scaffold_*` etc. |
| Umbrella harness gate | Done | `misc/scripts/check_harness.py` |
| CI wire-up | Done | `.github/workflows/static_checks.yml` |
| Hardcoded multi-tool workflows | Rejected | Prefer plan + packs + composites |
| Full UndoRedo migration | Partial | helpers exist; coverage ~40% |
| Feature flags for risky AI | Partial | settings exist; not all flagged |
| Dataset provenance | Todo | — |

## Key Research Takeaways

OpenAI's model says the engineer's leverage shifts from hand-writing code to designing the environment around agents. The repo should be the source of truth, with short entry docs pointing to deeper indexed docs. Agents need direct access to the app, logs, metrics, tests, review feedback, and repeatable commands. Architectural rules should be enforced mechanically through linters, structural tests, and custom checks with remediation-oriented error messages.

The most important point for Crosshair: if a rule only exists in a chat, a developer's head, or an outdated mega-prompt, it does not exist for future agent runs. It must be encoded as repo-local markdown, schemas, tests, scripts, or tool behavior.

Harness.io's IDP docs reinforce the same operational pattern from a platform-engineering angle: create a developer portal/catalog, expose self-service workflows, measure health with scorecards, and standardize golden paths. Their scorecards evaluate maturity across code quality, security, documentation, operational readiness, and compliance. Their self-service workflows use versioned `workflow.yaml` files with frontend inputs, backend actions, and outputs.

## Crosshair Current Fit

Crosshair already has strong pieces:

- `modules/yeet_ai/` contains an editor-native AI module with structured tool calling, providers, streaming, retry, cache, schemas, tools, and datasets.
- `docs/CLAUDB_CODE_PROMPT.md` is a useful agent briefing document for `yeet_ai`.
- `docs/CORE_ENGINE_IMPROVEMENTS.md` correctly identifies core gaps: UndoRedo, editor refresh, async execution, validation, context serialization, memory, and tests.
- `modules/yeet_ai/dataset/` has fine-tuning data and tool-call examples.
- `.github/workflows/static_checks.yml` and related GitHub Actions provide a CI base.

The main weakness is that the current repo knowledge is not yet organized as an agent-first system. There is useful documentation, but there is no small stable entry point, no doc index with freshness/ownership, no scorecard, and no mechanical validation that docs, schemas, tools, and tests remain in sync.

## What To Implement In Crosshair

### P0: Agent Knowledge Harness

Add a small `AGENTS.md` at the repo root. Keep it under roughly 100-150 lines. It should not be a huge manual. It should map agents to:

- `docs/CLAUDB_CODE_PROMPT.md` for `yeet_ai` architecture and patterns.
- `docs/CORE_ENGINE_IMPROVEMENTS.md` for known engine-level blockers.
- `modules/yeet_ai/dataset/README.md` for datasets and protocol examples.
- Build/test commands.
- Rules for adding tools: declare, implement, register, schema, tests.

Add `docs/INDEX.md` that lists high-signal docs, owners/status, last verified date, and the command or source used to verify each doc. Add a lightweight script to check that all linked docs exist and that important docs include a "Last verified" field.

Why: OpenAI's article emphasizes progressive disclosure and repository-local knowledge. Crosshair currently has valuable docs, but agents need a stable map.

### P0: Tool Registry Consistency Check

Add a deterministic script, for example `misc/scripts/check_yeet_ai_tools.py`, that verifies:

- Every `_tool_*` declared in `yeet_ai_dock.h` is either implemented or explicitly listed as a stub.
- Every dispatch-table tool in `yeet_ai_tools.cpp` appears in `yeet_ai_get_all_tool_names()`.
- Every tool has either an explicit schema or an inferred-schema reason.
- Dataset tool schema names in `modules/yeet_ai/dataset/tool_schemas/all_tools.json` do not silently diverge from runtime tool names.

Wire it into static checks.

Why: this is the Crosshair equivalent of architecture enforcement. It gives agents fast feedback and prevents tool drift.

### P0: Yeet AI Scorecard

Create `docs/YEET_AI_SCORECARD.md` and a script that emits pass/fail checks:

- Tool registry sync.
- Schema coverage percent.
- Stub count.
- Mutating tools with UndoRedo coverage.
- Mutating tools with editor refresh coverage.
- Read-only tools with cache safety.
- Tool test coverage.
- Dataset/runtime schema drift.
- Large-file warnings for `yeet_ai_tools_*.cpp`.

Why: Harness IDP scorecards are directly applicable. Crosshair needs a visible maturity board for agent-readiness, not just a free-form roadmap.

### P0: UndoRedo and Refresh as Harness Primitives

Do not wrap 250+ tools manually one by one without first creating a small harness layer. Add shared helpers in `modules/yeet_ai/editor/yeet_ai_helpers.cpp` or a new file:

- `YeetAIToolMutationScope`
- `begin_ai_action(name, context)`
- `commit_ai_action()`
- `refresh_editor_after_tool(tool_name, result)`
- `record_created_node`, `record_removed_node`, `record_property_change`, `record_file_change`

Then convert the high-impact mutating tools first:

- `_tool_add_node`
- `_tool_remove_node`
- `_tool_reparent_node`
- `_tool_set_node_property`
- `_tool_create_gdscript_file`
- `_tool_update_gdscript_file`
- `_tool_write_project_file`

Why: OpenAI's model says when agents fail, ask what capability is missing and make it legible/enforceable. For Crosshair, UndoRedo and refresh are missing platform capabilities, not just scattered bugs.

### P1: Dynamic composition (not hardcoded workflow files)

Do **not** add a parallel YAML/JSON multi-tool recipe runtime. That becomes a second source of truth that drifts from the dispatch table.

Instead:

1. **Plan per task** — in-editor `update_plan`, repo `docs/exec-plans/` for multi-session coding work.
2. **Tool packs** — progressive disclosure so the model only sees relevant tools.
3. **Composite tools** — when a multi-step pattern is stable product behavior (`scaffold_*`, batch helpers), implement it as one tool with undo/refresh.
4. **Verify from state** — inspect/audit/play/trace, not “did we finish the recipe.”

Why: OpenAI harness engineering optimizes for agent legibility and mechanical invariants, not canned step scripts. Anthropic progressive disclosure loads skills on demand — capability descriptions, not rigid DAGs of tool names.

### P1: Agent-Legible Editor Observability

Expose tool execution, editor state, and runtime validation as inspectable artifacts:

- `user://yeet_ai/runs/<run_id>/tool_trace.jsonl`
- `user://yeet_ai/runs/<run_id>/errors.jsonl`
- `user://yeet_ai/runs/<run_id>/screenshots/`
- `user://yeet_ai/runs/<run_id>/scorecard.json`

Add read-only tools:

- `get_last_tool_trace`
- `get_recent_editor_errors`
- `get_ai_run_artifacts`
- `validate_current_scene`

Why: OpenAI's article highlights app, logs, metrics, traces, screenshots, and UI state as things agents must be able to inspect. For Crosshair, the editor and game viewport are the app.

### P1: Feature Flags for Risky AI Behavior

Borrow the Harness Feature Flags idea in a local, editor-settings form:

- `yeet_ai/flags/native_tool_calling`
- `yeet_ai/flags/batch_tool_calls`
- `yeet_ai/flags/write_tools`
- `yeet_ai/flags/workflows`
- `yeet_ai/flags/auto_retry`
- `yeet_ai/flags/auto_undo_on_batch_failure`
- `yeet_ai/flags/experimental_3d_tools`

Each flag should be visible in the settings panel, included in the runtime context, and logged in run artifacts.

Why: Crosshair is changing editor state. Risky agent behavior needs local rollout controls even before there is a cloud feature-flag service.

### P2: Supply-Chain and Dataset Hygiene

Add checks for:

- Dataset provenance: source repo, license, commit, extraction date.
- Generated dataset freshness.
- Tool-call examples that reference missing tools.
- Secret scanning in prompts, datasets, and run artifacts.
- Optional SBOM/provenance generation for Crosshair builds.

Why: Harness supply-chain security focuses on trust, OSS management, policy compliance, vulnerabilities, SBOM, and provenance. Crosshair's fine-tuning dataset and generated artifacts need the same discipline.

### P2: Agent Review Loop

Create first-class review commands for agents:

- `python misc/scripts/check_yeet_ai_tools.py`
- `python misc/scripts/check_docs_index.py`
- focused SCons build commands for `modules/yeet_ai`
- a smoke test that opens a temp project, runs one read-only tool, and exits

Document the review loop in `AGENTS.md`: inspect, edit, run checks, produce evidence, update docs when behavior changes.

Why: Agent-generated work compounds only when every run has fast local feedback.

## Recommended Implementation Order

1. Add `AGENTS.md`, `docs/INDEX.md`, and doc-link/freshness validation.
2. Add `check_yeet_ai_tools.py` and wire it to static checks.
3. Add `docs/YEET_AI_SCORECARD.md` generated from checks.
4. Add a mutation harness for UndoRedo and editor refresh, then migrate the seven high-impact tools.
5. Strengthen tool packs + composite tools; never reintroduce hardcoded multi-tool recipe catalogs.
6. Add run artifacts and read-only observability tools.
7. Add local feature flags for risky AI capabilities.

## Non-Goals

- Do not integrate Harness.io as a paid dependency just to copy these ideas. The useful part is the operating model.
- Do not add another giant prompt file. Crosshair already has large docs; the next step is indexing and validation.
- Do not prioritize more tool count before UndoRedo, refresh, schema drift checks, and test harnesses. More tools without a harness will increase failure surface.
