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


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def strip_legacy_block(text: str) -> str:
    return LEGACY_BLOCK_RE.sub("", text)


def find_declared_tools() -> set[str]:
    return set(DECL_RE.findall(read_text(DOCK_H)))


def find_implemented_tools() -> set[str]:
    implemented: set[str] = set()
    for path in YEET_DIR.glob("*.cpp"):
        implemented.update(IMPL_RE.findall(read_text(path)))
    return implemented


def find_dispatch_entries() -> dict[str, str]:
    text = strip_legacy_block(read_text(TOOLS_CPP))
    entries: dict[str, str] = {}
    duplicates: dict[str, int] = {}
    for match in ENTRY_RE.finditer(text):
        name = match.group("name")
        handler = match.group("handler")
        if name in entries:
            duplicates[name] = duplicates.get(name, 1) + 1
        entries[name] = handler
    if duplicates:
        dupes = ", ".join(f"{name} x{count}" for name, count in sorted(duplicates.items()))
        raise ValueError(f"Duplicate dispatch table tool names: {dupes}")
    return entries


def find_explicit_schema_names() -> set[str]:
    return set(EXPLICIT_SCHEMA_RE.findall(read_text(SCHEMA_CPP)))


def find_dataset_tool_names() -> set[str]:
    if not DATASET_TOOLS.exists():
        return set()
    data = json.loads(DATASET_TOOLS.read_text(encoding="utf-8"))
    tools = data.get("tools", [])
    names = set()
    for tool in tools:
        name = tool.get("name")
        if isinstance(name, str):
            names.add(name)
    return names


def likely_inferred_schema(name: str) -> bool:
    prefixes = (
        "add_",
        "assign_",
        "attach_",
        "audit_",
        "batch_",
        "capture_",
        "clear_",
        "connect_",
        "copy_",
        "create_",
        "debugger_",
        "delete_",
        "disconnect_",
        "duplicate_",
        "edit_",
        "export_",
        "extract_",
        "file_",
        "fill_",
        "find_",
        "focus_",
        "get_",
        "git_",
        "grep_",
        "import_",
        "index_",
        "inspect_",
        "instantiate_",
        "lint_",
        "list_",
        "manage_",
        "memory_",
        "merge_",
        "monitor_",
        "move_",
        "open_",
        "paint_",
        "patch_",
        "play_",
        "profile_",
        "project_",
        "query_",
        "raycast_",
        "read_",
        "reimport_",
        "reload_",
        "request_",
        "remove_",
        "rename_",
        "reparent_",
        "repair_",
        "replace_",
        "resolve_",
        "resource_",
        "runtime_",
        "run_",
        "save_",
        "scaffold_",
        "scan_",
        "scene_",
        "search_",
        "select_",
        "set_",
        "shape_",
        "stop_",
        "update_",
        "validate_",
        "write_",
    )
    return name.startswith(prefixes)


def main() -> int:
    errors: list[str] = []
    warnings: list[str] = []

    try:
        dispatch_entries = find_dispatch_entries()
    except ValueError as exc:
        print(f"Yeet AI tool registry check failed: {exc}")
        return 1

    declared = find_declared_tools()
    implemented = find_implemented_tools()
    explicit_schemas = find_explicit_schema_names()
    dataset_names = find_dataset_tool_names()

    dispatch_handlers = set(dispatch_entries.values())
    dispatch_names = set(dispatch_entries.keys())

    missing_declarations = sorted(dispatch_handlers - declared)
    if missing_declarations:
        errors.append("Dispatch handlers missing declarations in yeet_ai_dock.h: " + ", ".join(missing_declarations))

    missing_implementations = sorted(dispatch_handlers - implemented)
    if missing_implementations:
        errors.append("Dispatch handlers missing implementations: " + ", ".join(missing_implementations))

    declared_but_unregistered = sorted(declared - dispatch_handlers)
    if declared_but_unregistered:
        warnings.append("Declared _tool_* handlers not registered in dispatch table: " + ", ".join(declared_but_unregistered))

    implemented_but_unregistered = sorted(implemented - dispatch_handlers)
    if implemented_but_unregistered:
        warnings.append("Implemented _tool_* handlers not registered in dispatch table: " + ", ".join(implemented_but_unregistered))

    schema_without_tool = sorted(explicit_schemas - dispatch_names)
    if schema_without_tool:
        warnings.append("Explicit schemas without dispatch tool: " + ", ".join(schema_without_tool))

    no_schema_route = sorted(name for name in dispatch_names if name not in explicit_schemas and not likely_inferred_schema(name))
    if no_schema_route:
        errors.append("Dispatch tools lack explicit schema and recognized inferred-schema prefix: " + ", ".join(no_schema_route))

    dataset_without_runtime = sorted(dataset_names - dispatch_names)
    if dataset_without_runtime:
        warnings.append("Dataset tool names not present in runtime dispatch table: " + ", ".join(dataset_without_runtime))

    runtime_without_dataset = sorted(dispatch_names - dataset_names)

    print("Yeet AI tool registry summary:")
    print(f"- dispatch tools: {len(dispatch_names)}")
    print(f"- declared handlers: {len(declared)}")
    print(f"- implemented handlers: {len(implemented)}")
    print(f"- explicit schemas: {len(explicit_schemas)}")
    print(f"- dataset tools: {len(dataset_names)}")
    print(f"- runtime tools absent from dataset definitions: {len(runtime_without_dataset)}")

    if warnings:
        print("\nWarnings:")
        for warning in warnings:
            print(f"- {warning}")

    if errors:
        print("\nErrors:")
        for error in errors:
            print(f"- {error}")
        return 1

    print("\nYeet AI tool registry check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
