# Crosshair Core Beliefs (Agent-First)

Last verified: 2026-07-09

These are non-negotiable operating principles. Prefer encoding them as scripts, schemas, and CI over repeating them in chat.

## 1. The repository is the only memory that counts

Anything an agent cannot read from this repo (Slack, Google Docs, someone's head) does not exist for future runs. Put decisions in versioned markdown, schemas, workflows, or code.

## 2. AGENTS.md is a map, not a manual

Keep the root entry short. Progressive disclosure: map → domain doc → file-level detail. Giant instruction files rot and crowd out the task.

## 3. Missing capability beats "try harder"

When an agent fails, ask: what tool, check, workflow, or invariant is missing? Make the fix **legible** (docs) and **enforceable** (script/CI/tool).

## 4. Mechanical rules over prompt-only taste

If a rule matters, it must fail a check. Registry sync, schema coverage, workflow tool references, scorecard gates — not "please remember to register the tool."

## 5. Plan → Act → Verify is the unit of work

Never claim done without evidence: harness check output, build log, tool_trace, or screenshot. Verification is part of the task, not a follow-up.

## 6. Mutating editor state is privileged

Write tools must prefer UndoRedo helpers, post-tool refresh, and risk tiers. Destructive paths need explicit user intent or workflow confirmation.

## 7. Compose dynamically; encode only stable multi-step patterns as tools

Do not maintain hardcoded multi-tool recipe files. Agents plan from live context. When a multi-step pattern is truly stable product behavior, implement it as a **composite tool** (e.g. `scaffold_*`) with undo/refresh — one callable capability, not a parallel workflow graph.

## 8. Leave the next agent a trail

Long work uses execution plans and progress artifacts so a new context window can resume without re-deriving intent.

## 9. Encode human taste once, enforce continuously

Review feedback that keeps recurring becomes a doc rule, then a linter/check. Treat AI-slop cleanup as garbage collection, not a weekly heroics ritual.

## 10. Prefer boring, inspectable structure

Agents replicate patterns they see. Prefer predictable file layout, one dispatch table, typed tool results (`ok` / error), and remediation-oriented error messages.
