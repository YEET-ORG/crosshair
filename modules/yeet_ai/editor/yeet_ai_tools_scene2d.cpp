/**************************************************************************/
/*  yeet_ai_tools_scene2d.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/resource_loader.h"
#include "scene/2d/animated_sprite_2d.h"
#include "scene/2d/light_2d.h"
#include "scene/2d/line_2d.h"
#include "scene/2d/path_2d.h"
#include "scene/2d/polygon_2d.h"
#include "scene/2d/sprite_2d.h"
#include "scene/2d/physics/area_2d.h"
#include "scene/2d/physics/character_body_2d.h"
#include "scene/2d/physics/ray_cast_2d.h"
#include "scene/2d/physics/rigid_body_2d.h"
#include "scene/resources/texture.h"

Dictionary YeetAIDock::_tool_create_sprite_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Sprite2D");

	Sprite2D *sprite = memnew(Sprite2D);
	sprite->set_name(node_name);
	sprite->set_centered(_arg_bool(p_args, "centered", true));
	sprite->set_flip_h(_arg_bool(p_args, "flip_h", false));
	sprite->set_flip_v(_arg_bool(p_args, "flip_v", false));

	const String texture_path = _arg_string(p_args, "texture_path", "");
	if (!texture_path.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(texture_path);
		if (tex.is_valid()) {
			sprite->set_texture(tex);
		}
	}

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, sprite, scene_root);

	sprite->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["node_path"] = String(sprite->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_animated_sprite_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "AnimatedSprite2D");

	AnimatedSprite2D *anim = memnew(AnimatedSprite2D);
	anim->set_name(node_name);

	const String sprite_frames_path = _arg_string(p_args, "sprite_frames_path", "");
	if (!sprite_frames_path.is_empty()) {
		Ref<SpriteFrames> frames = ResourceLoader::load(sprite_frames_path);
		if (frames.is_valid()) {
			anim->set_sprite_frames(frames);
		}
	}

	anim->set_centered(_arg_bool(p_args, "centered", true));
	anim->set_flip_h(_arg_bool(p_args, "flip_h", false));
	anim->set_flip_v(_arg_bool(p_args, "flip_v", false));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, anim, scene_root);

	anim->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["node_path"] = String(anim->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_rigid_body_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "RigidBody2D");

	RigidBody2D *body = memnew(RigidBody2D);
	body->set_name(node_name);
	body->set_mass(_arg_float(p_args, "mass", 1.0));
	body->set_gravity_scale(_arg_float(p_args, "gravity_scale", 1.0));
	body->set_linear_damp(_arg_float(p_args, "linear_damp", 0.0));
	body->set_angular_damp(_arg_float(p_args, "angular_damp", 0.0));

	const String mode = _arg_string(p_args, "mode", "rigid").to_lower();
	if (mode == "static") {
		body->set_freeze_enabled(true);
		body->set_freeze_mode(RigidBody2D::FREEZE_MODE_STATIC);
	} else if (mode == "kinematic") {
		body->set_freeze_enabled(true);
		body->set_freeze_mode(RigidBody2D::FREEZE_MODE_KINEMATIC);
	} else if (mode == "character") {
		body->set_freeze_enabled(false);
	}

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, body, scene_root);

	body->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["node_path"] = String(body->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_character_body_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "CharacterBody2D");

	CharacterBody2D *body = memnew(CharacterBody2D);
	body->set_name(node_name);

	const String motion = _arg_string(p_args, "motion_mode", "grounded").to_lower();
	if (motion == "floating") {
		body->set_motion_mode(CharacterBody2D::MOTION_MODE_FLOATING);
	}
	body->set_floor_snap_length(_arg_float(p_args, "floor_snap_length", 1.0));

	const Vector2 up_dir = Vector2(_arg_float(p_args, "up_direction_x", 0.0), _arg_float(p_args, "up_direction_y", -1.0));
	body->set("up_direction", up_dir);

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, body, scene_root);

	body->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["node_path"] = String(body->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_area_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Area2D");

	Area2D *area = memnew(Area2D);
	area->set_name(node_name);
	area->set_monitoring(_arg_bool(p_args, "monitoring", true));
	area->set_monitorable(_arg_bool(p_args, "monitorable", true));
	area->set_collision_mask(_arg_int(p_args, "collision_mask", 1));
	area->set_collision_layer(_arg_int(p_args, "collision_layer", 1));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, area, scene_root);

	area->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["node_path"] = String(area->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_ray_cast_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "RayCast2D");

	RayCast2D *rc = memnew(RayCast2D);
	rc->set_name(node_name);
	rc->set_target_position(Vector2(_arg_float(p_args, "target_x", 0.0), _arg_float(p_args, "target_y", 50.0)));
	rc->set_collision_mask(_arg_int(p_args, "collision_mask", 1));
	rc->set_enabled(_arg_bool(p_args, "enabled", true));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, rc, scene_root);

	rc->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["node_path"] = String(rc->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_line_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Line2D");

	Line2D *line = memnew(Line2D);
	line->set_name(node_name);

	Array points = _arg_array(p_args, "points");
	if (!points.is_empty()) {
		Vector<Vector2> pts;
		for (int i = 0; i < points.size(); i++) {
			Dictionary d = points[i];
			pts.push_back(Vector2(d.get("x", 0.0), d.get("y", 0.0)));
		}
		line->set_points(pts);
	}

	line->set_width(_arg_float(p_args, "width", 10.0));
	line->set_default_color(_arg_color(p_args, "default_color", Color(1, 1, 1)));

	const String begin_cap = _arg_string(p_args, "begin_cap_mode", "none").to_lower();
	if (begin_cap == "box") {
		line->set_begin_cap_mode(Line2D::LINE_CAP_BOX);
	} else if (begin_cap == "round") {
		line->set_begin_cap_mode(Line2D::LINE_CAP_ROUND);
	}

	const String end_cap = _arg_string(p_args, "end_cap_mode", "none").to_lower();
	if (end_cap == "box") {
		line->set_end_cap_mode(Line2D::LINE_CAP_BOX);
	} else if (end_cap == "round") {
		line->set_end_cap_mode(Line2D::LINE_CAP_ROUND);
	}

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, line, scene_root);

	line->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["node_path"] = String(line->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_path_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Path2D");

	Path2D *path = memnew(Path2D);
	path->set_name(node_name);

	Ref<Curve2D> curve = memnew(Curve2D);
	Array points = _arg_array(p_args, "points");
	if (points.is_empty()) {
		curve->add_point(Vector2(0, 0));
		curve->add_point(Vector2(50, 0));
		curve->add_point(Vector2(50, 50));
	} else {
		for (int i = 0; i < points.size(); i++) {
			Dictionary pd = points[i];
			Vector2 pos = Vector2(pd.get("x", 0.0), pd.get("y", 0.0));
			Vector2 in_val = Vector2(pd.get("in_x", 0.0), pd.get("in_y", 0.0));
			Vector2 out_val = Vector2(pd.get("out_x", 0.0), pd.get("out_y", 0.0));
			curve->add_point(pos, in_val, out_val);
		}
	}
	path->set_curve(curve);

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, path, scene_root);

	if (_arg_bool(p_args, "add_follow", true)) {
		PathFollow2D *follow = memnew(PathFollow2D);
		follow->set_name("PathFollow2D");
		_add_to_scene(path, follow, scene_root);
		result["follow_path"] = String(follow->get_path());
	}

	_mark_unsaved();
	result["node_path"] = String(path->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_polygon_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Polygon2D");

	Polygon2D *poly = memnew(Polygon2D);
	poly->set_name(node_name);

	Array vertices = _arg_array(p_args, "vertices");
	if (!vertices.is_empty()) {
		Vector<Vector2> verts;
		for (int i = 0; i < vertices.size(); i++) {
			Dictionary d = vertices[i];
			verts.push_back(Vector2(d.get("x", 0.0), d.get("y", 0.0)));
		}
		poly->set_polygon(verts);
	}

	poly->set_color(_arg_color(p_args, "color", Color(1, 1, 1)));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, poly, scene_root);

	poly->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["node_path"] = String(poly->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_light_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String type = _arg_string(p_args, "light_type", "point").to_lower();
	const String node_name = _arg_string(p_args, "name", type + "_light_2d");

	Light2D *light = nullptr;
	if (type == "directional") {
		light = memnew(DirectionalLight2D);
	} else {
		PointLight2D *plight = memnew(PointLight2D);
		const String texture_path = _arg_string(p_args, "texture_path", "");
		if (!texture_path.is_empty()) {
			Ref<Texture2D> tex = ResourceLoader::load(texture_path);
			if (tex.is_valid()) {
				plight->set_texture(tex);
			}
		}
		light = plight;
	}

	light->set_name(node_name);
	light->set_color(_arg_color(p_args, "color", Color(1, 1, 1)));
	light->set_energy(_arg_float(p_args, "energy", 1.0));
	light->set_shadow_enabled(_arg_bool(p_args, "shadow", false));
	light->set_enabled(_arg_bool(p_args, "enabled", true));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, light, scene_root);

	light->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["node_path"] = String(light->get_path());
	result["light_type"] = type;
	return result;
}
