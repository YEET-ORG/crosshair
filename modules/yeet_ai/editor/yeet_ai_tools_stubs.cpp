/**************************************************************************/
/*  yeet_ai_tools_stubs.cpp                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/core_bind.h"
#include "core/object/class_db.h"
#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/templates/local_vector.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_uid.h"
#include "editor/editor_data.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/editor_interface.h"
#include "editor/settings/editor_settings.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/animation/animation_player.h"
#include "scene/main/viewport.h"
#include "scene/resources/environment.h"
#include "scene/resources/sky.h"
#include "scene/resources/texture.h"

Dictionary YeetAIDock::_tool_attach_script(const Dictionary &p_args) const {
	Dictionary resource_args;
	resource_args["scene_path"] = p_args.get("scene_path", "");
	resource_args["node_path"] = p_args.get("node_path", "");
	resource_args["property"] = "script";
	resource_args["resource_path"] = p_args.get("script_path", "");
	return _tool_assign_resource_to_property(resource_args);
}

Dictionary YeetAIDock::_tool_read_project_file(const Dictionary &p_args) const {
	Dictionary result;
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		result["error"] = "path must start with res://";
		return result;
	}
	if (!_is_allowed_text_file(path)) {
		result["error"] = "File extension not in the allowed text file list.";
		return result;
	}

	Error read_error = OK;
	const String content = FileAccess::get_file_as_string(path, &read_error);
	if (read_error != OK) {
		result["error"] = vformat("Failed to read file (error %d).", read_error);
		return result;
	}

	result["path"] = path;
	result["content"] = content;
	result["size"] = content.to_utf8_buffer().size();
	return result;
}

Dictionary YeetAIDock::_tool_batch_set_node_property(const Dictionary &p_args) const {
	Dictionary result;
	const Array entries = p_args.get("entries", Array());
	if (entries.is_empty()) {
		result["error"] = "entries array is required.";
		return result;
	}

	Array results;
	int ok_count = 0;
	for (int i = 0; i < entries.size(); i++) {
		const Dictionary entry = entries[i];
		Dictionary call_args;
		call_args["scene_path"] = entry.get("scene_path", p_args.get("scene_path", ""));
		call_args["node_path"] = entry.get("node_path", "");
		call_args["property"] = entry.get("property", "");
		call_args["value"] = entry.get("value", Variant());
		Dictionary r = _tool_set_node_property(call_args);
		if (r.has("error")) {
			results.push_back(r);
		} else {
			ok_count++;
		}
	}

	result["ok"] = true;
	result["total"] = entries.size();
	result["succeeded"] = ok_count;
	result["failed"] = entries.size() - ok_count;
	if (results.size() > 0) {
		result["errors"] = results;
	}
	return result;
}

Dictionary YeetAIDock::_tool_capture_dual_view(const Dictionary &p_args) const {
	Dictionary editor_result = _tool_capture_editor_viewport(p_args);
	Dictionary game_result = _tool_capture_game_viewport(p_args);
	Dictionary result;
	result["editor"] = editor_result;
	result["game"] = game_result;
	if (editor_result.has("error") && game_result.has("error")) {
		result["error"] = vformat("Both captures failed. Editor: %s | Game: %s", String(editor_result["error"]), String(game_result["error"]));
	}
	return result;
}

Dictionary YeetAIDock::_tool_capture_subviewport(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}
	SubViewport *sv = Object::cast_to<SubViewport>(node);
	if (sv == nullptr) {
		return _make_error("Node is not a SubViewport.");
	}
	const Ref<Texture2D> tex = sv->get_texture();
	if (tex.is_null()) {
		return _make_error("SubViewport has no texture.");
	}
	Ref<Image> img = tex->get_image();
	if (img.is_null() || img->is_empty()) {
		return _make_error("SubViewport returned an empty image.");
	}
	const int max_w = CLAMP(int(p_args.get("max_width", 640)), 64, 4096);
	if (img->get_width() > max_w) {
		img = img->duplicate();
		img->resize(max_w, max_w * img->get_height() / img->get_width(), Image::INTERPOLATE_LANCZOS);
	}
	Dictionary result;
	result["ok"] = true;
	result["width"] = img->get_width();
	result["height"] = img->get_height();
	const PackedByteArray png = img->save_png_to_buffer();
	const String b64 = CoreBind::Marshalls::get_singleton()->raw_to_base64(png);
	result["png_base64"] = b64;
	result["image_base64"] = b64;
	result["size_bytes"] = png.size();
	return result;
}

Dictionary YeetAIDock::_tool_capture_texture_resource(const Dictionary &p_args) const {
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		return _make_error("path must start with res://");
	}
	Ref<Texture2D> tex = ResourceLoader::load(path);
	if (tex.is_null()) {
		return _make_error("Failed to load texture resource.");
	}
	Ref<Image> img = tex->get_image();
	if (img.is_null() || img->is_empty()) {
		return _make_error("Texture returned an empty image.");
	}
	const int max_w = CLAMP(int(p_args.get("max_width", 640)), 64, 4096);
	if (img->get_width() > max_w) {
		img = img->duplicate();
		img->resize(max_w, max_w * img->get_height() / img->get_width(), Image::INTERPOLATE_LANCZOS);
	}
	Dictionary result;
	result["ok"] = true;
	result["path"] = path;
	result["width"] = img->get_width();
	result["height"] = img->get_height();
	const PackedByteArray png = img->save_png_to_buffer();
	const String b64 = CoreBind::Marshalls::get_singleton()->raw_to_base64(png);
	result["png_base64"] = b64;
	result["image_base64"] = b64;
	result["size_bytes"] = png.size();
	return result;
}

Dictionary YeetAIDock::_tool_copy_project_file(const Dictionary &p_args) const {
	Dictionary result;
	const String from_path = p_args.get("from_path", "");
	const String to_path = p_args.get("to_path", "");
	if (!from_path.begins_with("res://") || !to_path.begins_with("res://")) {
		result["error"] = "Both from_path and to_path must start with res://";
		return result;
	}

	Dictionary read_args;
	read_args["path"] = from_path;
	Dictionary read_result = _tool_read_project_file(read_args);
	if (read_result.has("error")) {
		return read_result;
	}

	Dictionary write_args;
	write_args["path"] = to_path;
	write_args["contents"] = read_result["content"];
	write_args["overwrite"] = p_args.get("overwrite", false);
	Dictionary write_result = _tool_write_project_file(write_args);
	if (write_result.has("error")) {
		return write_result;
	}

	result["ok"] = true;
	result["from_path"] = from_path;
	result["to_path"] = to_path;
	result["size"] = read_result.get("size", 0);
	return result;
}

Dictionary YeetAIDock::_tool_create_particle_emitter(const Dictionary &p_args) const {
	Dictionary node_args;
	node_args["scene_path"] = p_args.get("scene_path", "");
	node_args["parent_path"] = p_args.get("parent_path", ".");
	const String dim = String(p_args.get("dimension", "3d")).to_lower();
	node_args["node_type"] = dim == "2d" ? "GPUParticles2D" : "GPUParticles3D";
	node_args["node_name"] = p_args.get("node_name", dim == "2d" ? "Particles2D" : "Particles3D");
	return _tool_add_node(node_args);
}

Dictionary YeetAIDock::_tool_create_ui_element(const Dictionary &p_args) const {
	const String element_type = String(p_args.get("element_type", "")).to_lower();
	Dictionary ctrl_args;
	ctrl_args["scene_path"] = p_args.get("scene_path", "");
	ctrl_args["parent_path"] = p_args.get("parent_path", ".");
	ctrl_args["node_name"] = p_args.get("node_name", element_type);
	if (element_type == "button" || element_type == "label" || element_type == "lineedit" ||
		element_type == "textedit" || element_type == "panel" || element_type == "hboxcontainer" ||
		element_type == "vboxcontainer" || element_type == "gridcontainer" || element_type == "margincontainer" ||
		element_type == "scrollcontainer" || element_type == "tabcontainer" || element_type == "checkbox" ||
		element_type == "spinbox" || element_type == "colorpickerbutton" || element_type == "texturebutton" ||
		element_type == "textureprogress" || element_type == "progressbar" || element_type == "slider" ||
		element_type == "hslider" || element_type == "vslider") {
		ctrl_args["node_type"] = element_type.capitalize().replace(" ", "");
		return _tool_add_node(ctrl_args);
	}
	return _make_error(vformat("Unknown element_type '%s'. Use create_control_node or create_container_layout for custom types.", element_type));
}

Dictionary YeetAIDock::_tool_disconnect_signal(const Dictionary &p_args) const {
	Dictionary result;
	const String signal_name = p_args.get("signal_name", "");
	const String source_node_path = p_args.get("source_node_path", "");
	if (signal_name.is_empty() || source_node_path.is_empty()) {
		result["error"] = "signal_name and source_node_path are required.";
		return result;
	}

	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "source_node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	if (!node->has_signal(signal_name)) {
		return _make_error(vformat("Node '%s' has no signal '%s'.", source_node_path, signal_name));
	}

	const String target_node_path = p_args.get("target_node_path", "");
	const String method_name = p_args.get("method_name", "");
	if (target_node_path.is_empty() || method_name.is_empty()) {
		List<Connection> conns;
		node->get_signal_connection_list(StringName(signal_name), &conns);
		for (const Connection &c : conns) {
			node->disconnect(StringName(signal_name), c.callable);
		}
		result["ok"] = true;
		result["disconnected_all"] = true;
		return result;
	}

	Node *target_node = _resolve_node_target(scene_root, target_node_path, err);
	if (target_node == nullptr) {
		return _make_error(err);
	}

	Callable callable(target_node, method_name);
	if (!node->is_connected(signal_name, callable)) {
		return _make_error(vformat("Signal '%s' is not connected to %s::%s.", signal_name, target_node_path, method_name));
	}

	node->disconnect(signal_name, callable);
	result["ok"] = true;
	result["scene_path"] = scene_root->get_scene_file_path();
	result["source_node_path"] = String(node->get_path());
	result["signal_name"] = signal_name;
	result["target_node_path"] = String(target_node->get_path());
	result["method_name"] = method_name;
	return result;
}

Dictionary YeetAIDock::_tool_focus_scene_tree_node(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}
	EditorInterface::get_singleton()->get_selection()->clear();
	EditorInterface::get_singleton()->get_selection()->add_node(node);
	Dictionary result;
	result["ok"] = true;
	result["node_path"] = String(node->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_get_binary_file_metadata(const Dictionary &p_args) const {
	Dictionary result;
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		result["error"] = "path must start with res://";
		return result;
	}

	const String global_path = ProjectSettings::get_singleton()->globalize_path(path);
	if (!FileAccess::exists(path)) {
		result["error"] = "File does not exist.";
		return result;
	}

	Ref<FileAccess> fa = FileAccess::open(path, FileAccess::READ);
	if (fa.is_null()) {
		result["error"] = "Failed to open file.";
		return result;
	}

	result["path"] = path;
	result["global_path"] = global_path;
	result["size"] = fa->get_length();
	result["modified_time"] = FileAccess::get_modified_time(global_path);
	return result;
}

Dictionary YeetAIDock::_tool_get_editor_3d_camera_transform(const Dictionary &p_args) const {
	EditorInterface *ei = EditorInterface::get_singleton();
	if (ei == nullptr) {
		return _make_error("EditorInterface is unavailable.");
	}
	SubViewport *vp = ei->get_editor_viewport_3d(0);
	if (vp == nullptr) {
		return _make_error("No 3D viewport available.");
	}
	Camera3D *cam = nullptr;
	for (int i = 0; i < vp->get_child_count(); i++) {
		cam = Object::cast_to<Camera3D>(vp->get_child(i));
		if (cam) break;
	}
	if (cam == nullptr) {
		return _make_error("No Camera3D found in the 3D viewport.");
	}
	Dictionary result;
	result["ok"] = true;
	const Transform3D xform = cam->get_global_transform();
	const Vector3 pos = xform.origin;
	const Basis basis = xform.basis;
	Array pos_arr;
	pos_arr.push_back(pos.x);
	pos_arr.push_back(pos.y);
	pos_arr.push_back(pos.z);
	result["position"] = pos_arr;
	Vector3 euler = basis.get_euler();
	Array rot_arr;
	rot_arr.push_back(Math::rad_to_deg(euler.x));
	rot_arr.push_back(Math::rad_to_deg(euler.y));
	rot_arr.push_back(Math::rad_to_deg(euler.z));
	result["rotation_degrees"] = rot_arr;
	result["fov"] = cam->get_fov();
	result["near"] = cam->get_near();
	result["far"] = cam->get_far();
	return result;
}

Dictionary YeetAIDock::_tool_get_editor_inspector_subject(const Dictionary &p_args) const {
	EditorInterface *ei = EditorInterface::get_singleton();
	if (ei == nullptr) {
		return _make_error("EditorInterface is unavailable.");
	}
	EditorSelection *sel = ei->get_selection();
	if (sel == nullptr) {
		return _make_error("No editor selection available.");
	}
	TypedArray<Node> selected = sel->get_selected_nodes();
	Dictionary result;
	result["ok"] = true;
	if (selected.size() == 0) {
		result["has_selection"] = false;
		return result;
	}
	Array entries;
	for (int i = 0; i < selected.size(); i++) {
		Node *n = Object::cast_to<Node>(selected[i]);
		if (n) {
			Dictionary entry;
			entry["path"] = String(n->get_path());
			entry["type"] = n->get_class();
			entries.push_back(entry);
		}
	}
	result["has_selection"] = true;
	result["count"] = entries.size();
	result["selected"] = entries;
	return result;
}

Dictionary YeetAIDock::_tool_get_editor_settings(const Dictionary &p_args) const {
	Dictionary result;
	const String prefix = String(p_args.get("prefix", "")).strip_edges();

	EditorSettings *es = EditorSettings::get_singleton();
	if (es == nullptr) {
		result["error"] = "EditorSettings is unavailable.";
		return result;
	}

	Dictionary values;
	List<PropertyInfo> props;
	es->get_property_list(&props);
	for (const PropertyInfo &pi : props) {
		if (((pi.usage & PROPERTY_USAGE_STORAGE) == 0) && ((pi.usage & PROPERTY_USAGE_EDITOR) == 0)) {
			continue;
		}
		if (!prefix.is_empty() && !String(pi.name).begins_with(prefix)) {
			continue;
		}
		values[pi.name] = _json_safe_variant(es->get(pi.name));
	}

	result["prefix"] = prefix;
	result["count"] = values.size();
	result["settings"] = values;
	return result;
}

Dictionary YeetAIDock::_tool_get_export_presets(const Dictionary &p_args) const {
	Dictionary result;
	const String path = "res://export_presets.cfg";
	Error read_err = OK;
	const String content = FileAccess::get_file_as_string(path, &read_err);
	if (read_err != OK) {
		result["error"] = "No export_presets.cfg found.";
		return result;
	}
	result["ok"] = true;
	result["path"] = path;
	result["content"] = content;
	result["size"] = content.to_utf8_buffer().size();
	return result;
}

Dictionary YeetAIDock::_tool_get_gdscript_errors(const Dictionary &p_args) const {
	Dictionary log_result = _tool_get_editor_log(p_args);
	if (log_result.has("error")) {
		return log_result;
	}
	const Array messages = log_result.get("messages", Array());
	Array errors;
	for (int i = 0; i < messages.size(); i++) {
		const String line = messages[i];
		if (line.contains("script error") || line.contains("GDScript") || line.contains("parse error") ||
			line.contains("validate error") || line.contains("runtime error") || line.contains("ERROR")) {
			errors.push_back(line);
		}
	}
	Dictionary result;
	result["ok"] = true;
	result["error_count"] = errors.size();
	result["errors"] = errors;
	return result;
}

Dictionary YeetAIDock::_tool_get_global_classes(const Dictionary &p_args) const {
	Dictionary result;
	const String query = String(p_args.get("query", "")).strip_edges().to_lower();
	const int max_results = CLAMP(int(p_args.get("max_results", 128)), 1, 1024);

	LocalVector<StringName> classes;
	ClassDB::get_class_list(classes);

	Array class_list;
	for (const StringName &cn : classes) {
		if (!ClassDB::can_instantiate(cn)) {
			continue;
		}
		if (ClassDB::is_virtual(cn)) {
			continue;
		}
		const String name = String(cn);
		if (!query.is_empty() && !name.to_lower().contains(query)) {
			continue;
		}
		Dictionary entry;
		entry["name"] = name;
		entry["parent"] = String(ClassDB::get_parent_class(cn));
		class_list.push_back(entry);
		if (class_list.size() >= max_results) {
			break;
		}
	}

	result["query"] = query;
	result["count"] = class_list.size();
	result["classes"] = class_list;
	return result;
}

Dictionary YeetAIDock::_tool_get_remote_scene_tree(const Dictionary &p_args) const {
	return _make_error("Not yet implemented. Requires a running game session with the remote scene tree accessible via the editor debugger.");
}

Dictionary YeetAIDock::_tool_get_resource_dependencies(const Dictionary &p_args) const {
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		return _make_error("path must start with res://");
	}
	List<String> deps;
	ResourceLoader::get_dependencies(path, &deps);
	Dictionary result;
	result["ok"] = true;
	result["path"] = path;
	Array dep_list;
	for (const String &d : deps) {
		dep_list.push_back(d);
	}
	result["dependencies"] = dep_list;
	result["count"] = dep_list.size();
	return result;
}

Dictionary YeetAIDock::_tool_get_scene_dependency_closure(const Dictionary &p_args) const {
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		return _make_error("path must start with res://");
	}
	const int max_depth = CLAMP(int(p_args.get("max_depth", 10)), 1, 50);
	Array all_deps;
	Vector<String> queue;
	queue.push_back(path);
	Vector<String> visited;
	int depth = 0;
	while (!queue.is_empty() && depth < max_depth) {
		const String current = queue[queue.size() - 1];
		queue.remove_at(queue.size() - 1);
		if (visited.has(current)) continue;
		visited.push_back(current);
		List<String> deps;
		ResourceLoader::get_dependencies(current, &deps);
		for (const String &d : deps) {
			if (!visited.has(d)) {
				queue.push_back(d);
			}
			if (!all_deps.has(d)) {
				all_deps.push_back(d);
			}
		}
		depth++;
	}
	Dictionary result;
	result["ok"] = true;
	result["path"] = path;
	result["dependencies"] = all_deps;
	result["count"] = all_deps.size();
	return result;
}

Dictionary YeetAIDock::_tool_get_shader_code(const Dictionary &p_args) const {
	Dictionary read_args;
	read_args["path"] = p_args.get("path", "");
	return _tool_read_project_file(read_args);
}

Dictionary YeetAIDock::_tool_get_signal_connections(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	Dictionary result;
	const String signal_name = p_args.get("signal_name", "");

	Array conn_list;
	if (!signal_name.is_empty()) {
		if (!node->has_signal(signal_name)) {
			return _make_error(vformat("Node has no signal '%s'.", signal_name));
		}
		List<Connection> conns;
		node->get_signal_connection_list(StringName(signal_name), &conns);
		for (const Connection &c : conns) {
			Dictionary entry;
			entry["signal"] = signal_name;
			Object *ot = c.callable.get_object();
			Node *tn = Object::cast_to<Node>(ot);
			entry["target"] = tn ? String(tn->get_path()) : String();
			entry["method"] = String(c.callable.get_method());
			conn_list.push_back(entry);
		}
	} else {
		List<MethodInfo> signals;
		node->get_signal_list(&signals);
		for (const MethodInfo &si : signals) {
			List<Connection> conns;
			node->get_signal_connection_list(si.name, &conns);
			for (const Connection &c : conns) {
				Dictionary entry;
				entry["signal"] = String(si.name);
				Object *ot = c.callable.get_object();
				Node *tn = Object::cast_to<Node>(ot);
				entry["target"] = tn ? String(tn->get_path()) : String();
				entry["method"] = String(c.callable.get_method());
				conn_list.push_back(entry);
			}
		}
	}

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(node->get_path());
	result["count"] = conn_list.size();
	result["connections"] = conn_list;
	return result;
}

Dictionary YeetAIDock::_tool_get_translation_overview(const Dictionary &p_args) const {
	Dictionary result;
	Ref<DirAccess> dir = DirAccess::open("res://");
	if (dir.is_null()) {
		return _make_error("Failed to open project directory.");
	}
	Array locales;
	Vector<String> queue;
	queue.push_back("res://");
	while (!queue.is_empty()) {
		const String current = queue[queue.size() - 1];
		queue.remove_at(queue.size() - 1);
		Ref<DirAccess> d = DirAccess::open(current);
		if (d.is_null()) continue;
		d->list_dir_begin();
		String fn = d->get_next();
		while (!fn.is_empty()) {
			if (fn == "." || fn == "..") { fn = d->get_next(); continue; }
			const String full = current.ends_with("/") ? current + fn : current + "/" + fn;
			if (d->current_is_dir()) {
				queue.push_back(full);
			} else if (fn.ends_with(".translation") || fn.ends_with(".po")) {
				locales.push_back(full);
			}
			fn = d->get_next();
		}
		d->list_dir_end();
		if (locales.size() >= 256) break;
	}
	result["ok"] = true;
	result["translation_files"] = locales;
	result["count"] = locales.size();
	return result;
}

Dictionary YeetAIDock::_tool_get_world_environment(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}
	WorldEnvironment *we = Object::cast_to<WorldEnvironment>(node);
	if (we == nullptr) {
		return _make_error("Node is not a WorldEnvironment.");
	}
	Dictionary result;
	result["ok"] = true;
	result["node_path"] = String(we->get_path());
	Ref<Environment> env = we->get_environment();
	result["has_environment"] = env.is_valid();
	if (env.is_valid()) {
		result["ambient_light_color"] = env->get_ambient_light_color().to_html();
		result["ambient_light_energy"] = env->get_ambient_light_energy();
	}
	Ref<Sky> sky = env.is_valid() ? env->get_sky() : Ref<Sky>();
	result["has_sky"] = sky.is_valid();
	return result;
}

Dictionary YeetAIDock::_tool_manage_autoloads(const Dictionary &p_args) const {
	const String action = String(p_args.get("action", "list")).to_lower();
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps == nullptr) {
		return _make_error("ProjectSettings is unavailable.");
	}
	Dictionary result;
	if (action == "list") {
		Array autoloads;
		const HashMap<StringName, ProjectSettings::AutoloadInfo> &map = ps->get_autoload_list();
		for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &E : map) {
			Dictionary entry;
			entry["name"] = String(E.key);
			entry["path"] = E.value.path;
			entry["singleton"] = E.value.is_singleton;
			autoloads.push_back(entry);
		}
		result["ok"] = true;
		result["autoloads"] = autoloads;
		result["count"] = autoloads.size();
	} else if (action == "add") {
		const String name = p_args.get("name", "");
		const String autoload_path = p_args.get("path", "");
		if (name.is_empty() || autoload_path.is_empty()) {
			return _make_error("name and path are required for add action.");
		}
		ProjectSettings::AutoloadInfo info;
		info.name = name;
		info.path = autoload_path;
		info.is_singleton = p_args.get("singleton", true);
		ps->add_autoload(info);
		ps->save();
		result["ok"] = true;
		result["added"] = name;
	} else if (action == "remove") {
		const String name = p_args.get("name", "");
		if (name.is_empty()) {
			return _make_error("name is required for remove action.");
		}
		if (!ps->has_autoload(StringName(name))) {
			return _make_error(vformat("Autoload '%s' not found.", name));
		}
		ps->remove_autoload(StringName(name));
		ps->save();
		result["ok"] = true;
		result["removed"] = name;
	} else {
		return _make_error(vformat("Unknown action '%s'. Use list, add, or remove.", action));
	}
	return result;
}

Dictionary YeetAIDock::_tool_patch_editor_settings(const Dictionary &p_args) const {
	Dictionary result;
	const Dictionary settings = p_args.get("settings", Dictionary());
	if (settings.is_empty()) {
		return _make_error("settings dictionary is required.");
	}

	EditorSettings *es = EditorSettings::get_singleton();
	if (es == nullptr) {
		return _make_error("EditorSettings is unavailable.");
	}

	if (!_get_editor_setting_bool("yeet_ai/chat/allow_editor_settings_write", true)) {
		return _make_error("Editor settings writes are disabled (yeet_ai/chat/allow_editor_settings_write).");
	}

	Array updated;
	const Array keys = settings.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String key = keys[i];
		if (es->has_setting(key)) {
			es->set_setting(key, settings[key]);
			updated.push_back(key);
		} else {
			es->set_initial_value(StringName(key), settings[key], true);
			es->set_setting(key, settings[key]);
			updated.push_back(key);
		}
	}

	EditorSettings::save();

	result["ok"] = true;
	result["updated"] = updated;
	result["count"] = updated.size();
	return result;
}

Dictionary YeetAIDock::_tool_patch_project_settings(const Dictionary &p_args) const {
	Dictionary result;
	const Dictionary settings = p_args.get("settings", Dictionary());
	if (settings.is_empty()) {
		return _make_error("settings dictionary is required.");
	}

	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps == nullptr) {
		return _make_error("ProjectSettings is unavailable.");
	}

	if (!_get_editor_setting_bool("yeet_ai/chat/allow_project_settings_write", true)) {
		return _make_error("Project settings writes are disabled (yeet_ai/chat/allow_project_settings_write).");
	}

	Array updated;
	const Array keys = settings.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String key = keys[i];
		ps->set_setting(key, settings[key]);
		updated.push_back(key);
	}

	Error save_err = ps->save();
	if (save_err != OK) {
		return _make_error(vformat("Failed to save project.godot (error %d).", save_err));
	}

	result["ok"] = true;
	result["updated"] = updated;
	result["count"] = updated.size();
	return result;
}

Dictionary YeetAIDock::_tool_play_animation(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(node);
	if (ap == nullptr) {
		return _make_error("Node is not an AnimationPlayer.");
	}

	const String anim_name = p_args.get("animation_name", "");
	if (anim_name.is_empty()) {
		return _make_error("animation_name is required.");
	}

	if (!ap->has_animation(anim_name)) {
		return _make_error(vformat("AnimationPlayer has no animation '%s'.", anim_name));
	}

	const float from = p_args.has("from_position") ? double(p_args["from_position"]) : 0.0;
	const float speed = p_args.has("speed") ? double(p_args["speed"]) : 1.0;
	const bool backwards = p_args.get("backwards", false);

	ap->set_speed_scale(speed);
	ap->play(anim_name, -1.0, backwards ? -1.0 : 1.0, from > 0.0);
	if (from > 0.0) {
		ap->seek(from, true);
	}

	Dictionary result;
	result["ok"] = true;
	result["animation"] = anim_name;
	result["playing"] = ap->is_playing();
	return result;
}

Dictionary YeetAIDock::_tool_query_physics(const Dictionary &p_args) const {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps == nullptr) {
		return _make_error("ProjectSettings is unavailable.");
	}
	Dictionary result;
	result["ok"] = true;
	result["physics_ticks_per_second"] = ps->get_setting("physics/common/physics_ticks_per_second");
	result["physics_jitter_fix"] = ps->get_setting("physics/common/physics_jitter_fix");
	result["gravity"] = ps->get_setting("physics/3d/default_gravity");
	result["gravity_vector"] = ps->get_setting("physics/3d/default_gravity_vector");
	return result;
}

Dictionary YeetAIDock::_tool_reimport_project_files(const Dictionary &p_args) const {
	if (!_get_editor_setting_bool("yeet_ai/chat/allow_reimport", true)) {
		return _make_error("Reimport is disabled (yeet_ai/chat/allow_reimport).");
	}
	Array paths = p_args.get("paths", Array());
	if (paths.is_empty()) {
		return _make_error("paths array is required.");
	}
	EditorFileSystem *efs = EditorFileSystem::get_singleton();
	if (efs == nullptr) {
		return _make_error("EditorFileSystem is unavailable.");
	}
	int reimported = 0;
	Vector<String> to_reimport;
	for (int i = 0; i < paths.size(); i++) {
		const String path = paths[i];
		if (efs->get_file_type(path) != String()) {
			to_reimport.push_back(path);
		}
	}
	if (!to_reimport.is_empty()) {
		efs->reimport_files(to_reimport);
		reimported = to_reimport.size();
	}
	Dictionary result;
	result["ok"] = true;
	result["reimported"] = reimported;
	result["attempted"] = paths.size();
	return result;
}

Dictionary YeetAIDock::_tool_rename_resource_references(const Dictionary &p_args) const {
	return _make_error("Not yet implemented. Renaming resource references requires a full project-wide search and replace across all text-based files. Use grep_project_files + update_gdscript_file / write_project_file to manually update references.");
}

Dictionary YeetAIDock::_tool_replace_in_project_files(const Dictionary &p_args) const {
	if (!_get_editor_setting_bool("yeet_ai/chat/allow_replace_in_files", true)) {
		return _make_error("Replace in files is disabled (yeet_ai/chat/allow_replace_in_files).");
	}
	const String query = p_args.get("query", "");
	const String replacement = p_args.get("replacement", "");
	if (query.is_empty()) {
		return _make_error("query is required.");
	}
	const Array include_extensions = p_args.get("include_extensions", Array());
	Dictionary grep_args;
	grep_args["query"] = query;
	grep_args["path"] = p_args.get("path", "res://");
	grep_args["include_extensions"] = include_extensions;
	grep_args["max_results"] = 500;
	Dictionary grep_result = _tool_grep_project_files(grep_args);
	if (grep_result.has("error")) {
		return grep_result;
	}
	const Array matches = grep_result.get("matches", Array());
	int replaced_count = 0;
	Array modified_files;
	for (int i = 0; i < matches.size(); i++) {
		const Dictionary match = matches[i];
		const String file_path = match.get("path", "");
		if (file_path.is_empty() || !_is_allowed_text_file(file_path)) {
			continue;
		}
		Error read_err = OK;
		String content = FileAccess::get_file_as_string(file_path, &read_err);
		if (read_err != OK) {
			continue;
		}
		const String new_content = content.replace(query, replacement);
		if (new_content == content) {
			continue;
		}
		Dictionary write_args;
		write_args["path"] = file_path;
		write_args["contents"] = new_content;
		write_args["overwrite"] = true;
		Dictionary write_result = _tool_write_project_file(write_args);
		if (!write_result.has("error")) {
			replaced_count++;
			if (!modified_files.has(file_path)) {
				modified_files.push_back(file_path);
			}
		}
	}
	Dictionary result;
	result["ok"] = true;
	result["query"] = query;
	result["replacement"] = replacement;
	result["files_modified"] = modified_files;
	result["replaced_count"] = replaced_count;
	return result;
}

Dictionary YeetAIDock::_tool_resolve_resource_uid(const Dictionary &p_args) const {
	const int64_t uid = int64_t(p_args.get("uid", -1));
	if (uid < 0) {
		return _make_error("uid must be a non-negative integer.");
	}
	ResourceUID *ruid = ResourceUID::get_singleton();
	if (ruid == nullptr) {
		return _make_error("ResourceUID is unavailable.");
	}
	const String path = ruid->get_id_path(uid);
	Dictionary result;
	if (path.is_empty()) {
		result["error"] = vformat("No resource found for UID %d.", uid);
	} else {
		result["ok"] = true;
		result["uid"] = uid;
		result["path"] = path;
	}
	return result;
}

Dictionary YeetAIDock::_tool_run_gdscript_expression(const Dictionary &p_args) const {
	return _make_error("Arbitrary GDScript expression evaluation is disabled for security. Use the Script editor or run_gdscript_test for controlled test execution.");
}

Dictionary YeetAIDock::_tool_save_resource(const Dictionary &p_args) const {
	Dictionary result;
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		return _make_error("path must start with res://");
	}

	const String global_path = ProjectSettings::get_singleton()->globalize_path(path);
	Ref<Resource> res = ResourceLoader::load(path);
	if (res.is_null()) {
		return _make_error("Failed to load resource.");
	}

	const int flags = p_args.get("merge_with_existing", true) ? ResourceSaver::FLAG_REPLACE_SUBRESOURCE_PATHS : 0;
	Error save_err = ResourceSaver::save(res, path, flags);
	if (save_err != OK) {
		return _make_error(vformat("Failed to save resource (error %d).", save_err));
	}

	result["ok"] = true;
	result["path"] = path;
	return result;
}

Dictionary YeetAIDock::_tool_scan_project_filesystem(const Dictionary &p_args) const {
	Dictionary tree_args;
	tree_args["root"] = p_args.get("root", "res://");
	tree_args["max_depth"] = p_args.get("max_depth", 5);
	tree_args["include_extensions"] = p_args.get("include_extensions", Array());
	return _tool_get_project_tree(tree_args);
}

Dictionary YeetAIDock::_tool_search_project_settings_keys(const Dictionary &p_args) const {
	Dictionary result;
	const String pattern = String(String(p_args.get("pattern", "")).strip_edges()).to_lower();
	const int max_results = CLAMP(int(p_args.get("max_results", 128)), 1, 1024);

	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps == nullptr) {
		result["error"] = "ProjectSettings is unavailable.";
		return result;
	}

	Array matches;
	List<PropertyInfo> props;
	ps->get_property_list(&props);
	for (const PropertyInfo &pi : props) {
		const String pin = String(pi.name);
		if (pin.begins_with("_") || pin.begins_with(".")) {
			continue;
		}
		if (!pattern.is_empty() && !pin.to_lower().contains(pattern)) {
			continue;
		}
		Dictionary entry;
		entry["key"] = pi.name;
		entry["type"] = Variant::get_type_name(pi.type);
		entry["value"] = _json_safe_variant(ps->get_setting_with_override(pi.name));
		matches.push_back(entry);
		if (matches.size() >= max_results) {
			break;
		}
	}

	result["pattern"] = pattern;
	result["count"] = matches.size();
	result["keys"] = matches;
	return result;
}

Dictionary YeetAIDock::_tool_set_editor_3d_camera(const Dictionary &p_args) const {
	EditorInterface *ei = EditorInterface::get_singleton();
	if (ei == nullptr) {
		return _make_error("EditorInterface is unavailable.");
	}
	SubViewport *vp = ei->get_editor_viewport_3d(0);
	if (vp == nullptr) {
		return _make_error("No 3D viewport available.");
	}
	Camera3D *cam = nullptr;
	for (int i = 0; i < vp->get_child_count(); i++) {
		cam = Object::cast_to<Camera3D>(vp->get_child(i));
		if (cam) break;
	}
	if (cam == nullptr) {
		return _make_error("No Camera3D found in the 3D viewport.");
	}
	const Vector3 pos = _arg_vector3(p_args, "position", cam->get_global_transform().origin);
	const Vector3 rot = _arg_vector3(p_args, "rotation_degrees", Vector3());
	const float fov = p_args.has("fov") ? double(p_args["fov"]) : cam->get_fov();
	Transform3D xform;
	xform.origin = pos;
	if (p_args.has("rotation_degrees")) {
		xform.basis = Basis::from_euler(Vector3(Math::deg_to_rad(rot.x), Math::deg_to_rad(rot.y), Math::deg_to_rad(rot.z)));
	}
	cam->set_global_transform(xform);
	cam->set_fov(fov);
	Dictionary result;
	result["ok"] = true;
	Array pos_out;
	pos_out.push_back(pos.x);
	pos_out.push_back(pos.y);
	pos_out.push_back(pos.z);
	result["position"] = pos_out;
	result["fov"] = fov;
	return result;
}

Dictionary YeetAIDock::_tool_set_node_collision_layers(const Dictionary &p_args) const {
	Dictionary result;
	const String property = p_args.get("property", "collision_layer");
	if (property != "collision_layer" && property != "collision_mask") {
		return _make_error("property must be 'collision_layer' or 'collision_mask'.");
	}
	Dictionary set_args;
	set_args["scene_path"] = p_args.get("scene_path", "");
	set_args["node_path"] = p_args.get("node_path", "");
	set_args["property"] = property;
	set_args["value"] = p_args.get("value", 1);
	return _tool_set_node_property(set_args);
}

Dictionary YeetAIDock::_tool_set_world_environment(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}
	WorldEnvironment *we = Object::cast_to<WorldEnvironment>(node);
	if (we == nullptr) {
		return _make_error("Node is not a WorldEnvironment.");
	}
	Ref<Environment> env = we->get_environment();
	if (env.is_null()) {
		env.instantiate();
		we->set_environment(env);
	}
	if (p_args.has("ambient_light_color")) {
		env->set_ambient_light_color(_arg_color(p_args, "ambient_light_color"));
	}
	if (p_args.has("ambient_light_energy")) {
		env->set_ambient_light_energy(double(p_args["ambient_light_energy"]));
	}
	if (p_args.has("tonemap_mode")) {
		env->set_tonemapper(Environment::ToneMapper(int(p_args["tonemap_mode"])));
	}
	if (p_args.has("tonemap_exposure")) {
		env->set_tonemap_exposure(double(p_args["tonemap_exposure"]));
	}
	_mark_unsaved();
	Dictionary result;
	result["ok"] = true;
	result["node_path"] = String(we->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_update_shader_code(const Dictionary &p_args) const {
	const String path = p_args.get("path", "");
	if (!path.begins_with("res://")) {
		return _make_error("path must start with res://");
	}
	Dictionary read_args;
	read_args["path"] = path;
	Dictionary read_result = _tool_read_project_file(read_args);
	if (read_result.has("error")) {
		return read_result;
	}
	Dictionary write_args;
	write_args["path"] = path;
	write_args["contents"] = p_args.get("contents", read_result.get("content", ""));
	write_args["overwrite"] = true;
	return _tool_write_project_file(write_args);
}

Dictionary YeetAIDock::_tool_validate_scene(const Dictionary &p_args) const {
	String err;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), err);
	if (scene_root == nullptr) {
		return _make_error(err);
	}
	Array issues;
	if (scene_root->get_scene_file_path().is_empty()) {
		issues.push_back("Scene has no file path (unsaved).");
	}
	int orphan_count = 0;
	Vector<Node *> stack;
	stack.push_back(scene_root);
	while (!stack.is_empty()) {
		Node *n = stack[stack.size() - 1];
		stack.remove_at(stack.size() - 1);
		if (n != scene_root && n->get_owner() != scene_root) {
			orphan_count++;
		}
		for (int i = 0; i < n->get_child_count(); i++) {
			stack.push_back(n->get_child(i));
		}
	}
	if (orphan_count > 0) {
		issues.push_back(vformat("Found %d node(s) without proper owner.", orphan_count));
	}
	Dictionary result;
	result["ok"] = true;
	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_count"] = scene_root->get_child_count();
	result["issues"] = issues;
	result["issue_count"] = issues.size();
	result["valid"] = issues.is_empty();
	return result;
}
