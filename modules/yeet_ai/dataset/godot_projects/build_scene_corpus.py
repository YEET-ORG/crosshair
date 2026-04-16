#!/usr/bin/env python3
import os
import json
import re
import glob

BASE_DIR = r"D:\Code\YEET\crosshair\modules\yeet_ai\dataset\godot_projects"
OUTPUT = r"D:\Code\YEET\crosshair\modules\yeet_ai\dataset\godot_projects\extracted\scene_corpus.jsonl"

REPO_DIRS = [
    "godot-demo-projects",
    "godot-open-rpg",
    "godot-mini-tuts-demos",
    "godot-make-pro-2d-games",
    "godot-shaders",
    "godot-2d-builder",
    "godot-2d-tower-defense",
    "godot-2d-space-game",
    "godot-2d-jrpg-combat",
    "beehave",
    "godot_dialogue_manager",
]

# Prioritize diverse node types for selection
PRIORITY_TYPES = {
    "CharacterBody2D", "CharacterBody3D", "RigidBody2D", "RigidBody3D",
    "Node2D", "Node3D", "Control", "Panel", "VBoxContainer", "HBoxContainer",
    "CanvasLayer", "ColorRect", "Label", "Button", "TextureRect",
    "Area2D", "Area3D", "StaticBody2D", "StaticBody3D",
    "AnimationPlayer", "Sprite2D", "Sprite3D", "Camera2D", "Camera3D",
    "TileMap", "TileMapLayer", "Polygon2D", "PathFollow2D",
}

def parse_tscn(content):
    """Parse a .tscn file and extract node information."""
    lines = content.split('\n')
    nodes = []
    is_godot4 = False
    
    for line in lines:
        line = line.strip()
        if line.startswith('[node '):
            # Extract name
            name_match = re.search(r'name="([^"]*)"', line)
            # Extract type (Godot 4 format)
            type_match = re.search(r'type="([^"]*)"', line)
            # Extract parent
            parent_match = re.search(r'parent="([^"]*)"', line)
            # Godot 3 uses instance= instead of type=
            instance_match = re.search(r'instance=', line)
            
            if type_match:
                is_godot4 = True
            
            name = name_match.group(1) if name_match else ""
            node_type = type_match.group(1) if type_match else ""
            parent = parent_match.group(1) if parent_match else None
            
            if name:
                nodes.append({
                    "name": name,
                    "type": node_type,
                    "parent": parent,
                    "is_instance": instance_match is not None,
                })
    
    if not is_godot4 and nodes:
        has_any_type = any(n["type"] for n in nodes)
        if not has_any_type:
            return None
    
    return nodes

def build_node_tree(nodes):
    """Build a flat node tree with paths."""
    tree = []
    
    # Build parent-child mapping
    for node in nodes:
        if node["parent"] is None:
            # Root node
            tree.append({
                "path": ".",
                "type": node["type"],
                "name": node["name"],
            })
        else:
            # Find parent name
            parent_path = node["parent"]
            if parent_path == ".":
                path = node["name"]
            else:
                path = parent_path + "/" + node["name"]
            
            tree.append({
                "path": path,
                "type": node["type"],
                "name": node["name"],
            })
    
    return tree

def score_diversity(nodes, tree):
    """Score a file for diversity (prefer files with interesting node types)."""
    score = 0
    types_seen = set()
    for node in nodes:
        t = node["type"]
        if t in PRIORITY_TYPES:
            score += 10
        if t and t not in types_seen:
            score += 5
            types_seen.add(t)
    # Bonus for root nodes that are character/physics bodies
    if nodes and nodes[0]["type"] in ("CharacterBody2D", "CharacterBody3D", "RigidBody2D", "RigidBody3D"):
        score += 20
    # Bonus for UI root nodes
    if nodes and nodes[0]["type"] in ("Control", "Panel", "VBoxContainer", "HBoxContainer", "CanvasLayer"):
        score += 15
    return score

def main():
    entries = []
    seen_types = {}  # Track diversity of root types
    
    for repo_name in REPO_DIRS:
        repo_path = os.path.join(BASE_DIR, repo_name)
        if not os.path.isdir(repo_path):
            continue
        
        tscn_files = glob.glob(os.path.join(repo_path, "**", "*.tscn"), recursive=True)
        
        for fpath in tscn_files:
            try:
                with open(fpath, 'r', encoding='utf-8', errors='replace') as f:
                    content = f.read()
            except Exception:
                continue
            
            # Skip very large files (>100KB)
            if len(content) > 100000:
                continue
            
            nodes = parse_tscn(content)
            
            if nodes is None:
                continue
            
            if not nodes:
                continue
            
            # Must have at least one node with a type
            typed_nodes = [n for n in nodes if n["type"]]
            if not typed_nodes:
                continue
            
            root_node = nodes[0]
            root_type = root_node["type"]
            root_name = root_node["name"]
            
            if not root_type:
                continue
            
            tree = build_node_tree(nodes)
            node_count = len(nodes)
            
            # Relative path from repo root
            rel_path = os.path.relpath(fpath, repo_path).replace("\\", "/")
            
            diversity_score = score_diversity(nodes, tree)
            
            entry = {
                "source_repo": repo_name,
                "file_path": rel_path,
                "root_node_type": root_type,
                "root_node_name": root_name,
                "node_count": node_count,
                "node_tree": tree,
                "raw_content": content,
                "_diversity_score": diversity_score,
            }
            
            entries.append(entry)
            
            # Track root types for diversity
            if root_type not in seen_types:
                seen_types[root_type] = 0
            seen_types[root_type] += 1
    
    print(f"Total parsed entries: {len(entries)}")
    print(f"Unique root types: {len(seen_types)}")
    
    # Sort by diversity score (descending) then ensure we get diverse types
    entries.sort(key=lambda e: e["_diversity_score"], reverse=True)
    
    # Select entries ensuring diversity of root types
    # Take all entries with good diversity scores, capped at 300
    # But ensure at least 2 of each root type if available
    selected = []
    type_counts = {}
    
    # First pass: take high-value entries (characters, UI, levels)
    for entry in entries:
        rt = entry["root_node_type"]
        if rt not in type_counts:
            type_counts[rt] = 0
        
        # Always include character body scenes
        if rt in ("CharacterBody2D", "CharacterBody3D", "RigidBody2D", "RigidBody3D"):
            if type_counts[rt] < 10:
                selected.append(entry)
                type_counts[rt] += 1
                continue
        
        # Include UI scenes
        if rt in ("Control", "Panel", "VBoxContainer", "HBoxContainer", "CanvasLayer", "ColorRect"):
            if type_counts[rt] < 10:
                selected.append(entry)
                type_counts[rt] += 1
                continue
        
        # Include level/environment scenes
        if rt in ("Node2D", "Node3D") and entry["node_count"] > 5:
            if type_counts[rt] < 10:
                selected.append(entry)
                type_counts[rt] += 1
                continue
    
    # Second pass: fill remaining with diverse types
    for entry in entries:
        if entry in selected:
            continue
        if len(selected) >= 300:
            break
        
        rt = entry["root_node_type"]
        if rt not in type_counts:
            type_counts[rt] = 0
        
        # Allow up to 5 of each type
        if type_counts[rt] < 5:
            selected.append(entry)
            type_counts[rt] += 1
    
    # Third pass: fill up to 300
    for entry in entries:
        if entry in selected:
            continue
        if len(selected) >= 300:
            break
        
        rt = entry["root_node_type"]
        if rt not in type_counts:
            type_counts[rt] = 0
        if type_counts[rt] < 3:
            selected.append(entry)
            type_counts[rt] += 1
    
    # If still under 100, just add more
    for entry in entries:
        if entry in selected:
            continue
        if len(selected) >= 300:
            break
        selected.append(entry)
    
    # Remove internal score field and write
    final_type_counts = {}
    for entry in selected:
        del entry["_diversity_score"]
        rt = entry["root_node_type"]
        if rt not in final_type_counts:
            final_type_counts[rt] = 0
        final_type_counts[rt] += 1
    
    print(f"Selected entries: {len(selected)}")
    print(f"Root type distribution:")
    for t, c in sorted(final_type_counts.items(), key=lambda x: -x[1]):
        print(f"  {t}: {c}")
    
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, 'w', encoding='utf-8') as f:
        for entry in selected:
            line = json.dumps(entry, ensure_ascii=False)
            f.write(line + '\n')
    
    print(f"Written to {OUTPUT}")

if __name__ == "__main__":
    main()
