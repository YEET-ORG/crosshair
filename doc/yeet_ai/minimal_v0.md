# Crosshair AI Minimal v0

This is the first editor-native prototype of the AI workflow.

## What it does

- Adds a `Crosshair AI` dock to the editor.
- Sends prompts to an OpenAI-compatible chat completions endpoint.
- Uses a JSON-only tool-call loop instead of native `tools` support.
- Executes two built-in read-only tools inside the editor:
  - `get_project_tree`
  - `read_project_file`

## Editor settings

These settings are registered under the editor settings database:

- `yeet_ai/enabled`
- `yeet_ai/chat/completions_url`
- `yeet_ai/chat/model`
- `yeet_ai/chat/api_key`
- `yeet_ai/chat/max_tokens`
- `yeet_ai/chat/max_tool_round_trips`
- `yeet_ai/chat/context_token_budget`
- `yeet_ai/chat/context_summarization_enabled`
- `yeet_ai/chat/context_summary_trigger_tokens`
- `yeet_ai/chat/context_summary_keep_recent_messages`
- `yeet_ai/chat/context_summary_max_chars`

## Context management

Long chats keep a persistent per-chat context summary. Older messages are compacted locally when the approximate token trigger is crossed, while the newest messages stay verbatim. Requests include the live editor snapshot, the saved summary, and a safe recent-message suffix.

Defaults are aimed at:

- endpoint: `https://llm.adityaberry.me/v1/chat/completions`
- model: `berrymodel`

## Current limitations

- No sidecar yet. Requests go directly from the editor to the model endpoint.
- No write tools yet. This version is intentionally read-only.
- No streaming yet. Responses are shown after the request completes.
- Tool use is bounded by a fixed round-trip limit.

## Purpose

This version proves the core loop:

1. prompt inside the editor
2. model requests a tool
3. editor executes the tool
4. model gets the tool result
5. final answer appears in the dock

This is the baseline that later versions can extend with:

- a local sidecar
- checkpoints
- write tools for scenes/resources/scripts
- validation and repair loops
