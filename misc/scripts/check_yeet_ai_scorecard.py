#!/usr/bin/env python3

if __name__ != "__main__":
    raise ImportError(f'Utility script "{__file__}" should not be used as a module!')

import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

if Path(os.getcwd()).resolve() != ROOT:
    raise RuntimeError(f'Utility script "{__file__}" must be run from the repository root!')

YEET_DIR = ROOT / "modules" / "yeet_ai" / "editor"
DOCK_H = YEET_DIR / "yeet_ai_dock.h"
TOOLS_CPP = YEET_DIR / "yeet_ai_tools.cpp"
SCHEMA_CPP = YEET_DIR / "yeet_ai_tool_schema.cpp"
DATASET_TOOLS = ROOT / "modules" / "yeet_ai" / "dataset" / "tool_schemas" / "all_tools.json"

DECL_RE = re.compile(r"\bDictionary\s+(_tool_[A-Za-z0-9_]+)\s*\(\s*const\s+Dictionary\s*&\s*p_args\s*\)\s+const\s*;")
IMPL_RE = re.compile(r"\bDictionary\s+YeetAIDock::(_tool_[A-Za-z0-9_]+)\s*\(")
ENTRY_RE = re.compile(r'\{\s*"(?P<name>[A-Za-z0-9_]+)"\s*,\s*&YeetAIDock::(?P<handler>_tool_[A-Za-z0-9_]+)\s*\}')
EXPLICIT_SCHEMA_RE = re.compile(r'schemas\["(?P<name>[A-Za-z0-9_]+)"\]\s*=')
LEGACY_BLOCK_RE = re.compile(r"/\* Legacy hand-maintained tool-name list.*?\*/", re.DOTALL)
STUB_RE = re.compile(r"Not yet implemented|Not implemented yet|TODO", re.IGNORECASE)
UNDO_RE = re.compile(r"EditorUndoRedoManager|UndoRedo|_current_tool_name_for_undo|_commit_ai_|_add_to_scene")
REFRESH_RE = re.compile(r"_refresh_editor_after_tool|scan_changes|notify_property_list_changed|edit_node|edit_script")
RUN_TRACE_RE = re.compile(r"_append_ai_run_trace|tool_trace\.jsonl")

MUTATING_PREFIXES = (
    "add_",
    "assign_",
    "attach_",
    "batch_",
    "clear_",
    "connect_",
    "copy_",
    "create_",
    "delete_",
    "disconnect_",
    "duplicate_",
    "edit_",
    "export_",
    "fill_",
    "import_",
    "instantiate_",
    "manage_",
    "merge_",
    "move_",
    "open_",
    "paint_",
    "patch_",
    "play_",
    "reimport_",
    "reload_",
    "remove_",
    "rename_",
    "reparent_",
    "repair_",
    "replace_",
    "resource_create",
    "resource_modify",
    "runtime_",
    "run_",
    "save_",
    "scaffold_",
    "scene_modify",
    "scene_remove",
    "select_",
    "set_",
    "stop_",
    "update_",
    "write_",
)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def dispatch_entries() -> dict[str, str]:
    text = LEGACY_BLOCK_RE.sub("", read_text(TOOLS_CPP))
    return {match.group("name"): match.group("handler") for match in ENTRY_RE.finditer(text)}


def declared_handlers() -> set[str]:
    return set(DECL_RE.findall(read_text(DOCK_H)))


def implemented_handlers() -> set[str]:
    handlers: set[str] = set()
    for path in YEET_DIR.glob("*.cpp"):
        handlers.update(IMPL_RE.findall(read_text(path)))
    return handlers


def explicit_schema_names() -> set[str]:
    return set(EXPLICIT_SCHEMA_RE.findall(read_text(SCHEMA_CPP)))


def dataset_tool_names() -> set[str]:
    if not DATASET_TOOLS.exists():
        return set()
    data = json.loads(DATASET_TOOLS.read_text(encoding="utf-8"))
    return {tool["name"] for tool in data.get("tools", []) if isinstance(tool.get("name"), str)}


def is_mutating(name: str) -> bool:
    return name.startswith(MUTATING_PREFIXES)


def tool_body(handler: str) -> str:
    pattern = re.compile(
        r"Dictionary\s+YeetAIDock::" + re.escape(handler) + r"\s*\([^)]*\)\s*const\s*\{(?P<body>.*?)\n\}",
        re.DOTALL,
    )
    for path in YEET_DIR.glob("*.cpp"):
        match = pattern.search(read_text(path))
        if match:
            return match.group("body")
    return ""


def status(ok: bool, warn: bool = False) -> str:
    if ok:
        return "pass"
    if warn:
        return "warn"
    return "fail"


def main() -> int:
    entries = dispatch_entries()
    names = set(entries)
    handlers = set(entries.values())
    declarations = declared_handlers()
    implementations = implemented_handlers()
    schemas = explicit_schema_names()
    dataset = dataset_tool_names()

    registry_ok = handlers <= declarations and handlers <= implementations
    schema_route_ok = True
    dataset_missing_runtime = sorted(dataset - names)
    mutating = sorted(name for name in names if is_mutating(name))

    stub_tools = []
    undo_covered = []
    refresh_covered = []
    central_refresh_hook = "_refresh_editor_after_tool(p_tool_name" in read_text(TOOLS_CPP)
    for name in sorted(names):
        body = tool_body(entries[name])
        if STUB_RE.search(body):
            stub_tools.append(name)
        if name in mutating and UNDO_RE.search(body):
            undo_covered.append(name)
        if name in mutating and REFRESH_RE.search(body):
            refresh_covered.append(name)
    if central_refresh_hook:
        refresh_covered = mutating

    large_files = []
    for path in sorted(YEET_DIR.glob("*.cpp")):
        line_count = len(read_text(path).splitlines())
        if line_count > 750:
            large_files.append((path.relative_to(ROOT).as_posix(), line_count))

    source_text = "\n".join(read_text(path) for path in sorted(YEET_DIR.glob("*.cpp")))
    has_run_trace = bool(RUN_TRACE_RE.search(source_text))

    explicit_schema_percent = (len(schemas & names) / len(names) * 100.0) if names else 0.0
    undo_percent = (len(undo_covered) / len(mutating) * 100.0) if mutating else 0.0
    refresh_percent = (len(refresh_covered) / len(mutating) * 100.0) if mutating else 0.0

    rows = [
        ("tool registry sync", status(registry_ok), f"{len(names)} tools, {len(declarations)} declarations, {len(implementations)} implementations"),
        ("schema route coverage", status(schema_route_ok), f"{len(schemas & names)} explicit schemas ({explicit_schema_percent:.1f}%), inferred fallback for remaining tools"),
        ("dataset/runtime drift", status(not dataset_missing_runtime, warn=bool(dataset_missing_runtime)), f"{len(dataset_missing_runtime)} dataset tools missing runtime dispatch"),
        ("stub visibility", status(True), f"{len(stub_tools)} tools contain stub/TODO markers"),
        ("UndoRedo heuristic coverage", status(False, warn=True), f"{len(undo_covered)}/{len(mutating)} mutating tools ({undo_percent:.1f}%) mention UndoRedo helpers"),
        ("editor refresh heuristic coverage", status(bool(central_refresh_hook), warn=not central_refresh_hook), f"{len(refresh_covered)}/{len(mutating)} mutating tools ({refresh_percent:.1f}%) covered by central post-tool refresh hook" if central_refresh_hook else f"{len(refresh_covered)}/{len(mutating)} mutating tools ({refresh_percent:.1f}%) mention refresh helpers"),
        ("run trace artifact", status(has_run_trace), "tool execution JSONL trace is wired" if has_run_trace else "missing tool execution JSONL trace"),
        ("large yeet_ai files", status(not large_files, warn=bool(large_files)), f"{len(large_files)} files over 750 lines"),
    ]

    print("Yeet AI scorecard:")
    for name, gate_status, detail in rows:
        print(f"- {gate_status}: {name} - {detail}")

    if dataset_missing_runtime:
        print("\nDataset tools missing runtime dispatch:")
        for name in dataset_missing_runtime:
            print(f"- {name}")

    uncovered_mutating = sorted(set(mutating) - set(undo_covered))
    if uncovered_mutating:
        print("\nSample mutating tools without UndoRedo helper markers:")
        for name in uncovered_mutating[:30]:
            print(f"- {name}")

    if large_files:
        print("\nLarge files:")
        for path, line_count in large_files:
            print(f"- {path}: {line_count} lines")

    return 0 if registry_ok and schema_route_ok and has_run_trace else 1


if __name__ == "__main__":
    sys.exit(main())
