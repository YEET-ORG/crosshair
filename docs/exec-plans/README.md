# Execution Plans

Last verified: 2026-07-09

First-class, versioned plans for multi-step agent work (OpenAI exec-plan pattern + Anthropic session handoff).

## Layout

```text
docs/exec-plans/
  README.md           # this file
  TEMPLATE.md         # copy for new plans
  active/             # in-flight work
  completed/          # finished plans (keep for history)
```

## When to create a plan

Create `active/<slug>.md` when any of:

- work spans more than one agent session or context window;
- more than one PR / logical commit series is expected;
- multiple domains touch (tools + schemas + docs + CI);
- the user asks for a roadmap or phased delivery.

Skip formal plans for single-file, single-session fixes.

## Rules

1. One active plan per goal; link related PRs in the plan.
2. Update **Progress** after each session before context dies.
3. Record decisions (and rejected options) in **Decision log**.
4. On completion: move file to `completed/`, set status, leave verify evidence.
5. Plans do not replace issues/PRs; they orient agents between sessions.
