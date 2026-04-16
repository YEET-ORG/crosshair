const fs = require('fs');
const path = require('path');

const BASE = path.resolve(__dirname, '..');
const OUT = path.join(BASE, 'extracted');
fs.mkdirSync(OUT, { recursive: true });

function readText(p) { try { return fs.readFileSync(p, 'utf8'); } catch { return null; } }

function walkDir(dir, ext, maxLen) {
  const results = [];
  (function walk(d, prefix) {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const full = path.join(d, e.name);
      const rel = prefix ? prefix + '/' + e.name : e.name;
      if (e.isDirectory()) walk(full, rel);
      else if (e.name.endsWith(ext)) {
        try { const s = fs.statSync(full); if (s.size > 10 && s.size < maxLen) results.push({ rel, full, size: s.size }); } catch {}
      }
    }
  })(dir, '');
  return results;
}

function extractExtends(c) { const m = c.match(/^extends\s+(\S+)/m); return m ? m[1] : null; }
function extractClassName(c) { const m = c.match(/^class_name\s+(\S+)/m); return m ? m[1] : null; }

function categorize(content, relPath) {
  const lo = relPath.toLowerCase();
  const ext = extractExtends(content);
  // Prioritize by extends class first (most reliable signal)
  if (ext === 'CharacterBody3D') {
    if (lo.includes('player') || lo.includes('fps') || lo.includes('tps')) return '3D CharacterBody movement';
    if (lo.includes('enemy') || lo.includes('mob')) return '3D AI / enemy behavior';
    return '3D CharacterBody movement';
  }
  if (ext === 'CharacterBody2D') {
    if (lo.includes('player')) return '2D CharacterBody movement';
    if (lo.includes('enemy') || lo.includes('mob')) return '2D AI / enemy behavior';
    return '2D CharacterBody movement';
  }
  if (ext === 'VehicleBody3D') return 'Vehicle controller';
  if (ext === 'RigidBody3D') return '3D RigidBody interaction';
  if (ext === 'RigidBody2D') return '2D RigidBody interaction';
  if (ext === 'Area2D') return '2D Area detection / signal';
  if (ext === 'Area3D') return '3D Area detection / signal';
  if (ext === 'Camera3D' || ext === 'Camera2D') return 'Camera system';
  if (ext === 'StaticBody3D') return 'Procedural generation';
  if (ext === 'Path2D') return 'Tactical / grid movement';
  // Then by path/content
  if (lo.includes('state_machine') || lo.includes('state.gd') || lo.includes('fsm')) return 'State machine';
  if (lo.includes('multiplayer') || lo.includes('networking') || content.includes('@rpc') || content.includes('multiplayer')) return 'Multiplayer';
  if (lo.includes('navigation') || lo.includes('pathfind') || lo.includes('nav') || lo.includes('navigationagent')) return 'Navigation / pathfinding';
  if (lo.includes('dialogue') || lo.includes('text_box') || lo.includes('visual_novel') || lo.includes('balloon')) return 'Dialogue system';
  if (lo.includes('tower') || lo.includes('weapon2d')) return 'Tower defense / weapons';
  if (lo.includes('steering') || lo.includes('gsai')) return 'Steering behaviors';
  if (lo.includes('procedural') || lo.includes('dungeon') || lo.includes('chunk') || lo.includes('voxel') || lo.includes('generator')) return 'Procedural generation';
  if (lo.includes('audio') || lo.includes('sound') || lo.includes('music') || lo.includes('piano') || lo.includes('rhythm') || lo.includes('conductor')) return 'Audio / music';
  if (lo.includes('save') || lo.includes('load') || lo.includes('serial')) return 'Saving / loading';
  if (lo.includes('beehave') || lo.includes('behavior_tree')) return 'Behavior tree';
  if (lo.includes('ragdoll') || lo.includes('vehicle')) return 'Physics interaction';
  if (lo.includes('tween')) return 'Animation / tween';
  if (lo.includes('shader') || lo.includes('material')) return 'Shader / material';
  if (lo.includes('hud') || lo.includes('pause_menu') || lo.includes('main_menu')) return 'HUD / menus';
  if (ext === 'Control') return 'UI pattern';
  if (ext === 'Node2D') return '2D game logic';
  if (ext === 'Node3D') return '3D game logic';
  if (ext === 'Node') return 'Utility / autoload';
  return 'General GDScript';
}

// Collect all files
const gdFiles = [], tscnFiles = [], projectFiles = [];
for (const repo of fs.readdirSync(BASE)) {
  const rp = path.join(BASE, repo);
  try { if (!fs.statSync(rp).isDirectory() || repo === 'extracted') continue; } catch { continue; }
  for (const f of walkDir(rp, '.gd', 20000)) gdFiles.push({ ...f, repo });
  for (const f of walkDir(rp, '.tscn', 25000)) tscnFiles.push({ ...f, repo });
  for (const f of walkDir(rp, 'project.godot', 30000)) projectFiles.push({ ...f, repo });
}
console.log('Found:', gdFiles.length, '.gd,', tscnFiles.length, '.tscn,', projectFiles.length, 'project.godot');

// === 1. gdscript_patterns.json ===
console.log('Building gdscript_patterns.json...');
const gdPatterns = [];
let gdCount = 0;

// Categorize all files first, then pick balanced set
var catMap = {};
for (var fi = 0; fi < gdFiles.length; fi++) {
  var f = gdFiles[fi];
  var content = readText(f.full);
  if (!content || content.trim().length < 20) continue;
  var ext = extractExtends(content);
  if (!ext) continue;
  var cat = categorize(content, f.rel);
  if (!catMap[cat]) catMap[cat] = [];
  catMap[cat].push({ f, content, ext, cls: extractClassName(content), cat });
}

// Pick up to 6 per category, prioritizing game-relevant ones
var gameCategories = ['3D CharacterBody movement', '2D CharacterBody movement', '3D AI / enemy behavior', '2D AI / enemy behavior', 'Camera system', 'State machine', 'Multiplayer', 'Navigation / pathfinding', 'Dialogue system', 'Tower defense / weapons', 'Steering behaviors', 'Procedural generation', 'Audio / music', 'Vehicle controller', '3D RigidBody interaction', '2D RigidBody interaction', '2D Area detection / signal', '3D Area detection / signal', 'Tactical / grid movement', 'Behavior tree', 'Saving / loading', 'HUD / menus', 'Animation / tween', 'Physics interaction', '2D game logic', '3D game logic', 'Utility / autoload'];

var selectedPatterns = [];
for (var gci = 0; gci < gameCategories.length; gci++) {
  var gc = gameCategories[gci];
  if (!catMap[gc]) continue;
  var items = catMap[gc].slice(0, 6);
  for (var ii = 0; ii < items.length; ii++) {
    selectedPatterns.push(items[ii]);
  }
  delete catMap[gc];
}

// Fill remaining with other categories
var remainingCats = Object.keys(catMap);
for (var rci = 0; rci < remainingCats.length && selectedPatterns.length < 80; rci++) {
  var items = catMap[remainingCats[rci]].slice(0, 3);
  for (var ii = 0; ii < items.length; ii++) {
    selectedPatterns.push(items[ii]);
  }
}

selectedPatterns = selectedPatterns.slice(0, 80);

for (var si = 0; si < selectedPatterns.length; si++) {
  var item = selectedPatterns[si];
  var f = item.f;
  var content = item.content;
  var ext = item.ext;
  var cls = item.cls;
  var cat = item.cat;
  if (gdCount >= 80) break;
  if (!content || content.trim().length < 20) continue;
  if (!ext) continue;

  const toolCalls = [];
  const nodeName = cls || 'NewNode';
  toolCalls.push({ tool: 'add_node', arguments: { node_type: ext, node_name: nodeName } });

  if (ext.includes('CharacterBody3D')) {
    toolCalls.push({ tool: 'add_collision_shape', arguments: { parent_path: nodeName, node_name: 'Collision', shape_type: 'capsule', parameters: { radius: 0.5, height: 1.6 } } });
  } else if (ext.includes('CharacterBody2D')) {
    toolCalls.push({ tool: 'add_collision_shape', arguments: { parent_path: nodeName, node_name: 'Collision', shape_type: 'capsule', parameters: { radius: 14, height: 32 } } });
  } else if (ext.includes('RigidBody')) {
    toolCalls.push({ tool: 'add_collision_shape', arguments: { parent_path: nodeName, node_name: 'Collision', shape_type: 'box', parameters: { size: [1, 1, 1] } } });
  } else if (ext.includes('Area')) {
    toolCalls.push({ tool: 'add_collision_shape', arguments: { parent_path: nodeName, node_name: 'Collision', shape_type: 'sphere', parameters: { radius: 1.0 } } });
  } else if (ext.includes('VehicleBody3D')) {
    toolCalls.push({ tool: 'add_collision_shape', arguments: { parent_path: nodeName, node_name: 'Collision', shape_type: 'box', parameters: { size: [2, 1, 4] } } });
  }

  const scriptName = f.rel.split('/').pop();
  toolCalls.push({ tool: 'create_gdscript_file', arguments: { script_path: 'res://scripts/' + scriptName, contents: content } });

  gdPatterns.push({
    pattern_name: cat + ' - ' + (cls || ext),
    source_repo: f.repo,
    source_path: f.rel,
    extends_class: ext,
    class_name: cls || null,
    script_content: content,
    tool_calls_needed: toolCalls,
    description: ext + ' script from ' + f.repo + ': ' + f.rel
  });
  gdCount++;
}
fs.writeFileSync(path.join(OUT, 'gdscript_patterns.json'), JSON.stringify(gdPatterns, null, 2));
console.log('  ->', gdPatterns.length, 'patterns');

// === 2. scene_patterns.json ===
console.log('Building scene_patterns.json...');
const scenePatterns = [];
let scCount = 0;

function parseTscn(text) {
  const nodes = [], connections = [];
  for (const line of text.split('\n')) {
    if (line.startsWith('[node ')) {
      const nm = line.match(/name="([^"]+)"/);
      const tm = line.match(/type="([^"]+)"/);
      const pm = line.match(/parent="([^"]+)"/);
      if (nm) nodes.push({ name: nm[1], type: tm ? tm[1] : null, parent: pm ? pm[1] : null });
    }
    if (line.startsWith('[connection ')) {
      const sg = line.match(/signal="([^"]+)"/);
      const fm = line.match(/from="([^"]+)"/);
      const tm = line.match(/to="([^"]+)"/);
      const mm = line.match(/method="([^"]+)"/);
      if (sg && fm) connections.push({ signal: sg[1], from: fm[1], to: tm || fm[1], method: mm || '' });
    }
  }
  return { nodes, connections };
}

function buildTree(parsed) {
  const root = parsed.find(n => n.parent === null);
  if (!root) return [];
  const tree = [{ name: root.name, type: root.type || 'Node', children: [] }];
  function addKids(pname, arr) {
    for (const n of parsed) {
      if (n.parent === pname) {
        const ch = { name: (n.name.split('/').pop() || n.name), type: n.type || 'Node', children: [] };
        arr.push(ch);
        addKids(n.name, ch.children);
      }
    }
  }
  addKids(root.name, tree[0].children);
  return tree;
}

function treeToToolCalls(tree, parentPath) {
  const calls = [];
  for (const node of tree) {
    const np = parentPath ? parentPath + '/' + node.name : node.name;
    calls.push({ tool: 'add_node', arguments: { node_type: node.type, node_name: node.name, parent_path: parentPath || null } });
    if (node.type && node.type.includes('CharacterBody')) {
      calls.push({ tool: 'add_collision_shape', arguments: { parent_path: np, node_name: 'Collision', shape_type: node.type.includes('3D') ? 'capsule' : 'capsule', parameters: node.type.includes('3D') ? { radius: 0.5, height: 1.6 } : { radius: 14, height: 32 } } });
    }
    if (node.children.length > 0) calls.push(...treeToToolCalls(node.children, np));
  }
  return calls;
}

for (const f of tscnFiles) {
  if (scCount >= 50) break;
  const content = readText(f.full);
  if (!content || !content.includes('[node ')) continue;
  const parsed = parseTscn(content);
  if (parsed.nodes.length < 2) continue;
  const tree = buildTree(parsed.nodes);
  if (tree.length === 0) continue;
  const calls = treeToToolCalls(tree, '');
  if (calls.length === 0) continue;
  scenePatterns.push({
    pattern_name: tree[0].type + ' scene - ' + path.basename(f.rel, '.tscn'),
    source_repo: f.repo,
    source_path: f.rel,
    root_node_type: tree[0].type,
    root_node_name: tree[0].name,
    node_tree: tree,
    connections: parsed.connections,
    tool_calls_to_recreate: calls,
    description: 'Scene with ' + parsed.nodes.length + ' nodes from ' + f.repo
  });
  scCount++;
}
fs.writeFileSync(path.join(OUT, 'scene_patterns.json'), JSON.stringify(scenePatterns, null, 2));
console.log('  ->', scenePatterns.length, 'scene patterns');

// === 3. project_configs.json ===
console.log('Building project_configs.json...');
const projConfigs = [];
let pjCount = 0;
for (const f of projectFiles) {
  if (pjCount >= 40) break;
  const content = readText(f.full);
  if (!content) continue;
  const config = {};
  let sec = null;
  for (const line of content.split('\n')) {
    const t = line.trim();
    if (t.startsWith(';') || !t) continue;
    const sm = t.match(/^\[([^\]]+)\]/);
    if (sm) { sec = sm[1]; config[sec] = {}; continue; }
    const kv = t.match(/^([^=]+)=(.*)$/);
    if (kv && sec) config[sec][kv[1].trim()] = kv[2].trim();
  }
  const appName = config.application ? (config.application['config/name'] || '').replace(/"/g, '') : 'Unknown';
  const mainScene = config.application ? (config.application['run/main_scene'] || '').replace(/"/g, '') : '';
  const features = config.application ? (config.application['config/features'] || '').replace(/"/g, '') : '4.x';
  const inputActions = config.input ? Object.keys(config.input) : [];
  const autoloads = config.autoload ? Object.keys(config.autoload) : [];

  const toolCalls = [];
  toolCalls.push({ tool: 'set_project_setting', arguments: { section: 'application', key: 'config/name', value: appName } });
  if (mainScene) toolCalls.push({ tool: 'set_project_setting', arguments: { section: 'application', key: 'run/main_scene', value: mainScene } });
  for (const act of inputActions.slice(0, 5)) {
    toolCalls.push({ tool: 'add_input_action', arguments: { action_name: act, deadzone: 0.2 } });
  }

  projConfigs.push({
    project_name: appName,
    source_repo: f.repo,
    source_path: f.rel,
    godot_version: features,
    main_scene: mainScene,
    config_sections: Object.keys(config),
    input_actions: inputActions,
    display_settings: config.display || {},
    physics_settings: config.physics || {},
    rendering_settings: config.rendering || {},
    autoloads: autoloads,
    layer_names: config['layer_names'] || {},
    tool_calls_to_configure: toolCalls,
    raw_config: content
  });
  pjCount++;
}
fs.writeFileSync(path.join(OUT, 'project_configs.json'), JSON.stringify(projConfigs, null, 2));
console.log('  ->', projConfigs.length, 'project configs');

// === 4. conversations_from_projects.jsonl ===
console.log('Building conversations_from_projects.jsonl...');

const SYS = 'You are Crosshair AI, a Godot 4 game development assistant. You help users build Godot projects by calling tools. Available tools:\n- add_node: Add a node (node_type, node_name, parent_path)\n- add_collision_shape: Add collision shape (parent_path, node_name, shape_type, parameters)\n- create_gdscript_file: Create GDScript (script_path, contents)\n- update_gdscript_file: Update GDScript (script_path, changes)\n- set_project_setting: Set project setting (section, key, value)\n- add_input_action: Add input action (action_name, deadzone)\n- batch_tool_calls: Execute multiple calls in batch (calls, shared_arguments)\n\nRules:\n- GDScript tools MUST be in their OWN batch_tool_calls\n- Use Godot 4 syntax (extends, @export, @onready, etc.)\n- Always use real Godot 4 class names';

function toolResult(tool, args) {
  if (tool === 'batch_tool_calls') {
    return { status: 'ok', results: (args.calls || []).map(function(c) { return { status: 'ok', tool: c.tool, message: 'Executed ' + c.tool }; }) };
  }
  if (tool === 'create_gdscript_file') return { status: 'ok', script_path: args.script_path, message: 'Created script at ' + args.script_path };
  if (tool === 'add_node') return { status: 'ok', node_path: args.node_name, message: 'Added ' + args.node_type + ' "' + args.node_name + '"' };
  if (tool === 'add_collision_shape') return { status: 'ok', shape_path: args.parent_path + '/' + args.node_name, message: 'Added ' + args.shape_type + ' collision shape' };
  if (tool === 'set_project_setting') return { status: 'ok', message: 'Set ' + args.section + '/' + args.key };
  if (tool === 'add_input_action') return { status: 'ok', action: args.action_name, message: 'Added input action "' + args.action_name + '"' };
  return { status: 'ok', message: 'Executed ' + tool };
}

function toolCall(tool, args) {
  return JSON.stringify({ type: 'tool_call', tool: tool, arguments: args });
}

function finalMsg(msg) {
  return JSON.stringify({ type: 'final', message: msg });
}

var conversations = [];

// Pattern-based conversations
var prompts = [
  { prompt: 'Create a 2D platformer player with WASD movement and double jump', match: function(p) { return p.extends_class === 'CharacterBody2D' && p.pattern_name.includes('movement'); } },
  { prompt: 'Build a 3D platformer character with camera-relative movement', match: function(p) { return p.extends_class === 'CharacterBody3D' && p.pattern_name.includes('movement'); } },
  { prompt: 'Make an enemy that patrols and turns at edges', match: function(p) { return p.pattern_name.includes('enemy'); } },
  { prompt: 'Create a follow camera for 3D games with auto-avoid', match: function(p) { return p.pattern_name.includes('Camera'); } },
  { prompt: 'Build a 2D click-to-move navigation system', match: function(p) { return p.pattern_name.includes('Navigation'); } },
  { prompt: 'Implement a state machine for player animation states', match: function(p) { return p.pattern_name.includes('State machine'); } },
  { prompt: 'Set up multiplayer networking with ENet', match: function(p) { return p.pattern_name.includes('Multiplayer'); } },
  { prompt: 'Add a dialogue system with typewriter text effect', match: function(p) { return p.pattern_name.includes('Dialogue'); } },
  { prompt: 'Create a tower defense weapon that auto-targets enemies', match: function(p) { return p.pattern_name.includes('Tower defense'); } },
  { prompt: 'Build a rhythm game conductor that tracks beats', match: function(p) { return p.pattern_name.includes('Audio') || p.pattern_name.includes('rhythm'); } },
  { prompt: 'Generate a procedural dungeon using cellular automata', match: function(p) { return p.pattern_name.includes('Procedural'); } },
  { prompt: 'Implement save/load with JSON serialization', match: function(p) { return p.pattern_name.includes('Saving'); } },
  { prompt: 'Add steering AI seek and flee behaviors', match: function(p) { return p.pattern_name.includes('Steering'); } },
  { prompt: 'Create a 3D third-person shooter with root motion', match: function(p) { return p.extends_class === 'CharacterBody3D'; } },
  { prompt: 'Make a 2D top-down dodge game using Area2D', match: function(p) { return p.extends_class === 'Area2D'; } },
  { prompt: 'Build a vehicle with headlights and turbo', match: function(p) { return p.extends_class === 'VehicleBody3D'; } },
  { prompt: 'Create a ragdoll physics spawning system', match: function(p) { return p.pattern_name.includes('Physics interaction'); } },
  { prompt: 'Implement a split-screen camera system', match: function(p) { return p.pattern_name.includes('Camera'); } },
  { prompt: 'Build a pause menu with tween animations', match: function(p) { return p.pattern_name.includes('UI'); } },
  { prompt: 'Create a voxel chunk system with mesh generation', match: function(p) { return p.extends_class === 'StaticBody3D'; } },
  { prompt: 'Build a 2D RigidBody platformer player', match: function(p) { return p.extends_class === 'RigidBody2D'; } },
  { prompt: 'Create an isometric movement controller with 8-direction animation', match: function(p) { return p.pattern_name.includes('movement'); } },
  { prompt: 'Implement a behavior tree for AI', match: function(p) { return p.pattern_name.includes('Behavior tree'); } },
];

var convId = 0;
for (var pi = 0; pi < prompts.length && convId < 120; pi++) {
  var tmpl = prompts[pi];
  var matching = gdPatterns.filter(tmpl.match);
  if (matching.length === 0) continue;
  for (var mi = 0; mi < Math.min(matching.length, 2) && convId < 120; mi++) {
    var pattern = matching[mi];
    var conv = { conversations: [] };
    conv.conversations.push({ from: 'system', value: SYS });
    conv.conversations.push({ from: 'human', value: tmpl.prompt });

    var nodeCalls = pattern.tool_calls_needed.filter(function(tc) { return tc.tool !== 'create_gdscript_file'; });
    var scriptCalls = pattern.tool_calls_needed.filter(function(tc) { return tc.tool === 'create_gdscript_file'; });

    if (nodeCalls.length > 0) {
      var batchArgs = { calls: nodeCalls };
      conv.conversations.push({ from: 'gpt', value: toolCall('batch_tool_calls', batchArgs) });
      conv.conversations.push({ from: 'human', value: JSON.stringify(toolResult('batch_tool_calls', batchArgs)) });
    }

    for (var si = 0; si < scriptCalls.length; si++) {
      var sc = scriptCalls[si];
      conv.conversations.push({ from: 'gpt', value: toolCall('create_gdscript_file', sc.arguments) });
      conv.conversations.push({ from: 'human', value: JSON.stringify(toolResult('create_gdscript_file', sc.arguments)) });
    }

    conv.conversations.push({
      from: 'gpt',
      value: finalMsg('Created a ' + pattern.pattern_name + ' using ' + pattern.extends_class + '. The script is attached and ready to use. ' + pattern.description)
    });

    conversations.push(conv);
    convId++;
  }
}

// Scene conversations
for (var sci = 0; sci < scenePatterns.length && convId < 140; sci++) {
  var scene = scenePatterns[sci];
  var conv = { conversations: [] };
  conv.conversations.push({ from: 'system', value: SYS });
  conv.conversations.push({ from: 'human', value: 'Build me a ' + scene.root_node_type + ' scene with child nodes' });

  var batchArgs = { calls: scene.tool_calls_to_recreate.slice(0, 8) };
  conv.conversations.push({ from: 'gpt', value: toolCall('batch_tool_calls', batchArgs) });
  conv.conversations.push({ from: 'human', value: JSON.stringify(toolResult('batch_tool_calls', batchArgs)) });

  var connDesc = scene.connections.length > 0 ? ' with ' + scene.connections.length + ' signal connections' : '';
  conv.conversations.push({ from: 'gpt', value: finalMsg('Built the ' + scene.pattern_name + connDesc + '.') });
  conversations.push(conv);
  convId++;
}

// Project config conversations
for (var pji = 0; pji < projConfigs.length && convId < 160; pji++) {
  var proj = projConfigs[pji];
  var conv = { conversations: [] };
  conv.conversations.push({ from: 'system', value: SYS });
  conv.conversations.push({ from: 'human', value: 'Configure a Godot project named "' + proj.project_name + '" with input actions' });

  var batchArgs = { calls: proj.tool_calls_to_configure.slice(0, 6) };
  conv.conversations.push({ from: 'gpt', value: toolCall('batch_tool_calls', batchArgs) });
  conv.conversations.push({ from: 'human', value: JSON.stringify(toolResult('batch_tool_calls', batchArgs)) });

  conv.conversations.push({ from: 'gpt', value: finalMsg('Configured project "' + proj.project_name + '" with ' + proj.input_actions.length + ' input actions and ' + proj.autoloads.length + ' autoloads.') });
  conversations.push(conv);
  convId++;
}

// Complex multi-turn conversations
var complexConvs = [
  {
    prompt: 'Build me a complete 2D platformer with player, enemy, and coin collection',
    steps: [
      { tool: 'batch_tool_calls', args: { calls: [
        { tool: 'add_node', arguments: { node_type: 'CharacterBody2D', node_name: 'Player' } },
        { tool: 'add_collision_shape', arguments: { parent_path: 'Player', node_name: 'Collision', shape_type: 'capsule', parameters: { radius: 14, height: 32 } } },
        { tool: 'add_node', arguments: { node_type: 'CharacterBody2D', node_name: 'Enemy' } },
        { tool: 'add_collision_shape', arguments: { parent_path: 'Enemy', node_name: 'Collision', shape_type: 'capsule', parameters: { radius: 14, height: 32 } } },
        { tool: 'add_node', arguments: { node_type: 'Area2D', node_name: 'Coin' } },
        { tool: 'add_collision_shape', arguments: { parent_path: 'Coin', node_name: 'Collision', shape_type: 'circle', parameters: { radius: 10 } } }
      ] } },
      { tool: 'create_gdscript_file', args: { script_path: 'res://player.gd', contents: 'extends CharacterBody2D\n\nconst SPEED = 300.0\nconst JUMP_VELOCITY = -725.0\nvar gravity = ProjectSettings.get_setting("physics/2d/default_gravity")\n\nfunc _physics_process(delta):\n\tvelocity.y += gravity * delta\n\tif is_on_floor() and Input.is_action_just_pressed("jump"):\n\t\tvelocity.y = JUMP_VELOCITY\n\tvar direction = Input.get_axis("move_left", "move_right") * SPEED\n\tvelocity.x = move_toward(velocity.x, direction, 1800.0 * delta)\n\tmove_and_slide()' } },
      { tool: 'create_gdscript_file', args: { script_path: 'res://enemy.gd', contents: 'extends CharacterBody2D\n\nconst SPEED = 22.0\nvar gravity = ProjectSettings.get_setting("physics/2d/default_gravity")\n\nfunc _physics_process(delta):\n\tvelocity.y += gravity * delta\n\tif is_on_wall():\n\t\tvelocity.x = -velocity.x\n\tif velocity.x == 0:\n\t\tvelocity.x = SPEED\n\tmove_and_slide()' } },
      { tool: 'create_gdscript_file', args: { script_path: 'res://coin.gd', contents: 'extends Area2D\n\nvar taken = false\n\nfunc _on_body_entered(body):\n\tif not taken and body.name == "Player":\n\t\ttaken = true\n\t\tqueue_free()' } }
    ]
  },
  {
    prompt: 'Create a 3D platformer with camera follow and coin pickup',
    steps: [
      { tool: 'batch_tool_calls', args: { calls: [
        { tool: 'add_node', arguments: { node_type: 'CharacterBody3D', node_name: 'Player' } },
        { tool: 'add_collision_shape', arguments: { parent_path: 'Player', node_name: 'Collision', shape_type: 'capsule', parameters: { radius: 0.5, height: 1.6 } } },
        { tool: 'add_node', arguments: { node_type: 'Camera3D', node_name: 'Camera', parent_path: 'Player' } },
        { tool: 'add_node', arguments: { node_type: 'Area3D', node_name: 'Coin' } }
      ] } },
      { tool: 'create_gdscript_file', args: { script_path: 'res://player_3d.gd', contents: 'extends CharacterBody3D\n\nconst SPEED = 6.0\nconst JUMP_VELOCITY = 12.5\nvar gravity = ProjectSettings.get_setting("physics/3d/default_gravity")\n\nfunc _physics_process(delta):\n\tvelocity.y += gravity * delta\n\tvar input = Input.get_vector("move_left", "move_right", "move_forward", "move_back")\n\tvar direction = (transform.basis * Vector3(input.x, 0, input.y)).normalized()\n\tif direction:\n\t\tvelocity.x = direction.x * SPEED\n\t\tvelocity.z = direction.z * SPEED\n\telse:\n\t\tvelocity.x = move_toward(velocity.x, 0, SPEED)\n\t\tvelocity.z = move_toward(velocity.z, 0, SPEED)\n\tif is_on_floor() and Input.is_action_pressed("jump"):\n\t\tvelocity.y = JUMP_VELOCITY\n\tmove_and_slide()' } }
    ]
  },
  {
    prompt: 'Set up a multiplayer bomber game with lobby',
    steps: [
      { tool: 'batch_tool_calls', args: { calls: [
        { tool: 'add_node', arguments: { node_type: 'CharacterBody2D', node_name: 'Player' } },
        { tool: 'add_node', arguments: { node_type: 'Node', node_name: 'GameState' } },
        { tool: 'add_node', arguments: { node_type: 'Control', node_name: 'Lobby' } }
      ] } },
      { tool: 'create_gdscript_file', args: { script_path: 'res://gamestate.gd', contents: 'extends Node\n\nconst DEFAULT_PORT = 10567\nconst MAX_PEERS = 12\n\nvar peer: ENetMultiplayerPeer\nvar player_name = "Player"\nvar players = {}\n\nsignal player_list_changed()\nsignal connection_failed()\nsignal connection_succeeded()\n\nfunc _ready():\n\tmultiplayer.peer_connected.connect(_player_connected)\n\tmultiplayer.peer_disconnected.connect(_player_disconnected)\n\nfunc _player_connected(id):\n\tregister_player.rpc_id(id, player_name)\n\n@rpc("any_peer")\nfunc register_player(new_name):\n\tvar id = multiplayer.get_remote_sender_id()\n\tplayers[id] = new_name\n\tplayer_list_changed.emit()\n\nfunc host_game(name):\n\tplayer_name = name\n\tpeer = ENetMultiplayerPeer.new()\n\tpeer.create_server(DEFAULT_PORT, MAX_PEERS)\n\tmultiplayer.set_multiplayer_peer(peer)\n\nfunc join_game(ip, name):\n\tplayer_name = name\n\tpeer = ENetMultiplayerPeer.new()\n\tpeer.create_client(ip, DEFAULT_PORT)\n\tmultiplayer.set_multiplayer_peer(peer)' } }
    ]
  },
  {
    prompt: 'Build a procedural dungeon generator using cellular automata',
    steps: [
      { tool: 'batch_tool_calls', args: { calls: [
        { tool: 'add_node', arguments: { node_type: 'Node2D', node_name: 'DungeonGenerator' } },
        { tool: 'add_node', arguments: { node_type: 'TileMap', node_name: 'TileMapDungeon', parent_path: 'DungeonGenerator' } }
      ] } },
      { tool: 'create_gdscript_file', args: { script_path: 'res://dungeon_generator.gd', contents: 'extends Node2D\n\nenum CellType { WALL, FLOOR }\nconst MAP_SIZE = Vector2(80, 45)\n\nvar _map = {}\n\n@onready var _tilemap = $TileMapDungeon\n\nfunc _ready():\n\tgenerate_new_dungeon()\n\nfunc generate_new_dungeon():\n\t_map = _generate_random_map()\n\tfor step in 10:\n\t\t_map = _advance_simulation()\n\t_paint_map()\n\nfunc _generate_random_map():\n\tvar map = {}\n\tfor x in range(MAP_SIZE.x):\n\t\tfor y in range(MAP_SIZE.y):\n\t\t\tmap[Vector2(x, y)] = CellType.WALL if randf() < 0.5 else CellType.FLOOR\n\treturn map\n\nfunc _advance_simulation():\n\tvar new_map = {}\n\tfor cell in _map:\n\t\tvar floor_neighbors = _count_floor_neighbors(cell)\n\t\tif _map[cell] == CellType.WALL:\n\t\t\tnew_map[cell] = CellType.FLOOR if floor_neighbors > 4 else CellType.WALL\n\t\telse:\n\t\t\tnew_map[cell] = CellType.WALL if 8 - floor_neighbors > 4 else CellType.FLOOR\n\treturn new_map\n\nfunc _paint_map():\n\tfor cell in _map:\n\t\t_tilemap.set_cell(0, cell, _map[cell], Vector2i.ZERO)\n\nfunc _count_floor_neighbors(loc):\n\tvar count = 0\n\tfor d in [Vector2.LEFT, Vector2.RIGHT, Vector2.UP, Vector2.DOWN]:\n\t\tif _map.has(loc + d) and _map[loc + d] == CellType.FLOOR:\n\t\t\tcount += 1\n\treturn count' } }
    ]
  },
  {
    prompt: 'Create a dialogue system with typewriter text and choices',
    steps: [
      { tool: 'batch_tool_calls', args: { calls: [
        { tool: 'add_node', arguments: { node_type: 'Control', node_name: 'TextBox' } },
        { tool: 'add_node', arguments: { node_type: 'RichTextLabel', node_name: 'DialogueText', parent_path: 'TextBox' } },
        { tool: 'add_node', arguments: { node_type: 'Label', node_name: 'NameLabel', parent_path: 'TextBox' } }
      ] } },
      { tool: 'create_gdscript_file', args: { script_path: 'res://text_box.gd', contents: 'extends Control\n\nsignal display_finished\nsignal next_requested\n\n@export var display_speed = 20.0\nvar text = ""\nvar _tween: Tween\n\n@onready var _name_label = $NameLabel\n@onready var _rich_text = $DialogueText\n\nfunc display(new_text, character_name = "", speed = display_speed):\n\ttext = new_text\n\tif character_name != "":\n\t\t_name_label.text = character_name\n\t_rich_text.text = text\n\tvar char_count = _rich_text.get_total_character_count()\n\t_rich_text.visible_characters = 0\n\t_tween = create_tween()\n\t_tween.tween_property(_rich_text, "visible_characters", char_count, char_count / speed)\n\t_tween.finished.connect(func(): display_finished.emit())\n\nfunc advance_dialogue():\n\tif _tween and _tween.is_running():\n\t\t_tween.custom_step(100.0)\n\telse:\n\t\tnext_requested.emit()' } }
    ]
  }
];

for (var ci = 0; ci < complexConvs.length && convId < 180; ci++) {
  var cc = complexConvs[ci];
  var conv = { conversations: [] };
  conv.conversations.push({ from: 'system', value: SYS });
  conv.conversations.push({ from: 'human', value: cc.prompt });

  for (var sti = 0; sti < cc.steps.length; sti++) {
    var step = cc.steps[sti];
    conv.conversations.push({ from: 'gpt', value: toolCall(step.tool, step.args) });
    conv.conversations.push({ from: 'human', value: JSON.stringify(toolResult(step.tool, step.args)) });
  }

  conv.conversations.push({ from: 'gpt', value: finalMsg('Done! Set up ' + cc.prompt.toLowerCase().replace('build me a ', '').replace('create a ', '').replace('set up a ', '') + ' with all necessary nodes and scripts.') });
  conversations.push(conv);
  convId++;
}

// Write JSONL
var jsonlPath = path.join(OUT, 'conversations_from_projects.jsonl');
var jsonlLines = [];
for (var i = 0; i < conversations.length; i++) {
  jsonlLines.push(JSON.stringify(conversations[i]));
}
fs.writeFileSync(jsonlPath, jsonlLines.join('\n') + '\n');
console.log('  ->', conversations.length, 'conversations');

// Summary
console.log('\n=== SUMMARY ===');
console.log('gdscript_patterns.json:', gdPatterns.length, 'entries');
console.log('scene_patterns.json:', scenePatterns.length, 'entries');
console.log('project_configs.json:', projConfigs.length, 'entries');
console.log('conversations_from_projects.jsonl:', conversations.length, 'entries');

// Verify sizes
for (var fn of ['gdscript_patterns.json', 'scene_patterns.json', 'project_configs.json']) {
  var fp = path.join(OUT, fn);
  var sz = fs.statSync(fp).size;
  console.log(fn + ':', (sz / 1024).toFixed(1) + ' KB');
}
try {
  var jsonlSz = fs.statSync(jsonlPath).size;
  console.log('conversations_from_projects.jsonl:', (jsonlSz / 1024).toFixed(1) + ' KB');
} catch(e) {
  console.log('conversations_from_projects.jsonl: not found');
}
