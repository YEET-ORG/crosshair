const fs = require('fs');
const path = require('path');

const outDir = __dirname;

function esc(s) {
  return s.replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n').replace(/\r/g, '').replace(/\t/g, '\\t');
}

const gdscriptPatterns = [
  {
    pattern_name: "2D Area2D top-down movement with AnimatedSprite2D",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/dodge_the_creeps/player.gd",
    extends_class: "Area2D",
    script_content: `extends Area2D

signal hit

@export var speed = 400
var screen_size

func _ready():
\tscreen_size = get_viewport_rect().size
\thide()

func _process(delta):
\tvar velocity = Vector2.ZERO
\tif Input.is_action_pressed(&"move_right"):
\t\tvelocity.x += 1
\tif Input.is_action_pressed(&"move_left"):
\t\tvelocity.x -= 1
\tif Input.is_action_pressed(&"move_down"):
\t\tvelocity.y += 1
\tif Input.is_action_pressed(&"move_up"):
\t\tvelocity.y -= 1

\tif velocity.length() > 0:
\t\tvelocity = velocity.normalized() * speed
\t\t$AnimatedSprite2D.play()
\telse:
\t\t$AnimatedSprite2D.stop()

\tposition += velocity * delta
\tposition = position.clamp(Vector2.ZERO, screen_size)

\tif velocity.x != 0:
\t\t$AnimatedSprite2D.animation = &"right"
\t\t$AnimatedSprite2D.flip_v = false
\t\t$Trail.rotation = 0
\t\t$AnimatedSprite2D.flip_h = velocity.x < 0
\telif velocity.y != 0:
\t\t$AnimatedSprite2D.animation = &"up"
\t\trotation = PI if velocity.y > 0 else 0

func start(pos):
\tposition = pos
\trotation = 0
\tshow()
\t$CollisionShape2D.disabled = false

func _on_body_entered(_body):
\thide()
\thit.emit()
\t$CollisionShape2D.set_deferred(&"disabled", true)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Area2D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AnimatedSprite2D", node_name: "AnimatedSprite2D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "circle", parameters: {radius: 20}}},
      {tool: "create_input_action", arguments: {action_name: "move_right", events: [{type: "key", "physical_keycode": "D"}]}},
      {tool: "create_input_action", arguments: {action_name: "move_left", events: [{type: "key", "physical_keycode": "A"}]}},
      {tool: "create_input_action", arguments: {action_name: "move_up", events: [{type: "key", "physical_keycode": "W"}]}},
      {tool: "create_input_action", arguments: {action_name: "move_down", events: [{type: "key", "physical_keycode": "S"}]}},
      {tool: "attach_script", arguments: {node_path: "Player", script_content: "extends Area2D\n\nsignal hit\n\n@export var speed = 400\nvar screen_size\n\nfunc _ready():\n\tscreen_size = get_viewport_rect().size\n\nfunc _process(delta):\n\tvar velocity = Vector2.ZERO\n\tif Input.is_action_pressed(&\"move_right\"):\n\t\tvelocity.x += 1\n\tif Input.is_action_pressed(&\"move_left\"):\n\t\tvelocity.x -= 1\n\tif Input.is_action_pressed(&\"move_down\"):\n\t\tvelocity.y += 1\n\tif Input.is_action_pressed(&\"move_up\"):\n\t\tvelocity.y -= 1\n\n\tif velocity.length() > 0:\n\t\tvelocity = velocity.normalized() * speed\n\tmove_and_slide()"}}
    ],
    description: "2D top-down movement using Area2D with WASD input, sprite flipping, and collision detection via signals"
  },
  {
    pattern_name: "HUD CanvasLayer with score, messages, and buttons",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/dodge_the_creeps/hud.gd",
    extends_class: "CanvasLayer",
    script_content: `extends CanvasLayer

signal start_game

func show_message(text):
\t$MessageLabel.text = text
\t$MessageLabel.show()
\t$MessageTimer.start()

func show_game_over():
\tshow_message("Game Over")
\tawait $MessageTimer.timeout
\t$MessageLabel.text = "Dodge the\\nCreeps"
\t$MessageLabel.show()
\tawait get_tree().create_timer(1).timeout
\t$StartButton.show()

func update_score(score):
\t$ScoreLabel.text = str(score)

func _on_StartButton_pressed():
\t$StartButton.hide()
\tstart_game.emit()

func _on_MessageTimer_timeout():
\t$MessageLabel.hide()`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CanvasLayer", node_name: "HUD"}},
      {tool: "add_child_node", arguments: {parent_path: "HUD", node_type: "Label", node_name: "ScoreLabel"}},
      {tool: "add_child_node", arguments: {parent_path: "HUD", node_type: "Label", node_name: "MessageLabel"}},
      {tool: "add_child_node", arguments: {parent_path: "HUD", node_type: "Button", node_name: "StartButton"}},
      {tool: "add_child_node", arguments: {parent_path: "HUD", node_type: "Timer", node_name: "MessageTimer"}},
      {tool: "attach_script", arguments: {node_path: "HUD", script_content: "extends CanvasLayer\n\nsignal start_game"}}
    ],
    description: "HUD overlay using CanvasLayer with score display, timed messages, and start button"
  },
  {
    pattern_name: "Game spawner main scene with timers and PackedScene instantiation",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/dodge_the_creeps/main.gd",
    extends_class: "Node",
    script_content: `extends Node

@export var mob_scene: PackedScene
var score

func game_over():
\t$ScoreTimer.stop()
\t$MobTimer.stop()
\t$HUD.show_game_over()
\t$Music.stop()
\t$DeathSound.play()

func new_game():
\tget_tree().call_group(&"mobs", &"queue_free")
\tscore = 0
\t$Player.start($StartPosition.position)
\t$StartTimer.start()
\t$HUD.update_score(score)
\t$HUD.show_message("Get Ready")
\t$Music.play()

func _on_MobTimer_timeout():
\tvar mob = mob_scene.instantiate()
\tvar mob_spawn_location = get_node(^"MobPath/MobSpawnLocation")
\tmob_spawn_location.progress_ratio = randf()
\tmob.position = mob_spawn_location.position
\tvar direction = mob_spawn_location.rotation + PI / 2
\tdirection += randf_range(-PI / 4, PI / 4)
\tmob.rotation = direction
\tvar velocity = Vector2(randf_range(150.0, 250.0), 0.0)
\tmob.linear_velocity = velocity.rotated(direction)
\tadd_child(mob)

func _on_ScoreTimer_timeout():
\tscore += 1
\t$HUD.update_score(score)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Node", node_name: "Main"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "Timer", node_name: "MobTimer"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "Timer", node_name: "ScoreTimer"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "Timer", node_name: "StartTimer"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "AudioStreamPlayer", node_name: "Music"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "AudioStreamPlayer", node_name: "DeathSound"}}
    ],
    description: "Main game scene orchestrator with mob spawning along Path2D, score timer, and audio management"
  },
  {
    pattern_name: "RigidBody2D enemy with auto-cleanup on screen exit",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/dodge_the_creeps/mob.gd",
    extends_class: "RigidBody2D",
    script_content: `extends RigidBody2D

func _ready():
\tvar mob_types = Array($AnimatedSprite2D.sprite_frames.get_animation_names())
\t$AnimatedSprite2D.animation = mob_types.pick_random()
\t$AnimatedSprite2D.play()

func _on_VisibilityNotifier2D_screen_exited():
\tqueue_free()`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "RigidBody2D", node_name: "Mob"}},
      {tool: "add_child_node", arguments: {parent_path: "Mob", node_type: "AnimatedSprite2D", node_name: "AnimatedSprite2D"}},
      {tool: "add_child_node", arguments: {parent_path: "Mob", node_type: "VisibleOnScreenNotifier2D", node_name: "VisibilityNotifier2D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Mob", node_name: "CollisionShape2D", shape_type: "capsule"}}
    ],
    description: "RigidBody2D mob that picks random animation and auto-frees when off-screen using VisibilityNotifier2D"
  },
  {
    pattern_name: "State machine base class with enter/exit/handle_input/update",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/finite_state_machine/state_machine/state.gd",
    extends_class: "Node",
    script_content: `extends Node

signal finished(next_state_name: StringName)

func enter() -> void:
\tpass

func exit() -> void:
\tpass

func handle_input(_input_event: InputEvent) -> void:
\tpass

func update(_delta: float) -> void:
\tpass

func _on_animation_finished(_anim_name: String) -> void:
\tpass`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Node", node_name: "State"}},
      {tool: "attach_script", arguments: {node_path: "State", script_content: "extends Node\n\nsignal finished(next_state_name: StringName)\n\nfunc enter() -> void:\n\tpass\n\nfunc exit() -> void:\n\tpass\n\nfunc handle_input(_input_event: InputEvent) -> void:\n\tpass\n\nfunc update(_delta: float) -> void:\n\tpass"}}
    ],
    description: "Abstract state base class with enter/exit/handle_input/update pattern for state machines"
  },
  {
    pattern_name: "State machine controller with stack-based state transitions",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/finite_state_machine/state_machine/state_machine.gd",
    extends_class: "Node",
    script_content: `extends Node

signal state_changed(current_state: Node)

@export var start_state: NodePath
var states_map := {}
var states_stack := []
var current_state: Node = null
var _active: bool = false:
\tset(value):
\t\t_active = value
\t\tset_active(value)

func _enter_tree() -> void:
\tif start_state.is_empty():
\t\tstart_state = get_child(0).get_path()
\tfor child in get_children():
\t\tvar err: bool = child.finished.connect(_change_state)
\t\tif err:
\t\t\tprinterr(err)
\tinitialize(start_state)

func initialize(initial_state: NodePath) -> void:
\t_active = true
\tstates_stack.push_front(get_node(initial_state))
\tcurrent_state = states_stack[0]
\tcurrent_state.enter()

func set_active(value: bool) -> void:
\tset_physics_process(value)
\tset_process_input(value)
\tif not _active:
\t\tstates_stack = []
\t\tcurrent_state = null

func _unhandled_input(input_event: InputEvent) -> void:
\tcurrent_state.handle_input(input_event)

func _physics_process(delta: float) -> void:
\tcurrent_state.update(delta)

func _change_state(state_name: String) -> void:
\tif not _active:
\t\treturn
\tcurrent_state.exit()
\tif state_name == "previous":
\t\tstates_stack.pop_front()
\telse:
\t\tstates_stack[0] = states_map[state_name]
\tcurrent_state = states_stack[0]
\tstate_changed.emit(current_state)
\tif state_name != "previous":
\t\tcurrent_state.enter()`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Node", node_name: "StateMachine"}},
      {tool: "attach_script", arguments: {node_path: "StateMachine", script_content: "extends Node\n\nsignal state_changed(current_state: Node)\n\n@export var start_state: NodePath\nvar states_map := {}\nvar states_stack := []\nvar current_state: Node = null"}}
    ],
    description: "Stack-based state machine with pushdown automaton support, signal-driven state transitions"
  },
  {
    pattern_name: "Idle state extending ground state with animation and transitions",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/finite_state_machine/player/states/motion/on_ground/idle.gd",
    extends_class: "on_ground.gd",
    script_content: `extends "on_ground.gd"

func enter() -> void:
\towner.get_node(^"AnimationPlayer").play(PLAYER_STATE.idle)

func handle_input(input_event: InputEvent) -> void:
\treturn super.handle_input(input_event)

func update(_delta: float) -> void:
\tvar input_direction: Vector2 = get_input_direction()
\tif input_direction:
\t\tfinished.emit(PLAYER_STATE.move)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Node", node_name: "Idle"}},
      {tool: "attach_script", arguments: {node_path: "Idle", script_content: "extends \"on_ground.gd\"\n\nfunc enter() -> void:\n\towner.get_node(^\"AnimationPlayer\").play(PLAYER_STATE.idle)\n\nfunc update(_delta: float) -> void:\n\tvar input_direction: Vector2 = get_input_direction()\n\tif input_direction:\n\t\tfinished.emit(PLAYER_STATE.move)"}}
    ],
    description: "Idle state inheriting from on_ground state, transitions to move when directional input detected"
  },
  {
    pattern_name: "Move state with walk/run speeds and CharacterBody2D movement",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/finite_state_machine/player/states/motion/on_ground/move.gd",
    extends_class: "on_ground.gd",
    script_content: `extends "on_ground.gd"

@export var max_walk_speed := 450.0
@export var max_run_speed := 700.0

func enter() -> void:
\tspeed = 0.0
\tvelocity = Vector2()
\tvar input_direction := get_input_direction()
\tupdate_look_direction(input_direction)
\towner.get_node(^"AnimationPlayer").play(PLAYER_STATE.walk)

func handle_input(input_event: InputEvent) -> void:
\treturn super.handle_input(input_event)

func update(_delta: float) -> void:
\tvar input_direction := get_input_direction()
\tif input_direction.is_zero_approx():
\t\tfinished.emit(PLAYER_STATE.idle)
\tupdate_look_direction(input_direction)

\tif Input.is_action_pressed(&"run"):
\t\tspeed = max_run_speed
\telse:
\t\tspeed = max_walk_speed

\tvar collision_info := move(speed, input_direction)
\tif not collision_info:
\t\treturn
\tif speed == max_run_speed and collision_info.collider.is_in_group(&"environment"):
\t\treturn

func move(p_speed: float, direction: Vector2) -> KinematicCollision2D:
\towner.velocity = direction.normalized() * p_speed
\towner.move_and_slide()
\tif owner.get_slide_collision_count() == 0:
\t\treturn null
\treturn owner.get_slide_collision(0)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Node", node_name: "Move"}},
      {tool: "attach_script", arguments: {node_path: "Move", script_content: "extends \"on_ground.gd\"\n\n@export var max_walk_speed := 450.0\n@export var max_run_speed := 700.0\n\nfunc enter() -> void:\n\tspeed = 0.0\n\tvelocity = Vector2()\n\nfunc update(_delta: float) -> void:\n\tvar input_direction := get_input_direction()\n\tif input_direction.is_zero_approx():\n\t\tfinished.emit(PLAYER_STATE.idle)\n\n\tif Input.is_action_pressed(&\"run\"):\n\t\tspeed = max_run_speed\n\telse:\n\t\tspeed = max_walk_speed"}}
    ],
    description: "Walk/run movement state with speed switching and collision detection"
  },
  {
    pattern_name: "CharacterBody2D with WASD movement, gravity, and jump",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/kinematic_character/player/player.gd",
    extends_class: "CharacterBody2D",
    script_content: `extends CharacterBody2D

const WALK_FORCE = 600
const WALK_MAX_SPEED = 200
const STOP_FORCE = 1300
const JUMP_SPEED = 200

@onready var gravity := float(ProjectSettings.get_setting("physics/2d/default_gravity"))

func _physics_process(delta: float) -> void:
\tvar walk := WALK_FORCE * (Input.get_axis(&"move_left", &"move_right"))
\tif abs(walk) < WALK_FORCE * 0.2:
\t\tvelocity.x = move_toward(velocity.x, 0, STOP_FORCE * delta)
\telse:
\t\tvelocity.x += walk * delta
\tvelocity.x = clamp(velocity.x, -WALK_MAX_SPEED, WALK_MAX_SPEED)

\tvelocity.y += gravity * delta
\tmove_and_slide()

\tif is_on_floor() and Input.is_action_just_pressed(&"jump"):
\t\tvelocity.y = -JUMP_SPEED`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CharacterBody2D", node_name: "Player"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "capsule", parameters: {radius: 10, height: 30}}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Sprite2D", node_name: "Sprite2D"}},
      {tool: "create_input_action", arguments: {action_name: "move_left", events: [{type: "key", "physical_keycode": "A"}]}},
      {tool: "create_input_action", arguments: {action_name: "move_right", events: [{type: "key", "physical_keycode": "D"}]}},
      {tool: "create_input_action", arguments: {action_name: "jump", events: [{type: "key", "physical_keycode": "Space"}]}}
    ],
    description: "2D platformer CharacterBody2D with force-based horizontal movement, gravity, and jump"
  },
  {
    pattern_name: "CharacterBody2D with NavigationAgent2D pathfinding",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/navigation/character.gd",
    extends_class: "CharacterBody2D",
    script_content: `extends CharacterBody2D

var movement_speed := 200.0

@onready var navigation_agent: NavigationAgent2D = $NavigationAgent2D

func _ready() -> void:
\tnavigation_agent.path_desired_distance = 2.0
\tnavigation_agent.target_desired_distance = 2.0
\tnavigation_agent.debug_enabled = true

func _unhandled_input(input_event: InputEvent) -> void:
\tif not input_event.is_action_pressed(&"click"):
\t\treturn
\tset_movement_target(get_global_mouse_position())

func set_movement_target(movement_target: Vector2) -> void:
\tnavigation_agent.target_position = movement_target

func _physics_process(_delta: float) -> void:
\tif navigation_agent.is_navigation_finished():
\t\treturn
\tvar current_agent_position: Vector2 = global_position
\tvar next_path_position: Vector2 = navigation_agent.get_next_path_position()
\tvelocity = current_agent_position.direction_to(next_path_position) * movement_speed
\tmove_and_slide()`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CharacterBody2D", node_name: "Character"}},
      {tool: "add_child_node", arguments: {parent_path: "Character", node_type: "NavigationAgent2D", node_name: "NavigationAgent2D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Character", node_name: "CollisionShape2D", shape_type: "circle", parameters: {radius: 15}}},
      {tool: "add_navigation_region", arguments: {dimensions: "2D"}}
    ],
    description: "2D click-to-move character using NavigationAgent2D for pathfinding with move_and_slide"
  },
  {
    pattern_name: "RigidBody2D platformer player with _integrate_forces movement",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/physics_platformer/player/player.gd",
    extends_class: "RigidBody2D",
    script_content: `class_name Player
extends RigidBody2D

const WALK_ACCEL = 1000.0
const WALK_DEACCEL = 1000.0
const WALK_MAX_VELOCITY = 200.0
const AIR_ACCEL = 250.0
const AIR_DEACCEL = 250.0
const JUMP_VELOCITY = 380.0
const STOP_JUMP_FORCE = 450.0
const MAX_FLOOR_AIRBORNE_TIME = 0.15

var jumping: bool = false
var stopping_jump: bool = false
var floor_h_velocity: float = 0.0
var airborne_time: float = 1e20

func _integrate_forces(state: PhysicsDirectBodyState2D) -> void:
\tvar velocity := state.get_linear_velocity()
\tvar step := state.get_step()
\tvar move_left := Input.is_action_pressed(&"move_left")
\tvar move_right := Input.is_action_pressed(&"move_right")
\tvar jump := Input.is_action_pressed(&"jump")

\tvelocity.x -= floor_h_velocity
\tfloor_h_velocity = 0.0

\tvar found_floor: bool = false
\tfor contact_index in state.get_contact_count():
\t\tvar collision_normal := state.get_contact_local_normal(contact_index)
\t\tif collision_normal.dot(Vector2(0, -1)) > 0.6:
\t\t\tfound_floor = true

\tif found_floor:
\t\tairborne_time = 0.0
\telse:
\t\tairborne_time += step

\tvar on_floor := airborne_time < MAX_FLOOR_AIRBORNE_TIME

\tif on_floor:
\t\tif move_left and not move_right:
\t\t\tif velocity.x > -WALK_MAX_VELOCITY:
\t\t\t\tvelocity.x -= WALK_ACCEL * step
\t\telif move_right and not move_left:
\t\t\tif velocity.x < WALK_MAX_VELOCITY:
\t\t\t\tvelocity.x += WALK_ACCEL * step
\t\telse:
\t\t\tvar xv := absf(velocity.x)
\t\t\txv -= WALK_DEACCEL * step
\t\t\tif xv < 0: xv = 0
\t\t\tvelocity.x = signf(velocity.x) * xv
\t\tif not jumping and jump:
\t\t\tvelocity.y = -JUMP_VELOCITY
\t\t\tjumping = true

\tvelocity += state.get_total_gravity() * step
\tstate.set_linear_velocity(velocity)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "RigidBody2D", node_name: "Player"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "capsule"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Sprite2D", node_name: "Sprite2D"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AnimationPlayer", node_name: "AnimationPlayer"}}
    ],
    description: "RigidBody2D platformer using _integrate_forces for floor detection, acceleration, and jump with coyote time"
  },
  {
    pattern_name: "3D CharacterBody3D with camera-relative WASD movement and jump",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/kinematic_character/player/cubio.gd",
    extends_class: "CharacterBody3D",
    script_content: `extends CharacterBody3D

const MAX_SPEED = 3.5
const JUMP_SPEED = 6.5
const ACCELERATION = 4
const DECELERATION = 4

@onready var camera: Camera3D = $Target/Camera3D
@onready var gravity := float(-ProjectSettings.get_setting("physics/3d/default_gravity"))
@onready var start_position := position

func _physics_process(delta: float) -> void:
\tif Input.is_action_just_pressed(&"reset_position") or global_position.y < -6.0:
\t\tposition = start_position
\t\tvelocity = Vector3.ZERO
\t\treset_physics_interpolation()

\tvar dir := Vector3()
\tdir.x = Input.get_axis(&"move_left", &"move_right")
\tdir.z = Input.get_axis(&"move_forward", &"move_back")

\tvar cam_basis := camera.global_transform.basis
\tcam_basis = cam_basis.rotated(cam_basis.x, -cam_basis.get_euler().x)
\tdir = cam_basis * dir

\tif dir.length_squared() > 1:
\t\tdir /= dir.length()

\tvelocity.y += delta * gravity

\tvar hvel := velocity
\thvel.y = 0
\tvar target := dir * MAX_SPEED
\tvar acceleration := ACCELERATION if dir.dot(hvel) > 0 else DECELERATION
\thvel = hvel.lerp(target, acceleration * delta)

\tvelocity.x = hvel.x
\tvelocity.z = hvel.z
\tmove_and_slide()

\tif is_on_floor() and Input.is_action_pressed(&"jump"):
\t\tvelocity.y = JUMP_SPEED`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Node3D", node_name: "Target"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/Target", node_type: "Camera3D", node_name: "Camera3D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "capsule", parameters: {radius: 0.5, height: 1.6}}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "MeshInstance3D", node_name: "Mesh"}}
    ],
    description: "3D CharacterBody3D with camera-relative movement, gravity, lerp-based acceleration/deceleration, and jump"
  },
  {
    pattern_name: "3D CharacterBody3D platformer with directional facing and bounce",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/squash_the_creeps/Player.gd",
    extends_class: "CharacterBody3D",
    script_content: `extends CharacterBody3D

signal hit

@export var speed = 14
@export var jump_impulse = 20
@export var bounce_impulse = 16
@export var fall_acceleration = 75

func _physics_process(delta):
\tvar direction = Vector3.ZERO
\tif Input.is_action_pressed(&"move_right"): direction.x += 1
\tif Input.is_action_pressed(&"move_left"): direction.x -= 1
\tif Input.is_action_pressed(&"move_back"): direction.z += 1
\tif Input.is_action_pressed(&"move_forward"): direction.z -= 1

\tif direction != Vector3.ZERO:
\t\tdirection = direction.normalized()
\t\tbasis = Basis.looking_at(direction)
\t\t$AnimationPlayer.speed_scale = 4
\telse:
\t\t$AnimationPlayer.speed_scale = 1

\tvelocity.x = direction.x * speed
\tvelocity.z = direction.z * speed

\tif is_on_floor() and Input.is_action_just_pressed(&"jump"):
\t\tvelocity.y += jump_impulse

\tvelocity.y -= fall_acceleration * delta
\tmove_and_slide()

\tfor index in range(get_slide_collision_count()):
\t\tvar collision = get_slide_collision(index)
\t\tif collision.get_collider().is_in_group(&"mob"):
\t\t\tvar mob = collision.get_collider()
\t\t\tif Vector3.UP.dot(collision.get_normal()) > 0.1:
\t\t\t\tmob.squash()
\t\t\t\tvelocity.y = bounce_impulse
\t\t\t\tbreak

\trotation.x = PI / 6 * velocity.y / jump_impulse

func die():
\thit.emit()
\tqueue_free()

func _on_MobDetector_body_entered(_body):
\tdie()`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AnimationPlayer", node_name: "AnimationPlayer"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "capsule"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Area3D", node_name: "MobDetector"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player/MobDetector", node_name: "CollisionShape3D", shape_type: "sphere"}}
    ],
    description: "3D platformer with directional facing via Basis.looking_at, bounce on enemy heads, and mob collision detection"
  },
  {
    pattern_name: "3D Mob with CharacterBody3D, look_at orientation, and screen cleanup",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/squash_the_creeps/Mob.gd",
    extends_class: "CharacterBody3D",
    script_content: `extends CharacterBody3D

signal squashed

@export var min_speed = 10
@export var max_speed = 18

func _physics_process(_delta):
\tmove_and_slide()

func initialize(start_position, player_position):
\tvar target = Vector3(player_position.x, start_position.y, player_position.z)
\tlook_at_from_position(start_position, target, Vector3.UP)
\trotate_y(randf_range(-PI / 4, PI / 4))
\tvar random_speed = randf_range(min_speed, max_speed)
\tvelocity = Vector3.FORWARD * random_speed
\tvelocity = velocity.rotated(Vector3.UP, rotation.y)
\t$AnimationPlayer.speed_scale = random_speed / min_speed

func squash():
\tsquashed.emit()
\tqueue_free()

func _on_visible_on_screen_notifier_screen_exited():
\tqueue_free()`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Mob"}},
      {tool: "add_child_node", arguments: {parent_path: "Mob", node_type: "AnimationPlayer", node_name: "AnimationPlayer"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Mob", node_name: "CollisionShape3D", shape_type: "box"}},
      {tool: "add_child_node", arguments: {parent_path: "Mob", node_type: "VisibleOnScreenNotifier3D", node_name: "VisibleOnScreenNotifier3D"}}
    ],
    description: "3D mob using CharacterBody3D with look_at initialization, random speed, and auto-cleanup when off-screen"
  },
  {
    pattern_name: "3D NavigationAgent3D click-to-move with path visualization",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/navigation/character.gd",
    extends_class: "Marker3D",
    script_content: `extends Marker3D

@export var character_speed := 10.0
@export var show_path: bool = true

var _nav_path_line: Line3D

@onready var _nav_agent := $NavigationAgent3D as NavigationAgent3D

func _ready() -> void:
\t_nav_path_line = Line3D.new()
\tadd_child(_nav_path_line)
\t_nav_path_line.set_as_top_level(true)

func _physics_process(delta: float) -> void:
\tif _nav_agent.is_navigation_finished():
\t\treturn
\tvar next_position := _nav_agent.get_next_path_position()
\tvar offset := next_position - global_position
\tglobal_position = global_position.move_toward(next_position, delta * character_speed)
\toffset.y = 0
\tif not offset.is_zero_approx():
\t\tlook_at(global_position + offset, Vector3.UP)

func set_target_position(target_position: Vector3) -> void:
\t_nav_agent.set_target_position(target_position)
\tif show_path:
\t\tvar start_position := global_transform.origin
\t\tvar navigation_map := get_world_3d().get_navigation_map()
\t\tvar path := NavigationServer3D.map_get_path(navigation_map, start_position, target_position, true)
\t\t_nav_path_line.draw_path(path)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Marker3D", node_name: "Character"}},
      {tool: "add_child_node", arguments: {parent_path: "Character", node_type: "NavigationAgent3D", node_name: "NavigationAgent3D"}},
      {tool: "add_navigation_region", arguments: {dimensions: "3D"}}
    ],
    description: "3D click-to-move using NavigationAgent3D with path visualization via Line3D and look_at rotation"
  },
  {
    pattern_name: "RigidBody3D player with ShapeCast3D ground check and impulses",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/rigidbody_character/player/cubio.gd",
    extends_class: "RigidBody3D",
    script_content: `extends RigidBody3D

@onready var shape_cast: ShapeCast3D = $ShapeCast3D
@onready var camera: Camera3D = $Target/Camera3D
@onready var start_position := position

func _physics_process(delta: float) -> void:
\tif Input.is_action_just_pressed(&"reset_position") or global_position.y < -6.0:
\t\tposition = start_position
\t\tlinear_velocity = Vector3.ZERO
\t\treset_physics_interpolation()

\tvar dir := Vector3()
\tdir.x = Input.get_axis(&"move_left", &"move_right")
\tdir.z = Input.get_axis(&"move_forward", &"move_back")

\tvar cam_basis := camera.global_transform.basis
\tcam_basis = cam_basis.rotated(cam_basis.x, -cam_basis.get_euler().x)
\tdir = cam_basis * dir

\tapply_central_impulse(dir.normalized() * 5.0 * delta)

\tif on_ground():
\t\tapply_central_impulse(dir.normalized() * 10.0 * delta)
\t\tif Input.is_action_pressed(&"jump"):
\t\t\tlinear_velocity.y = 7

func on_ground() -> bool:
\treturn shape_cast.is_colliding()`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "RigidBody3D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "ShapeCast3D", node_name: "ShapeCast3D"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Node3D", node_name: "Target"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/Target", node_type: "Camera3D", node_name: "Camera3D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "capsule"}}
    ],
    description: "RigidBody3D player using apply_central_impulse for movement with ShapeCast3D ground detection"
  },
  {
    pattern_name: "VehicleBody3D with steering, turbo, headlights, and audio",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/truck_town/vehicles/vehicle.gd",
    extends_class: "VehicleBody3D",
    script_content: `extends VehicleBody3D

const STEER_SPEED = 1.5
const STEER_LIMIT = 0.4

@export var engine_force_value := 40.0

var _steer_target := 0.0

func _physics_process(delta: float) -> void:
\t_steer_target = Input.get_axis(&"turn_right", &"turn_left")
\t_steer_target *= STEER_LIMIT

\tif DisplayServer.is_touchscreen_available() or Input.is_action_pressed(&"accelerate"):
\t\tvar speed := linear_velocity.length()
\t\tif speed < 5.0 and not is_zero_approx(speed):
\t\t\tengine_force = clampf(engine_force_value * 5.0 / speed, 0.0, 100.0)
\t\telse:
\t\t\tengine_force = engine_force_value
\t\tif not DisplayServer.is_touchscreen_available():
\t\t\tengine_force *= Input.get_action_strength(&"accelerate")
\telse:
\t\tengine_force = 0.0

\tif Input.is_action_pressed(&"reverse"):
\t\tengine_force = -engine_force_value

\tsteering = move_toward(steering, _steer_target, STEER_SPEED * delta)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "VehicleBody3D", node_name: "Vehicle"}},
      {tool: "add_child_node", arguments: {parent_path: "Vehicle", node_type: "MeshInstance3D", node_name: "Body"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Vehicle", node_name: "CollisionShape3D", shape_type: "box"}},
      {tool: "add_child_node", arguments: {parent_path: "Vehicle", node_type: "AudioStreamPlayer", node_name: "EngineSound"}}
    ],
    description: "VehicleBody3D with smooth steering, engine force scaling at low speeds, and reverse support"
  },
  {
    pattern_name: "JSON save/load system with var_to_str conversion",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "loading/serialization/save_load_json.gd",
    extends_class: "Button",
    script_content: `extends Button

@export var game_node: NodePath
@export var player_node: NodePath

const SAVE_PATH = "user://save_json.json"

func save_game() -> void:
\tvar file := FileAccess.open(SAVE_PATH, FileAccess.WRITE)
\tvar player := get_node(player_node)
\tvar save_dict := {
\t\tplayer = {
\t\t\tposition = var_to_str(player.position),
\t\t\thealth = var_to_str(player.health),
\t\t\trotation = var_to_str(player.sprite.rotation),
\t\t},
\t\tenemies = [],
\t}
\tfor enemy in get_tree().get_nodes_in_group(&"enemy"):
\t\tsave_dict.enemies.push_back({position = var_to_str(enemy.position)})
\tfile.store_line(JSON.stringify(save_dict))

func load_game() -> void:
\tvar file := FileAccess.open(SAVE_PATH, FileAccess.READ)
\tvar json := JSON.new()
\tjson.parse(file.get_line())
\tvar save_dict := json.get_data() as Dictionary
\tvar player := get_node(player_node) as Player
\tplayer.position = str_to_var(save_dict.player.position)
\tplayer.health = str_to_var(save_dict.player.health)
\tplayer.sprite.rotation = str_to_var(save_dict.player.rotation)
\tget_tree().call_group(&"enemy", &"queue_free")
\tvar game := get_node(game_node)
\tfor enemy_config: Dictionary in save_dict.enemies:
\t\tvar enemy: Enemy = preload("res://enemy.tscn").instantiate()
\t\tenemy.position = str_to_var(enemy_config.position)
\t\tgame.add_child(enemy)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Button", node_name: "SaveButton"}},
      {tool: "attach_script", arguments: {node_path: "SaveButton", script_content: "extends Button\n\nconst SAVE_PATH = \"user://save_json.json\"\n\nfunc save_game() -> void:\n\tvar file := FileAccess.open(SAVE_PATH, FileAccess.WRITE)\n\tvar save_dict := {player = {position = var_to_str(player.position)}}\n\tfile.store_line(JSON.stringify(save_dict))\n\nfunc load_game() -> void:\n\tvar file := FileAccess.open(SAVE_PATH, FileAccess.READ)\n\tvar json := JSON.new()\n\tjson.parse(file.get_line())\n\tvar save_dict := json.get_data() as Dictionary"}}
    ],
    description: "Save/load system using JSON with var_to_str/str_to_var for Vector2 conversion and enemy re-instantiation"
  },
  {
    pattern_name: "Multiplayer CharacterBody2D with @rpc and authority",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "networking/multiplayer_bomber/player.gd",
    extends_class: "CharacterBody2D",
    script_content: `extends CharacterBody2D

const MOTION_SPEED = 90.0
const BOMB_RATE = 0.5

@export var synced_position := Vector2()
@export var stunned: bool = false

var last_bomb_time := BOMB_RATE
var current_anim: String = ""

func _ready() -> void:
\tstunned = false
\tposition = synced_position
\tif str(name).is_valid_int():
\t\t$"Inputs/InputsSync".set_multiplayer_authority(str(name).to_int())

func _physics_process(delta: float) -> void:
\tif multiplayer.multiplayer_peer == null or str(multiplayer.get_unique_id()) == str(name):
\t\tinputs.update()
\tif multiplayer.multiplayer_peer == null or is_multiplayer_authority():
\t\tsynced_position = position
\t\tlast_bomb_time += delta
\t\tif not stunned and is_multiplayer_authority() and inputs.bombing and last_bomb_time >= BOMB_RATE:
\t\t\tlast_bomb_time = 0.0
\t\t\t$"../../BombSpawner".spawn([position, str(name).to_int()])
\telse:
\t\tposition = synced_position
\tif not stunned:
\t\tvelocity = inputs.motion * MOTION_SPEED
\t\tmove_and_slide()

\tvar new_anim := &"standing"
\tif inputs.motion.y < 0: new_anim = &"walk_up"
\telif inputs.motion.y > 0: new_anim = &"walk_down"
\telif inputs.motion.x < 0: new_anim = &"walk_left"
\telif inputs.motion.x > 0: new_anim = &"walk_right"
\tif stunned: new_anim = &"stunned"
\tif new_anim != current_anim:
\t\tcurrent_anim = new_anim
\t\t$anim.play(current_anim)

@rpc("call_local")
func set_player_name(value: String) -> void:
\t$label.text = value
\t$label.modulate = gamestate.get_player_color(value)

@rpc("call_local")
func exploded(_by_who: int) -> void:
\tif stunned: return
\tstunned = true
\t$anim.play(&"stunned")`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CharacterBody2D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AnimatedSprite2D", node_name: "anim"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Label", node_name: "label"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "circle"}}
    ],
    description: "Multiplayer CharacterBody2D with set_multiplayer_authority, @rpc annotations, and synced position"
  },
  {
    pattern_name: "High-performance bullet system using PhysicsServer2D directly",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/bullet_shower/bullets.gd",
    extends_class: "Node2D",
    script_content: `extends Node2D

const BULLET_COUNT = 500
const SPEED_MIN = 20
const SPEED_MAX = 80
const bullet_image := preload("res://bullet.png")

var bullets := []
var shape := RID()

class Bullet:
\tvar position := Vector2()
\tvar speed := 1.0
\tvar body := RID()

func _ready() -> void:
\tshape = PhysicsServer2D.circle_shape_create()
\tPhysicsServer2D.shape_set_data(shape, 8)
\tfor _i in BULLET_COUNT:
\t\tvar bullet := Bullet.new()
\t\tbullet.speed = randf_range(SPEED_MIN, SPEED_MAX)
\t\tbullet.body = PhysicsServer2D.body_create()
\t\tPhysicsServer2D.body_set_space(bullet.body, get_world_2d().get_space())
\t\tPhysicsServer2D.body_add_shape(bullet.body, shape)
\t\tPhysicsServer2D.body_set_collision_mask(bullet.body, 0)
\t\tbullet.position = Vector2(randf_range(0, get_viewport_rect().size.x), randf_range(0, get_viewport_rect().size.y))
\t\tvar transform2d := Transform2D()
\t\ttransform2d.origin = bullet.position
\t\tPhysicsServer2D.body_set_state(bullet.body, PhysicsServer2D.BODY_STATE_TRANSFORM, transform2d)
\t\tbullets.push_back(bullet)

func _physics_process(delta: float) -> void:
\tvar transform2d := Transform2D()
\tfor bullet: Bullet in bullets:
\t\tbullet.position.x -= bullet.speed * delta
\t\tif bullet.position.x < -16:
\t\t\tbullet.position.x = get_viewport_rect().size.x + 16
\t\ttransform2d.origin = bullet.position
\t\tPhysicsServer2D.body_set_state(bullet.body, PhysicsServer2D.BODY_STATE_TRANSFORM, transform2d)

func _draw() -> void:
\tvar offset := -bullet_image.get_size() * 0.5
\tfor bullet: Bullet in bullets:
\t\tdraw_texture(bullet_image, bullet.position + offset)

func _exit_tree() -> void:
\tfor bullet: Bullet in bullets:
\t\tPhysicsServer2D.free_rid(bullet.body)
\tPhysicsServer2D.free_rid(shape)
\tbullets.clear()`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Node2D", node_name: "BulletShower"}},
      {tool: "attach_script", arguments: {node_path: "BulletShower", script_content: "extends Node2D\n\nconst BULLET_COUNT = 500\n\nfunc _ready() -> void:\n\tpass\n\nfunc _physics_process(delta: float) -> void:\n\tpass\n\nfunc _draw() -> void:\n\tpass"}}
    ],
    description: "High-performance 500-bullet system using PhysicsServer2D RIDs directly, custom _draw rendering, and proper cleanup"
  },
  {
    pattern_name: "TileMapLayer with runtime collision modification and transparency animation",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/dynamic_tilemap_layers/level/tile_map.gd",
    extends_class: "TileMapLayer",
    script_content: `extends TileMapLayer

var player_in_secret: bool = false
var layer_alpha := 1.0

func _ready() -> void:
\tset_process(false)

func _process(delta: float) -> void:
\tif player_in_secret:
\t\tif layer_alpha > 0.3:
\t\t\tlayer_alpha = move_toward(layer_alpha, 0.3, delta)
\t\t\tself_modulate = Color(1, 1, 1, layer_alpha)
\t\telse:
\t\t\tset_process(false)
\telse:
\t\tif layer_alpha < 1.0:
\t\t\tlayer_alpha = move_toward(layer_alpha, 1.0, delta)
\t\t\tself_modulate = Color(1, 1, 1, layer_alpha)
\t\telse:
\t\t\tset_process(false)

func _use_tile_data_runtime_update(_coords: Vector2i) -> bool:
\treturn true

func _tile_data_runtime_update(_coords: Vector2i, tile_data: TileData) -> void:
\ttile_data.set_collision_polygons_count(0, 0)

func _on_secret_detector_body_entered(body: Node2D) -> void:
\tif body is not CharacterBody2D: return
\tplayer_in_secret = true
\tset_process(true)

func _on_secret_detector_body_exited(body: Node2D) -> void:
\tif body is not CharacterBody2D: return
\tplayer_in_secret = false
\tset_process(true)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "TileMapLayer", node_name: "SecretLayer"}},
      {tool: "add_child_node", arguments: {parent_path: "SecretLayer", node_type: "Area2D", node_name: "SecretDetector"}},
      {tool: "add_collision_shape", arguments: {parent_path: "SecretLayer/SecretDetector", node_name: "CollisionShape2D", shape_type: "rectangle"}}
    ],
    description: "TileMapLayer with dynamic transparency for secret areas, runtime collision modification via _tile_data_runtime_update"
  },
  {
    pattern_name: "Audio effects toggle system using AudioServer bus control",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "audio/audio_effects/audio_effects.gd",
    extends_class: "Control",
    script_content: `extends Control

func _on_toggle_music_toggled(button_pressed: bool) -> void:
\tif button_pressed:
\t\t$SoundEffects/Music.play()
\telse:
\t\t$SoundEffects/Music.stop()

func _on_ding_button_pressed() -> void:
\t$SoundEffects/Ding.play()

func _on_glass_button_pressed() -> void:
\t$SoundEffects/Glass.play()

func _on_toggle_amplify_toggled(button_pressed: bool) -> void:
\tAudioServer.set_bus_effect_enabled(0, 0, button_pressed)

func _on_toggle_reverb_toggled(button_pressed: bool) -> void:
\tAudioServer.set_bus_effect_enabled(0, 16, button_pressed)

func _on_toggle_chorus_toggled(button_pressed: bool) -> void:
\tAudioServer.set_bus_effect_enabled(0, 3, button_pressed)

func _on_toggle_compressor_toggled(button_pressed: bool) -> void:
\tAudioServer.set_bus_effect_enabled(0, 4, button_pressed)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Control", node_name: "AudioPanel"}},
      {tool: "add_child_node", arguments: {parent_path: "AudioPanel", node_type: "VBoxContainer", node_name: "SoundEffects"}},
      {tool: "add_child_node", arguments: {parent_path: "AudioPanel/SoundEffects", node_type: "AudioStreamPlayer", node_name: "Music"}},
      {tool: "add_child_node", arguments: {parent_path: "AudioPanel/SoundEffects", node_type: "AudioStreamPlayer", node_name: "Ding"}},
      {tool: "add_child_node", arguments: {parent_path: "AudioPanel", node_type: "CheckButton", node_name: "ToggleReverb"}}
    ],
    description: "Audio effects control panel using AudioServer.set_bus_effect_enabled to toggle effects on audio bus"
  },
  {
    pattern_name: "Drag and drop with _get_drag_data, _can_drop_data, _drop_data",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "gui/drag_and_drop/drag_drop_script.gd",
    extends_class: "ColorPickerButton",
    script_content: `extends ColorPickerButton

func _get_drag_data(_at_position: Vector2) -> Color:
\tvar cpb := ColorPickerButton.new()
\tcpb.color = color
\tcpb.size = Vector2(80.0, 50.0)
\tvar preview := Control.new()
\tpreview.add_child(cpb)
\tcpb.position = -0.5 * cpb.size
\tset_drag_preview(preview)
\treturn color

func _can_drop_data(_at_position: Vector2, data: Variant) -> bool:
\treturn typeof(data) == TYPE_COLOR

func _drop_data(_at_position: Vector2, data: Variant) -> void:
\tcolor = data`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "ColorPickerButton", node_name: "ColorPicker"}},
      {tool: "attach_script", arguments: {node_path: "ColorPicker", script_content: "extends ColorPickerButton\n\nfunc _get_drag_data(_at_position: Vector2) -> Color:\n\tvar cpb := ColorPickerButton.new()\n\tcpb.color = color\n\tset_drag_preview(cpb)\n\treturn color\n\nfunc _can_drop_data(_at_position: Vector2, data: Variant) -> bool:\n\treturn typeof(data) == TYPE_COLOR\n\nfunc _drop_data(_at_position: Vector2, data: Variant) -> void:\n\tcolor = data"}}
    ],
    description: "Godot drag and drop pattern with _get_drag_data, _can_drop_data, _drop_data override methods"
  },
  {
    pattern_name: "Dynamic split-screen camera with shader parameters",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "viewport/dynamic_split_screen/camera_controller.gd",
    extends_class: "Node3D",
    script_content: `extends Node3D

@export var max_separation := 20.0
@export var split_line_thickness := 3.0
@export var split_line_color := Color.BLACK
@export var adaptive_split_line_thickness: bool = true

@onready var player1: CharacterBody3D = $"../Player1"
@onready var player2: CharacterBody3D = $"../Player2"
@onready var view: TextureRect = $View
@onready var viewport1: SubViewport = $Viewport1
@onready var viewport2: SubViewport = $Viewport2
@onready var camera1: Camera3D = viewport1.get_node(^"Camera1")
@onready var camera2: Camera3D = viewport2.get_node(^"Camera2")

func _ready() -> void:
\t_on_size_changed()
\t_update_splitscreen()
\tget_viewport().size_changed.connect(_on_size_changed)
\tview.material.set_shader_parameter(&"viewport1", viewport1.get_texture())
\tview.material.set_shader_parameter(&"viewport2", viewport2.get_texture())

func _process(_delta: float) -> void:
\t_move_cameras()
\t_update_splitscreen()

func _move_cameras() -> void:
\tvar position_difference := _get_position_difference_in_world()
\tvar distance := clampf(_get_horizontal_length(position_difference), 0, max_separation)
\tposition_difference = position_difference.normalized() * distance
\tcamera1.position.x = player1.position.x + position_difference.x / 2.0
\tcamera1.position.z = player1.position.z + position_difference.z / 2.0
\tcamera2.position.x = player2.position.x - position_difference.x / 2.0
\tcamera2.position.z = player2.position.z - position_difference.z / 2.0

func _update_splitscreen() -> void:
\tvar screen_size := get_viewport().get_visible_rect().size
\tvar player1_position := camera1.unproject_position(player1.position) / screen_size
\tvar player2_position := camera2.unproject_position(player2.position) / screen_size
\tview.material.set_shader_parameter(&"split_active", _is_split_state())
\tview.material.set_shader_parameter(&"player1_position", player1_position)
\tview.material.set_shader_parameter(&"player2_position", player2_position)

func _is_split_state() -> bool:
\tvar position_difference := _get_position_difference_in_world()
\treturn _get_horizontal_length(position_difference) > max_separation`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Node3D", node_name: "CameraController"}},
      {tool: "add_child_node", arguments: {parent_path: "CameraController", node_type: "SubViewport", node_name: "Viewport1"}},
      {tool: "add_child_node", arguments: {parent_path: "CameraController/Viewport1", node_type: "Camera3D", node_name: "Camera1"}},
      {tool: "add_child_node", arguments: {parent_path: "CameraController", node_type: "SubViewport", node_name: "Viewport2"}},
      {tool: "add_child_node", arguments: {parent_path: "CameraController/Viewport2", node_type: "Camera3D", node_name: "Camera2"}},
      {tool: "add_child_node", arguments: {parent_path: "CameraController", node_type: "TextureRect", node_name: "View"}}
    ],
    description: "Dynamic split-screen system with two SubViewports, shader-driven split line, and adaptive camera positioning"
  },
  {
    pattern_name: "FPS CharacterBody3D with mouse look, voxel interaction, and crouch/sprint",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/voxel/player/player.gd",
    extends_class: "CharacterBody3D",
    script_content: `extends CharacterBody3D

const MOVEMENT_SPEED_GROUND = 70.0
const MOVEMENT_SPEED_AIR = 13.0
const MOVEMENT_JUMP_VELOCITY = 9.0
const EYE_HEIGHT_STAND = 1.6
const EYE_HEIGHT_CROUCH = 1.4

var _mouse_motion := Vector2()

@onready var head: Node3D = $Head
@onready var camera: Camera3D = $Head/Camera3D
@onready var raycast: RayCast3D = $Head/RayCast3D

func _ready() -> void:
\tInput.mouse_mode = Input.MOUSE_MODE_CAPTURED

func _process(_delta: float) -> void:
\t_mouse_motion.y = clampf(_mouse_motion.y, -1560, 1560)
\ttransform.basis = Basis.from_euler(Vector3(0, _mouse_motion.x * -0.001, 0))
\thead.transform.basis = Basis.from_euler(Vector3(_mouse_motion.y * -0.001, 0, 0))

func _physics_process(delta: float) -> void:
\tvar crouching: bool = Input.is_action_pressed(&"crouch")
\tvar sprinting: bool = Input.is_action_pressed(&"move_sprint")
\thead.transform.origin.y = lerpf(head.transform.origin.y, EYE_HEIGHT_CROUCH if crouching else EYE_HEIGHT_STAND, 1.0 - exp(-delta * 16.0))
\tvar movement_vec2: Vector2 = Input.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
\tvar movement: Vector3 = transform.basis * (Vector3(movement_vec2.x, 0, movement_vec2.y))
\tif is_on_floor():
\t\tmovement *= MOVEMENT_SPEED_GROUND
\telse:
\t\tmovement *= MOVEMENT_SPEED_AIR
\tif crouching: movement *= 0.5
\tif sprinting: movement *= 1.375

\tif not is_on_floor():
\t\tvelocity += get_gravity() * delta

\tvelocity += Vector3(movement.x, 0, movement.z) * delta
\tvar friction_delta := exp(-(12.5 if is_on_floor() else 2.25) * delta)
\tvelocity = Vector3(velocity.x * friction_delta, velocity.y, velocity.z * friction_delta)
\tmove_and_slide()

\tif is_on_floor() and Input.is_action_pressed(&"jump"):
\t\tvelocity.y = MOVEMENT_JUMP_VELOCITY

func _input(input_event: InputEvent) -> void:
\tif input_event is InputEventMouseMotion:
\t\tif Input.get_mouse_mode() == Input.MOUSE_MODE_CAPTURED:
\t\t\t_mouse_motion += input_event.screen_relative`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Node3D", node_name: "Head"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/Head", node_type: "Camera3D", node_name: "Camera3D"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/Head", node_type: "RayCast3D", node_name: "RayCast3D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "capsule", parameters: {radius: 0.5, height: 1.8}}}
    ],
    description: "FPS controller with mouse capture, Basis.from_euler rotation, crouch/sprint, and exponential friction"
  },
  {
    pattern_name: "3D platformer CharacterBody3D with AnimationTree blending",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/platformer/player/player.gd",
    extends_class: "CharacterBody3D",
    script_content: `class_name Player
extends CharacterBody3D

const MAX_SPEED: float = 6.0
const JUMP_VELOCITY: float = 12.5
const ACCEL: float = 14.0
const DEACCEL: float = 14.0

var coins: int = 0

@onready var _camera := $Target/Camera3D as Camera3D
@onready var _animation_tree := $AnimationTree as AnimationTree

func _physics_process(delta: float) -> void:
\tvelocity += gravity * delta

\tvar cam_basis := _camera.get_global_transform().basis
\tvar movement_vec2 := Input.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
\tvar movement_direction := cam_basis * Vector3(movement_vec2.x, 0, movement_vec2.y)
\tmovement_direction.y = 0
\tmovement_direction = movement_direction.normalized()

\tif is_on_floor():
\t\tif movement_direction.length() > 0.1:
\t\t\thorizontal_speed += ACCEL * delta
\t\telse:
\t\t\thorizontal_speed -= DEACCEL * delta
\t\tif not jumping and Input.is_action_pressed(&"jump"):
\t\t\tvelocity.y = JUMP_VELOCITY
\telse:
\t\tif movement_direction.length() > 0.1:
\t\t\thorizontal_velocity += movement_direction * (ACCEL * 0.5 * delta)

\tmove_and_slide()

\tif is_on_floor():
\t\t_animation_tree[&"parameters/run/blend_amount"] = horizontal_speed / MAX_SPEED
\t\t_animation_tree[&"parameters/speed/blend_amount"] = minf(1.0, horizontal_speed / (MAX_SPEED * 0.5))

\t_animation_tree[&"parameters/state/blend_amount"] = anim
\t_animation_tree[&"parameters/air_dir/blend_amount"] = clampf(-velocity.y / 4 + 0.5, 0, 1)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Node3D", node_name: "Target"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/Target", node_type: "Camera3D", node_name: "Camera3D"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AnimationTree", node_name: "AnimationTree"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "capsule"}}
    ],
    description: "3D platformer with AnimationTree blend parameters for run/walk/idle/jump state blending"
  },
  {
    pattern_name: "PhantomCamera2D player with interaction system and camera priority",
    source_repo: "ramokz/phantom-camera",
    source_path: "addons/phantom_camera/examples/scripts/2D/player_character_body_2d.gd",
    extends_class: "CharacterBody2D",
    script_content: `extends CharacterBody2D

const SPEED = 350.0
const JUMP_VELOCITY = -750.0

var gravity: int = 2400
var _is_interactive: bool
var _movement_disabled: bool
var _active_pcam: PhantomCamera2D

func _physics_process(delta: float) -> void:
\tif not is_on_floor():
\t\tvelocity.y += gravity * delta
\tif Input.is_action_just_pressed("ui_accept") and is_on_floor():
\t\tvelocity.y = JUMP_VELOCITY
\tif _movement_disabled: return

\tvar input_dir := Input.get_axis("move_left", "move_right")
\tif input_dir:
\t\tvelocity.x = input_dir * SPEED
\t\tif input_dir > 0:
\t\t\t%PlayerSprite.set_flip_h(false)
\t\telif input_dir < 0:
\t\t\t%PlayerSprite.set_flip_h(true)
\telse:
\t\tvelocity.x = move_toward(velocity.x, 0, SPEED)
\tmove_and_slide()`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CharacterBody2D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "PhantomCamera2D", node_name: "PlayerPhantomCamera2D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "capsule"}}
    ],
    description: "2D platformer with PhantomCamera2D integration for camera priority switching on interaction"
  },
  {
    pattern_name: "PhantomCamera3D third-person CharacterBody3D controller",
    source_repo: "ramokz/phantom-camera",
    source_path: "addons/phantom_camera/examples/scripts/3D/player_controller.gd",
    extends_class: "CharacterBody3D",
    script_content: `extends CharacterBody3D

@export var SPEED: float = 5.0
@export var JUMP_VELOCITY: float = 4.5
@export var enable_gravity = true

var gravity: float = 9.8
var movement_enabled: bool = true

func _ready() -> void:
\t_camera = owner.get_node("%MainCamera3D")
\tfor input in InputMovementDic:
\t\tvar key_val = InputMovementDic[input].get("Key")
\t\tvar action_val = InputMovementDic[input].get("Action")
\t\tvar movement_input = InputEventKey.new()
\t\tmovement_input.physical_keycode = key_val
\t\tInputMap.add_action(action_val)
\t\tInputMap.action_add_event(action_val, movement_input)

func _physics_process(delta: float) -> void:
\tif enable_gravity and not is_on_floor():
\t\tvelocity.y -= gravity * delta
\tif not movement_enabled: return

\tvar input_dir: Vector2 = Input.get_vector("move_left", "move_right", "move_up", "move_down")
\tvar direction: Vector3 = (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()
\tif direction:
\t\tvar move_dir: Vector3 = Vector3.ZERO
\t\tmove_dir.x = direction.x
\t\tmove_dir.z = direction.z
\t\tmove_dir = move_dir.rotated(Vector3.UP, _camera.rotation.y).normalized()
\t\tvelocity.x = move_dir.x * SPEED
\t\tvelocity.z = move_dir.z * SPEED
\telse:
\t\tvelocity.x = move_toward(velocity.x, 0, SPEED)
\t\tvelocity.z = move_toward(velocity.z, 0, SPEED)
\tmove_and_slide()`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "PhantomCamera3D", node_name: "PlayerPhantomCamera3D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "capsule"}}
    ],
    description: "3D player controller with PhantomCamera3D, camera-rotated movement, and runtime InputMap registration"
  },
  {
    pattern_name: "3D RigidBody enemy AI with raycast floor/wall detection",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/platformer/enemy/enemy.gd",
    extends_class: "RigidBody3D",
    script_content: `extends RigidBody3D

const ACCEL: float = 5.0
const DEACCEL: float = 20.0
const MAX_SPEED: float = 2.0
const ROT_SPEED: float = 1.0

var prev_advance: bool = false
var dying: bool = false
var rot_dir: float = 4.0

func _integrate_forces(state: PhysicsDirectBodyState3D) -> void:
\tvar delta := state.get_step()
\tvar lin_velocity := state.get_linear_velocity()
\tvar grav := state.get_total_gravity()
\tif grav.is_zero_approx(): grav = gravity
\tlin_velocity += grav * delta

\tif dying:
\t\tstate.set_linear_velocity(lin_velocity)
\t\treturn

\tfor i in state.get_contact_count():
\t\tvar contact_collider := state.get_contact_collider_object(i)
\t\tif contact_collider is Bullet and contact_collider.enabled:
\t\t\tdying = true
\t\t\tstate.set_angular_velocity(-contact_normal.cross(up).normalized() * 33.0)
\t\t\treturn

\tvar advance: bool = _ray_floor.is_colliding() and not _ray_wall.is_colliding()
\tvar dir: Vector3 = ($Enemy/Skeleton as Node3D).get_transform().basis.z.normalized()

\tif advance:
\t\tif dir.dot(lin_velocity) < MAX_SPEED:
\t\t\tlin_velocity += dir * ACCEL * delta
\telse:
\t\tif prev_advance: rot_dir = 1
\t\tdir = Basis(up, rot_dir * ROT_SPEED * delta) * dir

\tstate.set_linear_velocity(lin_velocity)
\tprev_advance = advance`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "RigidBody3D", node_name: "Enemy"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "RayCast3D", node_name: "RayFloor"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "RayCast3D", node_name: "RayWall"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Enemy", node_name: "CollisionShape3D", shape_type: "box"}}
    ],
    description: "3D enemy AI using RigidBody3D _integrate_forces with raycast-based navigation and wall avoidance"
  },
  {
    pattern_name: "2D RigidBody2D enemy with bullet detection and state machine",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/physics_platformer/enemy/enemy.gd",
    extends_class: "RigidBody2D",
    script_content: `class_name Enemy
extends RigidBody2D

const WALK_SPEED = 50

enum State { WALKING, DYING }
var _state := State.WALKING
var direction := -1

func _integrate_forces(state: PhysicsDirectBodyState2D) -> void:
\tvar velocity := state.get_linear_velocity()

\tif _state == State.DYING:
\t\tpass
\telif _state == State.WALKING:
\t\tvar wall_side := 0.0
\t\tfor collider_index in state.get_contact_count():
\t\t\tvar collider := state.get_contact_collider_object(collider_index)
\t\t\tvar collision_normal := state.get_contact_local_normal(collider_index)
\t\t\tif collider is Bullet:
\t\t\t\t_state = State.DYING
\t\t\t\tbreak
\t\t\tif collision_normal.x > 0.9: wall_side = 1.0
\t\t\telif collision_normal.x < -0.9: wall_side = -1.0
\t\tif wall_side != 0 and wall_side != direction:
\t\t\tdirection = -direction
\t\t\t$Sprite2D.scale.x = -direction
\t\tvelocity.x = direction * WALK_SPEED

\tstate.set_linear_velocity(velocity)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "RigidBody2D", node_name: "Enemy"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "Sprite2D", node_name: "Sprite2D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Enemy", node_name: "CollisionShape2D", shape_type: "rectangle"}}
    ],
    description: "2D enemy RigidBody2D with wall-direction switching, bullet detection, and state enum pattern"
  },
  {
    pattern_name: "Terrain generator with static methods for voxel data",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/voxel/world/terrain_generator.gd",
    extends_class: "Resource",
    script_content: `class_name TerrainGenerator
extends Resource

const RANDOM_BLOCK_PROBABILITY = 0.015

static func empty() -> Dictionary[Vector3i, int]:
\treturn {}

static func random_blocks() -> Dictionary[Vector3i, int]:
\tvar random_data: Dictionary[Vector3i, int] = {}
\tfor x in Chunk.CHUNK_SIZE:
\t\tfor y in Chunk.CHUNK_SIZE:
\t\t\tfor z in Chunk.CHUNK_SIZE:
\t\t\t\tvar vec := Vector3i(x, y, z)
\t\t\t\tif randf() < RANDOM_BLOCK_PROBABILITY:
\t\t\t\t\trandom_data[vec] = randi() % 29 + 1
\treturn random_data

static func flat(chunk_position: Vector3i) -> Dictionary[Vector3i, int]:
\tvar data: Dictionary[Vector3i, int] = {}
\tif chunk_position.y != -1: return data
\tfor x in Chunk.CHUNK_SIZE:
\t\tfor z in Chunk.CHUNK_SIZE:
\t\t\tdata[Vector3i(x, 2, z)] = 3  # Grass
\t\t\tdata[Vector3i(x, 1, z)] = 2  # Dirt
\t\t\tdata[Vector3i(x, 0, z)] = 2  # Dirt
\treturn data`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Resource", node_name: "TerrainGenerator"}},
      {tool: "attach_script", arguments: {node_path: "TerrainGenerator", script_content: "class_name TerrainGenerator\nextends Resource\n\nstatic func flat(chunk_position: Vector3i) -> Dictionary:\n\tvar data := {}\n\treturn data"}}
    ],
    description: "Voxel terrain generator using typed Dictionary[Vector3i, int] with static methods for different terrain types"
  },
  {
    pattern_name: "Runtime save/load with file format detection and multi-type preview",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "loading/runtime_save_load/runtime_save_load.gd",
    extends_class: "Control",
    script_content: `extends Control

var zip_reader := ZIPReader.new()

func open_file(path: String) -> void:
\tvar path_lower := path.to_lower()
\tif path_lower.ends_with(".png") or path_lower.ends_with(".jpg"):
\t\tvar image := Image.load_from_file(path)
\t\ttexture_viewer.texture = ImageTexture.create_from_image(image)
\telif path_lower.ends_with(".ogg"):
\t\taudio_stream_player.stream = AudioStreamOggVorbis.load_from_file(path)
\telif path_lower.ends_with(".gltf") or path_lower.ends_with(".glb"):
\t\tvar gltf_document := GLTFDocument.new()
\t\tvar gltf_state := GLTFState.new()
\t\tvar error := gltf_document.append_from_file(path, gltf_state)
\t\tif error == OK:
\t\t\tscene_viewer_root_node = gltf_document.generate_scene(gltf_state)
\telif path_lower.ends_with(".zip"):
\t\tzip_reader.open(path)
\t\tfor file in zip_reader.get_files():
\t\t\tzip_viewer_file_list.add_item(file)
\telse:
\t\tvar file_contents := FileAccess.get_file_as_string(path)
\t\tplain_text_viewer_label.text = file_contents`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Control", node_name: "FileViewer"}},
      {tool: "add_child_node", arguments: {parent_path: "FileViewer", node_type: "TextureRect", node_name: "TextureViewer"}},
      {tool: "add_child_node", arguments: {parent_path: "FileViewer", node_type: "AudioStreamPlayer", node_name: "AudioPlayer"}},
      {tool: "add_child_node", arguments: {parent_path: "FileViewer", node_type: "FileDialog", node_name: "FileDialog"}}
    ],
    description: "File viewer/manager with automatic format detection for images, audio, 3D scenes, and ZIP archives"
  },
  {
    pattern_name: "2D top-down player with Area2D collision and clamp",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/bullet_shower/player.gd",
    extends_class: "Area2D",
    script_content: `extends Area2D

@export var speed = 400
var screen_size

func _ready():
\tscreen_size = get_viewport_rect().size

func _process(delta):
\tvar velocity = Vector2.ZERO
\tif Input.is_action_pressed(&"move_right"): velocity.x += 1
\tif Input.is_action_pressed(&"move_left"): velocity.x -= 1
\tif Input.is_action_pressed(&"move_down"): velocity.y += 1
\tif Input.is_action_pressed(&"move_up"): velocity.y -= 1

\tif velocity.length() > 0:
\t\tvelocity = velocity.normalized() * speed
\t\tposition += velocity * delta
\tposition = position.clamp(Vector2.ZERO, screen_size)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "Area2D", node_name: "Player"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "circle", parameters: {radius: 20}}}
    ],
    description: "Minimal 2D top-down movement with Area2D, velocity normalization, and screen clamping"
  },
  {
    pattern_name: "3D CharacterBody3D with camera follow and physics interpolation",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/physics_interpolation/player.gd",
    extends_class: "CharacterBody3D",
    script_content: `extends CharacterBody3D

const SPEED = 5.0
const JUMP_VELOCITY = 4.5

@onready var gravity: float = ProjectSettings.get_setting("physics/3d/default_gravity")

func _physics_process(delta: float) -> void:
\tif not is_on_floor():
\t\tvelocity.y -= gravity * delta

\tif Input.is_action_just_pressed(&"jump") and is_on_floor():
\t\tvelocity.y = JUMP_VELOCITY

\tvar input_dir := Input.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
\tvar direction := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()
\tif direction:
\t\tvelocity.x = direction.x * SPEED
\t\tvelocity.z = direction.z * SPEED
\telse:
\t\tvelocity.x = move_toward(velocity.x, 0, SPEED)
\t\tvelocity.z = move_toward(velocity.z, 0, SPEED)

\tmove_and_slide()`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Player"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "capsule", parameters: {radius: 0.5, height: 1.6}}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "MeshInstance3D", node_name: "Mesh"}}
    ],
    description: "Standard Godot 4 CharacterBody3D first-person controller template with gravity, jump, and move_toward deceleration"
  },
  {
    pattern_name: "2D RigidBody2D enemy with patrol and direction switching",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/physics_platformer/enemy/enemy.gd",
    extends_class: "RigidBody2D",
    script_content: `class_name Enemy
extends RigidBody2D

const WALK_SPEED = 50
enum State { WALKING, DYING }
var _state := State.WALKING
var direction := -1

@onready var rc_left := $RaycastLeft as RayCast2D
@onready var rc_right := $RaycastRight as RayCast2D

func _integrate_forces(state: PhysicsDirectBodyState2D) -> void:
\tvar velocity := state.get_linear_velocity()
\tif _state == State.WALKING:
\t\tif direction < 0 and not rc_left.is_colliding() and rc_right.is_colliding():
\t\t\tdirection = -direction
\t\t\t$Sprite2D.scale.x = -direction
\t\tvelocity.x = direction * WALK_SPEED
\tstate.set_linear_velocity(velocity)`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "RigidBody2D", node_name: "Enemy"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "RayCast2D", node_name: "RaycastLeft"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "RayCast2D", node_name: "RaycastRight"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "Sprite2D", node_name: "Sprite2D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Enemy", node_name: "CollisionShape2D", shape_type: "rectangle"}}
    ],
    description: "2D enemy patrol using RigidBody2D with RayCast2D edge detection and direction switching"
  },
  {
    pattern_name: "2D top-down CharacterBody2D with move_and_slide",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/dynamic_tilemap_layers/player/player.gd",
    extends_class: "CharacterBody2D",
    script_content: `extends CharacterBody2D

@export var speed := 200.0

func _physics_process(_delta: float) -> void:
\tvar input_direction := Input.get_vector(&"move_left", &"move_right", &"move_up", &"move_down")
\tvelocity = input_direction * speed
\tmove_and_slide()`,
    tool_calls_needed: [
      {tool: "add_node", arguments: {node_type: "CharacterBody2D", node_name: "Player"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "circle", parameters: {radius: 16}}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Sprite2D", node_name: "Sprite2D"}},
      {tool: "create_input_action", arguments: {action_name: "move_left", events: [{type: "key", "physical_keycode": "A"}]}},
      {tool: "create_input_action", arguments: {action_name: "move_right", events: [{type: "key", "physical_keycode": "D"}]}},
      {tool: "create_input_action", arguments: {action_name: "move_up", events: [{type: "key", "physical_keycode": "W"}]}},
      {tool: "create_input_action", arguments: {action_name: "move_down", events: [{type: "key", "physical_keycode": "S"}]}}
    ],
    description: "Minimal 2D top-down CharacterBody2D with Input.get_vector and move_and_slide"
  }
];

fs.writeFileSync(path.join(outDir, 'gdscript_patterns.json'), JSON.stringify(gdscriptPatterns, null, 2));
console.log('gdscript_patterns.json written with', gdscriptPatterns.length, 'patterns');
