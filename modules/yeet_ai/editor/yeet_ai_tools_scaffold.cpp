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
#include "scene/2d/canvas_modulate.h"
#include "scene/2d/light_2d.h"
#include "scene/2d/physics/area_2d.h"
#include "scene/2d/physics/character_body_2d.h"
#include "scene/2d/physics/collision_shape_2d.h"
#include "scene/2d/sprite_2d.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/timer.h"
#include "scene/gui/button.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/label.h"
#include "scene/gui/texture_progress_bar.h"
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

// ═══════════════════════════════════════════════════════════════════════════
// Scaffold 5: Game HUD (2D)
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_scaffold_game_hud_2d(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "HUD");
	const String parent_path = _arg_string(p_args, "parent_path", "");
	const bool include_health = _arg_bool(p_args, "include_health", true);
	const bool include_score = _arg_bool(p_args, "include_score", true);
	const bool include_timer = _arg_bool(p_args, "include_timer", false);
	const bool include_pause = _arg_bool(p_args, "include_pause", true);

	Node *parent = _resolve_node_target(scene_root, parent_path, err);
	if (parent == nullptr) {
		parent = scene_root;
	}

	CanvasLayer *hud = memnew(CanvasLayer);
	hud->set_name(node_name);
	_add_to_scene(parent, hud, scene_root);

	// ── Health bar ───────────────────────────────────────────────────────────
	if (include_health) {
		TextureProgressBar *health_bar = memnew(TextureProgressBar);
		health_bar->set_name("HealthBar");
		health_bar->set_position(Vector2(20, 20));
		health_bar->set_size(Vector2(200, 20));
		hud->add_child(health_bar, true);
		health_bar->set_owner(scene_root);

		Label *health_label = memnew(Label);
		health_label->set_name("HealthLabel");
		health_label->set_position(Vector2(20, 45));
		health_label->set_text("HP: 100/100");
		hud->add_child(health_label, true);
		health_label->set_owner(scene_root);
	}

	// ── Score label ──────────────────────────────────────────────────────────
	if (include_score) {
		Label *score_label = memnew(Label);
		score_label->set_name("ScoreLabel");
		score_label->set_position(Vector2(20, include_health ? 70 : 20));
		score_label->set_text("Score: 0");
		hud->add_child(score_label, true);
		score_label->set_owner(scene_root);
	}

	// ── Timer label ──────────────────────────────────────────────────────────
	if (include_timer) {
		Label *timer_label = memnew(Label);
		timer_label->set_name("TimerLabel");
		timer_label->set_position(Vector2(20, include_health ? 100 : 50));
		timer_label->set_text("Time: 0:00");
		hud->add_child(timer_label, true);
		timer_label->set_owner(scene_root);
	}

	// ── Pause button ─────────────────────────────────────────────────────────
	if (include_pause) {
		Button *pause_btn = memnew(Button);
		pause_btn->set_name("PauseButton");
		pause_btn->set_position(Vector2(700, 20));
		pause_btn->set_size(Vector2(80, 40));
		pause_btn->set_text("Pause");
		hud->add_child(pause_btn, true);
		pause_btn->set_owner(scene_root);
	}

	_mark_unsaved();

	Dictionary manifest;
	manifest["ok"] = true;
	manifest["node_path"] = String(hud->get_path());
	manifest["type"] = "CanvasLayer";
	manifest["components"] = Array::make(
		include_health ? "TextureProgressBar" : "",
		include_health ? "Label(Health)" : "",
		include_score ? "Label(Score)" : "",
		include_timer ? "Label(Timer)" : "",
		include_pause ? "Button(Pause)" : ""
	);
	manifest["next_steps"] = Array::make(
		"Attach a script to HUD that exposes update_health(), update_score(), update_timer() methods",
		"Connect PauseButton.pressed to get_tree().paused toggle",
		"Reference HUD from player/enemy scripts to call update methods"
	);

	return manifest;
}

// ═══════════════════════════════════════════════════════════════════════════
// Scaffold 6: Main Menu (2D)
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_scaffold_main_menu_2d(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "MainMenu");
	const String parent_path = _arg_string(p_args, "parent_path", "");
	const String title_text = _arg_string(p_args, "title", "My Game");
	const bool include_options = _arg_bool(p_args, "include_options", true);
	const bool include_credits = _arg_bool(p_args, "include_credits", true);

	Node *parent = _resolve_node_target(scene_root, parent_path, err);
	if (parent == nullptr) {
		parent = scene_root;
	}

	CanvasLayer *menu = memnew(CanvasLayer);
	menu->set_name(node_name);
	_add_to_scene(parent, menu, scene_root);

	// ── Background ───────────────────────────────────────────────────────────
	ColorRect *bg = memnew(ColorRect);
	bg->set_name("Background");
	bg->set_anchors_preset(Control::PRESET_FULL_RECT);
	bg->set_color(Color(0.1, 0.1, 0.15));
	menu->add_child(bg, true);
	bg->set_owner(scene_root);

	// ── Title ────────────────────────────────────────────────────────────────
	Label *title = memnew(Label);
	title->set_name("Title");
	title->set_position(Vector2(0, 100));
	title->set_size(Vector2(800, 80));
	title->set_text(title_text);
	title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	menu->add_child(title, true);
	title->set_owner(scene_root);

	// ── Start button ─────────────────────────────────────────────────────────
	Button *start_btn = memnew(Button);
	start_btn->set_name("StartButton");
	start_btn->set_position(Vector2(300, 250));
	start_btn->set_size(Vector2(200, 50));
	start_btn->set_text("Start Game");
	menu->add_child(start_btn, true);
	start_btn->set_owner(scene_root);

	// ── Options button ───────────────────────────────────────────────────────
	if (include_options) {
		Button *options_btn = memnew(Button);
		options_btn->set_name("OptionsButton");
		options_btn->set_position(Vector2(300, 320));
		options_btn->set_size(Vector2(200, 50));
		options_btn->set_text("Options");
		menu->add_child(options_btn, true);
		options_btn->set_owner(scene_root);
	}

	// ── Credits button ───────────────────────────────────────────────────────
	if (include_credits) {
		Button *credits_btn = memnew(Button);
		credits_btn->set_name("CreditsButton");
		credits_btn->set_position(Vector2(300, 390));
		credits_btn->set_size(Vector2(200, 50));
		credits_btn->set_text("Credits");
		menu->add_child(credits_btn, true);
		credits_btn->set_owner(scene_root);
	}

	// ── Quit button ──────────────────────────────────────────────────────────
	Button *quit_btn = memnew(Button);
	quit_btn->set_name("QuitButton");
	quit_btn->set_position(Vector2(300, 460));
	quit_btn->set_size(Vector2(200, 50));
	quit_btn->set_text("Quit");
	menu->add_child(quit_btn, true);
	quit_btn->set_owner(scene_root);

	_mark_unsaved();

	Dictionary manifest;
	manifest["ok"] = true;
	manifest["node_path"] = String(menu->get_path());
	manifest["type"] = "CanvasLayer";
	manifest["components"] = Array::make("ColorRect", "Label", "Button(Start)", include_options ? "Button(Options)" : "", include_credits ? "Button(Credits)" : "", "Button(Quit)");
	manifest["next_steps"] = Array::make(
		"Attach script to MainMenu with _on_start_pressed(), _on_options_pressed(), etc.",
		"Connect button signals to script methods",
		"Start button should call get_tree().change_scene_to_file() for first level",
		"Quit button should call get_tree().quit()"
	);

	return manifest;
}

// ═══════════════════════════════════════════════════════════════════════════
// Scaffold 7: Pause Menu (2D)
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_scaffold_pause_menu_2d(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "PauseMenu");
	const String parent_path = _arg_string(p_args, "parent_path", "");

	Node *parent = _resolve_node_target(scene_root, parent_path, err);
	if (parent == nullptr) {
		parent = scene_root;
	}

	CanvasLayer *menu = memnew(CanvasLayer);
	menu->set_name(node_name);
	menu->set_layer(100); // On top of everything
	_add_to_scene(parent, menu, scene_root);

	// ── Semi-transparent overlay ─────────────────────────────────────────────
	ColorRect *overlay = memnew(ColorRect);
	overlay->set_name("Overlay");
	overlay->set_anchors_preset(Control::PRESET_FULL_RECT);
	overlay->set_color(Color(0, 0, 0, 0.7));
	menu->add_child(overlay, true);
	overlay->set_owner(scene_root);

	// ── Pause text ───────────────────────────────────────────────────────────
	Label *pause_text = memnew(Label);
	pause_text->set_name("PauseText");
	pause_text->set_position(Vector2(0, 150));
	pause_text->set_size(Vector2(800, 60));
	pause_text->set_text("PAUSED");
	pause_text->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	menu->add_child(pause_text, true);
	pause_text->set_owner(scene_root);

	// ── Resume button ────────────────────────────────────────────────────────
	Button *resume_btn = memnew(Button);
	resume_btn->set_name("ResumeButton");
	resume_btn->set_position(Vector2(300, 250));
	resume_btn->set_size(Vector2(200, 50));
	resume_btn->set_text("Resume");
	menu->add_child(resume_btn, true);
	resume_btn->set_owner(scene_root);

	// ── Restart button ───────────────────────────────────────────────────────
	Button *restart_btn = memnew(Button);
	restart_btn->set_name("RestartButton");
	restart_btn->set_position(Vector2(300, 320));
	restart_btn->set_size(Vector2(200, 50));
	restart_btn->set_text("Restart");
	menu->add_child(restart_btn, true);
	restart_btn->set_owner(scene_root);

	// ── Quit to menu button ──────────────────────────────────────────────────
	Button *quit_btn = memnew(Button);
	quit_btn->set_name("QuitToMenuButton");
	quit_btn->set_position(Vector2(300, 390));
	quit_btn->set_size(Vector2(200, 50));
	quit_btn->set_text("Quit to Menu");
	menu->add_child(quit_btn, true);
	quit_btn->set_owner(scene_root);

	// ── Hide by default ──────────────────────────────────────────────────────
	menu->set_visible(false);

	_mark_unsaved();

	Dictionary manifest;
	manifest["ok"] = true;
	manifest["node_path"] = String(menu->get_path());
	manifest["type"] = "CanvasLayer";
	manifest["components"] = Array::make("ColorRect(Overlay)", "Label", "Button(Resume)", "Button(Restart)", "Button(QuitToMenu)");
	manifest["next_steps"] = Array::make(
		"Attach script to PauseMenu with _on_resume_pressed(), _on_restart_pressed(), _on_quit_pressed()",
		"Connect button signals to script methods",
		"Resume: get_tree().paused = false + hide()",
		"Restart: get_tree().reload_current_scene()",
		"Quit: get_tree().change_scene_to_file(main_menu.tscn)",
		"Show pause menu when ESC is pressed (Input.is_action_just_pressed('ui_cancel'))"
	);

	return manifest;
}

// ═══════════════════════════════════════════════════════════════════════════
// Scaffold 8: Lighting Rig (2D)
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_scaffold_lighting_rig_2d(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "LightingRig");
	const String parent_path = _arg_string(p_args, "parent_path", "");
	const Color ambient_color = _arg_color(p_args, "ambient_color", Color(0.1, 0.1, 0.2));
	const float ambient_energy = _arg_float(p_args, "ambient_energy", 0.3);
	const bool include_directional = _arg_bool(p_args, "include_directional", true);
	const bool include_point_lights = _arg_bool(p_args, "include_point_lights", false);

	Node *parent = _resolve_node_target(scene_root, parent_path, err);
	if (parent == nullptr) {
		parent = scene_root;
	}

	Node2D *rig = memnew(Node2D);
	rig->set_name(node_name);
	_add_to_scene(parent, rig, scene_root);

	// ── CanvasModulate for ambient light ─────────────────────────────────────
	CanvasModulate *ambient = memnew(CanvasModulate);
	ambient->set_name("AmbientLight");
	ambient->set_color(ambient_color);
	rig->add_child(ambient, true);
	ambient->set_owner(scene_root);

	// ── DirectionalLight2D (sun/moon) ────────────────────────────────────────
	if (include_directional) {
		DirectionalLight2D *sun = memnew(DirectionalLight2D);
		sun->set_name("DirectionalLight");
		sun->set_energy(1.0);
		sun->set_color(Color(1, 1, 0.9));
		sun->set_blend_mode(Light2D::BLEND_MODE_ADD);
		sun->set_shadow_enabled(true);
		rig->add_child(sun, true);
		sun->set_owner(scene_root);
	}

	// ── Point lights (for torches/lamps) ─────────────────────────────────────
	if (include_point_lights) {
		for (int i = 0; i < 3; i++) {
			PointLight2D *light = memnew(PointLight2D);
			light->set_name("PointLight" + itos(i + 1));
			light->set_position(Vector2(200 + i * 200, 200));
			light->set_energy(0.8);
			light->set_texture_scale(2.0);
			light->set_color(Color(1, 0.8, 0.5));
			light->set_shadow_enabled(true);
			rig->add_child(light, true);
			light->set_owner(scene_root);
		}
	}

	_mark_unsaved();

	Dictionary manifest;
	manifest["ok"] = true;
	manifest["node_path"] = String(rig->get_path());
	manifest["type"] = "Node2D";
	manifest["components"] = Array::make("CanvasModulate", include_directional ? "DirectionalLight2D" : "", include_point_lights ? "PointLight2D(x3)" : "");
	manifest["properties"] = Dictionary();
	manifest["properties"]["ambient_color"] = ambient_color;
	manifest["properties"]["ambient_energy"] = ambient_energy;
	manifest["next_steps"] = Array::make(
		"Adjust CanvasModulate color for day/night cycle",
		"Enable shadows on lights for depth",
		"Add LightOccluder2D to walls/platforms for shadow casting",
		"Animate DirectionalLight2D energy/color for time-of-day effects"
	);

	return manifest;
}
