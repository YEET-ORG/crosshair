/**************************************************************************/
/*  yeet_ai_tools_scene2d.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "scene/2d/animated_sprite_2d.h"
#include "scene/2d/audio_stream_player_2d.h"
#include "scene/2d/camera_2d.h"
#include "scene/2d/canvas_modulate.h"
#include "scene/2d/cpu_particles_2d.h"
#include "scene/2d/damped_spring_joint_2d.h"
#include "scene/2d/gpu_particles_2d.h"
#include "scene/2d/light_2d.h"
#include "scene/2d/light_occluder_2d.h"
#include "scene/2d/line_2d.h"
#include "scene/2d/marker_2d.h"
#include "scene/2d/multimesh_instance_2d.h"
#include "scene/2d/parallax_background.h"
#include "scene/2d/parallax_layer.h"
#include "scene/2d/path_2d.h"
#include "scene/2d/physics/animatable_body_2d.h"
#include "scene/2d/physics/area_2d.h"
#include "scene/2d/physics/character_body_2d.h"
#include "scene/2d/physics/collision_polygon_2d.h"
#include "scene/2d/physics/collision_shape_2d.h"
#include "scene/2d/physics/ray_cast_2d.h"
#include "scene/2d/physics/rigid_body_2d.h"
#include "scene/2d/physics/shape_cast_2d.h"
#include "scene/2d/physics/static_body_2d.h"
#include "scene/2d/pin_joint_2d.h"
#include "scene/2d/polygon_2d.h"
#include "scene/2d/remote_transform_2d.h"
#include "scene/2d/skeleton_2d.h"
#include "scene/2d/sprite_2d.h"
#include "scene/2d/tile_map.h"
#include "scene/2d/touch_screen_button.h"
#include "scene/2d/visible_on_screen_notifier_2d.h"
#include "scene/main/canvas_layer.h"
#include "scene/2d/navigation/navigation_agent_2d.h"
#include "scene/2d/navigation/navigation_region_2d.h"
#include "scene/resources/2d/capsule_shape_2d.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/multimesh.h"
#include "servers/audio/audio_stream.h"
#include "scene/resources/2d/circle_shape_2d.h"
#include "scene/resources/2d/convex_polygon_shape_2d.h"
#include "scene/resources/2d/rectangle_shape_2d.h"
#include "scene/resources/2d/segment_shape_2d.h"
#include "scene/resources/2d/separation_ray_shape_2d.h"
#include "scene/resources/2d/world_boundary_shape_2d.h"
#include "scene/resources/texture.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/main/timer.h"
#include "scene/animation/animation_player.h"
#include "scene/resources/animation.h"
#include "scene/resources/animation_library.h"
#include "core/object/class_db.h"
#include "core/variant/callable.h"

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

	// Sprite sheet support: hframes/vframes for grid-based sprite sheets
	const int hframes = _arg_int(p_args, "hframes", 1);
	const int vframes = _arg_int(p_args, "vframes", 1);
	if (hframes > 1 || vframes > 1) {
		sprite->set_hframes(hframes);
		sprite->set_vframes(vframes);
		const int frame = _arg_int(p_args, "frame", 0);
		sprite->set_frame(frame);
	}

	// Region support for atlas/spritesheet sub-regions
	const bool use_region = _arg_bool(p_args, "use_region", false);
	if (use_region) {
		const int region_x = _arg_int(p_args, "region_x", 0);
		const int region_y = _arg_int(p_args, "region_y", 0);
		const int region_w = _arg_int(p_args, "region_w", 0);
		const int region_h = _arg_int(p_args, "region_h", 0);
		if (region_w > 0 && region_h > 0) {
			sprite->set_region_enabled(true);
			sprite->set_region_rect(Rect2(region_x, region_y, region_w, region_h));
		}
	}

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, sprite, scene_root);

	sprite->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(sprite->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_timer(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Timer");

	Timer *timer = memnew(Timer);
	timer->set_name(node_name);
	timer->set_wait_time(_arg_float(p_args, "wait_time", 1.0));
	timer->set_one_shot(_arg_bool(p_args, "one_shot", false));
	timer->set_autostart(_arg_bool(p_args, "autostart", false));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, timer, scene_root);

	// Optionally connect timeout signal to a method
	const String target_path = _arg_string(p_args, "timeout_target_path", "");
	const String target_method = _arg_string(p_args, "timeout_method", "");
	if (!target_path.is_empty() && !target_method.is_empty()) {
		Node *target = _resolve_node_target(scene_root, target_path, err);
		if (target != nullptr) {
			timer->connect("timeout", Callable(target, target_method));
			result["signal_connected"] = true;
		}
	}

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(timer->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_path_follow_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "PathFollow2D");
	const String parent_path = _arg_string(p_args, "path_2d_path", "");

	Path2D *path = Object::cast_to<Path2D>(_resolve_node_target(scene_root, parent_path, err));
	if (path == nullptr) {
		return _make_error("Parent must be a Path2D node. Provide 'path_2d_path'.");
	}

	PathFollow2D *pf = memnew(PathFollow2D);
	pf->set_name(node_name);
	pf->set_progress_ratio(_arg_float(p_args, "progress_ratio", 0.0));
	pf->set_rotation_enabled(_arg_bool(p_args, "rotates", true));
	pf->set_loop(_arg_bool(p_args, "loop", true));

	_add_to_scene(path, pf, scene_root);

	// Optionally add a child node to follow the path
	const String child_type = _arg_string(p_args, "child_type", "");
	if (!child_type.is_empty()) {
		Node *child = Object::cast_to<Node>(ClassDB::instantiate(child_type));
		if (child != nullptr) {
			child->set_name(_arg_string(p_args, "child_name", child_type));
			_add_to_scene(pf, child, scene_root);
			result["child_path"] = String(child->get_path());
		}
	}

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(pf->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_query_raycast_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	RayCast2D *raycast = Object::cast_to<RayCast2D>(_resolve_node_target(scene_root, _arg_string(p_args, "node_path", ""), err));
	if (raycast == nullptr) {
		return _make_error("RayCast2D node not found. Provide 'node_path'.");
	}

	// Force raycast update
	raycast->force_raycast_update();

	result["is_colliding"] = raycast->is_colliding();

	if (raycast->is_colliding()) {
		result["collision_point"] = raycast->get_collision_point();
		result["collision_normal"] = raycast->get_collision_normal();

		Object *collider = raycast->get_collider();
		if (collider != nullptr) {
			Node *collider_node = Object::cast_to<Node>(collider);
			if (collider_node != nullptr) {
				result["collider_path"] = String(collider_node->get_path());
				result["collider_name"] = collider_node->get_name();
			}
		}

		int collider_rid = raycast->get_collider_rid().get_id();
		result["collider_rid"] = collider_rid;
	}

	result["ok"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_create_button(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Button");

	Button *btn = memnew(Button);
	btn->set_name(node_name);
	btn->set_text(_arg_string(p_args, "text", "Button"));
	btn->set_disabled(_arg_bool(p_args, "disabled", false));
	btn->set_toggle_mode(_arg_bool(p_args, "toggle_mode", false));
	btn->set_pressed(_arg_bool(p_args, "pressed", false));

	// Optional icon
	const String icon_path = _arg_string(p_args, "icon_path", "");
	if (!icon_path.is_empty()) {
		Ref<Texture2D> icon = ResourceLoader::load(icon_path);
		if (icon.is_valid()) {
			btn->set_button_icon(icon);
		}
	}

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, btn, scene_root);

	// Position
	btn->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	btn->set_size(Vector2(_arg_float(p_args, "width", 100.0), _arg_float(p_args, "height", 30.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(btn->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_label(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Label");

	Label *label = memnew(Label);
	label->set_name(node_name);
	label->set_text(_arg_string(p_args, "text", ""));
	label->set_horizontal_alignment(static_cast<HorizontalAlignment>(_arg_int(p_args, "align", 0)));
	label->set_vertical_alignment(static_cast<VerticalAlignment>(_arg_int(p_args, "valign", 0)));
	label->set_autowrap_mode(static_cast<TextServer::AutowrapMode>(_arg_int(p_args, "autowrap", 0)));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, label, scene_root);

	// Position and size
	label->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	label->set_size(Vector2(_arg_float(p_args, "width", 100.0), _arg_float(p_args, "height", 30.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(label->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_tween(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	// Tween in Godot 4 is not a node — we create an AnimationPlayer with a simple tween-like animation instead.
	const String node_name = _arg_string(p_args, "name", "TweenAnimation");
	const String target_path = _arg_string(p_args, "target_path", "");
	const String property = _arg_string(p_args, "property", "position");
	const float duration = _arg_float(p_args, "duration", 1.0);

	Node *target = _resolve_node_target(scene_root, target_path, err);
	if (target == nullptr) {
		return _make_error("Target node not found. Provide 'target_path'.");
	}

	AnimationPlayer *player = memnew(AnimationPlayer);
	player->set_name(node_name);

	Ref<Animation> anim;
	anim.instantiate();
	anim->set_length(duration);
	anim->set_loop_mode(_arg_bool(p_args, "loop", false) ? Animation::LOOP_PINGPONG : Animation::LOOP_NONE);

	// Determine property track type
	Variant::Type property_type = Variant::FLOAT;
	if (property == "position" || property == "scale" || property == "modulate") {
		property_type = Variant::VECTOR2;
	} else if (property == "rotation") {
		property_type = Variant::FLOAT;
	}

	String track_path = String(target->get_path()).trim_prefix(String(scene_root->get_path())) + ":" + property;
	if (track_path.begins_with("/")) {
		track_path = track_path.substr(1);
	}

	int track_idx = anim->add_track(Animation::TYPE_VALUE);
	anim->track_set_path(track_idx, track_path);

	// Start value
	Dictionary start_val_dict = _arg_dict(p_args, "from");
	Variant start_val;
	if (property_type == Variant::VECTOR2) {
		start_val = Vector2(start_val_dict.get("x", 0.0), start_val_dict.get("y", 0.0));
	} else {
		start_val = start_val_dict.get("value", 0.0);
	}
	anim->track_insert_key(track_idx, 0.0, start_val);

	// End value
	Dictionary end_val_dict = _arg_dict(p_args, "to");
	Variant end_val;
	if (property_type == Variant::VECTOR2) {
		end_val = Vector2(end_val_dict.get("x", 0.0), end_val_dict.get("y", 0.0));
	} else {
		end_val = end_val_dict.get("value", 1.0);
	}
	anim->track_insert_key(track_idx, duration, end_val);

	// Easing
	const String ease_type = _arg_string(p_args, "ease", "in_out");
	Animation::InterpolationType interp = Animation::INTERPOLATION_LINEAR;
	if (ease_type == "ease_in") interp = Animation::INTERPOLATION_CUBIC;
	else if (ease_type == "ease_out") interp = Animation::INTERPOLATION_CUBIC;
	else if (ease_type == "ease_in_out") interp = Animation::INTERPOLATION_CUBIC;
	anim->track_set_interpolation_type(track_idx, interp);

	Ref<AnimationLibrary> lib;
	lib.instantiate();
	lib->add_animation("tween", anim);
	player->add_animation_library("", lib);

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, player, scene_root);

	// Optionally autoplay
	if (_arg_bool(p_args, "autoplay", false)) {
		player->play("tween");
	}

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(player->get_path());
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// BATCH 2: Essential 2D Game Dev Tools
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_create_animatable_body_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "AnimatableBody2D");

	AnimatableBody2D *body = memnew(AnimatableBody2D);
	body->set_name(node_name);
	body->set_constant_linear_velocity(Vector2(_arg_float(p_args, "velocity_x", 0.0), _arg_float(p_args, "velocity_y", 0.0)));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, body, scene_root);
	body->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(body->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_audio_stream_player_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "AudioStreamPlayer2D");

	AudioStreamPlayer2D *player = memnew(AudioStreamPlayer2D);
	player->set_name(node_name);
	player->set_volume_db(_arg_float(p_args, "volume_db", 0.0));
	player->set_pitch_scale(_arg_float(p_args, "pitch_scale", 1.0));
	player->set_max_distance(_arg_float(p_args, "max_distance", 2000.0));
	player->set_attenuation(_arg_float(p_args, "attenuation", 1.0));
	player->set_autoplay(_arg_bool(p_args, "autoplay", false));
	player->set_bus(_arg_string(p_args, "bus", "Master"));

	const String stream_path = _arg_string(p_args, "stream_path", "");
	if (!stream_path.is_empty()) {
		Ref<AudioStream> stream = ResourceLoader::load(stream_path);
		if (stream.is_valid()) {
			player->set_stream(stream);
		}
	}

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, player, scene_root);
	player->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(player->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_canvas_layer(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "CanvasLayer");

	CanvasLayer *layer = memnew(CanvasLayer);
	layer->set_name(node_name);
	layer->set_layer(_arg_int(p_args, "layer", 1));
	layer->set_offset(Vector2(_arg_float(p_args, "offset_x", 0.0), _arg_float(p_args, "offset_y", 0.0)));
	layer->set_scale(Vector2(_arg_float(p_args, "scale_x", 1.0), _arg_float(p_args, "scale_y", 1.0)));
	layer->set_visible(_arg_bool(p_args, "visible", true));

	_add_to_scene(scene_root, layer, scene_root);

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(layer->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_parallax_background(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "ParallaxBackground");

	ParallaxBackground *bg = memnew(ParallaxBackground);
	bg->set_name(node_name);
	bg->set_scroll_offset(Vector2(_arg_float(p_args, "scroll_offset_x", 0.0), _arg_float(p_args, "scroll_offset_y", 0.0)));
	bg->set_scroll_base_offset(Vector2(_arg_float(p_args, "scroll_base_offset_x", 0.0), _arg_float(p_args, "scroll_base_offset_y", 0.0)));
	bg->set_scroll_base_scale(Vector2(_arg_float(p_args, "scroll_base_scale_x", 1.0), _arg_float(p_args, "scroll_base_scale_y", 1.0)));

	_add_to_scene(scene_root, bg, scene_root);

	// Optionally create first parallax layer
	if (_arg_bool(p_args, "add_layer", true)) {
		ParallaxLayer *pl = memnew(ParallaxLayer);
		pl->set_name("ParallaxLayer");
		pl->set_motion_scale(Vector2(_arg_float(p_args, "motion_scale_x", 0.5), _arg_float(p_args, "motion_scale_y", 0.5)));
		_add_to_scene(bg, pl, scene_root);
		result["layer_path"] = String(pl->get_path());
	}

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(bg->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_parallax_layer(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "ParallaxLayer");
	const String parent_path = _arg_string(p_args, "parallax_background_path", "");

	ParallaxLayer *layer = memnew(ParallaxLayer);
	layer->set_name(node_name);
	layer->set_motion_scale(Vector2(_arg_float(p_args, "motion_scale_x", 0.5), _arg_float(p_args, "motion_scale_y", 0.5)));
	layer->set_motion_offset(Vector2(_arg_float(p_args, "motion_offset_x", 0.0), _arg_float(p_args, "motion_offset_y", 0.0)));

	Node *parent = scene_root;
	if (!parent_path.is_empty()) {
		parent = _resolve_node_target(scene_root, parent_path, err);
		if (parent == nullptr) {
			parent = scene_root;
		}
	}
	_add_to_scene(parent, layer, scene_root);

	// Optionally add a sprite to this layer
	const String texture_path = _arg_string(p_args, "texture_path", "");
	if (!texture_path.is_empty()) {
		Sprite2D *sprite = memnew(Sprite2D);
		sprite->set_name("BackgroundSprite");
		Ref<Texture2D> tex = ResourceLoader::load(texture_path);
		if (tex.is_valid()) {
			sprite->set_texture(tex);
		}
		_add_to_scene(layer, sprite, scene_root);
		result["sprite_path"] = String(sprite->get_path());
	}

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(layer->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_visible_on_screen_notifier_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "VisibleOnScreenNotifier2D");

	VisibleOnScreenNotifier2D *notifier = memnew(VisibleOnScreenNotifier2D);
	notifier->set_name(node_name);
	notifier->set_rect(Rect2(
		_arg_float(p_args, "rect_x", -10.0),
		_arg_float(p_args, "rect_y", -10.0),
		_arg_float(p_args, "rect_w", 20.0),
		_arg_float(p_args, "rect_h", 20.0)
	));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, notifier, scene_root);
	notifier->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(notifier->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_remote_transform_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "RemoteTransform2D");

	RemoteTransform2D *remote = memnew(RemoteTransform2D);
	remote->set_name(node_name);
	remote->set_remote_node(_arg_string(p_args, "remote_path", ""));
	remote->set_use_global_coordinates(_arg_bool(p_args, "use_global_coordinates", true));
	remote->set_update_position(_arg_bool(p_args, "update_position", true));
	remote->set_update_rotation(_arg_bool(p_args, "update_rotation", true));
	remote->set_update_scale(_arg_bool(p_args, "update_scale", true));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, remote, scene_root);
	remote->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(remote->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_navigation_agent_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "NavigationAgent2D");

	NavigationAgent2D *agent = memnew(NavigationAgent2D);
	agent->set_name(node_name);
	agent->set_target_desired_distance(_arg_float(p_args, "target_desired_distance", 1.0));
	agent->set_radius(_arg_float(p_args, "radius", 10.0));
	agent->set_neighbor_distance(_arg_float(p_args, "neighbor_distance", 50.0));
	agent->set_max_neighbors(_arg_int(p_args, "max_neighbors", 10));
	agent->set_max_speed(_arg_float(p_args, "max_speed", 100.0));
	agent->set_path_desired_distance(_arg_float(p_args, "path_desired_distance", 1.0));
	agent->set_avoidance_enabled(_arg_bool(p_args, "avoidance_enabled", true));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, agent, scene_root);

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(agent->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_navigation_region_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "NavigationRegion2D");

	NavigationRegion2D *region = memnew(NavigationRegion2D);
	region->set_name(node_name);
	region->set_travel_cost(_arg_float(p_args, "travel_cost", 1.0));
	region->set_enter_cost(_arg_float(p_args, "enter_cost", 0.0));

	// Optional: set navigation polygon from resource
	const String navpoly_path = _arg_string(p_args, "navigation_polygon_path", "");
	if (!navpoly_path.is_empty()) {
		Ref<NavigationPolygon> np = ResourceLoader::load(navpoly_path);
		if (np.is_valid()) {
			region->set_navigation_polygon(np);
		}
	}

	// Optional: create simple polygon from vertices
	Array vertices = _arg_array(p_args, "vertices");
	if (!vertices.is_empty() && navpoly_path.is_empty()) {
		Ref<NavigationPolygon> np = memnew(NavigationPolygon);
		Vector<Vector2> verts;
		for (int i = 0; i < vertices.size(); i++) {
			Dictionary d = vertices[i];
			verts.push_back(Vector2(d.get("x", 0.0), d.get("y", 0.0)));
		}
		np->set_vertices(verts);
		// Create a single outline polygon
		PackedInt32Array indices;
		for (int i = 0; i < verts.size(); i++) {
			indices.push_back(i);
		}
		np->add_polygon(indices);
		region->set_navigation_polygon(np);
	}

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, region, scene_root);
	region->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	// Optionally bake
	if (_arg_bool(p_args, "bake", false)) {
		region->bake_navigation_polygon(false);
	}

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(region->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_add_collision_shape_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	String parent_path = _arg_string(p_args, "parent_path", "");
	if (parent_path.is_empty()) {
		parent_path = _arg_string(p_args, "node_path", "");
	}
	Node *parent = _resolve_node_target(scene_root, parent_path, err);
	CollisionObject2D *collision_parent = Object::cast_to<CollisionObject2D>(parent);
	if (collision_parent == nullptr) {
		return _make_error(vformat("Parent '%s' must be a CollisionObject2D (Area2D, StaticBody2D, RigidBody2D, CharacterBody2D, AnimatableBody2D)", parent_path));
	}

	const String shape_type = _arg_string(p_args, "shape_type", "rectangle").to_lower();
	Ref<Shape2D> shape;
	Array warnings;

	const Vector2 rect_size = _arg_vector2(p_args, "size", Vector2(_arg_float(p_args, "width", 32.0), _arg_float(p_args, "height", 32.0)));
	const Vector2 shape_position = _arg_vector2(p_args, "position", Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	const double derived_radius = MAX(0.001, MIN(Math::abs(rect_size.x), Math::abs(rect_size.y)) * 0.5);
	const double radius = _arg_float(p_args, "radius", derived_radius);
	const double height = _arg_float(p_args, "height", MAX(Math::abs(rect_size.y), radius * 2.0));

	if (shape_type == "circle") {
		Ref<CircleShape2D> circle = memnew(CircleShape2D);
		circle->set_radius(radius);
		shape = circle;
	} else if (shape_type == "capsule") {
		Ref<CapsuleShape2D> capsule = memnew(CapsuleShape2D);
		capsule->set_radius(radius);
		capsule->set_height(MAX(height, radius * 2.0));
		shape = capsule;
	} else if (shape_type == "segment") {
		Ref<SegmentShape2D> segment = memnew(SegmentShape2D);
		Array points = _arg_array(p_args, "points");
		if (points.size() >= 2 && points[0].get_type() == Variant::DICTIONARY && points[1].get_type() == Variant::DICTIONARY) {
			const Dictionary a = points[0];
			const Dictionary b = points[1];
			segment->set_a(Vector2(_arg_float(a, "x", -rect_size.x * 0.5), _arg_float(a, "y", 0.0)));
			segment->set_b(Vector2(_arg_float(b, "x", rect_size.x * 0.5), _arg_float(b, "y", 0.0)));
		} else {
			segment->set_a(Vector2(_arg_float(p_args, "a_x", -rect_size.x * 0.5), _arg_float(p_args, "a_y", 0.0)));
			segment->set_b(Vector2(_arg_float(p_args, "b_x", rect_size.x * 0.5), _arg_float(p_args, "b_y", 0.0)));
		}
		shape = segment;
	} else if (shape_type == "convex") {
		Array points = _arg_array(p_args, "points");
		if (points.is_empty()) {
			points = _arg_array(p_args, "vertices");
		}
		if (points.size() < 3) {
			return _make_error("Convex CollisionShape2D requires at least 3 points in 'points' or 'vertices'.");
		}
		Vector<Vector2> convex_points;
		for (int i = 0; i < points.size(); i++) {
			if (points[i].get_type() == Variant::DICTIONARY) {
				const Dictionary d = points[i];
				convex_points.push_back(Vector2(_arg_float(d, "x", 0.0), _arg_float(d, "y", 0.0)));
			}
		}
		if (convex_points.size() < 3) {
			return _make_error("Convex CollisionShape2D points must be dictionaries with x/y values.");
		}
		Ref<ConvexPolygonShape2D> convex = memnew(ConvexPolygonShape2D);
		convex->set_points(convex_points);
		shape = convex;
	} else if (shape_type == "separation_ray") {
		Ref<SeparationRayShape2D> ray = memnew(SeparationRayShape2D);
		ray->set_length(_arg_float(p_args, "length", MAX(rect_size.y, 32.0)));
		shape = ray;
	} else if (shape_type == "world_boundary") {
		Ref<WorldBoundaryShape2D> boundary = memnew(WorldBoundaryShape2D);
		boundary->set_normal(Vector2(_arg_float(p_args, "normal_x", 0.0), _arg_float(p_args, "normal_y", -1.0)));
		boundary->set_distance(_arg_float(p_args, "distance", 0.0));
		shape = boundary;
	} else if (shape_type == "rectangle" || shape_type == "box") {
		// Default: rectangle
		Ref<RectangleShape2D> rect = memnew(RectangleShape2D);
		rect->set_size(rect_size);
		shape = rect;
	} else {
		return _make_error("Unknown 2D shape type. Use: rectangle, circle, capsule, segment, convex, separation_ray, world_boundary.");
	}

	if (!p_args.has("size") && !p_args.has("width") && !p_args.has("height") && shape_type != "world_boundary") {
		warnings.push_back("No explicit collider dimensions were provided; used conservative defaults. Prefer passing size, radius, or height.");
	}

	CollisionShape2D *cs = memnew(CollisionShape2D);
	cs->set_name(_arg_string(p_args, "name", "CollisionShape2D"));
	cs->set_shape(shape);
	cs->set_disabled(_arg_bool(p_args, "disabled", false));
	cs->set_one_way_collision(_arg_bool(p_args, "one_way_collision", false));
	cs->set_one_way_collision_margin(_arg_float(p_args, "one_way_collision_margin", 1.0));
	cs->set_position(shape_position);
	cs->set_rotation_degrees(_arg_float(p_args, "rotation_degrees", 0.0));

	_add_to_scene(collision_parent, cs, scene_root);

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(cs->get_path());
	result["shape_type"] = shape_type;
	result["shape_size"] = rect_size;
	result["shape_position"] = shape_position;
	result["warnings"] = warnings;
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
	result["ok"] = true;
	result["node_path"] = String(anim->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_sprite_frames(const Dictionary &p_args) const {
	const String save_path = _arg_string(p_args, "save_path", "");
	if (!save_path.begins_with("res://")) {
		return _make_error("save_path must start with res://");
	}

	const String texture_path = _arg_string(p_args, "texture_path", "");
	if (texture_path.is_empty()) {
		return _make_error("texture_path is required");
	}

	Ref<Texture2D> texture = ResourceLoader::load(texture_path, "Texture2D");
	if (texture.is_null()) {
		return _make_error("Failed to load texture: " + texture_path);
	}

	const int hframes = _arg_int(p_args, "hframes", 1);
	const int vframes = _arg_int(p_args, "vframes", 1);
	if (hframes < 1 || vframes < 1) {
		return _make_error("hframes and vframes must be >= 1");
	}

	Ref<SpriteFrames> frames;
	frames.instantiate();

	// Get animation name (default "default")
	const String anim_name = _arg_string(p_args, "animation_name", "default");
	const float fps = _arg_float(p_args, "fps", 5.0);

	// Calculate frame size from texture and grid
	const int tex_w = texture->get_width();
	const int tex_h = texture->get_height();
	const int frame_w = tex_w / hframes;
	const int frame_h = tex_h / vframes;

	if (frame_w <= 0 || frame_h <= 0) {
		return _make_error(vformat("Texture size (%dx%d) too small for grid (%dx%d)", tex_w, tex_h, hframes, vframes));
	}

	// Create AtlasTexture for each frame and add to animation
	for (int y = 0; y < vframes; y++) {
		for (int x = 0; x < hframes; x++) {
			Ref<AtlasTexture> at;
			at.instantiate();
			at->set_atlas(texture);
			at->set_region(Rect2(x * frame_w, y * frame_h, frame_w, frame_h));
			frames->add_frame(anim_name, at);
		}
	}

	frames->set_animation_speed(anim_name, fps);
	frames->set_animation_loop(anim_name, _arg_bool(p_args, "loop", true));

	const Error save_err = ResourceSaver::save(frames, save_path);
	if (save_err != OK) {
		return _make_error(vformat("Failed to save SpriteFrames: error %d", save_err));
	}

	Dictionary extra;
	extra["save_path"] = save_path;
	extra["animation_name"] = anim_name;
	extra["frame_count"] = hframes * vframes;
	extra["frame_size"] = vformat("%dx%d", frame_w, frame_h);
	return _make_ok(extra);
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
	result["ok"] = true;
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
	result["ok"] = true;
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
	result["ok"] = true;
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
	result["ok"] = true;
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
	result["ok"] = true;
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
		follow->set_name(node_name + "_follow");
		_add_to_scene(path, follow, scene_root);
		result["follow_path"] = String(follow->get_path());
	}

	_mark_unsaved();
	result["ok"] = true;
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
	result["ok"] = true;
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
	result["ok"] = true;
	result["node_path"] = String(light->get_path());
	result["light_type"] = type;
	return result;
}

Dictionary YeetAIDock::_tool_create_static_body_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "StaticBody2D");

	StaticBody2D *body = memnew(StaticBody2D);
	body->set_name(node_name);

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, body, scene_root);
	body->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(body->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_shape_cast_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "ShapeCast2D");

	ShapeCast2D *sc = memnew(ShapeCast2D);
	sc->set_name(node_name);
	sc->set_target_position(Vector2(_arg_float(p_args, "target_x", 0.0), _arg_float(p_args, "target_y", 50.0)));
	sc->set_collision_mask(_arg_int(p_args, "collision_mask", 1));
	sc->set_enabled(_arg_bool(p_args, "enabled", true));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, sc, scene_root);
	sc->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(sc->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_collision_polygon_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "CollisionPolygon2D");

	CollisionPolygon2D *poly = memnew(CollisionPolygon2D);
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

	poly->set_build_mode(_arg_bool(p_args, "convex", false) ? CollisionPolygon2D::BUILD_SEGMENTS : CollisionPolygon2D::BUILD_SOLIDS);

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, poly, scene_root);
	poly->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(poly->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_camera_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Camera2D");

	Camera2D *cam = memnew(Camera2D);
	cam->set_name(node_name);
	cam->set_enabled(_arg_bool(p_args, "enabled", true));
	cam->set_zoom(Vector2(_arg_float(p_args, "zoom_x", 1.0), _arg_float(p_args, "zoom_y", 1.0)));
	cam->set_offset(Vector2(_arg_float(p_args, "offset_x", 0.0), _arg_float(p_args, "offset_y", 0.0)));
	cam->set_position_smoothing_enabled(_arg_bool(p_args, "smoothing", false));
	cam->set_position_smoothing_speed(_arg_float(p_args, "smoothing_speed", 5.0));

	// Limits
	if (_arg_bool(p_args, "use_limits", false)) {
		cam->set_limit(SIDE_LEFT, _arg_int(p_args, "limit_left", -10000000));
		cam->set_limit(SIDE_TOP, _arg_int(p_args, "limit_top", -10000000));
		cam->set_limit(SIDE_RIGHT, _arg_int(p_args, "limit_right", 10000000));
		cam->set_limit(SIDE_BOTTOM, _arg_int(p_args, "limit_bottom", 10000000));
	}

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, cam, scene_root);
	cam->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(cam->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_marker_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Marker2D");

	Marker2D *marker = memnew(Marker2D);
	marker->set_name(node_name);
	marker->set_rotation_degrees(_arg_float(p_args, "rotation_degrees", 0.0));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, marker, scene_root);
	marker->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(marker->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_tile_map(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "TileMap");

	TileMap *tm = memnew(TileMap);
	tm->set_name(node_name);

	const String tile_set_path = _arg_string(p_args, "tile_set_path", "");
	if (!tile_set_path.is_empty()) {
		Ref<TileSet> ts = ResourceLoader::load(tile_set_path);
		if (ts.is_valid()) {
			tm->set_tileset(ts);
		}
	}

	// Cell size is now on TileSet, but we can set the rendering quadrant size
	tm->set_rendering_quadrant_size(_arg_int(p_args, "rendering_quadrant_size", 16));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, tm, scene_root);
	tm->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(tm->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_cpu_particles_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "CPUParticles2D");

	CPUParticles2D *particles = memnew(CPUParticles2D);
	particles->set_name(node_name);
	particles->set_emitting(_arg_bool(p_args, "emitting", true));
	particles->set_amount(_arg_int(p_args, "amount", 8));
	particles->set_lifetime(_arg_float(p_args, "lifetime", 1.0));
	particles->set_one_shot(_arg_bool(p_args, "one_shot", false));
	particles->set_explosiveness_ratio(_arg_float(p_args, "explosiveness", 0.0));
	particles->set_direction(Vector2(_arg_float(p_args, "direction_x", 0.0), _arg_float(p_args, "direction_y", -1.0)));
	particles->set_spread(_arg_float(p_args, "spread", 45.0));
	particles->set_gravity(Vector2(_arg_float(p_args, "gravity_x", 0.0), _arg_float(p_args, "gravity_y", 98.0)));
	particles->set_color(_arg_color(p_args, "color", Color(1, 1, 1)));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, particles, scene_root);
	particles->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(particles->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_light_occluder_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "LightOccluder2D");

	LightOccluder2D *occluder = memnew(LightOccluder2D);
	occluder->set_name(node_name);

	// Build polygon from points
	Array points = _arg_array(p_args, "polygon_points");
	if (!points.is_empty()) {
		Vector<Vector2> polygon;
		for (int i = 0; i < points.size(); i++) {
			Dictionary pd = points[i];
			polygon.push_back(Vector2(_arg_float(pd, "x", 0.0), _arg_float(pd, "y", 0.0)));
		}
		Ref<OccluderPolygon2D> oc_poly;
		oc_poly.instantiate();
		oc_poly->set_polygon(polygon);
		occluder->set_occluder_polygon(oc_poly);
	}

	// Optional: closed polygon
	if (_arg_bool(p_args, "closed", true)) {
		Ref<OccluderPolygon2D> oc = occluder->get_occluder_polygon();
		if (oc.is_valid()) {
			oc->set_closed(true);
		}
	}

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, occluder, scene_root);
	occluder->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(occluder->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_canvas_modulate(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "CanvasModulate");

	CanvasModulate *cm = memnew(CanvasModulate);
	cm->set_name(node_name);
	cm->set_color(_arg_color(p_args, "color", Color(1, 1, 1)));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, cm, scene_root);

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(cm->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_skeleton_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Skeleton2D");

	Skeleton2D *skel = memnew(Skeleton2D);
	skel->set_name(node_name);

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, skel, scene_root);
	skel->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(skel->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_bone_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Bone2D");

	Bone2D *bone = memnew(Bone2D);
	bone->set_name(node_name);
	bone->set_rest_length(_arg_float(p_args, "rest_length", 16.0));

	// Optional auto-calculate length
	if (_arg_bool(p_args, "auto_calculate_length", true)) {
		bone->set_auto_calculate_length_and_angle(true);
	} else {
		bone->set_auto_calculate_length_and_angle(false);
		bone->set_length(_arg_float(p_args, "length", 16.0));
	}

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, bone, scene_root);
	bone->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(bone->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_pin_joint_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "PinJoint2D");

	PinJoint2D *joint = memnew(PinJoint2D);
	joint->set_name(node_name);

	// Connect to two physics bodies
	const String node_a = _arg_string(p_args, "node_a", "");
	const String node_b = _arg_string(p_args, "node_b", "");
	if (!node_a.is_empty()) {
		Node *na = _resolve_node_target(scene_root, node_a, err);
		if (na != nullptr) {
			joint->set_node_a(na->get_path());
		}
	}
	if (!node_b.is_empty()) {
		Node *nb = _resolve_node_target(scene_root, node_b, err);
		if (nb != nullptr) {
			joint->set_node_b(nb->get_path());
		}
	}

	joint->set_softness(_arg_float(p_args, "softness", 0.0));
	joint->set_bias(_arg_float(p_args, "bias", 0.0));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, joint, scene_root);
	joint->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(joint->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_damped_spring_joint_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "DampedSpringJoint2D");

	DampedSpringJoint2D *joint = memnew(DampedSpringJoint2D);
	joint->set_name(node_name);

	const String node_a = _arg_string(p_args, "node_a", "");
	const String node_b = _arg_string(p_args, "node_b", "");
	if (!node_a.is_empty()) {
		Node *na = _resolve_node_target(scene_root, node_a, err);
		if (na != nullptr) {
			joint->set_node_a(na->get_path());
		}
	}
	if (!node_b.is_empty()) {
		Node *nb = _resolve_node_target(scene_root, node_b, err);
		if (nb != nullptr) {
			joint->set_node_b(nb->get_path());
		}
	}

	joint->set_length(_arg_float(p_args, "length", 16.0));
	joint->set_stiffness(_arg_float(p_args, "stiffness", 20.0));
	joint->set_damping(_arg_float(p_args, "damping", 1.5));
	joint->set_rest_length(_arg_float(p_args, "rest_length", 16.0));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, joint, scene_root);
	joint->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(joint->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_multimesh_instance_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "MultiMeshInstance2D");

	MultiMeshInstance2D *mmi = memnew(MultiMeshInstance2D);
	mmi->set_name(node_name);

	Ref<MultiMesh> mm;
	mm.instantiate();
	mm->set_transform_format(MultiMesh::TRANSFORM_2D);
	mm->set_use_colors(true);
	mm->set_use_custom_data(false);

	// Optional: set mesh and instance count
	const String mesh_path = _arg_string(p_args, "mesh_path", "");
	if (!mesh_path.is_empty()) {
		Ref<Mesh> mesh = ResourceLoader::load(mesh_path);
		if (mesh.is_valid()) {
			mm->set_mesh(mesh);
		}
	}

	const int instance_count = _arg_int(p_args, "instance_count", 0);
	if (instance_count > 0) {
		mm->set_instance_count(instance_count);
	}

	mmi->set_multimesh(mm);

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, mmi, scene_root);
	mmi->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(mmi->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_touch_screen_button(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "TouchScreenButton");

	TouchScreenButton *btn = memnew(TouchScreenButton);
	btn->set_name(node_name);

	const String texture_path = _arg_string(p_args, "texture_path", "");
	if (!texture_path.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(texture_path);
		if (tex.is_valid()) {
			btn->set_texture_normal(tex);
		}
	}

	const String pressed_path = _arg_string(p_args, "pressed_texture_path", "");
	if (!pressed_path.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(pressed_path);
		if (tex.is_valid()) {
			btn->set_texture_pressed(tex);
		}
	}

	btn->set_passby_press(_arg_bool(p_args, "passby_press", false));
	btn->set_visibility_mode(_arg_bool(p_args, "always_visible", true) ? TouchScreenButton::VISIBILITY_ALWAYS : TouchScreenButton::VISIBILITY_TOUCHSCREEN_ONLY);

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, btn, scene_root);
	btn->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(btn->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_subviewport(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "SubViewport");

	SubViewport *vp = memnew(SubViewport);
	vp->set_name(node_name);
	vp->set_size(Vector2i(_arg_int(p_args, "width", 512), _arg_int(p_args, "height", 512)));
	vp->set_disable_3d(_arg_bool(p_args, "disable_3d", true));
	vp->set_clear_mode(SubViewport::CLEAR_MODE_ALWAYS);
	vp->set_update_mode(SubViewport::UPDATE_ALWAYS);

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, vp, scene_root);

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(vp->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_mesh_instance_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "MeshInstance2D");

	MeshInstance2D *mi = memnew(MeshInstance2D);
	mi->set_name(node_name);

	const String mesh_path = _arg_string(p_args, "mesh_path", "");
	if (!mesh_path.is_empty()) {
		Ref<Mesh> mesh = ResourceLoader::load(mesh_path);
		if (mesh.is_valid()) {
			mi->set_mesh(mesh);
		}
	}

	const String texture_path = _arg_string(p_args, "texture_path", "");
	if (!texture_path.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(texture_path);
		if (tex.is_valid()) {
			mi->set_texture(tex);
		}
	}

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, mi, scene_root);
	mi->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(mi->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_gpu_particles_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "GPUParticles2D");

	GPUParticles2D *particles = memnew(GPUParticles2D);
	particles->set_name(node_name);
	particles->set_emitting(_arg_bool(p_args, "emitting", true));
	particles->set_amount(_arg_int(p_args, "amount", 8));
	particles->set_lifetime(_arg_float(p_args, "lifetime", 1.0));
	particles->set_one_shot(_arg_bool(p_args, "one_shot", false));
	particles->set_explosiveness_ratio(_arg_float(p_args, "explosiveness", 0.0));

	Node2D *parent = Object::cast_to<Node2D>(_resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err));
	if (parent == nullptr) {
		parent = Object::cast_to<Node2D>(scene_root);
	}
	_add_to_scene(parent, particles, scene_root);
	particles->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(particles->get_path());
	return result;
}
