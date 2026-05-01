/**************************************************************************/
/*  yeet_ai_tools_scaffold.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/
/*  Composite (scaffold) tools that create complete game objects in a     */
/*  single tool call by orchestrating multiple low-level operations.      */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "scene/2d/animated_sprite_2d.h"
#include "scene/2d/camera_2d.h"
#include "scene/2d/light_2d.h"
#include "scene/2d/physics/area_2d.h"
#include "scene/2d/physics/character_body_2d.h"
#include "scene/2d/physics/collision_shape_2d.h"
#include "scene/2d/sprite_2d.h"
#include "scene/main/timer.h"
#include "scene/resources/2d/capsule_shape_2d.h"
#include "scene/resources/2d/circle_shape_2d.h"
#include "scene/resources/2d/rectangle_shape_2d.h"
#include "scene/resources/sprite_frames.h"

// ═══════════════════════════════════════════════════════════════════════════
// Helper: create a basic CollisionShape2D child
// ═══════════════════════════════════════════════════════════════════════════

static CollisionShape2D *_create_collision_shape_2d_child(Node *p_owner, Node *p_parent, const String &p_name, const Vector2 &p_position) {
	CollisionShape2D *cs = memnew(CollisionShape2D);
	cs->set_name(p_name);
	cs->set_position(p_position);
	p_parent->add_child(cs, true);
	cs->set_owner(p_owner);
	return cs;
}

// ═══════════════════════════════════════════════════════════════════════════
// Scaffold 1: Platformer Player (2D)
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_scaffold_platformer_player_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "Player");
	const String parent_path = _arg_string(p_args, "parent_path", "");
	const Vector2 position = _arg_vector2(p_args, "position", Vector2(100, 100));
	const float gravity = _arg_float(p_args, "gravity", 980.0);
	const float jump_velocity = _arg_float(p_args, "jump_velocity", -400.0);
	const float speed = _arg_float(p_args, "speed", 300.0);
	const bool include_camera = _arg_bool(p_args, "include_camera", true);
	const String sprite_texture = _arg_string(p_args, "sprite_texture", "");

	Node *parent = _resolve_node_target(scene_root, parent_path, err);
	if (parent == nullptr) {
		parent = scene_root;
	}

	// ── 1. Create CharacterBody2D ────────────────────────────────────────────
	CharacterBody2D *player = memnew(CharacterBody2D);
	player->set_name(node_name);
	player->set_position(position);
	_add_to_scene(parent, player, scene_root);

	// ── 2. Add collision shape ───────────────────────────────────────────────
	CollisionShape2D *collision = memnew(CollisionShape2D);
	collision->set_name("CollisionShape2D");
	Ref<CapsuleShape2D> shape;
	shape.instantiate();
	shape->set_radius(16.0);
	shape->set_height(48.0);
	collision->set_shape(shape);
	player->add_child(collision, true);
	collision->set_owner(scene_root);

	// ── 3. Add sprite ────────────────────────────────────────────────────────
	Sprite2D *sprite = memnew(Sprite2D);
	sprite->set_name("Sprite2D");
	if (!sprite_texture.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(sprite_texture);
		if (tex.is_valid()) {
			sprite->set_texture(tex);
		}
	}
	player->add_child(sprite, true);
	sprite->set_owner(scene_root);

	// ── 4. Add camera (optional) ─────────────────────────────────────────────
	Camera2D *camera = nullptr;
	if (include_camera) {
		camera = memnew(Camera2D);
		camera->set_name("Camera2D");
		player->add_child(camera, true);
		camera->set_owner(scene_root);
	}

	_mark_unsaved();

	// ── Result manifest ──────────────────────────────────────────────────────
	Dictionary manifest;
	manifest["ok"] = true;
	manifest["node_path"] = String(player->get_path());
	manifest["type"] = "CharacterBody2D";
	manifest["components"] = Array::make("CollisionShape2D", "Sprite2D", include_camera ? "Camera2D" : "");
	manifest["properties"] = Dictionary();
	manifest["properties"]["gravity"] = gravity;
	manifest["properties"]["jump_velocity"] = jump_velocity;
	manifest["properties"]["speed"] = speed;
	manifest["next_steps"] = Array::make(
		"Create input actions: move_left (A/Left), move_right (D/Right), jump (Space/W)",
		"Attach a GDScript with _physics_process implementing movement + jump",
		"Set project main scene if this is the player scene"
	);

	return manifest;
}

// ═══════════════════════════════════════════════════════════════════════════
// Scaffold 2: Patrol Enemy (2D)
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_scaffold_patrol_enemy_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "PatrolEnemy");
	const String parent_path = _arg_string(p_args, "parent_path", "");
	const Vector2 position = _arg_vector2(p_args, "position", Vector2(300, 100));
	const float patrol_distance = _arg_float(p_args, "patrol_distance", 200.0);
	const float speed = _arg_float(p_args, "speed", 100.0);
	const String sprite_texture = _arg_string(p_args, "sprite_texture", "");

	Node *parent = _resolve_node_target(scene_root, parent_path, err);
	if (parent == nullptr) {
		parent = scene_root;
	}

	// ── 1. Create CharacterBody2D ────────────────────────────────────────────
	CharacterBody2D *enemy = memnew(CharacterBody2D);
	enemy->set_name(node_name);
	enemy->set_position(position);
	_add_to_scene(parent, enemy, scene_root);

	// ── 2. Add collision shape ───────────────────────────────────────────────
	CollisionShape2D *collision = memnew(CollisionShape2D);
	collision->set_name("CollisionShape2D");
	Ref<RectangleShape2D> shape;
	shape.instantiate();
	shape->set_size(Vector2(32, 48));
	collision->set_shape(shape);
	enemy->add_child(collision, true);
	collision->set_owner(scene_root);

	// ── 3. Add sprite ────────────────────────────────────────────────────────
	Sprite2D *sprite = memnew(Sprite2D);
	sprite->set_name("Sprite2D");
	if (!sprite_texture.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(sprite_texture);
		if (tex.is_valid()) {
			sprite->set_texture(tex);
		}
	}
	enemy->add_child(sprite, true);
	sprite->set_owner(scene_root);

	// ── 4. Add floor detector (RayCast2D) ────────────────────────────────────
	RayCast2D *floor_detector = memnew(RayCast2D);
	floor_detector->set_name("FloorDetector");
	floor_detector->set_target_position(Vector2(patrol_distance * 0.5f, 64.0));
	floor_detector->set_enabled(true);
	enemy->add_child(floor_detector, true);
	floor_detector->set_owner(scene_root);

	// ── 5. Add player detection Area2D ───────────────────────────────────────
	Area2D *detection = memnew(Area2D);
	detection->set_name("PlayerDetection");
	CollisionShape2D *detection_shape = memnew(CollisionShape2D);
	Ref<CircleShape2D> circle;
	circle.instantiate();
	circle->set_radius(150.0);
	detection_shape->set_shape(circle);
	detection->add_child(detection_shape, true);
	detection_shape->set_owner(scene_root);
	enemy->add_child(detection, true);
	detection->set_owner(scene_root);

	_mark_unsaved();

	Dictionary manifest;
	manifest["ok"] = true;
	manifest["node_path"] = String(enemy->get_path());
	manifest["type"] = "CharacterBody2D";
	manifest["components"] = Array::make("CollisionShape2D", "Sprite2D", "RayCast2D", "Area2D");
	manifest["properties"] = Dictionary();
	manifest["properties"]["patrol_distance"] = patrol_distance;
	manifest["properties"]["speed"] = speed;
	manifest["next_steps"] = Array::make(
		"Attach a GDScript with _physics_process for patrol logic",
		"Implement state machine: idle -> patrol -> chase",
		"Connect PlayerDetection.body_entered to detect player",
		"Add damage dealing on collision with player"
	);

	return manifest;
}

// ═══════════════════════════════════════════════════════════════════════════
// Scaffold 3: Collectible Item (2D)
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_scaffold_collectible_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "Coin");
	const String parent_path = _arg_string(p_args, "parent_path", "");
	const Vector2 position = _arg_vector2(p_args, "position", Vector2(200, 200));
	const String collectible_type = _arg_string(p_args, "collectible_type", "coin"); // coin, gem, heart, star
	const bool animate_bob = _arg_bool(p_args, "animate_bob", true);
	const String sprite_texture = _arg_string(p_args, "sprite_texture", "");

	Node *parent = _resolve_node_target(scene_root, parent_path, err);
	if (parent == nullptr) {
		parent = scene_root;
	}

	// ── 1. Create Area2D (trigger, not physics body) ─────────────────────────
	Area2D *item = memnew(Area2D);
	item->set_name(node_name);
	item->set_position(position);
	_add_to_scene(parent, item, scene_root);

	// ── 2. Add collision shape ───────────────────────────────────────────────
	CollisionShape2D *collision = memnew(CollisionShape2D);
	collision->set_name("CollisionShape2D");
	Ref<CircleShape2D> shape;
	shape.instantiate();
	shape->set_radius(16.0);
	collision->set_shape(shape);
	item->add_child(collision, true);
	collision->set_owner(scene_root);

	// ── 3. Add sprite ────────────────────────────────────────────────────────
	Sprite2D *sprite = memnew(Sprite2D);
	sprite->set_name("Sprite2D");
	if (!sprite_texture.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(sprite_texture);
		if (tex.is_valid()) {
			sprite->set_texture(tex);
		}
	}
	item->add_child(sprite, true);
	sprite->set_owner(scene_root);

	// ── 4. Add bobbing animation (Tween-based via Timer + script hint) ───────
	if (animate_bob) {
		Timer *bob_timer = memnew(Timer);
		bob_timer->set_name("BobTimer");
		bob_timer->set_wait_time(1.0);
		bob_timer->set_autostart(true);
		item->add_child(bob_timer, true);
		bob_timer->set_owner(scene_root);
	}

	_mark_unsaved();

	Dictionary manifest;
	manifest["ok"] = true;
	manifest["node_path"] = String(item->get_path());
	manifest["type"] = "Area2D";
	manifest["collectible_type"] = collectible_type;
	manifest["components"] = Array::make("CollisionShape2D", "Sprite2D", animate_bob ? "Timer" : "");
	manifest["next_steps"] = Array::make(
		"Connect body_entered signal to player collection logic",
		"Add sound effect on pickup (AudioStreamPlayer2D)",
		"Add particle effect on pickup (CPUParticles2D)",
		"Implement score/health increment in collection handler"
	);

	return manifest;
}

// ═══════════════════════════════════════════════════════════════════════════
// Scaffold 4: Moving Platform (2D)
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_scaffold_moving_platform_2d(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "MovingPlatform");
	const String parent_path = _arg_string(p_args, "parent_path", "");
	const Vector2 position = _arg_vector2(p_args, "position", Vector2(400, 300));
	const Vector2 size = _arg_vector2(p_args, "size", Vector2(128, 32));
	const Vector2 move_offset = _arg_vector2(p_args, "move_offset", Vector2(200, 0));
	const float move_duration = _arg_float(p_args, "move_duration", 2.0);
	const String sprite_texture = _arg_string(p_args, "sprite_texture", "");

	Node *parent = _resolve_node_target(scene_root, parent_path, err);
	if (parent == nullptr) {
		parent = scene_root;
	}

	// ── 1. Create AnimatableBody2D ───────────────────────────────────────────
	AnimatableBody2D *platform = memnew(AnimatableBody2D);
	platform->set_name(node_name);
	platform->set_position(position);
	_add_to_scene(parent, platform, scene_root);

	// ── 2. Add collision shape ───────────────────────────────────────────────
	CollisionShape2D *collision = memnew(CollisionShape2D);
	collision->set_name("CollisionShape2D");
	Ref<RectangleShape2D> shape;
	shape.instantiate();
	shape->set_size(size);
	collision->set_shape(shape);
	platform->add_child(collision, true);
	collision->set_owner(scene_root);

	// ── 3. Add sprite ────────────────────────────────────────────────────────
	Sprite2D *sprite = memnew(Sprite2D);
	sprite->set_name("Sprite2D");
	if (!sprite_texture.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(sprite_texture);
		if (tex.is_valid()) {
			sprite->set_texture(tex);
		}
	}
	platform->add_child(sprite, true);
	sprite->set_owner(scene_root);

	_mark_unsaved();

	Dictionary manifest;
	manifest["ok"] = true;
	manifest["node_path"] = String(platform->get_path());
	manifest["type"] = "AnimatableBody2D";
	manifest["components"] = Array::make("CollisionShape2D", "Sprite2D");
	manifest["properties"] = Dictionary();
	manifest["properties"]["move_offset"] = move_offset;
	manifest["properties"]["move_duration"] = move_duration;
	manifest["next_steps"] = Array::make(
		"Create a Tween in _ready() to animate between position and position + move_offset",
		"Set Tween to ping-pong (set_loops() + set_trans()) for back-and-forth motion",
		"Ensure collision layer matches player's floor detection"
	);

	return manifest;
}
