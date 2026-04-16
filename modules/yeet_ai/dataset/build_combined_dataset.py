import json
import os
import glob
import re

BASE = r"D:\Code\YEET\crosshair\modules\yeet_ai\dataset"
OPENAI_DIR = os.path.join(BASE, "finetune_openai")
SHAREGPT_DIR = os.path.join(BASE, "finetune_sharegpt")
EXTRACTED_DIR = os.path.join(BASE, "godot_projects", "extracted")
GODOT_PROJECTS_DIR = os.path.join(BASE, "godot_projects")

SYSTEM_PROMPT = (
    'You are Crosshair AI, a game-building assistant inside the Godot editor. '
    'Respond with JSON only:\n'
    '- Tool call: {"type":"tool_call","tool":"TOOL_NAME","arguments":{...}}\n'
    '- Final answer: {"type":"final","message":"text"}\n\n'
    'Rules:\n'
    '1. GDScript tools (create_gdscript_file, update_gdscript_file) MUST be in their own batch_tool_calls, never mixed with other tools.\n'
    '2. Use batch_tool_calls with shared_arguments for multi-step scene work.\n'
    '3. For physics floors: StaticBody3D + add_primitive_mesh + add_collision_shape as children.\n'
    '4. For playable characters: CharacterBody3D + collision + input actions + mesh + script.\n'
    '5. set_node_property position: {"property":"position","value":{"x":0,"y":5,"z":0}}\n'
    '6. add_primitive_mesh types: box, sphere, capsule, cylinder, plane.\n'
    '7. Never wrap JSON in markdown. After tool result, call another tool or return final.\n'
)


def load_jsonl(path):
    items = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                items.append(json.loads(line))
    return items


def write_jsonl(path, items):
    with open(path, "w", encoding="utf-8") as f:
        for item in items:
            f.write(json.dumps(item, ensure_ascii=False) + "\n")


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def sharegpt_to_openai(conv):
    messages = []
    for turn in conv.get("conversations", []):
        fr = turn["from"]
        val = turn["value"]
        if fr == "system":
            messages.append({"role": "system", "content": SYSTEM_PROMPT})
        elif fr == "human":
            messages.append({"role": "user", "content": val})
        elif fr == "gpt":
            messages.append({"role": "assistant", "content": val})
    return {"messages": messages}


def openai_to_sharegpt(messages_obj):
    convs = []
    for msg in messages_obj["messages"]:
        role = msg["role"]
        content = msg["content"]
        if role == "system":
            convs.append({"from": "system", "value": SYSTEM_PROMPT})
        elif role == "user":
            convs.append({"from": "human", "value": content})
        elif role == "assistant":
            convs.append({"from": "gpt", "value": content})
    return {"conversations": convs}


def make_tool_result(results):
    if isinstance(results, list):
        return json.dumps({"status": "ok", "results": results})
    return json.dumps({"status": "ok", "message": str(results)})


def make_ok_result(tool_name, msg="Executed " + "{}"):
    return {"status": "ok", "tool": tool_name, "message": msg.format(tool_name) if "{}" in msg else msg}


# ─── Task A: crosshair_real_world.jsonl ───────────────────────────────────────
def task_a():
    print("=== Task A: Converting real-world conversations ===")
    convs = load_jsonl(os.path.join(EXTRACTED_DIR, "conversations_from_projects.jsonl"))
    openai_items = []
    for conv in convs:
        item = sharegpt_to_openai(conv)
        msgs = item["messages"]
        if len(msgs) >= 3:
            openai_items.append(item)
    out = os.path.join(OPENAI_DIR, "crosshair_real_world.jsonl")
    write_jsonl(out, openai_items)
    print(f"  Wrote {len(openai_items)} examples to {out}")
    return openai_items


# ─── Task B helpers: generate examples from patterns/configs ─────────────────
def split_tool_calls_gdscript_separate(tool_calls):
    gdscript_calls = []
    other_calls = []
    for tc in tool_calls:
        tool = tc.get("tool", "")
        if tool in ("create_gdscript_file", "update_gdscript_file"):
            gdscript_calls.append(tc)
        else:
            other_calls.append(tc)
    return other_calls, gdscript_calls


def build_batch_call(calls):
    return json.dumps({"type": "tool_call", "tool": "batch_tool_calls", "arguments": {"calls": calls}})


def build_single_call(tool_name, arguments):
    return json.dumps({"type": "tool_call", "tool": tool_name, "arguments": arguments})


def build_tool_call_response(tool_calls_list):
    if len(tool_calls_list) == 1:
        tc = tool_calls_list[0]
        return build_single_call(tc["tool"], tc["arguments"])
    return build_batch_call(tool_calls_list)


def make_tool_result_from_calls(calls):
    results = []
    for tc in calls:
        tool = tc.get("tool", "unknown")
        if tool in ("create_gdscript_file", "update_gdscript_file"):
            path = tc["arguments"].get("script_path", "res://unknown.gd")
            results.append({"status": "ok", "script_path": path, "message": f"Created script at {path}"})
        else:
            results.append(make_ok_result(tool))
    return json.dumps({"status": "ok", "results": results})


def gdscript_pattern_to_openai(pattern):
    pattern_name = pattern.get("pattern_name", "GDScript pattern")
    source_repo = pattern.get("source_repo", "godot-demo-projects")
    source_path = pattern.get("source_path", "")
    extends_class = pattern.get("extends_class", "Node")
    description = pattern.get("description", "")
    tool_calls_needed = pattern.get("tool_calls_needed", [])

    user_msg = f"Create a {pattern_name.split(' - ')[-1] if ' - ' in pattern_name else extends_class} script like the one from {source_repo}"

    other_calls, gdscript_calls = split_tool_calls_gdscript_separate(tool_calls_needed)

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": user_msg},
    ]

    if other_calls and gdscript_calls:
        messages.append({"role": "assistant", "content": build_batch_call(other_calls)})
        messages.append({"role": "user", "content": make_tool_result_from_calls(other_calls)})
        messages.append({"role": "assistant", "content": build_batch_call(gdscript_calls)})
        messages.append({"role": "user", "content": make_tool_result_from_calls(gdscript_calls)})
    elif other_calls:
        messages.append({"role": "assistant", "content": build_tool_call_response(other_calls)})
        messages.append({"role": "user", "content": make_tool_result_from_calls(other_calls)})
    elif gdscript_calls:
        messages.append({"role": "assistant", "content": build_batch_call(gdscript_calls)})
        messages.append({"role": "user", "content": make_tool_result_from_calls(gdscript_calls)})

    final_msg = f"Created {extends_class} script"
    if source_path:
        final_msg += f" based on {source_repo}: {source_path}"
    final_msg += ". The node and script are attached and ready to use."
    messages.append({"role": "assistant", "content": json.dumps({"type": "final", "message": final_msg})})

    return {"messages": messages}


def scene_pattern_to_openai(pattern):
    pattern_name = pattern.get("pattern_name", "Scene pattern")
    source_repo = pattern.get("source_repo", "")
    root_type = pattern.get("root_node_type", "Node")
    root_name = pattern.get("root_node_name", "Root")
    node_tree = pattern.get("node_tree", [])
    description = pattern.get("description", "")
    tool_calls_to_recreate = pattern.get("tool_calls_to_recreate", [])

    user_msg = f"Create a scene like {pattern_name} with root type {root_type}"

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": user_msg},
    ]

    if tool_calls_to_recreate:
        other_calls, gdscript_calls = split_tool_calls_gdscript_separate(tool_calls_to_recreate)
        all_non_gd = other_calls
        if all_non_gd:
            messages.append({"role": "assistant", "content": build_batch_call(all_non_gd)})
            messages.append({"role": "user", "content": make_tool_result_from_calls(all_non_gd)})
        if gdscript_calls:
            messages.append({"role": "assistant", "content": build_batch_call(gdscript_calls)})
            messages.append({"role": "user", "content": make_tool_result_from_calls(gdscript_calls)})
    else:
        node_count = _count_nodes(node_tree)
        calls = []
        for node in node_tree:
            calls.append({"tool": "add_node", "arguments": {"node_type": node["type"], "node_name": node["name"], "parent_path": None}})
        if calls:
            messages.append({"role": "assistant", "content": build_batch_call(calls)})
            messages.append({"role": "user", "content": make_tool_result_from_calls(calls)})

    final_msg = f"Created scene {root_name} ({root_type})"
    if description:
        final_msg += f". {description}"
    messages.append({"role": "assistant", "content": json.dumps({"type": "final", "message": final_msg})})

    return {"messages": messages}


def _count_nodes(tree):
    count = 0
    for node in tree:
        count += 1
        if "children" in node:
            count += _count_nodes(node["children"])
    return count


def project_config_to_openai(config):
    project_name = config.get("project_name", "MyProject")
    source_repo = config.get("source_repo", "")
    main_scene = config.get("main_scene", "")
    input_actions = config.get("input_actions", [])
    tool_calls_to_configure = config.get("tool_calls_to_configure", [])
    autoloads = config.get("autoloads", [])
    layer_names = config.get("layer_names", {})

    user_msg = f"Set up a project like {project_name}"
    if source_repo:
        user_msg += f" (from {source_repo})"
    if main_scene:
        user_msg += f" with main scene {main_scene}"

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": user_msg},
    ]

    if tool_calls_to_configure:
        other_calls, gdscript_calls = split_tool_calls_gdscript_separate(tool_calls_to_configure)
        all_non_gd = other_calls
        if all_non_gd:
            messages.append({"role": "assistant", "content": build_batch_call(all_non_gd)})
            messages.append({"role": "user", "content": make_tool_result_from_calls(all_non_gd)})
        if gdscript_calls:
            messages.append({"role": "assistant", "content": build_batch_call(gdscript_calls)})
            messages.append({"role": "user", "content": make_tool_result_from_calls(gdscript_calls)})
    else:
        basic_calls = []
        basic_calls.append({"tool": "set_project_setting", "arguments": {"section": "application", "key": "config/name", "value": project_name}})
        if main_scene:
            basic_calls.append({"tool": "set_project_setting", "arguments": {"section": "application", "key": "run/main_scene", "value": main_scene}})
        for action in input_actions[:5]:
            basic_calls.append({"tool": "add_input_action", "arguments": {"action_name": action, "deadzone": 0.2}})
        if basic_calls:
            messages.append({"role": "assistant", "content": build_batch_call(basic_calls)})
            messages.append({"role": "user", "content": make_tool_result_from_calls(basic_calls)})

    final_msg = f"Project {project_name} configured"
    if input_actions:
        final_msg += f" with {len(input_actions)} input actions"
    if autoloads:
        final_msg += f" and {len(autoloads)} autoloads"
    final_msg += ". Ready to build!"
    messages.append({"role": "assistant", "content": json.dumps({"type": "final", "message": final_msg})})

    return {"messages": messages}


# ─── Task B ───────────────────────────────────────────────────────────────────
def task_b(real_world_items):
    print("=== Task B: Building combined OpenAI dataset ===")
    synthetic = load_jsonl(os.path.join(OPENAI_DIR, "crosshair_tool_calls.jsonl"))
    gdscript_patterns = load_json(os.path.join(EXTRACTED_DIR, "gdscript_patterns.json"))
    scene_patterns = load_json(os.path.join(EXTRACTED_DIR, "scene_patterns.json"))
    project_configs = load_json(os.path.join(EXTRACTED_DIR, "project_configs.json"))

    all_items = []

    for item in synthetic:
        msgs = item["messages"]
        if msgs and msgs[0]["role"] == "system":
            msgs[0]["content"] = SYSTEM_PROMPT
        all_items.append(item)

    all_items.extend(real_world_items)

    gdscript_examples = []
    for p in gdscript_patterns[:50]:
        ex = gdscript_pattern_to_openai(p)
        gdscript_examples.append(ex)
    all_items.extend(gdscript_examples)

    scene_examples = []
    for p in scene_patterns[:30]:
        ex = scene_pattern_to_openai(p)
        scene_examples.append(ex)
    all_items.extend(scene_examples)

    config_examples = []
    for c in project_configs[:20]:
        ex = project_config_to_openai(c)
        config_examples.append(ex)
    all_items.extend(config_examples)

    out = os.path.join(OPENAI_DIR, "crosshair_all_combined.jsonl")
    write_jsonl(out, all_items)
    print(f"  Wrote {len(all_items)} examples to {out}")
    print(f"    Synthetic: {len(synthetic)}, Real-world: {len(real_world_items)}, GDScript patterns: {len(gdscript_examples)}, Scene patterns: {len(scene_examples)}, Project configs: {len(config_examples)}")
    return all_items


# ─── Task C ───────────────────────────────────────────────────────────────────
def task_c(all_openai_items):
    print("=== Task C: Building combined ShareGPT dataset ===")
    sharegpt_items = []
    sharegpt_synthetic = load_jsonl(os.path.join(SHAREGPT_DIR, "crosshair_tool_calls_sharegpt.jsonl"))

    for item in sharegpt_synthetic:
        convs = item["conversations"]
        if convs and convs[0]["from"] == "system":
            convs[0]["value"] = SYSTEM_PROMPT
        sharegpt_items.append(item)

    for item in all_openai_items:
        sg = openai_to_sharegpt(item)
        sharegpt_items.append(sg)

    seen = set()
    unique_items = []
    for item in sharegpt_items:
        key = json.dumps(item, sort_keys=True)
        if key not in seen:
            seen.add(key)
            unique_items.append(item)

    out = os.path.join(SHAREGPT_DIR, "crosshair_all_combined_sharegpt.jsonl")
    write_jsonl(out, unique_items)
    print(f"  Wrote {len(unique_items)} examples to {out}")


# ─── Task D ───────────────────────────────────────────────────────────────────
def task_d():
    print("=== Task D: Building GDScript code corpus ===")
    gd_files = []

    priority_repos = ["godot-demo-projects", "beehave", "phantom-camera"]
    for repo_name in priority_repos:
        repo_path = os.path.join(GODOT_PROJECTS_DIR, repo_name)
        if os.path.isdir(repo_path):
            found = glob.glob(os.path.join(repo_path, "**", "*.gd"), recursive=True)
            gd_files.extend(found)

    if len(gd_files) < 200:
        for d in os.listdir(GODOT_PROJECTS_DIR):
            dp = os.path.join(GODOT_PROJECTS_DIR, d)
            if os.path.isdir(dp) and d not in priority_repos:
                found = glob.glob(os.path.join(dp, "**", "*.gd"), recursive=True)
                gd_files.extend(found)

    corpus = []
    seen_hashes = set()

    for fpath in gd_files:
        try:
            with open(fpath, "r", encoding="utf-8", errors="replace") as f:
                code = f.read()
        except Exception:
            continue

        if len(code.strip()) < 20:
            continue

        code_hash = hash(code.strip())
        if code_hash in seen_hashes:
            continue
        seen_hashes.add(code_hash)

        rel_path = os.path.relpath(fpath, GODOT_PROJECTS_DIR)
        parts = rel_path.replace("\\", "/").split("/")
        source_repo = parts[0] if parts else "unknown"
        file_path = "/".join(parts[1:]) if len(parts) > 1 else rel_path

        extends_class = "Node"
        extends_match = re.search(r'^extends\s+(\w+)', code, re.MULTILINE)
        if extends_match:
            extends_class = extends_match.group(1)

        entry = {
            "code": code,
            "extends": extends_class,
            "source_repo": source_repo,
            "file_path": file_path,
        }
        corpus.append(entry)

        if len(corpus) >= 300:
            break

    out = os.path.join(EXTRACTED_DIR, "gdscript_code_corpus.jsonl")
    write_jsonl(out, corpus)
    print(f"  Wrote {len(corpus)} entries to {out}")


# ─── Main ─────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    real_world_items = task_a()
    all_openai_items = task_b(real_world_items)
    task_c(all_openai_items)
    task_d()
    print("\nAll tasks complete!")
