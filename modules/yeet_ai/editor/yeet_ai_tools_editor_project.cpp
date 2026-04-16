/**************************************************************************/
/*  yeet_ai_tools_editor_project.cpp                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "core/templates/list.h"
#include "core/version.h"
#include "scene/resources/packed_scene.h"
#include "servers/rendering/rendering_server_enums.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "scene/3d/fog_volume.h"
#include "scene/3d/reflection_probe.h"
#include "scene/3d/voxel_gi.h"
#include "scene/3d/world_environment.h"
#include "scene/resources/environment.h"
#include "scene/resources/sky.h"
#include "scene/resources/3d/sky_material.h"

// ═══════════════════════════════════════════════════════════════════════════
// N. Editor Workflow
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_manage_editor_plugins(const Dictionary &p_args) const {
	const String action = _arg_string(p_args, "action", "list").to_lower();

	if (action == "list") {
		Dictionary result;
		Array plugins;
		EditorInterface *ei = EditorInterface::get_singleton();
		if (ei == nullptr) {
			return _make_error("EditorInterface not available");
		}
		Ref<DirAccess> da = DirAccess::open("res://addons");
		if (da.is_valid() && da->list_dir_begin() == OK) {
			while (true) {
				const String name = da->get_next();
				if (name.is_empty()) {
					break;
				}
				if (!da->current_is_dir()) {
					continue;
				}
				Dictionary entry;
				entry["name"] = name;
				entry["path"] = "res://addons/" + name;
				const String cfg_path = "res://addons/" + name + "/plugin.cfg";
				entry["has_config"] = FileAccess::exists(cfg_path);
				plugins.push_back(entry);
			}
			da->list_dir_end();
		}
		result["plugins"] = plugins;
		return result;
	}

	return _make_error("Plugin enable/disable requires editor restart. Use the Project → Project Settings → Plugins dialog.");
}

Dictionary YeetAIDock::_tool_run_scene_script(const Dictionary &p_args) const {
	return _make_error("Direct script invocation at runtime requires the game to be running. Use play_current_scene + run_gdscript_expression to call methods at runtime.");
}

Dictionary YeetAIDock::_tool_get_editor_version(const Dictionary &p_args) const {
	Dictionary result;
	result["version"] = VERSION_NUMBER;
	result["version_string"] = String(VERSION_FULL_CONFIG);
	result["build_info"] = String(VERSION_BUILD);
	result["crosshair"] = true;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// O. Project Configuration
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_manage_export_presets(const Dictionary &p_args) const {
	const String action = _arg_string(p_args, "action", "list").to_lower();
	if (action == "list") {
		return _tool_get_export_presets(p_args);
	}
	return _make_error("Export preset modifications require the Export dialog. Use get_export_presets to read current presets.");
}

Dictionary YeetAIDock::_tool_export_project(const Dictionary &p_args) const {
	return _make_error("Project export should be triggered from the Export dialog for safety. Use manage_export_presets to read preset configurations.");
}

Dictionary YeetAIDock::_tool_add_custom_class(const Dictionary &p_args) const {
	const String script_path = _arg_string(p_args, "script_path", "");
	if (script_path.is_empty() || !script_path.begins_with("res://")) {
		return _make_error("script_path must be a res:// path");
	}
	return _make_error("Add a 'class_name' declaration at the top of the script, then use scan_project_filesystem to refresh the class registry.");
}

Dictionary YeetAIDock::_tool_set_default_import_presets(const Dictionary &p_args) const {
	return _make_error("Default import presets are configured via Editor → Editor Settings → FileSystem → Import. Modify them there.");
}

// ═══════════════════════════════════════════════════════════════════════════
// P. Version Control
// ═══════════════════════════════════════════════════════════════════════════

static Error _yeet_os_execute_git(const List<String> &p_args, String &r_output, int *r_exit_code) {
	return OS::get_singleton()->execute("git", p_args, &r_output, r_exit_code);
}

Dictionary YeetAIDock::_tool_git_status(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	String output;
	List<String> args;
	args.push_back("status");
	args.push_back("--porcelain");
	int exit_code = 0;
	const Error err = _yeet_os_execute_git(args, output, &exit_code);
	if (err != OK || exit_code != 0) {
		return _make_error("git status failed (not a git repo or git not installed): " + output.substr(0, 200));
	}

	Array modified;
	Array untracked;
	Array staged;
	const PackedStringArray lines = output.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i];
		if (line.length() < 3) {
			continue;
		}
		const String xy = line.substr(0, 2);
		const String file = line.substr(3);
		if (xy[0] != ' ' && xy[0] != '?') {
			staged.push_back(file);
		}
		if (xy[1] != ' ' && xy[1] != '?') {
			modified.push_back(file);
		}
		if (xy.begins_with("??")) {
			untracked.push_back(file);
		}
	}
	result["staged"] = staged;
	result["modified"] = modified;
	result["untracked"] = untracked;
	return result;
}

Dictionary YeetAIDock::_tool_git_diff_file(const Dictionary &p_args) const {
	const String file_path = _arg_string(p_args, "file_path", "");
	const bool staged = _arg_bool(p_args, "staged", false);

	PackedStringArray args;
	args.push_back("diff");
	if (staged) {
		args.push_back("--cached");
	}
	args.push_back("--");
	args.push_back(file_path);

	String output;
	List<String> arg_list;
	for (int i = 0; i < args.size(); i++) {
		arg_list.push_back(args[i]);
	}
	int exit_code = 0;
	const Error err = _yeet_os_execute_git(arg_list, output, &exit_code);
	if (err != OK || exit_code != 0) {
		return _make_error("git diff failed: " + output.substr(0, 200));
	}

	Dictionary result;
	result["diff"] = output;
	return result;
}

Dictionary YeetAIDock::_tool_git_log(const Dictionary &p_args) const {
	const int max_count = _arg_int(p_args, "max_count", 10);

	PackedStringArray args;
	args.push_back("log");
	args.push_back("--oneline");
	args.push_back("-" + String::num(max_count));

	String output;
	List<String> arg_list;
	for (int i = 0; i < args.size(); i++) {
		arg_list.push_back(args[i]);
	}
	int exit_code = 0;
	const Error err = _yeet_os_execute_git(arg_list, output, &exit_code);
	if (err != OK || exit_code != 0) {
		return _make_error("git log failed: " + output.substr(0, 200));
	}

	Dictionary result;
	Array commits;
	const PackedStringArray lines = output.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();
		if (line.is_empty()) {
			continue;
		}
		const int space = line.find(" ");
		Dictionary entry;
		if (space >= 0) {
			entry["hash"] = line.substr(0, space);
			entry["message"] = line.substr(space + 1);
		} else {
			entry["hash"] = line;
			entry["message"] = "";
		}
		commits.push_back(entry);
	}
	result["commits"] = commits;
	return result;
}

Dictionary YeetAIDock::_tool_git_branch(const Dictionary &p_args) const {
	(void)p_args;
	String output;
	List<String> args;
	args.push_back("branch");
	int exit_code = 0;
	const Error err = _yeet_os_execute_git(args, output, &exit_code);
	if (err != OK || exit_code != 0) {
		return _make_error("git branch failed: " + output.substr(0, 200));
	}

	Dictionary result;
	Array branches;
	String current;
	const PackedStringArray lines = output.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i].strip_edges();
		if (line.is_empty()) {
			continue;
		}
		const bool is_current = line.begins_with("* ");
		if (is_current) {
			line = line.substr(2);
			current = line;
		}
		branches.push_back(line);
	}
	result["current"] = current;
	result["branches"] = branches;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Q. Scene Refactoring
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_merge_scenes(const Dictionary &p_args) const {
	return _make_error("Scene merging is destructive. Open both scenes, use get_scene_tree on each, then use add_node/instantiate_scene to manually compose the result.");
}

Dictionary YeetAIDock::_tool_extract_sub_scene(const Dictionary &p_args) const {
	return _make_error("Sub-scene extraction is complex. Create a new scene file with create_scene_file, then use add_node/instantiate_scene to compose it, then remove_node from the original.");
}

Dictionary YeetAIDock::_tool_replace_node_with_scene(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	Node *old_node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &old_node, err)) {
		return _make_error(err);
	}
	const String scene_file_path = _arg_string(p_args, "scene_file_path", "");
	if (scene_file_path.is_empty() || !scene_file_path.begins_with("res://")) {
		return _make_error("scene_file_path must be a res:// path");
	}
	const bool keep_transform = _arg_bool(p_args, "keep_transform", true);

	Ref<PackedScene> scene = ResourceLoader::load(scene_file_path);
	if (scene.is_null()) {
		return _make_error("Failed to load scene: " + scene_file_path);
	}

	Node *instance = scene->instantiate();
	if (instance == nullptr) {
		return _make_error("Failed to instantiate scene: " + scene_file_path);
	}

	Node *parent = old_node->get_parent();
	if (parent == nullptr) {
		return _make_error("Cannot replace the scene root node");
	}

	Transform3D old_xform;
	Vector2 old_pos_2d;
	bool is_3d = false;
	if (keep_transform) {
		Node3D *n3d = Object::cast_to<Node3D>(old_node);
		if (n3d) {
			old_xform = n3d->get_transform();
			is_3d = true;
		}
	}

	int old_idx = old_node->get_index();

	old_node->get_parent()->remove_child(old_node);

	_add_to_scene(parent, instance, scene_root);

	parent->move_child(instance, old_idx);

	if (keep_transform) {
		Node3D *new_3d = Object::cast_to<Node3D>(instance);
		if (new_3d && is_3d) {
			new_3d->set_transform(old_xform);
		}
	}

	old_node->queue_free();
	_mark_unsaved();

	result["node_path"] = String(instance->get_path());
	result["replaced"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_batch_reparent_nodes(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String new_parent_path = _arg_string(p_args, "new_parent_path", "");
	Node *new_parent = _resolve_node_target(scene_root, new_parent_path, err);
	if (new_parent == nullptr) {
		return _make_error("new_parent_path not found: " + err);
	}

	Array node_paths = _arg_array(p_args, "node_paths");
	Array reparented;
	for (int i = 0; i < node_paths.size(); i++) {
		const String path = String(node_paths[i]);
		Node *node = _resolve_node_target(scene_root, path, err);
		if (node == nullptr) {
			continue;
		}
		if (node == scene_root) {
			continue;
		}
		Node *old_parent = node->get_parent();
		if (old_parent == nullptr) {
			continue;
		}
		old_parent->remove_child(node);
		new_parent->add_child(node, true);
		reparented.push_back(path);
	}

	_mark_unsaved();
	result["reparented"] = reparented;
	result["count"] = reparented.size();
	return result;
}

Dictionary YeetAIDock::_tool_set_node_meta(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}
	const String action = _arg_string(p_args, "action", "list").to_lower();
	const String meta_name = _arg_string(p_args, "meta_name", "");

	if (action == "set") {
		if (meta_name.is_empty()) {
			return _make_error("meta_name is required for set action");
		}
		const Variant meta_value = p_args.get("meta_value", Variant());
		node->set_meta(meta_name, meta_value);
		_mark_unsaved();
		result["set"] = true;
		result["meta_name"] = meta_name;
		return result;
	}

	if (action == "get") {
		if (meta_name.is_empty()) {
			return _make_error("meta_name is required for get action");
		}
		if (!node->has_meta(meta_name)) {
			return _make_error("Node has no meta named: " + meta_name);
		}
		result["meta_name"] = meta_name;
		result["meta_value"] = _json_safe_variant(node->get_meta(meta_name));
		return result;
	}

	// list
	List<StringName> meta_list;
	node->get_meta_list(&meta_list);
	Array metas;
	for (const StringName &name : meta_list) {
		Dictionary entry;
		entry["name"] = String(name);
		entry["value"] = _json_safe_variant(node->get_meta(name));
		metas.push_back(entry);
	}
	result["metas"] = metas;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// R. Environment & Rendering
// ═══════════════════════════════════════════════════════════════════════════

static WorldEnvironment *_get_or_create_world_environment(Node *p_scene_root) {
	if (p_scene_root == nullptr) {
		return nullptr;
	}
	WorldEnvironment *existing = Object::cast_to<WorldEnvironment>(p_scene_root->find_child("WorldEnvironment", true, false));
	if (existing != nullptr) {
		return existing;
	}
	WorldEnvironment *we = memnew(WorldEnvironment);
	we->set_name("WorldEnvironment");
	p_scene_root->add_child(we, true);
	we->set_owner(p_scene_root);
	Ref<Environment> env = memnew(Environment);
	we->set_environment(env);
	return we;
}

Dictionary YeetAIDock::_tool_create_sky(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String sky_type = _arg_string(p_args, "sky_type", "procedural").to_lower();

	Ref<Sky> sky = memnew(Sky);

	if (sky_type == "panorama") {
		const String panorama_path = _arg_string(p_args, "panorama_path", "");
		if (panorama_path.is_empty()) {
			return _make_error("panorama_path is required for panorama sky type");
		}
		Ref<PanoramaSkyMaterial> psm = memnew(PanoramaSkyMaterial);
		Ref<Texture2D> tex = ResourceLoader::load(panorama_path);
		if (tex.is_null()) {
			return _make_error("Failed to load panorama texture: " + panorama_path);
		}
		psm->set_panorama(tex);
		sky->set_material(psm);
	} else {
		Ref<ProceduralSkyMaterial> psm = memnew(ProceduralSkyMaterial);
		psm->set_sky_top_color(_arg_color(p_args, "sky_top_color", Color(0.385, 0.554, 0.706)));
		psm->set_sky_horizon_color(_arg_color(p_args, "sky_horizon_color", Color(0.646, 0.758, 0.872)));
		psm->set_sky_curve(_arg_float(p_args, "sky_curve", 0.25));
		psm->set_ground_bottom_color(_arg_color(p_args, "ground_bottom_color", Color(0.12, 0.12, 0.12)));
		psm->set_ground_horizon_color(_arg_color(p_args, "ground_horizon_color", Color(0.336, 0.346, 0.356)));
	// ProceduralSkyMaterial uses sun disk size (angle) + falloff curve (legacy sun_latitude/sun_longitude kept as aliases).
		const float sun_angle = p_args.has("sun_angle_max") ? _arg_float(p_args, "sun_angle_max", 35.0) : _arg_float(p_args, "sun_latitude", 35.0);
		const float sun_c = p_args.has("sun_curve") ? _arg_float(p_args, "sun_curve", 0.05) : _arg_float(p_args, "sun_longitude", 0.0);
		psm->set_sun_angle_max(sun_angle);
		psm->set_sun_curve(sun_c);
		sky->set_material(psm);
	}

	WorldEnvironment *we = _get_or_create_world_environment(scene_root);
	if (we == nullptr) {
		return _make_error("Failed to create WorldEnvironment");
	}
	we->get_environment()->set_sky(sky);
	we->get_environment()->set_sky_custom_fov(_arg_float(p_args, "sky_custom_fov", 0.0));

	_mark_unsaved();
	result["sky_type"] = sky_type;
	return result;
}

Dictionary YeetAIDock::_tool_set_environment_fog(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	WorldEnvironment *we = _get_or_create_world_environment(scene_root);
	if (we == nullptr || we->get_environment().is_null()) {
		return _make_error("Failed to get WorldEnvironment");
	}
	Ref<Environment> env = we->get_environment();

	const String fog_type = _arg_string(p_args, "fog_type", "depth").to_lower();

	if (fog_type == "depth" || fog_type == "volumetric") {
		env->set_fog_enabled(_arg_bool(p_args, "enabled", true));
		env->set_fog_mode(Environment::FOG_MODE_DEPTH);
		env->set_fog_light_color(_arg_color(p_args, "light_color", Color(0.518, 0.553, 0.612)));
		env->set_fog_light_energy(_arg_float(p_args, "light_energy", 1.0));
		env->set_fog_depth_begin(_arg_float(p_args, "depth_begin", 10.0));
		env->set_fog_depth_end(_arg_float(p_args, "depth_end", 100.0));
		env->set_fog_depth_curve(_arg_float(p_args, "depth_curve", 1.0));
	}

	if (fog_type == "height" || fog_type == "volumetric") {
		if (fog_type == "height") {
			env->set_fog_enabled(_arg_bool(p_args, "enabled", true));
		}
		const bool height_on = _arg_bool(p_args, "height_fog_enabled", true);
		const float hmin = _arg_float(p_args, "height_min", 0.0);
		const float hmax = _arg_float(p_args, "height_max", 100.0);
		const float hcurve = _arg_float(p_args, "height_curve", 1.0);
		env->set_fog_height(hmin);
		if (height_on) {
			float hd = (float)_arg_float(p_args, "height_density", 0.0);
			if (!p_args.has("height_density")) {
				hd = (0.02f / MAX(hmax - hmin, 0.01f)) * (float)hcurve;
			}
			env->set_fog_height_density(hd);
		} else {
			env->set_fog_height_density(0.0f);
		}
	}

	if (fog_type == "volumetric") {
		env->set_volumetric_fog_enabled(_arg_bool(p_args, "volumetric_enabled", true));
		env->set_volumetric_fog_density(_arg_float(p_args, "volumetric_fog_density", 0.01));
		env->set_volumetric_fog_albedo(_arg_color(p_args, "volumetric_fog_albedo", Color(1, 1, 1)));
		env->set_volumetric_fog_emission(_arg_color(p_args, "volumetric_fog_emission", Color(0, 0, 0)));
	}

	_mark_unsaved();
	result["fog_type"] = fog_type;
	return result;
}

Dictionary YeetAIDock::_tool_set_environment_tonemap(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	WorldEnvironment *we = _get_or_create_world_environment(scene_root);
	if (we == nullptr || we->get_environment().is_null()) {
		return _make_error("Failed to get WorldEnvironment");
	}
	Ref<Environment> env = we->get_environment();

	const String mapper = _arg_string(p_args, "tone_mapper", "aces").to_lower();
	if (mapper == "linear") {
		env->set_tonemapper(Environment::TONE_MAPPER_LINEAR);
	} else if (mapper == "reinhard") {
		env->set_tonemapper(Environment::TONE_MAPPER_REINHARDT);
	} else if (mapper == "filmic") {
		env->set_tonemapper(Environment::TONE_MAPPER_FILMIC);
	} else {
		env->set_tonemapper(Environment::TONE_MAPPER_ACES);
	}

	env->set_tonemap_exposure(_arg_float(p_args, "exposure", 1.0));
	env->set_tonemap_white(_arg_float(p_args, "white", 1.0));

	_mark_unsaved();
	result["tone_mapper"] = mapper;
	return result;
}

Dictionary YeetAIDock::_tool_set_environment_ss_effects(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	WorldEnvironment *we = _get_or_create_world_environment(scene_root);
	if (we == nullptr || we->get_environment().is_null()) {
		return _make_error("Failed to get WorldEnvironment");
	}
	Ref<Environment> env = we->get_environment();

	if (p_args.has("ssao_enabled")) {
		env->set_ssao_enabled(_arg_bool(p_args, "ssao_enabled", false));
		env->set_ssao_radius(_arg_float(p_args, "ssao_radius", 1.0));
		env->set_ssao_intensity(_arg_float(p_args, "ssao_intensity", 1.0));
	}

	if (p_args.has("ssr_enabled")) {
		env->set_ssr_enabled(_arg_bool(p_args, "ssr_enabled", false));
		env->set_ssr_max_steps(_arg_int(p_args, "ssr_max_steps", 64));
	}

	if (p_args.has("glow_enabled")) {
		env->set_glow_enabled(_arg_bool(p_args, "glow_enabled", false));
		env->set_glow_intensity(_arg_float(p_args, "glow_intensity", 0.8));
		env->set_glow_hdr_bleed_threshold(_arg_float(p_args, "glow_threshold", 1.0));
	}

	_mark_unsaved();
	result["updated"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_create_fog_volume(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "FogVolume");
	FogVolume *fv = memnew(FogVolume);
	fv->set_name(node_name);
	fv->set_size(_arg_vector3(p_args, "size", Vector3(10, 10, 10)));

	const String shape = _arg_string(p_args, "shape", "box").to_lower();
	if (shape == "ellipsoid") {
		fv->set_shape(RSE::FOG_VOLUME_SHAPE_ELLIPSOID);
	} else if (shape == "cone") {
		fv->set_shape(RSE::FOG_VOLUME_SHAPE_CONE);
	} else if (shape == "cylinder") {
		fv->set_shape(RSE::FOG_VOLUME_SHAPE_CYLINDER);
	} else {
		fv->set_shape(RSE::FOG_VOLUME_SHAPE_BOX);
	}

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, fv, scene_root);
	fv->set_position(_arg_vector3(p_args, "position", Vector3()));
	result["node_path"] = String(fv->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_reflection_probe(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "ReflectionProbe");
	ReflectionProbe *rp = memnew(ReflectionProbe);
	rp->set_name(node_name);
	rp->set_size(_arg_vector3(p_args, "extents", Vector3(10, 10, 10)));

	const String update_mode = _arg_string(p_args, "update_mode", "once").to_lower();
	if (update_mode == "always") {
		rp->set_update_mode(ReflectionProbe::UPDATE_ALWAYS);
	} else {
		rp->set_update_mode(ReflectionProbe::UPDATE_ONCE);
	}

	rp->set_enable_box_projection(_arg_bool(p_args, "box_projection", false));
	rp->set_intensity(_arg_float(p_args, "intensity", 1.0));
	rp->set_max_distance(_arg_float(p_args, "max_distance", 0.0));

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, rp, scene_root);
	rp->set_position(_arg_vector3(p_args, "position", Vector3()));
	result["node_path"] = String(rp->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_gi_probe(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "VoxelGI");
	VoxelGI *gi = memnew(VoxelGI);
	gi->set_name(node_name);
	gi->set_size(_arg_vector3(p_args, "extents", Vector3(10, 10, 10)));

	const String subdiv = _arg_string(p_args, "subdiv", "128").to_lower();
	if (subdiv == "64") {
		gi->set_subdiv(VoxelGI::SUBDIV_64);
	} else if (subdiv == "128") {
		gi->set_subdiv(VoxelGI::SUBDIV_128);
	} else if (subdiv == "256") {
		gi->set_subdiv(VoxelGI::SUBDIV_256);
	} else {
		gi->set_subdiv(VoxelGI::SUBDIV_512);
	}

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, gi, scene_root);
	gi->set_position(_arg_vector3(p_args, "position", Vector3()));
	result["node_path"] = String(gi->get_path());
	result["note"] = "Bake the VoxelGI manually from the editor (select the node → Bake).";
	return result;
}
