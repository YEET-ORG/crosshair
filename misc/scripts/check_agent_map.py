#!/usr/bin/env python3

if __name__ != "__main__":
    raise ImportError(f'Utility script "{__file__}" should not be used as a module!')

"""
Validate progressive-disclosure agent map:

- AGENTS.md stays short and points at the knowledge base
- Required harness docs exist
- Active exec-plans directory exists
- Core beliefs and agent loop are present
- No hardcoded multi-tool workflow recipe directory
"""

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

if Path(os.getcwd()).resolve() != ROOT:
    raise RuntimeError(f'Utility script "{__file__}" must be run from the repository root!')

AGENTS = ROOT / "AGENTS.md"
MAX_AGENTS_LINES = 160
# Hardcoded multi-tool recipe catalogs are not part of the harness.
FORBIDDEN_WORKFLOW_DIR = ROOT / "modules" / "yeet_ai" / "workflows"

REQUIRED_PATHS = [
    "AGENTS.md",
    "docs/INDEX.md",
    "docs/ARCHITECTURE.md",
    "docs/AGENT_LOOP.md",
    "docs/design-docs/core-beliefs.md",
    "docs/exec-plans/README.md",
    "docs/exec-plans/TEMPLATE.md",
    "docs/exec-plans/active/README.md",
    "docs/YEET_AI_SCORECARD.md",
    "docs/HARNESS_ENGINEERING_RESEARCH.md",
    "misc/scripts/check_harness.py",
    "misc/scripts/check_yeet_ai_tools.py",
    "misc/scripts/check_yeet_ai_scorecard.py",
    "misc/scripts/check_docs_index.py",
]

REQUIRED_AGENTS_PHRASES = [
    "docs/INDEX.md",
    "docs/AGENT_LOOP.md",
    "check_harness.py",
    "modules/yeet_ai",
    "not hardcoded",
]


def main() -> int:
    errors: list[str] = []

    for rel in REQUIRED_PATHS:
        if not (ROOT / rel).exists():
            errors.append(f"Missing required harness path: {rel}")

    if FORBIDDEN_WORKFLOW_DIR.exists():
        errors.append(
            "modules/yeet_ai/workflows/ should not exist: "
            "use dynamic planning + composite tools, not hardcoded multi-tool recipes."
        )

    if not AGENTS.is_file():
        errors.append("AGENTS.md missing")
    else:
        text = AGENTS.read_text(encoding="utf-8", errors="replace")
        lines = text.splitlines()
        if len(lines) > MAX_AGENTS_LINES:
            errors.append(
                f"AGENTS.md is {len(lines)} lines (max {MAX_AGENTS_LINES}). "
                "Keep it a map; move detail into docs/."
            )
        if not re.search(r"^Last verified:\s*\d{4}-\d{2}-\d{2}\s*$", text, re.MULTILINE):
            errors.append("AGENTS.md must include 'Last verified: YYYY-MM-DD'")
        for phrase in REQUIRED_AGENTS_PHRASES:
            if phrase not in text:
                errors.append(f"AGENTS.md must reference '{phrase}'")

    index_path = ROOT / "docs" / "INDEX.md"
    if index_path.is_file():
        index_text = index_path.read_text(encoding="utf-8", errors="replace")
        for rel in (
            "docs/AGENT_LOOP.md",
            "docs/ARCHITECTURE.md",
            "docs/design-docs/core-beliefs.md",
        ):
            if f"`{rel}`" not in index_text:
                errors.append(f"docs/INDEX.md should index `{rel}`")

    if errors:
        print("Agent map check failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    agents_lines = len(AGENTS.read_text(encoding="utf-8").splitlines())
    print(
        f"Agent map check passed "
        f"({len(REQUIRED_PATHS)} required paths, AGENTS.md {agents_lines} lines, "
        "no hardcoded workflow recipes)."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
