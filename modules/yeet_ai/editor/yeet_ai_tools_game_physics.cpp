/**************************************************************************/
/*  yeet_ai_tools_game_physics.cpp                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "scene/2d/physics/area_2d.h"
#include "scene/2d/physics/character_body_2d.h"
#include "scene/2d/physics/collision_object_2d.h"
#include "scene/2d/physics/collision_polygon_2d.h"
#include "scene/2d/physics/collision_shape_2d.h"
#include "scene/2d/physics/rigid_body_2d.h"
#include "scene/2d/physics/static_body_2d.h"
#include "scene/2d/polygon_2d.h"
#include "scene/2d/sprite_2d.h"
#include "scene/2d/tile_map.h"
#include "scene/2d/tile_map_layer.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/area_3d.h"
#include "scene/3d/physics/character_body_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/resources/2d/capsule_shape_2d.h"
#include "scene/resources/2d/circle_shape_2d.h"
#include "scene/resources/2d/convex_polygon_shape_2d.h"
#include "scene/resources/2d/rectangle_shape_2d.h"
#include "scene/resources/2d/segment_shape_2d.h"
#include "scene/resources/2d/separation_ray_shape_2d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/2d/tile_set.h"
#include "scene/resources/texture.h"

static bool _issue_filter_allows(const Array &p_filter, const String &p_id) {
	if (p_filter.is_empty()) {
		return true;
	}
	for (int i = 0; i < p_filter.size(); i++) {
		if (String(p_filter[i]) == p_id) {
			return true;
		}
	}
	return false;
}

static Dictionary _make_issue(const String &p_id, const String &p_severity, const String &p_code, const Node *p_node, const String &p_message) {
	Dictionary issue;
	issue["id"] = p_id;
	issue["severity"] = p_severity;
	issue["code"] = p_code;
	issue["node_path"] = p_node != nullptr ? String(p_node->get_path()) : String();
	issue["message"] = p_message;
	return issue;
}

static Dictionary _suggest_tool_call(const String &p_tool, const Dictionary &p_args) {
	Dictionary call;
	call["tool"] = p_tool;
	call["arguments"] = p_args;
	return call;
}

static Color _actor_color_2d(const String &p_role) {
	if (p_role == "player") {
		return Color(0.25, 0.55, 1.0, 1.0);
	}
	if (p_role == "enemy" || p_role == "hazard") {
		return Color(1.0, 0.22, 0.18, 1.0);
	}
	if (p_role == "collectible") {
		return Color(1.0, 0.82, 0.16, 1.0);
	}
	if (p_role == "platform" || p_role == "wall") {
		return Color(0.36, 0.42, 0.48, 1.0);
	}
	if (p_role == "projectile") {
		return Color(0.98, 0.98, 0.98, 1.0);
	}
	return Color(0.7, 0.75, 0.8, 1.0);
}

static Vector<Vector2> _rect_polygon(const Vector2 &p_size) {
	const Vector2 half = p_size * 0.5;
	Vector<Vector2> points;
	points.push_back(Vector2(-half.x, -half.y));
	points.push_back(Vector2(half.x, -half.y));
	points.push_back(Vector2(half.x, half.y));
	points.push_back(Vector2(-half.x, half.y));
	return points;
}

static Vector<Vector2> _circle_polygon(double p_radius) {
	Vector<Vector2> points;
	const int steps = 16;
	for (int i = 0; i < steps; i++) {
		const double a = Math::TAU * double(i) / double(steps);
		points.push_back(Vector2(Math::cos(a) * p_radius, Math::sin(a) * p_radius));
	}
	return points;
}

static Vector<Vector2> _capsule_polygon(const Vector2 &p_size) {
	const double radius = MAX(0.001, MIN(Math::abs(p_size.x), Math::abs(p_size.y)) * 0.5);
	const double half_straight = MAX(0.0, Math::abs(p_size.y) * 0.5 - radius);
	Vector<Vector2> points;
	const int arc_steps = 8;
	for (int i = 0; i <= arc_steps; i++) {
		const double a = Math::PI - (Math::PI * double(i) / double(arc_steps));
		points.push_back(Vector2(Math::cos(a) * radius, -half_straight - Math::sin(a) * radius));
	}
	for (int i = 0; i <= arc_steps; i++) {
		const double a = Math::PI * double(i) / double(arc_steps);
		points.push_back(Vector2(Math::cos(a) * radius, half_straight + Math::sin(a) * radius));
	}
	return points;
}

static String _default_body_type_2d(const String &p_role) {
	if (p_role == "platform" || p_role == "wall") {
		return "StaticBody2D";
	}
	if (p_role == "collectible" || p_role == "hazard" || p_role == "projectile" || p_role == "trigger") {
		return "Area2D";
	}
	return "CharacterBody2D";
}

static String _default_shape_type_2d(const String &p_role) {
	if (p_role == "player" || p_role == "enemy") {
		return "capsule";
	}
	if (p_role == "collectible" || p_role == "projectile") {
		return "circle";
	}
	return "rectangle";
}

static Vector2 _default_size_2d(const String &p_role) {
	if (p_role == "player") {
		return Vector2(24, 40);
	}
	if (p_role == "enemy") {
		return Vector2(24, 32);
	}
	if (p_role == "platform") {
		return Vector2(128, 24);
	}
	if (p_role == "wall") {
		return Vector2(32, 128);
	}
	if (p_role == "collectible") {
		return Vector2(20, 20);
	}
	if (p_role == "projectile") {
		return Vector2(12, 12);
	}
	return Vector2(32, 32);
}

static String _default_body_type_3d(const String &p_role) {
	if (p_role == "platform" || p_role == "wall") {
		return "StaticBody3D";
	}
	if (p_role == "collectible" || p_role == "hazard" || p_role == "projectile" || p_role == "trigger") {
		return "Area3D";
	}
	return "CharacterBody3D";
}

static String _default_shape_type_3d(const String &p_role) {
	if (p_role == "player" || p_role == "enemy") {
		return "capsule";
	}
	if (p_role == "collectible" || p_role == "projectile") {
		return "sphere";
	}
	return "box";
}

static Vector3 _default_size_3d(const String &p_role) {
	if (p_role == "player") {
		return Vector3(0.7, 1.8, 0.7);
	}
	if (p_role == "enemy") {
		return Vector3(0.7, 1.4, 0.7);
	}
	if (p_role == "platform") {
		return Vector3(4.0, 0.25, 4.0);
	}
	if (p_role == "wall") {
		return Vector3(4.0, 2.0, 0.25);
	}
	if (p_role == "collectible") {
		return Vector3(0.5, 0.5, 0.5);
	}
	if (p_role == "projectile") {
		return Vector3(0.25, 0.25, 0.25);
	}
	return Vector3(1.0, 1.0, 1.0);
}

static Vector2 _shape2d_size(const Ref<Shape2D> &p_shape) {
	if (p_shape.is_null()) {
		return Vector2();
	}
	Ref<RectangleShape2D> rect = p_shape;
	if (rect.is_valid()) {
		return rect->get_size();
	}
	Ref<CircleShape2D> circle = p_shape;
	if (circle.is_valid()) {
		const double d = circle->get_radius() * 2.0;
		return Vector2(d, d);
	}
	Ref<CapsuleShape2D> capsule = p_shape;
	if (capsule.is_valid()) {
		return Vector2(capsule->get_radius() * 2.0, capsule->get_height());
	}
	Ref<SegmentShape2D> segment = p_shape;
	if (segment.is_valid()) {
		const Vector2 delta = segment->get_b() - segment->get_a();
		return Vector2(Math::abs(delta.x), Math::abs(delta.y));
	}
	Ref<SeparationRayShape2D> ray = p_shape;
	if (ray.is_valid()) {
		return Vector2(1.0, ray->get_length());
	}
	Ref<ConvexPolygonShape2D> convex = p_shape;
	if (convex.is_valid()) {
		const Vector<Vector2> points = convex->get_points();
		if (points.is_empty()) {
			return Vector2();
		}
		Vector2 min_p = points[0];
		Vector2 max_p = points[0];
		for (int i = 1; i < points.size(); i++) {
			min_p.x = MIN(min_p.x, points[i].x);
			min_p.y = MIN(min_p.y, points[i].y);
			max_p.x = MAX(max_p.x, points[i].x);
			max_p.y = MAX(max_p.y, points[i].y);
		}
		return max_p - min_p;
	}
	return Vector2();
}

static Vector3 _shape3d_size(const Ref<Shape3D> &p_shape) {
	if (p_shape.is_null()) {
		return Vector3();
	}
	Ref<BoxShape3D> box = p_shape;
	if (box.is_valid()) {
		return box->get_size();
	}
	Ref<SphereShape3D> sphere = p_shape;
	if (sphere.is_valid()) {
		const double d = sphere->get_radius() * 2.0;
		return Vector3(d, d, d);
	}
	Ref<CapsuleShape3D> capsule = p_shape;
	if (capsule.is_valid()) {
		const double d = capsule->get_radius() * 2.0;
		return Vector3(d, capsule->get_height(), d);
	}
	Ref<CylinderShape3D> cylinder = p_shape;
	if (cylinder.is_valid()) {
		const double d = cylinder->get_radius() * 2.0;
		return Vector3(d, cylinder->get_height(), d);
	}
	return Vector3();
}

Dictionary YeetAIDock::_tool_create_game_actor_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String role = _arg_string(p_args, "role", "generic").to_lower();
	const String node_name = _arg_string(p_args, "name", role == "generic" ? "GameActor2D" : role.capitalize());
	const String body_type_raw = _arg_string(p_args, "body_type", _default_body_type_2d(role));
	const String body_type = body_type_raw.to_lower().replace("_", "");
	const Vector2 size = _arg_vector2(p_args, "size", _default_size_2d(role));
	const Vector2 position = _arg_vector2(p_args, "position", Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	const String shape_type = _arg_string(p_args, "shape_type", _default_shape_type_2d(role)).to_lower();

	Node2D *body = nullptr;
	CollisionObject2D *collision = nullptr;
	if (body_type == "characterbody2d") {
		CharacterBody2D *n = memnew(CharacterBody2D);
		n->set_floor_snap_length(_arg_float(p_args, "floor_snap_length", 1.0));
		body = n;
		collision = n;
	} else if (body_type == "staticbody2d") {
		StaticBody2D *n = memnew(StaticBody2D);
		body = n;
		collision = n;
	} else if (body_type == "rigidbody2d") {
		RigidBody2D *n = memnew(RigidBody2D);
		n->set_mass(_arg_float(p_args, "mass", 1.0));
		body = n;
		collision = n;
	} else if (body_type == "area2d") {
		Area2D *n = memnew(Area2D);
		n->set_monitoring(_arg_bool(p_args, "monitoring", true));
		n->set_monitorable(_arg_bool(p_args, "monitorable", true));
		body = n;
		collision = n;
	} else {
		return _make_error("body_type must be CharacterBody2D, StaticBody2D, RigidBody2D, or Area2D.");
	}

	body->set_name(node_name);
	collision->set_collision_layer(_arg_int(p_args, "collision_layer", 1));
	collision->set_collision_mask(_arg_int(p_args, "collision_mask", 1));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, body, scene_root);
	body->set_position(position);

	Dictionary shape_args;
	shape_args["parent_path"] = String(body->get_path());
	shape_args["shape_type"] = shape_type;
	shape_args["size"] = size;
	shape_args["radius"] = MIN(size.x, size.y) * 0.5;
	shape_args["height"] = size.y;
	Dictionary shape_result = _tool_add_collision_shape_2d(shape_args);
	if (shape_result.has("error")) {
		return shape_result;
	}

	String visual_path;
	if (_arg_bool(p_args, "add_visual", true)) {
		Polygon2D *visual = memnew(Polygon2D);
		visual->set_name("Visual");
		if (shape_type == "circle") {
			visual->set_polygon(_circle_polygon(MIN(size.x, size.y) * 0.5));
		} else if (shape_type == "capsule") {
			visual->set_polygon(_capsule_polygon(size));
		} else {
			visual->set_polygon(_rect_polygon(size));
		}
		visual->set_color(_arg_color(p_args, "color", _actor_color_2d(role)));
		_add_to_scene(body, visual, scene_root);
		visual_path = String(visual->get_path());
	}

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(body->get_path());
	result["collision_shape_path"] = shape_result.get("node_path", "");
	result["visual_path"] = visual_path;
	result["role"] = role;
	result["body_type"] = body_type_raw;
	result["shape_type"] = shape_type;
	result["size"] = size;
	return result;
}

Dictionary YeetAIDock::_tool_create_game_actor_3d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String role = _arg_string(p_args, "role", "generic").to_lower();
	const String node_name = _arg_string(p_args, "name", role == "generic" ? "GameActor3D" : role.capitalize());
	const String body_type_raw = _arg_string(p_args, "body_type", _default_body_type_3d(role));
	const String body_type = body_type_raw.to_lower().replace("_", "");
	const Vector3 size = _arg_vector3(p_args, "size", _default_size_3d(role));
	const Vector3 position = _arg_vector3(p_args, "position", Vector3());
	const Vector3 shape_position = _arg_vector3(p_args, "shape_position", Vector3());
	const String shape_type = _arg_string(p_args, "shape_type", _default_shape_type_3d(role)).to_lower();

	Node3D *body = nullptr;
	CollisionObject3D *collision = nullptr;
	if (body_type == "characterbody3d") {
		CharacterBody3D *n = memnew(CharacterBody3D);
		body = n;
		collision = n;
	} else if (body_type == "staticbody3d") {
		StaticBody3D *n = memnew(StaticBody3D);
		body = n;
		collision = n;
	} else if (body_type == "rigidbody3d") {
		RigidBody3D *n = memnew(RigidBody3D);
		n->set_mass(_arg_float(p_args, "mass", 1.0));
		body = n;
		collision = n;
	} else if (body_type == "area3d") {
		Area3D *n = memnew(Area3D);
		n->set_monitoring(_arg_bool(p_args, "monitoring", true));
		n->set_monitorable(_arg_bool(p_args, "monitorable", true));
		body = n;
		collision = n;
	} else {
		return _make_error("body_type must be CharacterBody3D, StaticBody3D, RigidBody3D, or Area3D.");
	}

	body->set_name(node_name);
	collision->set_collision_layer(_arg_int(p_args, "collision_layer", 1));
	collision->set_collision_mask(_arg_int(p_args, "collision_mask", 1));

	Node3D *parent = Object::cast_to<Node3D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node3D>(scene_root);
	}
	_add_to_scene(parent, body, scene_root);
	body->set_position(position);

	Ref<Shape3D> shape;
	if (shape_type == "sphere") {
		Ref<SphereShape3D> sphere;
		sphere.instantiate();
		sphere->set_radius(_arg_float(p_args, "radius", MIN(size.x, MIN(size.y, size.z)) * 0.5));
		shape = sphere;
	} else if (shape_type == "capsule") {
		Ref<CapsuleShape3D> capsule;
		capsule.instantiate();
		capsule->set_radius(_arg_float(p_args, "radius", MAX(size.x, size.z) * 0.5));
		capsule->set_height(_arg_float(p_args, "height", size.y));
		shape = capsule;
	} else if (shape_type == "cylinder") {
		Ref<CylinderShape3D> cylinder;
		cylinder.instantiate();
		cylinder->set_radius(_arg_float(p_args, "radius", MAX(size.x, size.z) * 0.5));
		cylinder->set_height(_arg_float(p_args, "height", size.y));
		shape = cylinder;
	} else if (shape_type == "box") {
		Ref<BoxShape3D> box;
		box.instantiate();
		box->set_size(size);
		shape = box;
	} else {
		return _make_error("shape_type must be box, sphere, capsule, or cylinder.");
	}

	CollisionShape3D *collision_shape = memnew(CollisionShape3D);
	collision_shape->set_name("CollisionShape3D");
	collision_shape->set_shape(shape);
	collision_shape->set_position(shape_position);
	_add_to_scene(body, collision_shape, scene_root);

	String visual_path;
	if (_arg_bool(p_args, "add_visual", true)) {
		Ref<Mesh> mesh;
		if (shape_type == "sphere") {
			Ref<SphereMesh> sphere_mesh;
			sphere_mesh.instantiate();
			const double sphere_radius = _arg_float(p_args, "radius", MIN(size.x, MIN(size.y, size.z)) * 0.5);
			sphere_mesh->set_radius(sphere_radius);
			sphere_mesh->set_height(sphere_radius * 2.0);
			mesh = sphere_mesh;
		} else if (shape_type == "capsule") {
			Ref<CapsuleMesh> capsule_mesh;
			capsule_mesh.instantiate();
			capsule_mesh->set_radius(_arg_float(p_args, "radius", MAX(size.x, size.z) * 0.5));
			capsule_mesh->set_height(_arg_float(p_args, "height", size.y));
			mesh = capsule_mesh;
		} else if (shape_type == "cylinder") {
			Ref<CylinderMesh> cylinder_mesh;
			cylinder_mesh.instantiate();
			const double cylinder_radius = _arg_float(p_args, "radius", MAX(size.x, size.z) * 0.5);
			cylinder_mesh->set_top_radius(cylinder_radius);
			cylinder_mesh->set_bottom_radius(cylinder_radius);
			cylinder_mesh->set_height(_arg_float(p_args, "height", size.y));
			mesh = cylinder_mesh;
		} else {
			Ref<BoxMesh> box_mesh;
			box_mesh.instantiate();
			box_mesh->set_size(size);
			mesh = box_mesh;
		}
		MeshInstance3D *visual = memnew(MeshInstance3D);
		visual->set_name("Visual");
		visual->set_mesh(mesh);
		_add_to_scene(body, visual, scene_root);
		visual_path = String(visual->get_path());
	}

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(body->get_path());
	result["collision_shape_path"] = String(collision_shape->get_path());
	result["visual_path"] = visual_path;
	result["role"] = role;
	result["body_type"] = body_type_raw;
	result["shape_type"] = shape_type;
	result["size"] = size;
	return result;
}

Dictionary YeetAIDock::_tool_audit_game_physics(const Dictionary &p_args) const {
	String err;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), err);
	if (scene_root == nullptr) {
		return _make_error(err);
	}

	const String dimensions = _arg_string(p_args, "dimensions", "both").to_lower();
	const bool include_2d = dimensions == "both" || dimensions == "2d";
	const bool include_3d = dimensions == "both" || dimensions == "3d";
	const bool include_suggestions = _arg_bool(p_args, "include_suggestions", true);
	const double min_2d_size = _arg_float(p_args, "min_2d_size_px", 8.0);
	const double min_3d_size = _arg_float(p_args, "min_3d_size", 0.05);

	Array issues;
	Array suggested_calls;
	int bodies_checked = 0;
	int shapes_checked = 0;

	Vector<Node *> stack;
	stack.push_back(scene_root);
	while (!stack.is_empty()) {
		Node *node = stack[stack.size() - 1];
		stack.remove_at(stack.size() - 1);
		for (int i = 0; i < node->get_child_count(); i++) {
			stack.push_back(node->get_child(i));
		}

		if (include_2d) {
			CollisionObject2D *co2 = Object::cast_to<CollisionObject2D>(node);
			if (co2 != nullptr) {
				bodies_checked++;
				int direct_shapes = 0;
				for (int i = 0; i < node->get_child_count(); i++) {
					Node *child = node->get_child(i);
					CollisionShape2D *cs = Object::cast_to<CollisionShape2D>(child);
					if (cs != nullptr) {
						direct_shapes++;
						shapes_checked++;
						const String child_path = String(cs->get_path());
						if (cs->is_disabled()) {
							Dictionary issue = _make_issue("disabled_collider_2d:" + child_path, "error", "disabled_collider_2d", cs, "CollisionShape2D is disabled.");
							issue["collision_node_path"] = child_path;
							issues.push_back(issue);
						}
						Ref<Shape2D> shape = cs->get_shape();
						if (shape.is_null()) {
							Dictionary issue = _make_issue("missing_shape_resource_2d:" + child_path, "error", "missing_shape_resource_2d", cs, "CollisionShape2D has no Shape2D resource.");
							issue["collision_node_path"] = child_path;
							issues.push_back(issue);
							continue;
						}
						const Vector2 shape_size = _shape2d_size(shape);
						if (shape_size != Vector2() && MIN(Math::abs(shape_size.x), Math::abs(shape_size.y)) < min_2d_size) {
							Dictionary issue = _make_issue("tiny_collider_2d:" + child_path, "warning", "tiny_collider_2d", cs, vformat("CollisionShape2D is very small: %.2f x %.2f.", shape_size.x, shape_size.y));
							issue["collision_node_path"] = child_path;
							issue["shape_size"] = shape_size;
							issues.push_back(issue);
						}
					}

					CollisionPolygon2D *cp = Object::cast_to<CollisionPolygon2D>(child);
					if (cp != nullptr) {
						direct_shapes++;
						shapes_checked++;
						if (cp->is_disabled()) {
							Dictionary issue = _make_issue("disabled_collision_polygon_2d:" + String(cp->get_path()), "error", "disabled_collision_polygon_2d", cp, "CollisionPolygon2D is disabled.");
							issue["collision_node_path"] = String(cp->get_path());
							issues.push_back(issue);
						}
						if (cp->get_polygon().size() < 3) {
							Dictionary issue = _make_issue("invalid_collision_polygon_2d:" + String(cp->get_path()), "error", "invalid_collision_polygon_2d", cp, "CollisionPolygon2D needs at least 3 points.");
							issue["collision_node_path"] = String(cp->get_path());
							issues.push_back(issue);
						}
					}
				}

				if (direct_shapes == 0) {
					Dictionary issue = _make_issue("missing_collider_2d:" + String(node->get_path()), "error", "missing_collider_2d", node, "CollisionObject2D has no direct CollisionShape2D or CollisionPolygon2D child.");
					if (include_suggestions) {
						Dictionary args;
						args["parent_path"] = String(node->get_path());
						args["shape_type"] = "rectangle";
						args["size"] = Vector2(32, 32);
						Dictionary call = _suggest_tool_call("add_collision_shape_2d", args);
						issue["suggested_tool_call"] = call;
						suggested_calls.push_back(call);
					}
					issues.push_back(issue);
				}
				if (co2->get_collision_layer() == 0) {
					issues.push_back(_make_issue("zero_collision_layer_2d:" + String(node->get_path()), "warning", "zero_collision_layer_2d", node, "CollisionObject2D has collision_layer set to 0."));
				}
				if (co2->get_collision_mask() == 0 && Object::cast_to<StaticBody2D>(node) == nullptr) {
					issues.push_back(_make_issue("zero_collision_mask_2d:" + String(node->get_path()), "warning", "zero_collision_mask_2d", node, "Interactive CollisionObject2D has collision_mask set to 0."));
				}
			}

			TileMap *tile_map = Object::cast_to<TileMap>(node);
			if (tile_map != nullptr && tile_map->get_tileset().is_valid() && tile_map->get_tileset()->get_physics_layers_count() == 0) {
				issues.push_back(_make_issue("tilemap_no_physics_layers:" + String(node->get_path()), "warning", "tilemap_no_physics_layers", node, "TileMap TileSet has no physics layers, so painted tiles will not collide."));
			}
			TileMapLayer *tile_layer = Object::cast_to<TileMapLayer>(node);
			if (tile_layer != nullptr && tile_layer->get_tile_set().is_valid() && tile_layer->get_tile_set()->get_physics_layers_count() == 0) {
				issues.push_back(_make_issue("tilemap_layer_no_physics_layers:" + String(node->get_path()), "warning", "tilemap_layer_no_physics_layers", node, "TileMapLayer TileSet has no physics layers, so painted tiles will not collide."));
			}
		}

		if (include_3d) {
			CollisionObject3D *co3 = Object::cast_to<CollisionObject3D>(node);
			if (co3 != nullptr) {
				bodies_checked++;
				int direct_shapes = 0;
				for (int i = 0; i < node->get_child_count(); i++) {
					Node *child = node->get_child(i);
					CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(child);
					if (cs == nullptr) {
						continue;
					}
					direct_shapes++;
					shapes_checked++;
					const String child_path = String(cs->get_path());
					if (cs->is_disabled()) {
						Dictionary issue = _make_issue("disabled_collider_3d:" + child_path, "error", "disabled_collider_3d", cs, "CollisionShape3D is disabled.");
						issue["collision_node_path"] = child_path;
						issues.push_back(issue);
					}
					Ref<Shape3D> shape = cs->get_shape();
					if (shape.is_null()) {
						Dictionary issue = _make_issue("missing_shape_resource_3d:" + child_path, "error", "missing_shape_resource_3d", cs, "CollisionShape3D has no Shape3D resource.");
						issue["collision_node_path"] = child_path;
						issues.push_back(issue);
						continue;
					}
					Ref<ConcavePolygonShape3D> concave = shape;
					if (concave.is_valid() && (Object::cast_to<RigidBody3D>(node) != nullptr || Object::cast_to<CharacterBody3D>(node) != nullptr)) {
						issues.push_back(_make_issue("dynamic_concave_collider_3d:" + child_path, "warning", "dynamic_concave_collider_3d", cs, "Dynamic 3D bodies should use primitive or convex collision, not concave collision."));
					}
					const Vector3 shape_size = _shape3d_size(shape);
					if (shape_size != Vector3() && MIN(Math::abs(shape_size.x), MIN(Math::abs(shape_size.y), Math::abs(shape_size.z))) < min_3d_size) {
						Dictionary issue = _make_issue("tiny_collider_3d:" + child_path, "warning", "tiny_collider_3d", cs, vformat("CollisionShape3D is very small: %.3f x %.3f x %.3f.", shape_size.x, shape_size.y, shape_size.z));
						issue["collision_node_path"] = child_path;
						issue["shape_size"] = shape_size;
						issues.push_back(issue);
					}
				}
				if (direct_shapes == 0) {
					Dictionary issue = _make_issue("missing_collider_3d:" + String(node->get_path()), "error", "missing_collider_3d", node, "CollisionObject3D has no direct CollisionShape3D child.");
					if (include_suggestions) {
						Dictionary args;
						args["parent_path"] = String(node->get_path());
						args["node_name"] = "CollisionShape3D";
						args["shape_type"] = "box";
						Dictionary parameters;
						parameters["size"] = Vector3(1, 1, 1);
						args["parameters"] = parameters;
						Dictionary call = _suggest_tool_call("add_collision_shape", args);
						issue["suggested_tool_call"] = call;
						suggested_calls.push_back(call);
					}
					issues.push_back(issue);
				}
				if (co3->get_collision_layer() == 0) {
					issues.push_back(_make_issue("zero_collision_layer_3d:" + String(node->get_path()), "warning", "zero_collision_layer_3d", node, "CollisionObject3D has collision_layer set to 0."));
				}
				if (co3->get_collision_mask() == 0 && Object::cast_to<StaticBody3D>(node) == nullptr) {
					issues.push_back(_make_issue("zero_collision_mask_3d:" + String(node->get_path()), "warning", "zero_collision_mask_3d", node, "Interactive CollisionObject3D has collision_mask set to 0."));
				}
			}
		}
	}

	Dictionary metrics;
	metrics["bodies_checked"] = bodies_checked;
	metrics["shapes_checked"] = shapes_checked;

	Dictionary result;
	result["ok"] = true;
	result["scene_path"] = scene_root->get_scene_file_path();
	result["valid"] = issues.is_empty();
	result["issue_count"] = issues.size();
	result["issues"] = issues;
	result["suggested_tool_calls"] = suggested_calls;
	result["metrics"] = metrics;
	return result;
}

Dictionary YeetAIDock::_tool_repair_game_physics(const Dictionary &p_args) const {
	Dictionary audit = _tool_audit_game_physics(p_args);
	if (audit.has("error")) {
		return audit;
	}

	const bool apply = _arg_bool(p_args, "apply", false);
	const Array issue_filter = _arg_array(p_args, "issue_ids");
	if (!apply) {
		audit["dry_run"] = true;
		audit["message"] = "Set apply:true to apply missing/tiny collider repairs.";
		return audit;
	}

	String err;
	Node *scene_root = _resolve_scene_root(p_args.get("scene_path", ""), err);
	if (scene_root == nullptr) {
		return _make_error(err);
	}

	Array repaired;
	const Array issues = audit.get("issues", Array());
	for (int i = 0; i < issues.size(); i++) {
		if (issues[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary issue = issues[i];
		const String id = String(issue.get("id", ""));
		if (!_issue_filter_allows(issue_filter, id)) {
			continue;
		}
		const String code = String(issue.get("code", ""));
		if (code == "missing_collider_2d") {
			Dictionary shape_args;
			shape_args["parent_path"] = issue.get("node_path", "");
			shape_args["shape_type"] = "rectangle";
			shape_args["size"] = Vector2(32, 32);
			Dictionary repaired_result = _tool_add_collision_shape_2d(shape_args);
			if (!repaired_result.has("error")) {
				repaired.push_back(id);
			}
		} else if (code == "missing_collider_3d") {
			Node *node = _resolve_node_target(scene_root, issue.get("node_path", ""), err);
			CollisionObject3D *co3 = Object::cast_to<CollisionObject3D>(node);
			if (co3 != nullptr) {
				Ref<BoxShape3D> box;
				box.instantiate();
				box->set_size(Vector3(1, 1, 1));
				CollisionShape3D *cs = memnew(CollisionShape3D);
				cs->set_name("CollisionShape3D");
				cs->set_shape(box);
				_add_to_scene(co3, cs, scene_root);
				repaired.push_back(id);
			}
		} else if (code == "tiny_collider_2d") {
			Node *node = _resolve_node_target(scene_root, issue.get("collision_node_path", ""), err);
			CollisionShape2D *cs = Object::cast_to<CollisionShape2D>(node);
			if (cs != nullptr && cs->get_shape().is_valid()) {
				Ref<RectangleShape2D> rect = cs->get_shape();
				if (rect.is_valid()) {
					rect->set_size(Vector2(MAX(rect->get_size().x, 16.0), MAX(rect->get_size().y, 16.0)));
					repaired.push_back(id);
				}
				Ref<CircleShape2D> circle = cs->get_shape();
				if (circle.is_valid()) {
					circle->set_radius(MAX(circle->get_radius(), 8.0));
					repaired.push_back(id);
				}
				Ref<CapsuleShape2D> capsule = cs->get_shape();
				if (capsule.is_valid()) {
					capsule->set_radius(MAX(capsule->get_radius(), 8.0));
					capsule->set_height(MAX(capsule->get_height(), 24.0));
					repaired.push_back(id);
				}
			}
		} else if (code == "tiny_collider_3d") {
			Node *node = _resolve_node_target(scene_root, issue.get("collision_node_path", ""), err);
			CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(node);
			if (cs != nullptr && cs->get_shape().is_valid()) {
				Ref<BoxShape3D> box = cs->get_shape();
				if (box.is_valid()) {
					const Vector3 size = box->get_size();
					box->set_size(Vector3(MAX(size.x, 0.1), MAX(size.y, 0.1), MAX(size.z, 0.1)));
					repaired.push_back(id);
				}
				Ref<SphereShape3D> sphere = cs->get_shape();
				if (sphere.is_valid()) {
					sphere->set_radius(MAX(sphere->get_radius(), 0.05));
					repaired.push_back(id);
				}
				Ref<CapsuleShape3D> capsule = cs->get_shape();
				if (capsule.is_valid()) {
					capsule->set_radius(MAX(capsule->get_radius(), 0.05));
					capsule->set_height(MAX(capsule->get_height(), 0.2));
					repaired.push_back(id);
				}
			}
		}
	}

	_mark_unsaved();
	Dictionary result;
	result["ok"] = true;
	result["repaired_issue_ids"] = repaired;
	result["repair_count"] = repaired.size();
	result["post_audit"] = _tool_audit_game_physics(p_args);
	return result;
}
