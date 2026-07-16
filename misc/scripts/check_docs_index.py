#!/usr/bin/env python3

if __name__ != "__main__":
    raise ImportError(f'Utility script "{__file__}" should not be used as a module!')

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

if Path(os.getcwd()).resolve() != ROOT:
    raise RuntimeError(f'Utility script "{__file__}" must be run from the repository root!')

INDEX_PATH = ROOT / "docs" / "INDEX.md"
TABLE_ROW_RE = re.compile(r"^\|\s*`(?P<path>[^`]+)`\s*\|\s*(?P<status>[^|]+)\|", re.MULTILINE)
LAST_VERIFIED_RE = re.compile(r"^Last verified:\s*\d{4}-\d{2}-\d{2}\s*$", re.MULTILINE)


def main() -> int:
    errors: list[str] = []

    if not INDEX_PATH.exists():
        print("Missing docs/INDEX.md")
        return 1

    index_text = INDEX_PATH.read_text(encoding="utf-8")
    if not LAST_VERIFIED_RE.search(index_text):
        errors.append("docs/INDEX.md must include 'Last verified: YYYY-MM-DD'.")

    rows = list(TABLE_ROW_RE.finditer(index_text))
    if not rows:
        errors.append("docs/INDEX.md does not contain any indexed document rows.")

    for row in rows:
        rel_path = row.group("path").strip()
        status = row.group("status").strip().lower()
        path = ROOT / rel_path
        if not path.exists():
            errors.append(f"Indexed document is missing: {rel_path}")
            continue

        if status == "active":
            text = path.read_text(encoding="utf-8", errors="replace")
            if not LAST_VERIFIED_RE.search(text):
                errors.append(f"Active indexed document lacks 'Last verified: YYYY-MM-DD': {rel_path}")

    if errors:
        print("Documentation index check failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print(f"Documentation index check passed ({len(rows)} indexed documents).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
