/**************************************************************************/
/*  yeet_ai_tools_animation.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "scene/animation/animation_player.h"
#include "scene/animation/animation_tree.h"
#include "scene/animation/animation_blend_tree.h"
#include "scene/animation/animation_node_state_machine.h"
#include "scene/resources/animation.h"
#include "scene/resources/animation_library.h"


Dictionary YeetAIDock::_tool_create_animation(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(node);
	if (ap == nullptr) {
		return _make_error("Node is not an AnimationPlayer");
	}

	const String animation_name = _arg_string(p_args, "animation_name", "");
	if (animation_name.is_empty()) {
		return _make_error("animation_name is required");
	}

	if (ap->has_animation(animation_name)) {
		return _make_error(vformat("Animation '%s' already exists in AnimationPlayer", animation_name));
	}

	const double length = _arg_float(p_args, "length", 1.0);
	const bool loop = _arg_bool(p_args, "loop", false);
	const String loop_mode_str = _arg_string(p_args, "loop_mode", "default").to_lower();

	Ref<Animation> anim;
	anim.instantiate();
	anim->set_length(length);

	if (loop) {
		if (loop_mode_str == "linear") {
			anim->set_loop_mode(Animation::LOOP_LINEAR);
		} else if (loop_mode_str == "pingpong") {
			anim->set_loop_mode(Animation::LOOP_PINGPONG);
		} else {
			anim->set_loop_mode(Animation::LOOP_LINEAR);
		}
	} else {
		anim->set_loop_mode(Animation::LOOP_NONE);
	}

	Ref<AnimationLibrary> lib;
	if (ap->has_animation_library("")) {
		lib = ap->get_animation_library("");
	} else {
		lib.instantiate();
		Error lib_err = ap->add_animation_library("", lib);
		if (lib_err != OK) {
			return _make_error("Failed to create default animation library");
		}
	}

	Error add_err = lib->add_animation(animation_name, anim);
	if (add_err != OK) {
		return _make_error(vformat("Failed to add animation '%s' to library", animation_name));
	}

	_mark_unsaved();
	Dictionary extra;
	extra["animation_name"] = animation_name;
	extra["length"] = length;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_add_animation_track(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(node);
	if (ap == nullptr) {
		return _make_error("Node is not an AnimationPlayer");
	}

	const String animation_name = _arg_string(p_args, "animation_name", "");
	if (animation_name.is_empty()) {
		return _make_error("animation_name is required");
	}

	if (!ap->has_animation(animation_name)) {
		return _make_error(vformat("Animation '%s' not found", animation_name));
	}

	Ref<Animation> anim = ap->get_animation(animation_name);
	if (anim.is_null()) {
		return _make_error("Failed to get animation");
	}

	const String track_type_str = _arg_string(p_args, "track_type", "value").to_lower();
	Animation::TrackType track_type = Animation::TYPE_VALUE;
	if (track_type_str == "position") {
		track_type = Animation::TYPE_POSITION_3D;
	} else if (track_type_str == "rotation") {
		track_type = Animation::TYPE_ROTATION_3D;
	} else if (track_type_str == "scale") {
		track_type = Animation::TYPE_SCALE_3D;
	} else if (track_type_str == "bezier") {
		track_type = Animation::TYPE_BEZIER;
	} else if (track_type_str == "method") {
		track_type = Animation::TYPE_METHOD;
	} else if (track_type_str == "audio") {
		track_type = Animation::TYPE_AUDIO;
	} else if (track_type_str == "animation") {
		track_type = Animation::TYPE_ANIMATION;
	} else if (track_type_str == "blend_shape") {
		track_type = Animation::TYPE_BLEND_SHAPE;
	} else {
		track_type = Animation::TYPE_VALUE;
	}

	int track_index = anim->add_track(track_type);

	const String target_node_path = _arg_string(p_args, "target_node_path", "");
	if (!target_node_path.is_empty()) {
		anim->track_set_path(track_index, NodePath(target_node_path));
	}

	if (track_type == Animation::TYPE_VALUE) {
		const String property = _arg_string(p_args, "property", "");
		if (!property.is_empty() && !target_node_path.is_empty()) {
			anim->track_set_path(track_index, NodePath(target_node_path + ":" + property));
		}
	}

	_mark_unsaved();
	Dictionary extra;
	extra["track_index"] = track_index;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_remove_animation_track(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(node);
	if (ap == nullptr) {
		return _make_error("Node is not an AnimationPlayer");
	}

	const String animation_name = _arg_string(p_args, "animation_name", "");
	if (animation_name.is_empty()) {
		return _make_error("animation_name is required");
	}

	if (!ap->has_animation(animation_name)) {
		return _make_error(vformat("Animation '%s' not found", animation_name));
	}

	Ref<Animation> anim = ap->get_animation(animation_name);
	if (anim.is_null()) {
		return _make_error("Failed to get animation");
	}

	const int track_index = _arg_int(p_args, "track_index", -1);
	if (track_index < 0 || track_index >= anim->get_track_count()) {
		return _make_error(vformat("Invalid track_index %d (animation has %d tracks)", track_index, anim->get_track_count()));
	}

	anim->remove_track(track_index);

	_mark_unsaved();
	return _make_ok();
}

Dictionary YeetAIDock::_tool_set_animation_track_key(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(node);
	if (ap == nullptr) {
		return _make_error("Node is not an AnimationPlayer");
	}

	const String animation_name = _arg_string(p_args, "animation_name", "");
	if (animation_name.is_empty()) {
		return _make_error("animation_name is required");
	}

	if (!ap->has_animation(animation_name)) {
		return _make_error(vformat("Animation '%s' not found", animation_name));
	}

	Ref<Animation> anim = ap->get_animation(animation_name);
	if (anim.is_null()) {
		return _make_error("Failed to get animation");
	}

	const int track_index = _arg_int(p_args, "track_index", -1);
	if (track_index < 0 || track_index >= anim->get_track_count()) {
		return _make_error(vformat("Invalid track_index %d (animation has %d tracks)", track_index, anim->get_track_count()));
	}

	const double time = _arg_float(p_args, "time", 0.0);
	const float transition = _arg_float(p_args, "transition", 1.0);

	Animation::TrackType tt = anim->track_get_type(track_index);
	int key_index = -1;

	if (tt == Animation::TYPE_POSITION_3D) {
		Dictionary val_dict = _arg_dict(p_args, "value");
		Vector3 pos;
		if (!val_dict.is_empty()) {
			Variant converted = _variant_from_json(val_dict, Variant::VECTOR3);
			pos = converted;
		}
		key_index = anim->position_track_insert_key(track_index, time, pos);

	} else if (tt == Animation::TYPE_ROTATION_3D) {
		Dictionary val_dict = _arg_dict(p_args, "value");
		Quaternion rot;
		if (!val_dict.is_empty()) {
			Variant converted = _variant_from_json(val_dict, Variant::QUATERNION);
			rot = converted;
		}
		key_index = anim->rotation_track_insert_key(track_index, time, rot);

	} else if (tt == Animation::TYPE_SCALE_3D) {
		Dictionary val_dict = _arg_dict(p_args, "value");
		Vector3 scale;
		if (!val_dict.is_empty()) {
			Variant converted = _variant_from_json(val_dict, Variant::VECTOR3);
			scale = converted;
		}
		key_index = anim->scale_track_insert_key(track_index, time, scale);

	} else if (tt == Animation::TYPE_BLEND_SHAPE) {
		const float blend_val = _arg_float(p_args, "value", 0.0);
		key_index = anim->blend_shape_track_insert_key(track_index, time, blend_val);

	} else if (tt == Animation::TYPE_BEZIER) {
		const float bezier_val = _arg_float(p_args, "value", 0.0);
		key_index = anim->bezier_track_insert_key(track_index, time, bezier_val, Vector2(), Vector2());

	} else {
		Variant value = _variant_from_json(p_args.get("value", Variant()), Variant::NIL);
		key_index = anim->track_insert_key(track_index, time, value, transition);
	}

	if (key_index >= 0 && tt != Animation::TYPE_VALUE && tt != Animation::TYPE_METHOD) {
		anim->track_set_key_transition(track_index, key_index, transition);
	}

	_mark_unsaved();
	Dictionary extra;
	extra["key_index"] = key_index;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_create_blend_tree(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String parent_path = _arg_string(p_args, "parent_path", "");
	Node *parent_node = _resolve_node_target(scene_root, parent_path, err);
	if (parent_node == nullptr) {
		return _make_error(vformat("Parent node not found: %s", err));
	}

	const String node_name = _arg_string(p_args, "name", "AnimationTree");
	const String anim_player_path = _arg_string(p_args, "anim_player_path", "");

	AnimationTree *tree = memnew(AnimationTree);
	tree->set_name(node_name);

	if (!anim_player_path.is_empty()) {
		tree->set_animation_player(NodePath(anim_player_path));
	}

	Ref<AnimationNodeBlendTree> blend_tree;
	blend_tree.instantiate();
	tree->set_root_animation_node(blend_tree);

	_add_to_scene(parent_node, tree, scene_root);

	Dictionary extra;
	extra["node_path"] = String(tree->get_path());
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_add_animation_transition(const Dictionary &p_args) const {
	return _make_error("Animation state machine transitions are complex to modify programmatically. "
					   "Use set_node_property on AnimationTree nodes to configure blend amounts, "
					   "or use add_node with type=AnimationNodeStateMachine and set_node_property "
					   "to configure transitions via the AnimationNodeStateMachineTransition resource properties.");
}

Dictionary YeetAIDock::_tool_set_animation_blend_amount(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	AnimationTree *tree = Object::cast_to<AnimationTree>(node);
	if (tree == nullptr) {
		return _make_error("Node is not an AnimationTree");
	}

	const String parameter_name = _arg_string(p_args, "parameter_name", "");
	if (parameter_name.is_empty()) {
		return _make_error("parameter_name is required");
	}

	const double value = _arg_float(p_args, "value", 0.0);

	const StringName param_name = StringName("parameters/" + parameter_name);
	bool valid = false;
	const Variant old_value = tree->get(param_name, &valid);
	if (!valid) {
		return _make_error("AnimationTree parameter not found: " + String(param_name));
	}

	_commit_ai_property_change(tree, param_name, old_value, value, "Set Animation Blend Amount");
	return _make_ok();
}
