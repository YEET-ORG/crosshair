/**************************************************************************/
/*  yeet_ai_tools_scene3d.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/json.h"
#include "core/string/translation.h"
#include "editor/editor_interface.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/2d/physics/collision_object_2d.h"
#include "scene/2d/physics/collision_shape_2d.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/physics/area_3d.h"
#include "scene/3d/physics/character_body_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/3d/physics/ray_cast_3d.h"
#include "scene/3d/physics/shape_cast_3d.h"
#include "modules/csg/csg_shape.h"
#include "scene/3d/path_3d.h"
#include "scene/resources/2d/capsule_shape_2d.h"
#include "scene/resources/2d/circle_shape_2d.h"
#include "scene/resources/2d/convex_polygon_shape_2d.h"
#include "scene/resources/2d/rectangle_shape_2d.h"
#include "scene/resources/2d/world_boundary_shape_2d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/resources/3d/world_boundary_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/physics_material.h"

Dictionary YeetAIDock::_tool_create_light(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String type = _arg_string(p_args, "light_type", "directional").to_lower();
	const String node_name = _arg_string(p_args, "name", type + "_light");
	const Color color = _arg_color(p_args, "color", Color(1, 1, 1));
	const double energy = _arg_float(p_args, "energy", 1.0);
	const bool shadows = _arg_bool(p_args, "shadows", false);

	Light3D *light = nullptr;
	if (type == "omni") {
		light = memnew(OmniLight3D);
		light->set_param(Light3D::PARAM_RANGE, _arg_float(p_args, "range", 10.0));
		light->set_param(Light3D::PARAM_ATTENUATION, _arg_float(p_args, "attenuation", 1.0));
	} else if (type == "spot") {
		light = memnew(SpotLight3D);
		light->set_param(Light3D::PARAM_RANGE, _arg_float(p_args, "range", 10.0));
		light->set_param(Light3D::PARAM_SPOT_ANGLE, _arg_float(p_args, "spot_angle", 45.0));
		light->set_param(Light3D::PARAM_SPOT_ATTENUATION, _arg_float(p_args, "spot_attenuation", 1.0));
	} else {
		DirectionalLight3D *dir_light = memnew(DirectionalLight3D);
		dir_light->set_shadow_mode(DirectionalLight3D::SHADOW_PARALLEL_2_SPLITS);
		light = dir_light;
	}

	light->set_name(node_name);
	light->set_color(color);
	light->set_param(Light3D::PARAM_ENERGY, energy);
	light->set_shadow(shadows);

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, light, scene_root);

	Vector3 pos = _arg_vector3(p_args, "position", Vector3());
	light->set_position(pos);
	light->set_rotation_degrees(_arg_vector3(p_args, "rotation_degrees", Vector3()));

	_mark_unsaved();
	result["node_path"] = String(light->get_path());
	result["light_type"] = type;
	return result;
}

Dictionary YeetAIDock::_tool_create_camera_3d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Camera3D");

	Camera3D *cam = memnew(Camera3D);
	cam->set_name(node_name);
	cam->set_current(_arg_bool(p_args, "current", true));
	cam->set_fov(_arg_float(p_args, "fov", 75.0));
	cam->set_near(_arg_float(p_args, "near", 0.05));
	cam->set_far(_arg_float(p_args, "far", 4000.0));

	const String proj = _arg_string(p_args, "projection", "perspective").to_lower();
	if (proj == "orthogonal" || proj == "orthographic") {
		cam->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
		cam->set_size(_arg_float(p_args, "size", 10.0));
	} else if (proj == "frustum") {
		cam->set_projection(Camera3D::PROJECTION_FRUSTUM);
	}

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, cam, scene_root);

	cam->set_position(_arg_vector3(p_args, "position", Vector3(0, 1, 3)));
	cam->set_rotation_degrees(_arg_vector3(p_args, "rotation_degrees", Vector3()));

	_mark_unsaved();
	result["node_path"] = String(cam->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_add_2d_collision_shape(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}
	CollisionObject2D *co = Object::cast_to<CollisionObject2D>(node);
	if (co == nullptr) {
		return _make_error("Target node is not a CollisionObject2D");
	}

	const String shape_type = _arg_string(p_args, "shape_type", "rectangle").to_lower();
	const String shape_name = _arg_string(p_args, "name", shape_type + "_collision");

	CollisionShape2D *cs = memnew(CollisionShape2D);
	cs->set_name(shape_name);

	Ref<Shape2D> shape;
	if (shape_type == "circle") {
		Ref<CircleShape2D> s = memnew(CircleShape2D);
		s->set_radius(_arg_float(p_args, "radius", 10.0));
		shape = s;
	} else if (shape_type == "rectangle" || shape_type == "box") {
		Ref<RectangleShape2D> s = memnew(RectangleShape2D);
		const Vector3 sz = _arg_vector3(p_args, "size", Vector3(20, 20, 0));
		s->set_size(Vector2(sz.x, sz.y));
		shape = s;
	} else if (shape_type == "capsule") {
		Ref<CapsuleShape2D> s = memnew(CapsuleShape2D);
		s->set_radius(_arg_float(p_args, "radius", 10.0));
		s->set_height(_arg_float(p_args, "height", 30.0));
		shape = s;
	} else if (shape_type == "convex") {
		Ref<ConvexPolygonShape2D> s = memnew(ConvexPolygonShape2D);
		Array pts = _arg_array(p_args, "points");
		Vector<Vector2> points;
		for (int i = 0; i < pts.size(); i++) {
			Dictionary d = pts[i];
			points.push_back(Vector2(d.get("x", 0.0), d.get("y", 0.0)));
		}
		s->set_points(points);
		shape = s;
	} else if (shape_type == "world_boundary") {
		Ref<WorldBoundaryShape2D> s = memnew(WorldBoundaryShape2D);
		shape = s;
	} else {
		return _make_error("Unknown 2D shape type. Use: circle, rectangle, capsule, convex, world_boundary");
	}

	cs->set_shape(shape);
	_add_to_scene(co, cs, scene_root);

	_mark_unsaved();
	result["node_path"] = String(cs->get_path());
	result["shape_type"] = shape_type;
	return result;
}

Dictionary YeetAIDock::_tool_create_rigid_body_3d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "RigidBody3D");

	RigidBody3D *body = memnew(RigidBody3D);
	body->set_name(node_name);
	body->set_mass(_arg_float(p_args, "mass", 1.0));
	body->set_linear_damp(_arg_float(p_args, "linear_damp", 0.0));
	body->set_angular_damp(_arg_float(p_args, "angular_damp", 0.0));

	const String mode = _arg_string(p_args, "mode", "rigid").to_lower();
	if (mode == "static") {
		body->set_freeze_enabled(true);
		body->set_freeze_mode(RigidBody3D::FREEZE_MODE_STATIC);
	} else if (mode == "character" || mode == "kinematic") {
		// Godot 4: use frozen kinematic rigid body; prefer CharacterBody3D for character controllers.
		body->set_freeze_enabled(true);
		body->set_freeze_mode(RigidBody3D::FREEZE_MODE_KINEMATIC);
	}

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, body, scene_root);
	body->set_position(_arg_vector3(p_args, "position", Vector3()));

	_mark_unsaved();
	result["node_path"] = String(body->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_static_body_3d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "StaticBody3D");

	StaticBody3D *body = memnew(StaticBody3D);
	body->set_name(node_name);
	Ref<PhysicsMaterial> pm;
	pm.instantiate();
	pm->set_friction(_arg_float(p_args, "friction", 1.0));
	pm->set_bounce(_arg_float(p_args, "bounce", 0.0));
	body->set_physics_material_override(pm);

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, body, scene_root);
	body->set_position(_arg_vector3(p_args, "position", Vector3()));

	_mark_unsaved();
	result["node_path"] = String(body->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_character_body_3d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "CharacterBody3D");

	CharacterBody3D *body = memnew(CharacterBody3D);
	body->set_name(node_name);

	const String motion = _arg_string(p_args, "motion_mode", "grounded").to_lower();
	if (motion == "floating") {
		body->set_motion_mode(CharacterBody3D::MOTION_MODE_FLOATING);
	}
	body->set_floor_snap_length(_arg_float(p_args, "floor_snap_length", 0.1));
	body->set("up_direction", _arg_vector3(p_args, "up_direction", Vector3(0, 1, 0)));

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, body, scene_root);
	body->set_position(_arg_vector3(p_args, "position", Vector3()));

	_mark_unsaved();
	result["node_path"] = String(body->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_area_3d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Area3D");

	Area3D *area = memnew(Area3D);
	area->set_name(node_name);
	area->set_monitorable(_arg_bool(p_args, "monitorable", true));
	area->set_monitoring(_arg_bool(p_args, "monitoring", true));

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, area, scene_root);
	area->set_position(_arg_vector3(p_args, "position", Vector3()));

	_mark_unsaved();
	result["node_path"] = String(area->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_ray_cast_3d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "RayCast3D");

	RayCast3D *rc = memnew(RayCast3D);
	rc->set_name(node_name);
	rc->set_target_position(_arg_vector3(p_args, "target_position", Vector3(0, -1, 0)));
	rc->set_enabled(_arg_bool(p_args, "enabled", true));
	rc->set_collision_mask(_arg_int(p_args, "collision_mask", 1));

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, rc, scene_root);
	rc->set_position(_arg_vector3(p_args, "position", Vector3()));

	_mark_unsaved();
	result["node_path"] = String(rc->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_shape_cast_3d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "ShapeCast3D");

	ShapeCast3D *sc = memnew(ShapeCast3D);
	sc->set_name(node_name);
	sc->set_target_position(_arg_vector3(p_args, "target_position", Vector3(0, -5, 0)));
	sc->set_enabled(_arg_bool(p_args, "enabled", true));

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, sc, scene_root);
	sc->set_position(_arg_vector3(p_args, "position", Vector3()));

	_mark_unsaved();
	result["node_path"] = String(sc->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_add_csg_primitive(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String type = _arg_string(p_args, "csg_type", "box").to_lower();
	const String node_name = _arg_string(p_args, "name", "CSG" + type.capitalize());

	CSGShape3D *csg = nullptr;
	if (type == "box") {
		CSGBox3D *box = memnew(CSGBox3D);
		box->set_size(_arg_vector3(p_args, "size", Vector3(1, 1, 1)));
		csg = box;
	} else if (type == "sphere") {
		CSGSphere3D *sphere = memnew(CSGSphere3D);
		sphere->set_radius(_arg_float(p_args, "radius", 0.5));
		sphere->set_radial_segments(_arg_int(p_args, "radial_segments", 16));
		csg = sphere;
	} else if (type == "cylinder") {
		CSGCylinder3D *cyl = memnew(CSGCylinder3D);
		cyl->set_radius(_arg_float(p_args, "radius", 0.5));
		cyl->set_height(_arg_float(p_args, "height", 1.0));
		csg = cyl;
	} else if (type == "torus") {
		CSGTorus3D *torus = memnew(CSGTorus3D);
		torus->set_inner_radius(_arg_float(p_args, "inner_radius", 0.3));
		torus->set_outer_radius(_arg_float(p_args, "outer_radius", 0.5));
		csg = torus;
	} else {
		return _make_error("Unknown CSG type. Use: box, sphere, cylinder, torus");
	}

	csg->set_name(node_name);
	csg->set_operation(CSGShape3D::OPERATION_UNION);
	csg->set_snap(_arg_float(p_args, "snap", 0.0) > 0.0 ? _arg_float(p_args, "snap", 0.01) : 0.0);

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, csg, scene_root);
	csg->set_position(_arg_vector3(p_args, "position", Vector3()));
	csg->set_rotation_degrees(_arg_vector3(p_args, "rotation_degrees", Vector3()));

	_mark_unsaved();
	result["node_path"] = String(csg->get_path());
	result["csg_type"] = type;
	return result;
}

Dictionary YeetAIDock::_tool_create_path_3d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Path3D");

	Path3D *path = memnew(Path3D);
	path->set_name(node_name);

	Ref<Curve3D> curve = memnew(Curve3D);
	Array points = _arg_array(p_args, "points");
	if (points.is_empty()) {
		curve->add_point(Vector3(0, 0, 0));
		curve->add_point(Vector3(0, 0, -5));
		curve->add_point(Vector3(5, 0, -5));
	} else {
		for (int i = 0; i < points.size(); i++) {
			Dictionary pd = points[i];
			curve->add_point(_arg_vector3(pd, "position", Vector3()),
					_arg_vector3(pd, "in", Vector3()),
					_arg_vector3(pd, "out", Vector3()));
		}
	}
	path->set_curve(curve);

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, path, scene_root);

	if (_arg_bool(p_args, "add_follow", true)) {
		PathFollow3D *follow = memnew(PathFollow3D);
		follow->set_name("PathFollow3D");
		_add_to_scene(path, follow, scene_root);
		result["follow_path"] = String(follow->get_path());
	}

	_mark_unsaved();
	result["node_path"] = String(path->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_vehicle_body_3d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	// This is a minimal vehicle stub — full VehicleBody3D with wheels is complex.
	// Return a helpful error guiding the AI to compose it manually.
	return _make_error("VehicleBody3D setup is complex. Use add_node with type=VehicleBody3D, then add_node with type=VehicleWheel3D as children, then set_node_property for each wheel's steering/engine/brake properties.");
}

// ═══════════════════════════════════════════════════════════════════════════
// Static helpers — single definitions shared by all tool files
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_make_error(const String &p_message) {
	Dictionary result;
	result["error"] = p_message;
	return result;
}

Dictionary YeetAIDock::_make_ok() {
	Dictionary result;
	result["ok"] = true;
	return result;
}

Dictionary YeetAIDock::_make_ok(const Dictionary &p_extra) {
	Dictionary result;
	result["ok"] = true;
	const Array keys = p_extra.keys();
	for (int i = 0; i < keys.size(); i++) {
		const Variant key = keys[i];
		result[key] = p_extra[key];
	}
	return result;
}

String YeetAIDock::_arg_string(const Dictionary &p_args, const String &p_key, const String &p_default) {
	if (p_args.has(p_key)) {
		return String(p_args[p_key]).strip_edges();
	}
	return p_default;
}

int YeetAIDock::_arg_int(const Dictionary &p_args, const String &p_key, int p_default) {
	if (p_args.has(p_key)) {
		return int(p_args[p_key]);
	}
	return p_default;
}

double YeetAIDock::_arg_float(const Dictionary &p_args, const String &p_key, double p_default) {
	if (p_args.has(p_key)) {
		return double(p_args[p_key]);
	}
	return p_default;
}

bool YeetAIDock::_arg_bool(const Dictionary &p_args, const String &p_key, bool p_default) {
	if (p_args.has(p_key)) {
		return bool(p_args[p_key]);
	}
	return p_default;
}

Array YeetAIDock::_arg_array(const Dictionary &p_args, const String &p_key) {
	if (p_args.has(p_key)) {
		return p_args[p_key];
	}
	return Array();
}

Dictionary YeetAIDock::_arg_dict(const Dictionary &p_args, const String &p_key) {
	if (p_args.has(p_key)) {
		return p_args[p_key];
	}
	return Dictionary();
}

Vector3 YeetAIDock::_arg_vector3(const Dictionary &p_args, const String &p_key, const Vector3 &p_default) {
	if (p_args.has(p_key)) {
		return (Vector3)p_args[p_key];
	}
	return p_default;
}

Color YeetAIDock::_arg_color(const Dictionary &p_args, const String &p_key, const Color &p_default) {
	if (!p_args.has(p_key)) {
		return p_default;
	}

	const Variant &v = p_args[p_key];
	if (v.get_type() == Variant::COLOR) {
		return v;
	}
	if (v.get_type() == Variant::DICTIONARY) {
		Dictionary d = v;
		return Color(d.get("r", 0.0), d.get("g", 0.0), d.get("b", 0.0), d.get("a", 1.0));
	}
	return p_default;
}

bool YeetAIDock::_resolve_scene(const Dictionary &p_args, Node **r_root, String &r_error) const {
	*r_root = _resolve_scene_root(p_args.get("scene_path", ""), r_error);
	return *r_root != nullptr;
}

bool YeetAIDock::_resolve_scene_and_node(const Dictionary &p_args, const String &p_node_key, Node **r_scene_root, Node **r_node, String &r_error) const {
	if (!_resolve_scene(p_args, r_scene_root, r_error)) {
		return false;
	}
	const String node_path = p_args.get(p_node_key, "");
	if (node_path.is_empty()) {
		*r_node = *r_scene_root;
		return true;
	}
	*r_node = _resolve_node_target(*r_scene_root, node_path, r_error);
	return *r_node != nullptr;
}

bool YeetAIDock::_require_editor(EditorInterface **r_editor, String &r_error) const {
	*r_editor = EditorInterface::get_singleton();
	if (*r_editor == nullptr) {
		r_error = "EditorInterface is not available.";
		return false;
	}
	return true;
}
