const fs = require('fs');
const path = require('path');

const BASE = 'D:/Code/YEET/crosshair/modules/yeet_ai/dataset/godot_projects';
const OUT = 'D:/Code/YEET/crosshair/modules/yeet_ai/dataset/godot_projects/extracted/scene_corpus.jsonl';

function parseTscn(content) {
  const nodes = [];
  for (const line of content.split('\n')) {
    const t = line.trim();
    if (t.startsWith('[node')) {
      const n = {};
      const nm = t.match(/name="([^"]+)"/);
      const tm = t.match(/type="([^"]+)"/);
      const pm = t.match(/parent="([^"]+)"/);
      if (nm) n.name = nm[1];
      if (tm) n.type = tm[1];
      if (pm) n.parent = pm[1];
      nodes.push(n);
    }
  }
  return nodes;
}

function buildTree(nodes) {
  return nodes.map((n, i) => ({
    path: n.parent ? n.parent : '.',
    type: n.type || 'Node',
    name: n.name || 'Node' + i
  }));
}

const files = [];
const repoCounts = {};
function walk(d) {
  let entries;
  try { entries = fs.readdirSync(d, {withFileTypes: true}); } catch { return; }
  for (const e of entries) {
    if (['extract','extracted','.git','addons'].includes(e.name)) continue;
    const full = path.join(d, e.name);
    if (e.isDirectory()) walk(full);
    else if (e.name.endsWith('.tscn')) {
      const repo = full.replace(BASE + path.sep, '').split(path.sep)[0];
      repoCounts[repo] = (repoCounts[repo] || 0) + 1;
      if (repoCounts[repo] <= 20) files.push(full);
    }
  }
}
walk(BASE);
console.log('Found ' + files.length + ' .tscn files');

const prio = [
  'godot-demo-projects','tps-demo','godot-2d-space-game','phantom-camera',
  'beehave','godot_dialogue_manager','godot-2d-action-platformer',
  'godot-2d-tower-defense','godot-2d-rhythm','godot-2d-visual-novel',
  'godot-open-rpg','godot-2d-builder','godot-2d-tactical-rpg-movement',
  'godot-2d-jrpg-combat','godot-2d-tactical-space-combat'
];
const sel = []; const seen = new Set();
for (const r of prio) for (const f of files) if (f.includes(r) && !seen.has(f)) { seen.add(f); sel.push(f); }
for (const f of files) if (!seen.has(f)) { seen.add(f); sel.push(f); }

const out = []; let cnt = 0;
for (const fp of sel) {
  if (cnt >= 120) break;
  let c; try { c = fs.readFileSync(fp, 'utf8'); } catch { continue; }
  if (!c.includes('[node')) continue;
  const nodes = parseTscn(c);
  if (!nodes.length) continue;
  const root = nodes.find(n => !n.parent) || nodes[0];
  if (!root) continue;
  const rel = fp.replace(BASE + path.sep, '');
  const repo = rel.split(path.sep)[0];
  const fpath = rel.substring(repo.length + 1).replace(/\\/g, '/');
  out.push(JSON.stringify({
    source_repo: repo,
    file_path: fpath,
    root_node_type: root.type || 'Node',
    root_node_name: root.name || 'Root',
    node_count: nodes.length,
    node_tree: buildTree(nodes),
    raw_content: c
  }));
  cnt++;
}
fs.writeFileSync(OUT, out.join('\n'), 'utf8');
console.log('Wrote ' + out.length + ' entries to ' + OUT);

let bad = 0;
for (const line of out) {
  try { JSON.parse(line); } catch { bad++; }
}
console.log(bad > 0 ? bad + ' invalid lines!' : 'All lines valid!');
