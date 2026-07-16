/**************************************************************************/
/*  yeet_ai_tools_headless.cpp                                            */
/**************************************************************************/
/*  Phase 3: Headless scene/resource editing + project health tools.      */
/*  These tools operate on .tscn/.tres files WITHOUT opening them in      */
/*  the editor, plus project-wide health audits.                          */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "core/config/project_settings.h"
#include "editor/editor_interface.h"
#include "editor/file_system/editor_file_system.h"
#include "modules/gdscript/gdscript.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

// ═══════════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════════

static Dictionary _headless_error(const String &msg) {
	Dictionary r;
	r["error"] = msg;
	return r;
}

// Serialize a node's non-default properties to a Dictionary.
static Dictionary _serialize_node_properties(Node *node) {
	Dictionary props;
	List<PropertyInfo> plist;
	node->get_property_list(&plist);
	for (const PropertyInfo &pi : plist) {
		if (pi.usage & PROPERTY_USAGE_CATEGORY || pi.usage & PROPERTY_USAGE_GROUP || pi.usage & PROPERTY_USAGE_SUBGROUP) {
			continue;
		}
		if (!(pi.usage & PROPERTY_USAGE_EDITOR) && !(pi.usage & PROPERTY_USAGE_STORAGE)) {
			continue;
		}
		if (pi.name.begins_with("_") || pi.name.begins_with("metadata/")) {
			continue;
		}
		Variant val = node->get(pi.name);
		Variant def = ClassDB::class_get_default_property_value(node->get_class(), pi.name);
		if (val != def) {
			// Simple serialization for common types.
			switch (val.get_type()) {
				case Variant::VECTOR2: {
					Vector2 v = val;
					Dictionary d; d["x"] = v.x; d["y"] = v.y;
					props[pi.name] = d;
				} break;
				case Variant::VECTOR3: {
					Vector3 v = val;
					Dictionary d; d["x"] = v.x; d["y"] = v.y; d["z"] = v.z;
					props[pi.name] = d;
				} break;
				case Variant::COLOR: {
					Color c = val;
					Dictionary d; d["r"] = c.r; d["g"] = c.g; d["b"] = c.b; d["a"] = c.a;
					props[pi.name] = d;
				} break;
				case Variant::OBJECT: {
					Object *obj = val;
					if (obj) {
						Resource *res = Object::cast_to<Resource>(obj);
						if (res && !res->get_path().is_empty()) {
							props[pi.name] = res->get_path();
						} else if (res) {
							props[pi.name] = vformat("[%s]", res->get_class());
						}
					}
				} break;
				default:
					props[pi.name] = val;
					break;
			}
		}
	}
	return props;
}

// Recursively serialize a node tree to a Dictionary.
static Dictionary _serialize_node_tree(Node *node, int depth, int max_depth) {
	Dictionary nd;
	nd["name"] = node->get_name();
	nd["type"] = node->get_class();

	Ref<Script> script = node->get_script();
	if (script.is_valid() && !script->get_path().is_empty()) {
		nd["script"] = script->get_path();
	}

	nd["properties"] = _serialize_node_properties(node);

	if (max_depth < 0 || depth < max_depth) {
		Array children;
		for (int i = 0; i < node->get_child_count(); i++) {
			children.push_back(_serialize_node_tree(node->get_child(i), depth + 1, max_depth));
		}
		nd["children"] = children;
		nd["child_count"] = node->get_child_count();
	} else {
		nd["child_count"] = node->get_child_count();
	}

	return nd;
}

// ═══════════════════════════════════════════════════════════════════════════════
// scene_read — Read a .tscn file as structured JSON (without opening in editor)
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_scene_read(const Dictionary &p_args) const {
	const String path = p_args.get("scene_path", "");
	if (path.is_empty() || !path.ends_with(".tscn")) {
		return _headless_error("scene_path is required and must be a .tscn file.");
	}

	Ref<PackedScene> scene = ResourceLoader::load(path, "PackedScene");
	if (scene.is_null()) {
		return _headless_error(vformat("Failed to load scene: %s", path));
	}

	Node *root = scene->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	if (root == nullptr) {
		return _headless_error(vformat("Failed to instantiate scene: %s", path));
	}

	const int max_depth = int(p_args.get("max_depth", -1));
	Dictionary tree = _serialize_node_tree(root, 0, max_depth);

	// Count total nodes.
	int total = 0;
	List<Node *> queue;
	queue.push_back(root);
	while (!queue.is_empty()) {
		Node *n = queue.front()->get();
		queue.pop_front();
		total++;
		for (int i = 0; i < n->get_child_count(); i++) {
			queue.push_back(n->get_child(i));
		}
	}

	memdelete(root);

	Dictionary result;
	result["scene_path"] = path;
	result["tree"] = tree;
	result["total_nodes"] = total;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// scene_modify_node — Modify a node's properties in a .tscn file
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_scene_modify_node(const Dictionary &p_args) const {
	const String path = p_args.get("scene_path", "");
	const String node_path = p_args.get("node_path", "");
	const Dictionary properties = p_args.get("properties", Dictionary());

	if (path.is_empty()) {
		return _headless_error("scene_path is required.");
	}
	if (node_path.is_empty()) {
		return _headless_error("node_path is required (relative to scene root, e.g. 'Player/Sprite2D').");
	}
	if (properties.is_empty()) {
		return _headless_error("properties dictionary is required.");
	}

	Ref<PackedScene> scene = ResourceLoader::load(path, "PackedScene");
	if (scene.is_null()) {
		return _headless_error(vformat("Failed to load scene: %s", path));
	}

	Node *root = scene->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	if (root == nullptr) {
		return _headless_error("Failed to instantiate scene.");
	}

	// Find the target node.
	Node *target = nullptr;
	if (node_path == "." || node_path == root->get_name()) {
		target = root;
	} else {
		target = root->get_node_or_null(node_path);
	}

	if (target == nullptr) {
		memdelete(root);
		return _headless_error(vformat("Node '%s' not found in scene.", node_path));
	}

	// Apply properties.
	Array keys = properties.keys();
	int applied = 0;
	for (int i = 0; i < keys.size(); i++) {
		String prop_name = keys[i];
		target->set(prop_name, properties[prop_name]);
		applied++;
	}

	// Pack and save.
	Ref<PackedScene> packed;
	packed.instantiate();
	packed->pack(root);
	Error err = ResourceSaver::save(packed, path);
	memdelete(root);

	if (err != OK) {
		return _headless_error(vformat("Failed to save scene (error %d).", err));
	}

	Dictionary result;
	result["ok"] = true;
	result["scene_path"] = path;
	result["node_path"] = node_path;
	result["properties_applied"] = applied;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// scene_remove_node — Remove a node from a .tscn file
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_scene_remove_node(const Dictionary &p_args) const {
	const String path = p_args.get("scene_path", "");
	const String node_path = p_args.get("node_path", "");

	if (path.is_empty() || node_path.is_empty()) {
		return _headless_error("scene_path and node_path are required.");
	}

	Ref<PackedScene> scene = ResourceLoader::load(path, "PackedScene");
	if (scene.is_null()) {
		return _headless_error(vformat("Failed to load scene: %s", path));
	}

	Node *root = scene->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	if (root == nullptr) {
		return _headless_error("Failed to instantiate scene.");
	}

	Node *target = root->get_node_or_null(node_path);
	if (target == nullptr) {
		memdelete(root);
		return _headless_error(vformat("Node '%s' not found in scene.", node_path));
	}
	if (target == root) {
		memdelete(root);
		return _headless_error("Cannot remove the scene root node.");
	}

	target->get_parent()->remove_child(target);
	memdelete(target);

	Ref<PackedScene> packed;
	packed.instantiate();
	packed->pack(root);
	Error err = ResourceSaver::save(packed, path);
	memdelete(root);

	if (err != OK) {
		return _headless_error(vformat("Failed to save scene (error %d).", err));
	}

	Dictionary result;
	result["ok"] = true;
	result["scene_path"] = path;
	result["removed_node"] = node_path;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// resource_create — Create a .tres resource file
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_resource_create(const Dictionary &p_args) const {
	const String path = p_args.get("save_path", "");
	const String type = p_args.get("resource_type", "");
	const Dictionary properties = p_args.get("properties", Dictionary());

	if (path.is_empty() || type.is_empty()) {
		return _headless_error("save_path and resource_type are required.");
	}

	if (!ClassDB::class_exists(type)) {
		return _headless_error(vformat("Unknown resource type: %s", type));
	}
	if (!ClassDB::is_parent_class(type, "Resource")) {
		return _headless_error(vformat("'%s' is not a Resource subclass.", type));
	}

	Variant instance = ClassDB::instantiate(type);
	Resource *res = Object::cast_to<Resource>(instance);
	if (res == nullptr) {
		return _headless_error(vformat("Failed to instantiate resource type: %s", type));
	}

	// Apply properties.
	Array keys = properties.keys();
	for (int i = 0; i < keys.size(); i++) {
		res->set(keys[i], properties[keys[i]]);
	}

	Ref<Resource> ref(res);
	Error err = ResourceSaver::save(ref, path);
	if (err != OK) {
		return _headless_error(vformat("Failed to save resource (error %d).", err));
	}

	Dictionary result;
	result["ok"] = true;
	result["save_path"] = path;
	result["resource_type"] = type;
	result["properties_set"] = keys.size();
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// resource_read — Read a .tres/.res resource file as JSON
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_resource_read(const Dictionary &p_args) const {
	const String path = p_args.get("path", "");
	if (path.is_empty()) {
		return _headless_error("path is required.");
	}

	Ref<Resource> res = ResourceLoader::load(path);
	if (res.is_null()) {
		return _headless_error(vformat("Failed to load resource: %s", path));
	}

	Dictionary props;
	List<PropertyInfo> plist;
	res->get_property_list(&plist);
	for (const PropertyInfo &pi : plist) {
		if (pi.usage & PROPERTY_USAGE_CATEGORY || pi.usage & PROPERTY_USAGE_GROUP || pi.usage & PROPERTY_USAGE_SUBGROUP) {
			continue;
		}
		if (!(pi.usage & PROPERTY_USAGE_EDITOR) && !(pi.usage & PROPERTY_USAGE_STORAGE)) {
			continue;
		}
		Variant val = res->get(pi.name);
		Variant def = ClassDB::class_get_default_property_value(res->get_class(), pi.name);
		if (val != def || (pi.usage & PROPERTY_USAGE_STORAGE)) {
			if (val.get_type() == Variant::OBJECT) {
				Object *obj = val;
				if (obj) {
					Resource *sub = Object::cast_to<Resource>(obj);
					if (sub && !sub->get_path().is_empty()) {
						props[pi.name] = sub->get_path();
					} else if (sub) {
						props[pi.name] = vformat("[%s]", sub->get_class());
					}
				}
			} else {
				props[pi.name] = val;
			}
		}
	}

	Dictionary result;
	result["path"] = path;
	result["type"] = res->get_class();
	result["properties"] = props;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// resource_modify — Modify properties in a .tres/.res file
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_resource_modify(const Dictionary &p_args) const {
	const String path = p_args.get("path", "");
	const Dictionary properties = p_args.get("properties", Dictionary());

	if (path.is_empty()) {
		return _headless_error("path is required.");
	}
	if (properties.is_empty()) {
		return _headless_error("properties dictionary is required.");
	}

	Ref<Resource> res = ResourceLoader::load(path);
	if (res.is_null()) {
		return _headless_error(vformat("Failed to load resource: %s", path));
	}

	Array keys = properties.keys();
	int applied = 0;
	for (int i = 0; i < keys.size(); i++) {
		res->set(keys[i], properties[keys[i]]);
		applied++;
	}

	Error err = ResourceSaver::save(res, path);
	if (err != OK) {
		return _headless_error(vformat("Failed to save resource (error %d).", err));
	}

	Dictionary result;
	result["ok"] = true;
	result["path"] = path;
	result["type"] = res->get_class();
	result["properties_applied"] = applied;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// scene_get_signals — List signal connections in a .tscn file
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_scene_get_signals(const Dictionary &p_args) const {
	const String path = p_args.get("scene_path", "");
	if (path.is_empty()) {
		return _headless_error("scene_path is required.");
	}

	Ref<PackedScene> scene = ResourceLoader::load(path, "PackedScene");
	if (scene.is_null()) {
		return _headless_error(vformat("Failed to load scene: %s", path));
	}

	Node *root = scene->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	if (root == nullptr) {
		return _headless_error("Failed to instantiate scene.");
	}

	// Walk the tree and collect signal connections.
	Array connections_arr;
	List<Node *> queue;
	queue.push_back(root);
	while (!queue.is_empty()) {
		Node *n = queue.front()->get();
		queue.pop_front();

		List<Node::Connection> conns;
		n->get_all_signal_connections(&conns);
		for (const Node::Connection &c : conns) {
			Dictionary cd;
			cd["source"] = String(n->get_name());
			cd["signal"] = c.signal.get_name();
			if (c.callable.get_object()) {
				Node *target_node = Object::cast_to<Node>(c.callable.get_object());
				if (target_node) {
					cd["target"] = String(target_node->get_name());
				}
			}
			cd["method"] = c.callable.get_method();
			cd["flags"] = c.flags;
			connections_arr.push_back(cd);
		}

		for (int i = 0; i < n->get_child_count(); i++) {
			queue.push_back(n->get_child(i));
		}
	}

	memdelete(root);

	Dictionary result;
	result["scene_path"] = path;
	result["connections"] = connections_arr;
	result["count"] = connections_arr.size();
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// PROJECT HEALTH TOOLS
// ═══════════════════════════════════════════════════════════════════════════════

// Helper: recursively collect files with specific extensions.
static void _collect_files(const String &p_path, const Vector<String> &p_extensions, Vector<String> &r_files, int depth = 0) {
	if (depth > 20) return;
	Ref<DirAccess> dir = DirAccess::open(p_path);
	if (dir.is_null()) return;

	dir->list_dir_begin();
	String fname = dir->get_next();
	while (!fname.is_empty()) {
		if (dir->current_is_dir()) {
			if (!fname.begins_with(".") && fname != "addons") {
				_collect_files(p_path.path_join(fname), p_extensions, r_files, depth + 1);
			}
		} else {
			for (const String &ext : p_extensions) {
				if (fname.ends_with(ext)) {
					r_files.push_back(p_path.path_join(fname));
					break;
				}
			}
		}
		fname = dir->get_next();
	}
}

// ═══════════════════════════════════════════════════════════════════════════════
// project_detect_broken_scripts — Scan for GDScript files with parse errors
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_project_detect_broken_scripts(const Dictionary &p_args) const {
	const String root = p_args.get("root", "res://");

	Vector<String> files;
	Vector<String> exts = { ".gd" };
	_collect_files(root, exts, files);

	Array broken;
	int scanned = 0;

	for (const String &file_path : files) {
		scanned++;
		Error load_err;
		String content = FileAccess::get_file_as_string(file_path, &load_err);
		if (load_err != OK) {
			Dictionary entry;
			entry["file"] = file_path;
			entry["error"] = "Could not read file";
			broken.push_back(entry);
			continue;
		}

		// Try loading as a GDScript to check for parse errors.
		Ref<GDScript> script;
		script.instantiate();
		script->set_source_code(content);
		script->set_path(file_path);
		Error parse_err = script->reload(false);
		if (parse_err != OK) {
			Dictionary entry;
			entry["file"] = file_path;
			entry["error"] = vformat("Parse/compile error (code %d)", parse_err);
			broken.push_back(entry);
		}
	}

	Dictionary result;
	result["scanned"] = scanned;
	result["broken_count"] = broken.size();
	result["broken"] = broken;
	if (broken.size() == 0) {
		result["status"] = "All scripts parse cleanly.";
	}
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// project_scan_missing_deps — Find broken/missing resource dependencies
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_project_scan_missing_deps(const Dictionary &p_args) const {
	const String root = p_args.get("root", "res://");

	Vector<String> files;
	Vector<String> exts = { ".tscn", ".tres", ".res" };
	_collect_files(root, exts, files);

	Array missing;
	int scanned = 0;

	for (const String &file_path : files) {
		scanned++;
		List<String> deps;
		ResourceLoader::get_dependencies(file_path, &deps);

		for (const String &dep : deps) {
			// Dependencies can be "path::type" format.
			String dep_path = dep.get_slice("::", 0);
			if (dep_path.is_empty()) continue;

			if (!FileAccess::exists(dep_path)) {
				Dictionary entry;
				entry["file"] = file_path;
				entry["missing_dependency"] = dep_path;
				missing.push_back(entry);
			}
		}
	}

	Dictionary result;
	result["scanned"] = scanned;
	result["missing_count"] = missing.size();
	result["missing"] = missing;
	if (missing.size() == 0) {
		result["status"] = "No missing dependencies found.";
	}
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// project_scan_cyclic_deps — Find cyclic dependency chains
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_project_scan_cyclic_deps(const Dictionary &p_args) const {
	const String root = p_args.get("root", "res://");

	Vector<String> files;
	Vector<String> exts = { ".tscn", ".tres" };
	_collect_files(root, exts, files);

	// Build dependency graph.
	HashMap<String, Vector<String>> graph;
	for (const String &file_path : files) {
		List<String> deps;
		ResourceLoader::get_dependencies(file_path, &deps);
		Vector<String> dep_paths;
		for (const String &dep : deps) {
			String dp = dep.get_slice("::", 0);
			if (!dp.is_empty() && dp != file_path) {
				dep_paths.push_back(dp);
			}
		}
		graph[file_path] = dep_paths;
	}

	// Simple cycle detection via DFS.
	Array cycles;
	HashSet<String> visited;
	HashSet<String> in_stack;

	// We'll check for self-referencing and simple 2-cycles.
	for (const KeyValue<String, Vector<String>> &kv : graph) {
		const String &file = kv.key;
		for (const String &dep : kv.value) {
			if (dep == file) {
				Dictionary c;
				c["type"] = "self_reference";
				c["file"] = file;
				cycles.push_back(c);
			} else if (graph.has(dep)) {
				// Check if dep points back to file (2-cycle).
				for (const String &dep2 : graph[dep]) {
					if (dep2 == file) {
						Dictionary c;
						c["type"] = "cycle_2";
						c["file_a"] = file;
						c["file_b"] = dep;
						cycles.push_back(c);
					}
				}
			}
		}
	}

	Dictionary result;
	result["scanned"] = files.size();
	result["cycle_count"] = cycles.size();
	result["cycles"] = cycles;
	if (cycles.size() == 0) {
		result["status"] = "No cyclic dependencies detected.";
	}
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// project_audit_health — Comprehensive project health audit
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_project_audit_health(const Dictionary &p_args) const {
	const String root = p_args.get("root", "res://");

	Array issues;
	int total_files = 0;

	// 1. Check for broken scripts.
	{
		Vector<String> gd_files;
		Vector<String> gd_exts = { ".gd" };
		_collect_files(root, gd_exts, gd_files);
		total_files += gd_files.size();

		for (const String &file_path : gd_files) {
			Error load_err;
			String content = FileAccess::get_file_as_string(file_path, &load_err);
			if (load_err != OK) {
				Dictionary issue;
				issue["severity"] = "error";
				issue["type"] = "unreadable_script";
				issue["file"] = file_path;
				issue["message"] = "Cannot read file";
				issues.push_back(issue);
			}
		}
	}

	// 2. Check for missing dependencies.
	{
		Vector<String> res_files;
		Vector<String> res_exts = { ".tscn", ".tres", ".res" };
		_collect_files(root, res_exts, res_files);
		total_files += res_files.size();

		for (const String &file_path : res_files) {
			List<String> deps;
			ResourceLoader::get_dependencies(file_path, &deps);
			for (const String &dep : deps) {
				String dp = dep.get_slice("::", 0);
				if (!dp.is_empty() && !FileAccess::exists(dp)) {
					Dictionary issue;
					issue["severity"] = "error";
					issue["type"] = "missing_dependency";
					issue["file"] = file_path;
					issue["dependency"] = dp;
					issue["message"] = vformat("Missing dependency: %s", dp);
					issues.push_back(issue);
				}
			}
		}
	}

	// 3. Check for missing main scene.
	{
		String main_scene = GLOBAL_GET("application/run/main_scene");
		if (main_scene.is_empty()) {
			Dictionary issue;
			issue["severity"] = "warning";
			issue["type"] = "no_main_scene";
			issue["message"] = "No main scene set in project settings.";
			issues.push_back(issue);
		} else if (!FileAccess::exists(main_scene)) {
			Dictionary issue;
			issue["severity"] = "error";
			issue["type"] = "main_scene_missing";
			issue["file"] = main_scene;
			issue["message"] = vformat("Main scene file does not exist: %s", main_scene);
			issues.push_back(issue);
		}
	}

	// 4. Check for orphan .import files.
	{
		Vector<String> import_files;
		Vector<String> imp_exts = { ".import" };
		_collect_files(root, imp_exts, import_files);
		for (const String &imp_path : import_files) {
			String source_path = imp_path.replace(".import", "");
			if (!FileAccess::exists(source_path)) {
				Dictionary issue;
				issue["severity"] = "warning";
				issue["type"] = "orphan_import";
				issue["file"] = imp_path;
				issue["message"] = vformat("Import file without source: %s", source_path);
				issues.push_back(issue);
			}
		}
	}

	Dictionary result;
	result["total_files_scanned"] = total_files;
	result["issue_count"] = issues.size();
	result["issues"] = issues;

	int errors = 0, warnings = 0;
	for (int i = 0; i < issues.size(); i++) {
		Dictionary issue = issues[i];
		if (String(issue["severity"]) == "error") errors++;
		else warnings++;
	}
	result["errors"] = errors;
	result["warnings"] = warnings;

	if (issues.size() == 0) {
		result["status"] = "Project health is good. No issues found.";
	} else {
		result["status"] = vformat("%d errors, %d warnings found.", errors, warnings);
	}
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// project_get_class_api — Get ClassDB metadata for a class
// ═══════════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_project_get_class_api(const Dictionary &p_args) const {
	const String class_name = p_args.get("class_name", "");
	if (class_name.is_empty()) {
		return _headless_error("class_name is required.");
	}

	if (!ClassDB::class_exists(class_name)) {
		return _headless_error(vformat("Class '%s' not found in ClassDB.", class_name));
	}

	Dictionary result;
	result["class_name"] = class_name;
	result["parent_class"] = ClassDB::get_parent_class(class_name);
	result["is_virtual"] = ClassDB::is_virtual(class_name);

	// Methods (first 50).
	Array methods;
	List<MethodInfo> method_list;
	ClassDB::get_method_list(class_name, &method_list, true, false);
	int count = 0;
	for (const MethodInfo &mi : method_list) {
		if (count >= 50) break;
		Dictionary m;
		m["name"] = mi.name;
		m["return_type"] = Variant::get_type_name(mi.return_val.type);
		Array args;
		for (const PropertyInfo &arg : mi.arguments) {
			Dictionary a;
			a["name"] = arg.name;
			a["type"] = Variant::get_type_name(arg.type);
			args.push_back(a);
		}
		m["arguments"] = args;
		methods.push_back(m);
		count++;
	}
	result["methods"] = methods;
	result["method_count"] = method_list.size();

	// Signals.
	Array signals_arr;
	List<MethodInfo> signal_list;
	ClassDB::get_signal_list(class_name, &signal_list, true);
	for (const MethodInfo &si : signal_list) {
		Dictionary s;
		s["name"] = si.name;
		s["arg_count"] = si.arguments.size();
		signals_arr.push_back(s);
	}
	result["signals"] = signals_arr;

	// Properties (first 50).
	Array props;
	List<PropertyInfo> prop_list;
	ClassDB::get_property_list(class_name, &prop_list, true);
	count = 0;
	for (const PropertyInfo &pi : prop_list) {
		if (pi.usage & PROPERTY_USAGE_CATEGORY || pi.usage & PROPERTY_USAGE_GROUP) continue;
		if (count >= 50) break;
		Dictionary p;
		p["name"] = pi.name;
		p["type"] = Variant::get_type_name(pi.type);
		props.push_back(p);
		count++;
	}
	result["properties"] = props;
	result["property_count"] = prop_list.size();

	// Enums.
	Array enums;
	List<StringName> enum_list;
	ClassDB::get_enum_list(class_name, &enum_list, true);
	for (const StringName &e : enum_list) {
		Dictionary ed;
		ed["name"] = e;
		List<StringName> constants;
		ClassDB::get_enum_constants(class_name, e, &constants, true);
		Array vals;
		for (const StringName &c : constants) {
			Dictionary cv;
			cv["name"] = c;
			cv["value"] = ClassDB::get_integer_constant(class_name, c);
			vals.push_back(cv);
		}
		ed["values"] = vals;
		enums.push_back(ed);
	}
	result["enums"] = enums;

	return result;
}
