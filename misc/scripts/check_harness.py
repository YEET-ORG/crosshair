#!/usr/bin/env python3

if __name__ != "__main__":
    raise ImportError(f'Utility script "{__file__}" should not be used as a module!')

"""
Umbrella agent-harness gate for Crosshair.

Runs progressive-disclosure and mechanical invariant checks that coding agents
must pass before claiming work complete. Prefer this over ad-hoc partial runs.

Exit code is non-zero if any sub-check fails.
"""

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

if Path(os.getcwd()).resolve() != ROOT:
    raise RuntimeError(f'Utility script "{__file__}" must be run from the repository root!')

CHECKS = [
    ("docs index", [sys.executable, "misc/scripts/check_docs_index.py"]),
    ("tool registry", [sys.executable, "misc/scripts/check_yeet_ai_tools.py"]),
    ("scorecard", [sys.executable, "misc/scripts/check_yeet_ai_scorecard.py"]),
    ("agent map", [sys.executable, "misc/scripts/check_agent_map.py"]),
]


def main() -> int:
    print("=== Crosshair agent harness ===")
    print(f"root: {ROOT}")
    print()

    failed: list[str] = []
    for name, cmd in CHECKS:
        print(f"--- {name} ---")
        proc = subprocess.run(cmd, cwd=ROOT)
        if proc.returncode != 0:
            failed.append(name)
            print(f"[FAIL] {name} (exit {proc.returncode})")
        else:
            print(f"[OK] {name}")
        print()

    if failed:
        print("Harness FAILED:", ", ".join(failed))
        print("Fix mechanical failures before product polish.")
        return 1

    print("Harness PASSED (all gates green).")
    print("Next for C++ changes: py -m SCons -j12 accesskit=no d3d12=no")
    return 0


if __name__ == "__main__":
    sys.exit(main())
