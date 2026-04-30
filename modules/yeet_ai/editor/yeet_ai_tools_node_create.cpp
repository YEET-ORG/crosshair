/**************************************************************************/
/*  yeet_ai_tools_node_create.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/editor_interface.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "scene/resources/packed_scene.h"

Dictionary YeetAIDock::_tool_add_node(const Dictionary &p_args) const {
	Dictionary result;
	const String node_type = p_args.get("node_type", "");
	const String node_name = p_args.get("node_name", "");
	const String parent_path = p_args.get("parent_path", ".");
	if (node_type.is_empty() || node_name.is_empty()) {
		result["error"] = "node_type and node_name are required.";
		return result;
	}

	if (!ClassDB::class_exists(node_type) || !ClassDB::can_instantiate(node_type) || ClassDB::is_virtual(node_type)) {
		result["error"] = "node_type is not instantiable.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *parent_node = _resolve_node_target(scene_root, parent_path, error);
	if (parent_node == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *new_node = Object::cast_to<Node>(ClassDB::instantiate(node_type));
	if (new_node == nullptr) {
		result["error"] = "Failed to instantiate node_type as a Node.";
		return result;
	}

	new_node->set_name(node_name);
	_add_to_scene(parent_node, new_node, scene_root);
	new_node->set_name(parent_node->validate_child_name(new_node));

	_mark_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(new_node->get_path());
	result["node_name"] = new_node->get_name();
	result["node_type"] = new_node->get_class();
	return result;
}

Dictionary YeetAIDock::_tool_instantiate_scene(const Dictionary &p_args) const {
	Dictionary result;
	const String packed_scene_path = p_args.get("packed_scene_path", "");
	const String parent_path = p_args.get("parent_path", ".");
	if (!packed_scene_path.begins_with("res://")) {
		result["error"] = "packed_scene_path must start with res://";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *parent_node = _resolve_node_target(scene_root, parent_path, error);
	if (parent_node == nullptr) {
		result["error"] = error;
		return result;
	}

	Error load_error = OK;
	Ref<PackedScene> packed_scene = ResourceLoader::load(packed_scene_path, "PackedScene", ResourceFormatLoader::CACHE_MODE_REPLACE, &load_error);
	if (packed_scene.is_null() || load_error != OK) {
		result["error"] = vformat("Failed to load PackedScene: %d", load_error);
		return result;
	}

	Node *instance = packed_scene->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	if (instance == nullptr) {
		result["error"] = "Failed to instantiate PackedScene.";
		return result;
	}

	const String node_name = p_args.get("node_name", "");
	if (!node_name.is_empty()) {
		instance->set_name(node_name);
	}

	_add_to_scene(parent_node, instance, scene_root);
	instance->set_name(parent_node->validate_child_name(instance));
	_mark_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["packed_scene_path"] = packed_scene_path;
	result["node_path"] = String(instance->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_add_primitive_mesh(const Dictionary &p_args) const {
	Dictionary result;
	const String node_name = p_args.get("node_name", "");
	const String mesh_type = String(p_args.get("mesh_type", "box")).to_lower();
	const String parent_path = p_args.get("parent_path", ".");
	if (node_name.is_empty()) {
		result["error"] = "node_name is required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *parent_node = _resolve_node_target(scene_root, parent_path, error);
	if (parent_node == nullptr) {
		result["error"] = error;
		return result;
	}

	Dictionary parameters = p_args.get("parameters", Dictionary());
	Ref<Mesh> mesh;
	if (mesh_type == "box") {
		Ref<BoxMesh> box;
		box.instantiate();
		if (parameters.has("size")) {
			box->set_size((Vector3)_variant_from_json(parameters["size"], Variant::VECTOR3));
		}
		if (parameters.has("subdivide_width")) {
			box->set_subdivide_width(int(parameters["subdivide_width"]));
		}
		if (parameters.has("subdivide_height")) {
			box->set_subdivide_height(int(parameters["subdivide_height"]));
		}
		if (parameters.has("subdivide_depth")) {
			box->set_subdivide_depth(int(parameters["subdivide_depth"]));
		}
		mesh = box;
	} else if (mesh_type == "sphere") {
		Ref<SphereMesh> sphere;
		sphere.instantiate();
		if (parameters.has("radius")) {
			sphere->set_radius((double)parameters["radius"]);
		}
		if (parameters.has("height")) {
			sphere->set_height((double)parameters["height"]);
		}
		if (parameters.has("radial_segments")) {
			sphere->set_radial_segments(int(parameters["radial_segments"]));
		}
		if (parameters.has("rings")) {
			sphere->set_rings(int(parameters["rings"]));
		}
		if (parameters.has("is_hemisphere")) {
			sphere->set_is_hemisphere((bool)parameters["is_hemisphere"]);
		}
		mesh = sphere;
	} else if (mesh_type == "capsule") {
		Ref<CapsuleMesh> capsule;
		capsule.instantiate();
		if (parameters.has("radius")) {
			capsule->set_radius((double)parameters["radius"]);
		}
		if (parameters.has("height")) {
			capsule->set_height((double)parameters["height"]);
		}
		if (parameters.has("radial_segments")) {
			capsule->set_radial_segments(int(parameters["radial_segments"]));
		}
		if (parameters.has("rings")) {
			capsule->set_rings(int(parameters["rings"]));
		}
		mesh = capsule;
	} else if (mesh_type == "cylinder") {
		Ref<CylinderMesh> cylinder;
		cylinder.instantiate();
		if (parameters.has("top_radius")) {
			cylinder->set_top_radius((double)parameters["top_radius"]);
		}
		if (parameters.has("bottom_radius")) {
			cylinder->set_bottom_radius((double)parameters["bottom_radius"]);
		}
		if (parameters.has("height")) {
			cylinder->set_height((double)parameters["height"]);
		}
		if (parameters.has("radial_segments")) {
			cylinder->set_radial_segments(int(parameters["radial_segments"]));
		}
		if (parameters.has("rings")) {
			cylinder->set_rings(int(parameters["rings"]));
		}
		if (parameters.has("cap_top")) {
			cylinder->set_cap_top((bool)parameters["cap_top"]);
		}
		if (parameters.has("cap_bottom")) {
			cylinder->set_cap_bottom((bool)parameters["cap_bottom"]);
		}
		mesh = cylinder;
	} else if (mesh_type == "plane") {
		Ref<PlaneMesh> plane;
		plane.instantiate();
		if (parameters.has("size")) {
			plane->set_size((Vector2)_variant_from_json(parameters["size"], Variant::VECTOR2));
		}
		if (parameters.has("subdivide_width")) {
			plane->set_subdivide_width(int(parameters["subdivide_width"]));
		}
		if (parameters.has("subdivide_depth")) {
			plane->set_subdivide_depth(int(parameters["subdivide_depth"]));
		}
		if (parameters.has("center_offset")) {
			plane->set_center_offset((Vector3)_variant_from_json(parameters["center_offset"], Variant::VECTOR3));
		}
		if (parameters.has("orientation")) {
			const String orientation = String(parameters["orientation"]).to_lower();
			if (orientation == "x") {
				plane->set_orientation(PlaneMesh::FACE_X);
			} else if (orientation == "z") {
				plane->set_orientation(PlaneMesh::FACE_Z);
			} else {
				plane->set_orientation(PlaneMesh::FACE_Y);
			}
		}
		mesh = plane;
	} else {
		result["error"] = "mesh_type must be one of: box, sphere, capsule, cylinder, plane.";
		return result;
	}

	MeshInstance3D *mesh_node = memnew(MeshInstance3D);
	mesh_node->set_name(node_name);
	mesh_node->set_mesh(mesh);
	_add_to_scene(parent_node, mesh_node, scene_root);
	mesh_node->set_name(parent_node->validate_child_name(mesh_node));
	_mark_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(mesh_node->get_path());
	result["mesh_type"] = mesh_type;
	return result;
}

Dictionary YeetAIDock::_tool_add_collision_shape(const Dictionary &p_args) const {
	Dictionary result;
	const String node_name = p_args.get("node_name", "");
	const String shape_type = String(p_args.get("shape_type", "box")).to_lower();
	const String parent_path = p_args.get("parent_path", ".");
	if (node_name.is_empty()) {
		result["error"] = "node_name is required.";
		return result;
	}

	String error;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), error);
	if (scene_root == nullptr) {
		result["error"] = error;
		return result;
	}

	Node *parent_node = _resolve_node_target(scene_root, parent_path, error);
	if (parent_node == nullptr) {
		result["error"] = error;
		return result;
	}

	Dictionary parameters = p_args.get("parameters", Dictionary());
	Ref<Shape3D> shape;
	if (shape_type == "box") {
		Ref<BoxShape3D> box;
		box.instantiate();
		if (parameters.has("size")) {
			box->set_size((Vector3)_variant_from_json(parameters["size"], Variant::VECTOR3));
		}
		shape = box;
	} else if (shape_type == "sphere") {
		Ref<SphereShape3D> sphere;
		sphere.instantiate();
		if (parameters.has("radius")) {
			sphere->set_radius((double)parameters["radius"]);
		}
		shape = sphere;
	} else if (shape_type == "capsule") {
		Ref<CapsuleShape3D> capsule;
		capsule.instantiate();
		if (parameters.has("radius")) {
			capsule->set_radius((double)parameters["radius"]);
		}
		if (parameters.has("height")) {
			capsule->set_height((double)parameters["height"]);
		}
		shape = capsule;
	} else if (shape_type == "cylinder") {
		Ref<CylinderShape3D> cylinder;
		cylinder.instantiate();
		if (parameters.has("radius")) {
			cylinder->set_radius((double)parameters["radius"]);
		}
		if (parameters.has("height")) {
			cylinder->set_height((double)parameters["height"]);
		}
		shape = cylinder;
	} else {
		result["error"] = "shape_type must be one of: box, sphere, capsule, cylinder.";
		return result;
	}

	CollisionShape3D *collision_node = memnew(CollisionShape3D);
	collision_node->set_name(node_name);
	collision_node->set_shape(shape);
	if (p_args.has("disabled")) {
		collision_node->set_disabled(bool(p_args["disabled"]));
	}
	_add_to_scene(parent_node, collision_node, scene_root);
	collision_node->set_name(parent_node->validate_child_name(collision_node));
	_mark_unsaved();

	result["scene_path"] = scene_root->get_scene_file_path();
	result["node_path"] = String(collision_node->get_path());
	result["shape_type"] = shape_type;
	return result;
}

Dictionary YeetAIDock::_tool_create_standard_material(const Dictionary &p_args) const {
	Dictionary result;
	const String material_path = p_args.get("material_path", "");
	if (!material_path.begins_with("res://") || material_path.get_extension().is_empty()) {
		result["error"] = "material_path must be a res:// path with a resource extension.";
		return result;
	}
	if (FileAccess::exists(material_path) && !bool(p_args.get("overwrite", false))) {
		result["error"] = "material_path already exists. Pass overwrite=true to replace it.";
		return result;
	}

	Ref<StandardMaterial3D> std_material;
	std_material.instantiate();
	if (p_args.has("albedo")) {
		std_material->set_albedo((Color)_variant_from_json(p_args["albedo"], Variant::COLOR));
	}
	if (p_args.has("metallic")) {
		std_material->set_metallic((double)p_args["metallic"]);
	}
	if (p_args.has("roughness")) {
		std_material->set_roughness((double)p_args["roughness"]);
	}
	if (p_args.has("transparency")) {
		bool transparency_ok = false;
		const BaseMaterial3D::Transparency transparency = YeetAIDock::parse_transparency_mode(String(p_args["transparency"]), transparency_ok);
		if (!transparency_ok) {
			result["error"] = "transparency must be one of: disabled, alpha, alpha_scissor, alpha_hash, alpha_depth_pre_pass.";
			return result;
		}
		std_material->set_transparency(transparency);
	}

	const Error dir_error = DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(material_path.get_base_dir()));
	if (dir_error != OK) {
		result["error"] = vformat("Failed to create material directory: %d", dir_error);
		return result;
	}

	const Error save_error = ResourceSaver::save(std_material, material_path, ResourceSaver::FLAG_CHANGE_PATH);
	if (save_error != OK) {
		result["error"] = vformat("Failed to save material: %d", save_error);
		return result;
	}

	EditorInterface *editor = EditorInterface::get_singleton();
	if (editor != nullptr && editor->get_resource_filesystem() != nullptr) {
		editor->get_resource_filesystem()->update_file(material_path);
	}

	result["material_path"] = material_path;
	_mark_unsaved();
	if (p_args.has("scene_path") && p_args.has("node_path")) {
		Dictionary assign_args;
		assign_args["scene_path"] = p_args.get("scene_path", "");
		assign_args["node_path"] = p_args.get("node_path", "");
		assign_args["property"] = p_args.get("property", "material_override");
		assign_args["resource_path"] = material_path;
		const Dictionary assign_result = _tool_assign_resource_to_property(assign_args);
		if (assign_result.has("error")) {
			return assign_result;
		}
		result["assigned_to"] = assign_result.get("node_path", "");
		result["property"] = assign_result.get("property", "");
	}
	return result;
}
