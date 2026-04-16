const fs = require('fs');
const path = require('path');
const outDir = __dirname;

function makeConv(userMsg, assistantToolCalls) {
  return {
    conversations: [
      {role: "user", content: userMsg},
      {role: "assistant", content: null, tool_calls: assistantToolCalls}
    ]
  };
}

const conversations = [];

conversations.push(makeConv("Create a 2D top-down player that moves with WASD and has sprite animation", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"Area2D","node_name":"Player"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"AnimatedSprite2D","node_name":"AnimatedSprite2D"}'}},
  {id: "call_3", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player","node_name":"CollisionShape2D","shape_type":"circle","parameters":{"radius":20}}'}},
  {id: "call_4", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_right","events":[{"type":"key","physical_keycode":"D"}]}'}},
  {id: "call_5", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_left","events":[{"type":"key","physical_keycode":"A"}]}'}},
  {id: "call_6", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_up","events":[{"type":"key","physical_keycode":"W"}]}'}},
  {id: "call_7", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_down","events":[{"type":"key","physical_keycode":"S"}]}'}},
  {id: "call_8", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Player","script_content":"extends Area2D\\n\\nsignal hit\\n\\n@export var speed = 400\\nvar screen_size\\n\\nfunc _ready():\\n\\tscreen_size = get_viewport_rect().size\\n\\nfunc _process(delta):\\n\\tvar velocity = Vector2.ZERO\\n\\tif Input.is_action_pressed(&\\\"move_right\\\"): velocity.x += 1\\n\\tif Input.is_action_pressed(&\\\"move_left\\\"): velocity.x -= 1\\n\\tif Input.is_action_pressed(&\\\"move_down\\\"): velocity.y += 1\\n\\tif Input.is_action_pressed(&\\\"move_up\\\"): velocity.y -= 1\\n\\tif velocity.length() > 0:\\n\\t\\tvelocity = velocity.normalized() * speed\\n\\t\\t$AnimatedSprite2D.play()\\n\\telse:\\n\\t\\t$AnimatedSprite2D.stop()\\n\\tposition += velocity * delta\\n\\tposition = position.clamp(Vector2.ZERO, screen_size)"}'}}
]));

conversations.push(makeConv("Create a 2D platformer player with CharacterBody2D, gravity, and jump", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"CharacterBody2D","node_name":"Player"}'}},
  {id: "call_2", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player","node_name":"CollisionShape2D","shape_type":"capsule","parameters":{"radius":10,"height":30}}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"Sprite2D","node_name":"Sprite2D"}'}},
  {id: "call_4", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_left","events":[{"type":"key","physical_keycode":"A"}]}'}},
  {id: "call_5", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_right","events":[{"type":"key","physical_keycode":"D"}]}'}},
  {id: "call_6", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"jump","events":[{"type":"key","physical_keycode":"Space"}]}'}},
  {id: "call_7", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Player","script_content":"extends CharacterBody2D\\n\\nconst WALK_FORCE = 600\\nconst WALK_MAX_SPEED = 200\\nconst STOP_FORCE = 1300\\nconst JUMP_SPEED = 200\\n\\n@onready var gravity := float(ProjectSettings.get_setting(\\\"physics/2d/default_gravity\\\"))\\n\\nfunc _physics_process(delta: float) -> void:\\n\\tvar walk := WALK_FORCE * (Input.get_axis(&\\\"move_left\\\", &\\\"move_right\\\"))\\n\\tif abs(walk) < WALK_FORCE * 0.2:\\n\\t\\tvelocity.x = move_toward(velocity.x, 0, STOP_FORCE * delta)\\n\\telse:\\n\\t\\tvelocity.x += walk * delta\\n\\tvelocity.x = clamp(velocity.x, -WALK_MAX_SPEED, WALK_MAX_SPEED)\\n\\tvelocity.y += gravity * delta\\n\\tmove_and_slide()\\n\\tif is_on_floor() and Input.is_action_just_pressed(&\\\"jump\\\"):\\n\\t\\tvelocity.y = -JUMP_SPEED"}'}}
]));

conversations.push(makeConv("Create a 3D platformer player with camera-relative WASD movement and jump", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"CharacterBody3D","node_name":"Player"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"Node3D","node_name":"Target"}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player/Target","node_type":"Camera3D","node_name":"Camera3D"}'}},
  {id: "call_4", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player","node_name":"CollisionShape3D","shape_type":"capsule","parameters":{"radius":0.5,"height":1.6}}'}},
  {id: "call_5", type: "function", function: {name: "add_primitive_mesh", arguments: '{"parent_path":"Player","node_name":"Mesh","mesh_type":"capsule"}'}},
  {id: "call_6", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Player","script_content":"extends CharacterBody3D\\n\\nconst MAX_SPEED = 3.5\\nconst JUMP_SPEED = 6.5\\nconst ACCELERATION = 4\\nconst DECELERATION = 4\\n\\n@onready var camera: Camera3D = $Target/Camera3D\\n@onready var gravity := float(-ProjectSettings.get_setting(\\\"physics/3d/default_gravity\\\"))\\n\\nfunc _physics_process(delta: float) -> void:\\n\\tvar dir := Vector3()\\n\\tdir.x = Input.get_axis(&\\\"move_left\\\", &\\\"move_right\\\")\\n\\tdir.z = Input.get_axis(&\\\"move_forward\\\", &\\\"move_back\\\")\\n\\tvar cam_basis := camera.global_transform.basis\\n\\tcam_basis = cam_basis.rotated(cam_basis.x, -cam_basis.get_euler().x)\\n\\tdir = cam_basis * dir\\n\\tif dir.length_squared() > 1: dir /= dir.length()\\n\\tvelocity.y += delta * gravity\\n\\tvar hvel := velocity; hvel.y = 0\\n\\tvar target := dir * MAX_SPEED\\n\\tvar acceleration := ACCELERATION if dir.dot(hvel) > 0 else DECELERATION\\n\\thvel = hvel.lerp(target, acceleration * delta)\\n\\tvelocity.x = hvel.x; velocity.z = hvel.z\\n\\tmove_and_slide()\\n\\tif is_on_floor() and Input.is_action_pressed(&\\\"jump\\\"):\\n\\t\\tvelocity.y = JUMP_SPEED"}'}}
]));

conversations.push(makeConv("Create a 3D character with mouse look, FPS style, crouch and sprint", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"CharacterBody3D","node_name":"Player"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"Node3D","node_name":"Head"}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player/Head","node_type":"Camera3D","node_name":"Camera3D"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player/Head","node_type":"RayCast3D","node_name":"RayCast3D"}'}},
  {id: "call_5", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player","node_name":"CollisionShape3D","shape_type":"capsule","parameters":{"radius":0.5,"height":1.8}}'}},
  {id: "call_6", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Player","script_content":"extends CharacterBody3D\\n\\nconst EYE_HEIGHT_STAND = 1.6\\nconst EYE_HEIGHT_CROUCH = 1.4\\nconst MOVEMENT_SPEED_GROUND = 70.0\\nconst MOVEMENT_SPEED_AIR = 13.0\\nconst MOVEMENT_JUMP_VELOCITY = 9.0\\n\\nvar _mouse_motion := Vector2()\\n\\n@onready var head: Node3D = $Head\\n@onready var camera: Camera3D = $Head/Camera3D\\n\\nfunc _ready() -> void:\\n\\tInput.mouse_mode = Input.MOUSE_MODE_CAPTURED\\n\\nfunc _process(_delta: float) -> void:\\n\\t_mouse_motion.y = clampf(_mouse_motion.y, -1560, 1560)\\n\\ttransform.basis = Basis.from_euler(Vector3(0, _mouse_motion.x * -0.001, 0))\\n\\thead.transform.basis = Basis.from_euler(Vector3(_mouse_motion.y * -0.001, 0, 0))\\n\\nfunc _physics_process(delta: float) -> void:\\n\\tvar crouching := Input.is_action_pressed(&\\\"crouch\\\")\\n\\tvar sprinting := Input.is_action_pressed(&\\\"move_sprint\\\")\\n\\thead.transform.origin.y = lerpf(head.transform.origin.y, EYE_HEIGHT_CROUCH if crouching else EYE_HEIGHT_STAND, 1.0 - exp(-delta * 16.0))\\n\\tvar movement_vec2 := Input.get_vector(&\\\"move_left\\\", &\\\"move_right\\\", &\\\"move_forward\\\", &\\\"move_back\\\")\\n\\tvar movement := transform.basis * Vector3(movement_vec2.x, 0, movement_vec2.y)\\n\\tif is_on_floor(): movement *= MOVEMENT_SPEED_GROUND\\n\\telse: movement *= MOVEMENT_SPEED_AIR\\n\\tif crouching: movement *= 0.5\\n\\tif sprinting: movement *= 1.375\\n\\tif not is_on_floor(): velocity += get_gravity() * delta\\n\\tvelocity += Vector3(movement.x, 0, movement.z) * delta\\n\\tvar friction_delta := exp(-(12.5 if is_on_floor() else 2.25) * delta)\\n\\tvelocity = Vector3(velocity.x * friction_delta, velocity.y, velocity.z * friction_delta)\\n\\tmove_and_slide()\\n\\tif is_on_floor() and Input.is_action_pressed(&\\\"jump\\\"): velocity.y = MOVEMENT_JUMP_VELOCITY\\n\\nfunc _input(input_event: InputEvent) -> void:\\n\\tif input_event is InputEventMouseMotion:\\n\\t\\tif Input.get_mouse_mode() == Input.MOUSE_MODE_CAPTURED:\\n\\t\\t\\t_mouse_motion += input_event.screen_relative"}'}}
]));

conversations.push(makeConv("Create a game HUD with score label, message, start button, and timer", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"CanvasLayer","node_name":"HUD"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"HUD","node_type":"Label","node_name":"ScoreLabel"}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"HUD","node_type":"Label","node_name":"MessageLabel"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"HUD","node_type":"Button","node_name":"StartButton"}'}},
  {id: "call_5", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"HUD","node_type":"Timer","node_name":"MessageTimer"}'}},
  {id: "call_6", type: "function", function: {name: "attach_script", arguments: '{"node_path":"HUD","script_content":"extends CanvasLayer\\n\\nsignal start_game\\n\\nfunc show_message(text):\\n\\t$MessageLabel.text = text\\n\\t$MessageLabel.show()\\n\\t$MessageTimer.start()\\n\\nfunc show_game_over():\\n\\tshow_message(\\\"Game Over\\\")\\n\\tawait $MessageTimer.timeout\\n\\t$MessageLabel.text = \\\"Dodge the\\\\nCreeps\\\"\\n\\t$MessageLabel.show()\\n\\tawait get_tree().create_timer(1).timeout\\n\\t$StartButton.show()\\n\\nfunc update_score(score):\\n\\t$ScoreLabel.text = str(score)\\n\\nfunc _on_StartButton_pressed():\\n\\t$StartButton.hide()\\n\\tstart_game.emit()\\n\\nfunc _on_MessageTimer_timeout():\\n\\t$MessageLabel.hide()"}'}}
]));

conversations.push(makeConv("Create an enemy mob that moves and auto-frees when off-screen", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"RigidBody2D","node_name":"Mob"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Mob","node_type":"AnimatedSprite2D","node_name":"AnimatedSprite2D"}'}},
  {id: "call_3", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Mob","node_name":"CollisionShape2D","shape_type":"capsule"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Mob","node_type":"VisibleOnScreenNotifier2D","node_name":"VisibilityNotifier2D"}'}},
  {id: "call_5", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Mob","script_content":"extends RigidBody2D\\n\\nfunc _ready():\\n\\tvar mob_types = Array($AnimatedSprite2D.sprite_frames.get_animation_names())\\n\\t$AnimatedSprite2D.animation = mob_types.pick_random()\\n\\t$AnimatedSprite2D.play()\\n\\nfunc _on_VisibilityNotifier2D_screen_exited():\\n\\tqueue_free()"}'}}
]));

conversations.push(makeConv("Create a state machine base class for game AI", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"Node","node_name":"StateMachine"}'}},
  {id: "call_2", type: "function", function: {name: "attach_script", arguments: '{"node_path":"StateMachine","script_content":"extends Node\\n\\nsignal state_changed(current_state: Node)\\n\\n@export var start_state: NodePath\\nvar states_map := {}\\nvar states_stack := []\\nvar current_state: Node = null\\nvar _active: bool = false\\n\\nfunc _enter_tree() -> void:\\n\\tif start_state.is_empty():\\n\\t\\tstart_state = get_child(0).get_path()\\n\\tfor child in get_children():\\n\\t\\tchild.finished.connect(_change_state)\\n\\tinitialize(start_state)\\n\\nfunc initialize(initial_state: NodePath) -> void:\\n\\t_active = true\\n\\tstates_stack.push_front(get_node(initial_state))\\n\\tcurrent_state = states_stack[0]\\n\\tcurrent_state.enter()\\n\\nfunc _unhandled_input(input_event: InputEvent) -> void:\\n\\tcurrent_state.handle_input(input_event)\\n\\nfunc _physics_process(delta: float) -> void:\\n\\tcurrent_state.update(delta)\\n\\nfunc _change_state(state_name: String) -> void:\\n\\tif not _active: return\\n\\tcurrent_state.exit()\\n\\tif state_name == \\\"previous\\\":\\n\\t\\tstates_stack.pop_front()\\n\\telse:\\n\\t\\tstates_stack[0] = states_map[state_name]\\n\\tcurrent_state = states_stack[0]\\n\\tstate_changed.emit(current_state)\\n\\tif state_name != \\\"previous\\\": current_state.enter()"}'}}
]));

conversations.push(makeConv("Create a 2D click-to-move character with NavigationAgent2D", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"CharacterBody2D","node_name":"Character"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Character","node_type":"NavigationAgent2D","node_name":"NavigationAgent2D"}'}},
  {id: "call_3", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Character","node_name":"CollisionShape2D","shape_type":"circle","parameters":{"radius":15}}'}},
  {id: "call_4", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Character","script_content":"extends CharacterBody2D\\n\\nvar movement_speed := 200.0\\n\\n@onready var navigation_agent: NavigationAgent2D = $NavigationAgent2D\\n\\nfunc _ready() -> void:\\n\\tnavigation_agent.path_desired_distance = 2.0\\n\\tnavigation_agent.target_desired_distance = 2.0\\n\\nfunc _unhandled_input(input_event: InputEvent) -> void:\\n\\tif not input_event.is_action_pressed(&\\\"click\\\"): return\\n\\tnavigation_agent.target_position = get_global_mouse_position()\\n\\nfunc _physics_process(_delta: float) -> void:\\n\\tif navigation_agent.is_navigation_finished(): return\\n\\tvar next_path_position := navigation_agent.get_next_path_position()\\n\\tvelocity = global_position.direction_to(next_path_position) * movement_speed\\n\\tmove_and_slide()"}'}}
]));

conversations.push(makeConv("Create a 3D navigation character that clicks to move and visualizes the path", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"Marker3D","node_name":"Character"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Character","node_type":"NavigationAgent3D","node_name":"NavigationAgent3D"}'}},
  {id: "call_3", type: "function", function: {name: "add_primitive_mesh", arguments: '{"parent_path":"Character","node_name":"Robot","mesh_type":"capsule"}'}},
  {id: "call_4", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Character","script_content":"extends Marker3D\\n\\n@export var character_speed := 10.0\\n@export var show_path: bool = true\\n\\n@onready var _nav_agent := $NavigationAgent3D as NavigationAgent3D\\n\\nfunc _physics_process(delta: float) -> void:\\n\\tif _nav_agent.is_navigation_finished(): return\\n\\tvar next_position := _nav_agent.get_next_path_position()\\n\\tglobal_position = global_position.move_toward(next_position, delta * character_speed)\\n\\tvar offset := next_position - global_position\\n\\toffset.y = 0\\n\\tif not offset.is_zero_approx():\\n\\t\\tlook_at(global_position + offset, Vector3.UP)\\n\\nfunc set_target_position(target_position: Vector3) -> void:\\n\\t_nav_agent.set_target_position(target_position)"}'}}
]));

conversations.push(makeConv("Create a RigidBody3D player with ground detection using ShapeCast3D", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"RigidBody3D","node_name":"Player"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"ShapeCast3D","node_name":"ShapeCast3D"}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"Node3D","node_name":"Target"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player/Target","node_type":"Camera3D","node_name":"Camera3D"}'}},
  {id: "call_5", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player","node_name":"CollisionShape3D","shape_type":"capsule"}'}},
  {id: "call_6", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Player","script_content":"extends RigidBody3D\\n\\n@onready var shape_cast: ShapeCast3D = $ShapeCast3D\\n@onready var camera: Camera3D = $Target/Camera3D\\n\\nfunc _physics_process(delta: float) -> void:\\n\\tvar dir := Vector3()\\n\\tdir.x = Input.get_axis(&\\\"move_left\\\", &\\\"move_right\\\")\\n\\tdir.z = Input.get_axis(&\\\"move_forward\\\", &\\\"move_back\\\")\\n\\tvar cam_basis := camera.global_transform.basis\\n\\tcam_basis = cam_basis.rotated(cam_basis.x, -cam_basis.get_euler().x)\\n\\tdir = cam_basis * dir\\n\\tapply_central_impulse(dir.normalized() * 5.0 * delta)\\n\\tif on_ground():\\n\\t\\tapply_central_impulse(dir.normalized() * 10.0 * delta)\\n\\t\\tif Input.is_action_pressed(&\\\"jump\\\"): linear_velocity.y = 7\\n\\nfunc on_ground() -> bool:\\n\\treturn shape_cast.is_colliding()"}'}}
]));

conversations.push(makeConv("Create a 3D squash-the-creeps player with bounce and directional facing", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"CharacterBody3D","node_name":"Player"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"AnimationPlayer","node_name":"AnimationPlayer"}'}},
  {id: "call_3", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player","node_name":"CollisionShape3D","shape_type":"capsule"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"Area3D","node_name":"MobDetector"}'}},
  {id: "call_5", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player/MobDetector","node_name":"CollisionShape3D","shape_type":"sphere"}'}},
  {id: "call_6", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Player","script_content":"extends CharacterBody3D\\n\\nsignal hit\\n\\n@export var speed = 14\\n@export var jump_impulse = 20\\n@export var bounce_impulse = 16\\n@export var fall_acceleration = 75\\n\\nfunc _physics_process(delta):\\n\\tvar direction = Vector3.ZERO\\n\\tif Input.is_action_pressed(&\\\"move_right\\\"): direction.x += 1\\n\\tif Input.is_action_pressed(&\\\"move_left\\\"): direction.x -= 1\\n\\tif Input.is_action_pressed(&\\\"move_back\\\"): direction.z += 1\\n\\tif Input.is_action_pressed(&\\\"move_forward\\\"): direction.z -= 1\\n\\tif direction != Vector3.ZERO:\\n\\t\\tdirection = direction.normalized()\\n\\t\\tbasis = Basis.looking_at(direction)\\n\\t\\t$AnimationPlayer.speed_scale = 4\\n\\telse:\\n\\t\\t$AnimationPlayer.speed_scale = 1\\n\\tvelocity.x = direction.x * speed\\n\\tvelocity.z = direction.z * speed\\n\\tif is_on_floor() and Input.is_action_just_pressed(&\\\"jump\\\"): velocity.y += jump_impulse\\n\\tvelocity.y -= fall_acceleration * delta\\n\\tmove_and_slide()\\n\\tfor index in range(get_slide_collision_count()):\\n\\t\\tvar collision = get_slide_collision(index)\\n\\t\\tif collision.get_collider().is_in_group(&\\\"mob\\\"):\\n\\t\\t\\tif Vector3.UP.dot(collision.get_normal()) > 0.1:\\n\\t\\t\\t\\tcollision.get_collider().squash()\\n\\t\\t\\t\\tvelocity.y = bounce_impulse\\n\\t\\t\\t\\tbreak"}'}}
]));

conversations.push(makeConv("Create a vehicle with VehicleBody3D, steering, and engine force", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"VehicleBody3D","node_name":"Vehicle"}'}},
  {id: "call_2", type: "function", function: {name: "add_primitive_mesh", arguments: '{"parent_path":"Vehicle","node_name":"Body","mesh_type":"box"}'}},
  {id: "call_3", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Vehicle","node_name":"CollisionShape3D","shape_type":"box"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Vehicle","node_type":"VehicleWheel3D","node_name":"FrontLeftWheel"}'}},
  {id: "call_5", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Vehicle","node_type":"VehicleWheel3D","node_name":"FrontRightWheel"}'}},
  {id: "call_6", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Vehicle","node_type":"VehicleWheel3D","node_name":"RearLeftWheel"}'}},
  {id: "call_7", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Vehicle","node_type":"VehicleWheel3D","node_name":"RearRightWheel"}'}},
  {id: "call_8", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Vehicle","node_type":"AudioStreamPlayer3D","node_name":"EngineSound"}'}},
  {id: "call_9", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Vehicle","script_content":"extends VehicleBody3D\\n\\nconst STEER_SPEED = 1.5\\nconst STEER_LIMIT = 0.4\\n\\n@export var engine_force_value := 40.0\\nvar _steer_target := 0.0\\n\\nfunc _physics_process(delta: float) -> void:\\n\\t_steer_target = Input.get_axis(&\\\"turn_right\\\", &\\\"turn_left\\\")\\n\\t_steer_target *= STEER_LIMIT\\n\\tif Input.is_action_pressed(&\\\"accelerate\\\"):\\n\\t\\tengine_force = engine_force_value\\n\\telse:\\n\\t\\tengine_force = 0.0\\n\\tif Input.is_action_pressed(&\\\"reverse\\\"): engine_force = -engine_force_value\\n\\tsteering = move_toward(steering, _steer_target, STEER_SPEED * delta)"}'}}
]));

conversations.push(makeConv("Create a JSON save/load system with var_to_str for Vector2", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"Control","node_name":"SaveLoadUI"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"SaveLoadUI","node_type":"MarginContainer","node_name":"MarginContainer"}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"SaveLoadUI/MarginContainer","node_type":"VBoxContainer","node_name":"VBoxContainer"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"SaveLoadUI/MarginContainer/VBoxContainer","node_type":"Button","node_name":"SaveJSON"}'}},
  {id: "call_5", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"SaveLoadUI/MarginContainer/VBoxContainer","node_type":"Button","node_name":"LoadJSON"}'}},
  {id: "call_6", type: "function", function: {name: "attach_script", arguments: '{"node_path":"SaveLoadUI","script_content":"extends Control\\n\\nconst SAVE_PATH = \\\"user://save_json.json\\\"\\n\\nfunc save_game() -> void:\\n\\tvar file := FileAccess.open(SAVE_PATH, FileAccess.WRITE)\\n\\tvar save_dict := {player = {position = var_to_str(player.position), health = var_to_str(player.health)}}\\n\\tfile.store_line(JSON.stringify(save_dict))\\n\\nfunc load_game() -> void:\\n\\tvar file := FileAccess.open(SAVE_PATH, FileAccess.READ)\\n\\tvar json := JSON.new()\\n\\tjson.parse(file.get_line())\\n\\tvar save_dict := json.get_data() as Dictionary\\n\\tplayer.position = str_to_var(save_dict.player.position)\\n\\tplayer.health = str_to_var(save_dict.player.health)"}'}}
]));

conversations.push(makeConv("Create a multiplayer player with authority-based movement and @rpc", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"CharacterBody2D","node_name":"Player"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"AnimatedSprite2D","node_name":"anim"}'}},
  {id: "call_3", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player","node_name":"CollisionShape2D","shape_type":"circle"}'}},
  {id: "call_4", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Player","script_content":"extends CharacterBody2D\\n\\nconst MOTION_SPEED = 90.0\\n\\n@export var synced_position := Vector2()\\n@export var stunned: bool = false\\n\\nfunc _ready() -> void:\\n\\tif str(name).is_valid_int():\\n\\t\\t$Inputs/InputsSync.set_multiplayer_authority(str(name).to_int())\\n\\nfunc _physics_process(delta: float) -> void:\\n\\tif is_multiplayer_authority():\\n\\t\\tsynced_position = position\\n\\telse:\\n\\t\\tposition = synced_position\\n\\tif not stunned:\\n\\t\\tvelocity = inputs.motion * MOTION_SPEED\\n\\t\\tmove_and_slide()\\n\\n@rpc(\\\"call_local\\\")\\nfunc exploded(_by_who: int) -> void:\\n\\tif stunned: return\\n\\tstunned = true"}'}}
]));

conversations.push(makeConv("Create a drag and drop system for colors", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"ColorPickerButton","node_name":"ColorPicker"}'}},
  {id: "call_2", type: "function", function: {name: "attach_script", arguments: '{"node_path":"ColorPicker","script_content":"extends ColorPickerButton\\n\\nfunc _get_drag_data(_at_position: Vector2) -> Color:\\n\\tvar cpb := ColorPickerButton.new()\\n\\tcpb.color = color\\n\\tcpb.size = Vector2(80.0, 50.0)\\n\\tvar preview := Control.new()\\n\\tpreview.add_child(cpb)\\n\\tcpb.position = -0.5 * cpb.size\\n\\tset_drag_preview(preview)\\n\\treturn color\\n\\nfunc _can_drop_data(_at_position: Vector2, data: Variant) -> bool:\\n\\treturn typeof(data) == TYPE_COLOR\\n\\nfunc _drop_data(_at_position: Vector2, data: Variant) -> void:\\n\\tcolor = data"}'}}
]));

conversations.push(makeConv("Create a dynamic split-screen system with two cameras and shader", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"Node3D","node_name":"CameraController"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"CameraController","node_type":"SubViewport","node_name":"Viewport1"}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"CameraController/Viewport1","node_type":"Camera3D","node_name":"Camera1"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"CameraController","node_type":"SubViewport","node_name":"Viewport2"}'}},
  {id: "call_5", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"CameraController/Viewport2","node_type":"Camera3D","node_name":"Camera2"}'}},
  {id: "call_6", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"CameraController","node_type":"TextureRect","node_name":"View"}'}},
  {id: "call_7", type: "function", function: {name: "attach_script", arguments: '{"node_path":"CameraController","script_content":"extends Node3D\\n\\n@export var max_separation := 20.0\\n\\n@onready var player1 := $\\\"../Player1\\\"\\n@onready var player2 := $\\\"../Player2\\\"\\n@onready var view := $View\\n@onready var camera1 := $Viewport1/Camera1\\n@onready var camera2 := $Viewport2/Camera2\\n\\nfunc _ready() -> void:\\n\\tview.material.set_shader_parameter(&\\\"viewport1\\\", $Viewport1.get_texture())\\n\\tview.material.set_shader_parameter(&\\\"viewport2\\\", $Viewport2.get_texture())\\n\\nfunc _process(_delta: float) -> void:\\n\\t_move_cameras()\\n\\tvar screen_size := get_viewport().get_visible_rect().size\\n\\tview.material.set_shader_parameter(&\\\"player1_position\\\", camera1.unproject_position(player1.position) / screen_size)\\n\\tview.material.set_shader_parameter(&\\\"player2_position\\\", camera2.unproject_position(player2.position) / screen_size)"}'}}
]));

conversations.push(makeConv("Create a high-performance bullet system using PhysicsServer2D RIDs", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"Node2D","node_name":"BulletShower"}'}},
  {id: "call_2", type: "function", function: {name: "attach_script", arguments: '{"node_path":"BulletShower","script_content":"extends Node2D\\n\\nconst BULLET_COUNT = 500\\nconst SPEED_MIN = 20\\nconst SPEED_MAX = 80\\n\\nvar bullets := []\\nvar shape := RID()\\n\\nclass Bullet:\\n\\tvar position := Vector2()\\n\\tvar speed := 1.0\\n\\tvar body := RID()\\n\\nfunc _ready() -> void:\\n\\tshape = PhysicsServer2D.circle_shape_create()\\n\\tPhysicsServer2D.shape_set_data(shape, 8)\\n\\tfor _i in BULLET_COUNT:\\n\\t\\tvar bullet := Bullet.new()\\n\\t\\tbullet.speed = randf_range(SPEED_MIN, SPEED_MAX)\\n\\t\\tbullet.body = PhysicsServer2D.body_create()\\n\\t\\tPhysicsServer2D.body_set_space(bullet.body, get_world_2d().get_space())\\n\\t\\tPhysicsServer2D.body_add_shape(bullet.body, shape)\\n\\t\\tPhysicsServer2D.body_set_collision_mask(bullet.body, 0)\\n\\t\\tbullets.push_back(bullet)\\n\\nfunc _physics_process(delta: float) -> void:\\n\\tvar transform2d := Transform2D()\\n\\tfor bullet: Bullet in bullets:\\n\\t\\tbullet.position.x -= bullet.speed * delta\\n\\t\\ttransform2d.origin = bullet.position\\n\\t\\tPhysicsServer2D.body_set_state(bullet.body, PhysicsServer2D.BODY_STATE_TRANSFORM, transform2d)\\n\\nfunc _exit_tree() -> void:\\n\\tfor bullet: Bullet in bullets:\\n\\t\\tPhysicsServer2D.free_rid(bullet.body)\\n\\tPhysicsServer2D.free_rid(shape)\\n\\tbullets.clear()"}'}}
]));

conversations.push(makeConv("Create an audio effects control panel with bus toggle", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"Control","node_name":"AudioEffects"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"AudioEffects","node_type":"VBoxContainer","node_name":"SoundEffects"}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"AudioEffects/SoundEffects","node_type":"AudioStreamPlayer","node_name":"Music"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"AudioEffects/SoundEffects","node_type":"AudioStreamPlayer","node_name":"Ding"}'}},
  {id: "call_5", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"AudioEffects","node_type":"CheckButton","node_name":"ToggleReverb"}'}},
  {id: "call_6", type: "function", function: {name: "attach_script", arguments: '{"node_path":"AudioEffects","script_content":"extends Control\\n\\nfunc _on_toggle_music_toggled(button_pressed: bool) -> void:\\n\\tif button_pressed: $SoundEffects/Music.play()\\n\\telse: $SoundEffects/Music.stop()\\n\\nfunc _on_ding_button_pressed() -> void:\\n\\t$SoundEffects/Ding.play()\\n\\nfunc _on_toggle_reverb_toggled(button_pressed: bool) -> void:\\n\\tAudioServer.set_bus_effect_enabled(0, 16, button_pressed)"}'}}
]));

conversations.push(makeConv("Create a TileMapLayer with dynamic transparency for secret areas", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"TileMapLayer","node_name":"SecretLayer"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"SecretLayer","node_type":"Area2D","node_name":"SecretDetector"}'}},
  {id: "call_3", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"SecretLayer/SecretDetector","node_name":"CollisionShape2D","shape_type":"rectangle"}'}},
  {id: "call_4", type: "function", function: {name: "attach_script", arguments: '{"node_path":"SecretLayer","script_content":"extends TileMapLayer\\n\\nvar player_in_secret: bool = false\\nvar layer_alpha := 1.0\\n\\nfunc _ready() -> void:\\n\\tset_process(false)\\n\\nfunc _process(delta: float) -> void:\\n\\tif player_in_secret:\\n\\t\\tif layer_alpha > 0.3:\\n\\t\\t\\tlayer_alpha = move_toward(layer_alpha, 0.3, delta)\\n\\t\\t\\tself_modulate = Color(1, 1, 1, layer_alpha)\\n\\telse:\\n\\t\\tif layer_alpha < 1.0:\\n\\t\\t\\tlayer_alpha = move_toward(layer_alpha, 1.0, delta)\\n\\t\\t\\tself_modulate = Color(1, 1, 1, layer_alpha)\\n\\nfunc _use_tile_data_runtime_update(_coords: Vector2i) -> bool:\\n\\treturn true\\n\\nfunc _tile_data_runtime_update(_coords: Vector2i, tile_data: TileData) -> void:\\n\\ttile_data.set_collision_polygons_count(0, 0)\\n\\nfunc _on_secret_detector_body_entered(body: Node2D) -> void:\\n\\tif body is not CharacterBody2D: return\\n\\tplayer_in_secret = true\\n\\tset_process(true)"}'}}
]));

conversations.push(makeConv("Create a 2D RigidBody2D enemy with patrol AI and wall detection", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"RigidBody2D","node_name":"Enemy"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Enemy","node_type":"Sprite2D","node_name":"Sprite2D"}'}},
  {id: "call_3", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Enemy","node_name":"CollisionShape2D","shape_type":"rectangle"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Enemy","node_type":"RayCast2D","node_name":"RaycastLeft"}'}},
  {id: "call_5", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Enemy","node_type":"RayCast2D","node_name":"RaycastRight"}'}},
  {id: "call_6", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Enemy","script_content":"extends RigidBody2D\\n\\nconst WALK_SPEED = 50\\nenum State { WALKING, DYING }\\nvar _state := State.WALKING\\nvar direction := -1\\n\\nfunc _integrate_forces(state: PhysicsDirectBodyState2D) -> void:\\n\\tvar velocity := state.get_linear_velocity()\\n\\tif _state == State.WALKING:\\n\\t\\tif direction < 0 and not $RaycastLeft.is_colliding():\\n\\t\\t\\tdirection = -direction\\n\\t\\t\\t$Sprite2D.scale.x = -direction\\n\\t\\tvelocity.x = direction * WALK_SPEED\\n\\tstate.set_linear_velocity(velocity)"}'}}
]));

conversations.push(makeConv("Create a main game scene with mob spawning on Path2D", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"Node","node_name":"Main"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main","node_type":"ColorRect","node_name":"Background"}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main","node_type":"Timer","node_name":"MobTimer"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main","node_type":"Timer","node_name":"ScoreTimer"}'}},
  {id: "call_5", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main","node_type":"Path2D","node_name":"MobPath"}'}},
  {id: "call_6", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main/MobPath","node_type":"PathFollow2D","node_name":"MobSpawnLocation"}'}},
  {id: "call_7", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main","node_type":"AudioStreamPlayer","node_name":"Music"}'}},
  {id: "call_8", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main","node_type":"Marker2D","node_name":"StartPosition"}'}}
]));

conversations.push(makeConv("Create a 2D top-down player with simple 4-directional movement", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"CharacterBody2D","node_name":"Player"}'}},
  {id: "call_2", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player","node_name":"CollisionShape2D","shape_type":"circle","parameters":{"radius":16}}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"Sprite2D","node_name":"Sprite2D"}'}},
  {id: "call_4", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Player","script_content":"extends CharacterBody2D\\n\\n@export var speed := 200.0\\n\\nfunc _physics_process(_delta: float) -> void:\\n\\tvar input_direction := Input.get_vector(&\\\"move_left\\\", &\\\"move_right\\\", &\\\"move_up\\\", &\\\"move_down\\\")\\n\\tvelocity = input_direction * speed\\n\\tmove_and_slide()"}'}}
]));

conversations.push(makeConv("Create a 3D mob that spawns facing the player with random speed", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"CharacterBody3D","node_name":"Mob"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Mob","node_type":"AnimationPlayer","node_name":"AnimationPlayer"}'}},
  {id: "call_3", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Mob","node_name":"CollisionShape3D","shape_type":"box"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Mob","node_type":"VisibleOnScreenNotifier3D","node_name":"VisibleOnScreenNotifier3D"}'}},
  {id: "call_5", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Mob","script_content":"extends CharacterBody3D\\n\\nsignal squashed\\n\\n@export var min_speed = 10\\n@export var max_speed = 18\\n\\nfunc _physics_process(_delta):\\n\\tmove_and_slide()\\n\\nfunc initialize(start_position, player_position):\\n\\tvar target = Vector3(player_position.x, start_position.y, player_position.z)\\n\\tlook_at_from_position(start_position, target, Vector3.UP)\\n\\trotate_y(randf_range(-PI / 4, PI / 4))\\n\\tvar random_speed = randf_range(min_speed, max_speed)\\n\\tvelocity = Vector3.FORWARD * random_speed\\n\\tvelocity = velocity.rotated(Vector3.UP, rotation.y)\\n\\t$AnimationPlayer.speed_scale = random_speed / min_speed\\n\\nfunc squash():\\n\\tsquashed.emit()\\n\\tqueue_free()\\n\\nfunc _on_visible_on_screen_notifier_screen_exited():\\n\\tqueue_free()"}'}}
]));

conversations.push(makeConv("Set up a Godot project for a 3D platformer with WASD + jump inputs and physics interpolation", [
  {id: "call_1", type: "function", function: {name: "configure_project", arguments: '{"settings":{"application/config/name":"Platformer 3D","application/run/main_scene":"res://game.tscn","display/window/stretch/mode":"canvas_items","display/window/stretch/aspect":"expand","physics/common/physics_ticks_per_second":120,"physics/3d/default_gravity":22.0,"physics/common/physics_interpolation":true,"rendering/anti_aliasing/quality/msaa_3d":2}}'}},
  {id: "call_2", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_forward","events":[{"type":"key","physical_keycode":"W"},{"type":"key","physical_keycode":"Up"}]}'}},
  {id: "call_3", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_back","events":[{"type":"key","physical_keycode":"S"},{"type":"key","physical_keycode":"Down"}]}'}},
  {id: "call_4", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_left","events":[{"type":"key","physical_keycode":"A"},{"type":"key","physical_keycode":"Left"}]}'}},
  {id: "call_5", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_right","events":[{"type":"key","physical_keycode":"D"},{"type":"key","physical_keycode":"Right"}]}'}},
  {id: "call_6", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"jump","events":[{"type":"key","physical_keycode":"Space"}]}'}}
]));

conversations.push(makeConv("Set up a Godot project for a multiplayer bomber game with networking inputs", [
  {id: "call_1", type: "function", function: {name: "configure_project", arguments: '{"settings":{"application/config/name":"Multiplayer Bomber","application/run/main_scene":"res://lobby.tscn","display/window/stretch/mode":"canvas_items","rendering/renderer/rendering_method":"gl_compatibility"}}'}},
  {id: "call_2", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_up","events":[{"type":"key","physical_keycode":"W"}]}'}},
  {id: "call_3", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_down","events":[{"type":"key","physical_keycode":"S"}]}'}},
  {id: "call_4", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_left","events":[{"type":"key","physical_keycode":"A"}]}'}},
  {id: "call_5", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_right","events":[{"type":"key","physical_keycode":"D"}]}'}},
  {id: "call_6", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"set_bomb","events":[{"type":"key","physical_keycode":"Space"}]}'}}
]));

conversations.push(makeConv("Create a PhantomCamera2D player with interaction system", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"CharacterBody2D","node_name":"Player"}'}},
  {id: "call_2", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player","node_name":"CollisionShape2D","shape_type":"capsule"}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"Sprite2D","node_name":"PlayerSprite"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"Area2D","node_name":"PlayerArea2D"}'}},
  {id: "call_5", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"PhantomCamera2D","node_name":"PlayerPhantomCamera2D"}'}},
  {id: "call_6", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Player","script_content":"extends CharacterBody2D\\n\\nconst SPEED = 350.0\\nconst JUMP_VELOCITY = -750.0\\nvar gravity: int = 2400\\nvar _movement_disabled: bool = false\\n\\nfunc _physics_process(delta: float) -> void:\\n\\tif not is_on_floor(): velocity.y += gravity * delta\\n\\tif Input.is_action_just_pressed(\\\"ui_accept\\\") and is_on_floor(): velocity.y = JUMP_VELOCITY\\n\\tif _movement_disabled: return\\n\\tvar input_dir := Input.get_axis(\\\"move_left\\\", \\\"move_right\\\")\\n\\tif input_dir:\\n\\t\\tvelocity.x = input_dir * SPEED\\n\\telse:\\n\\t\\tvelocity.x = move_toward(velocity.x, 0, SPEED)\\n\\tmove_and_slide()"}'}}
]));

conversations.push(makeConv("Create a PhantomCamera3D player controller with camera-relative movement", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"CharacterBody3D","node_name":"Player"}'}},
  {id: "call_2", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player","node_name":"CollisionShape3D","shape_type":"capsule"}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"PhantomCamera3D","node_name":"PlayerPhantomCamera3D"}'}},
  {id: "call_4", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Player","script_content":"extends CharacterBody3D\\n\\n@export var SPEED: float = 5.0\\n@export var JUMP_VELOCITY: float = 4.5\\nvar gravity: float = 9.8\\nvar movement_enabled: bool = true\\n\\nfunc _physics_process(delta: float) -> void:\\n\\tif not is_on_floor(): velocity.y -= gravity * delta\\n\\tif not movement_enabled: return\\n\\tvar input_dir := Input.get_vector(\\\"move_left\\\", \\\"move_right\\\", \\\"move_up\\\", \\\"move_down\\\")\\n\\tvar direction := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()\\n\\tif direction:\\n\\t\\tvar move_dir := Vector3(direction.x, 0, direction.z)\\n\\t\\tmove_dir = move_dir.rotated(Vector3.UP, _camera.rotation.y).normalized()\\n\\t\\tvelocity.x = move_dir.x * SPEED\\n\\t\\tvelocity.z = move_dir.z * SPEED\\n\\telse:\\n\\t\\tvelocity.x = move_toward(velocity.x, 0, SPEED)\\n\\t\\tvelocity.z = move_toward(velocity.z, 0, SPEED)\\n\\tmove_and_slide()"}'}}
]));

conversations.push(makeConv("Create a voxel terrain generator with flat and random modes", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"Resource","node_name":"TerrainGenerator"}'}},
  {id: "call_2", type: "function", function: {name: "attach_script", arguments: '{"node_path":"TerrainGenerator","script_content":"class_name TerrainGenerator\\nextends Resource\\n\\nstatic func empty() -> Dictionary:\\n\\treturn {}\\n\\nstatic func random_blocks() -> Dictionary:\\n\\tvar random_data := {}\\n\\tfor x in 16:\\n\\t\\tfor y in 16:\\n\\t\\t\\tfor z in 16:\\n\\t\\t\\t\\tif randf() < 0.015:\\n\\t\\t\\t\\t\\trandom_data[Vector3i(x, y, z)] = randi() % 29 + 1\\n\\treturn random_data\\n\\nstatic func flat(chunk_position: Vector3i) -> Dictionary:\\n\\tvar data := {}\\n\\tif chunk_position.y != -1: return data\\n\\tfor x in 16:\\n\\t\\tfor z in 16:\\n\\t\\t\\tdata[Vector3i(x, 2, z)] = 3\\n\\t\\t\\tdata[Vector3i(x, 1, z)] = 2\\n\\t\\t\\tdata[Vector3i(x, 0, z)] = 2\\n\\treturn data"}'}}
]));

conversations.push(makeConv("Create an idle state that transitions to move on input", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"Node","node_name":"Idle"}'}},
  {id: "call_2", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Idle","script_content":"extends \\\"on_ground.gd\\\"\\n\\nfunc enter() -> void:\\n\\towner.get_node(^\\\"AnimationPlayer\\\").play(PLAYER_STATE.idle)\\n\\nfunc update(_delta: float) -> void:\\n\\tvar input_direction := get_input_direction()\\n\\tif input_direction:\\n\\t\\tfinished.emit(PLAYER_STATE.move)"}'}}
]));

conversations.push(makeConv("Create a walk state with run/walk speed toggle", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"Node","node_name":"Move"}'}},
  {id: "call_2", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Move","script_content":"extends \\\"on_ground.gd\\\"\\n\\n@export var max_walk_speed := 450.0\\n@export var max_run_speed := 700.0\\n\\nfunc enter() -> void:\\n\\tspeed = 0.0\\n\\tvelocity = Vector2()\\n\\towner.get_node(^\\\"AnimationPlayer\\\").play(PLAYER_STATE.walk)\\n\\nfunc update(_delta: float) -> void:\\n\\tvar input_direction := get_input_direction()\\n\\tif input_direction.is_zero_approx():\\n\\t\\tfinished.emit(PLAYER_STATE.idle)\\n\\tif Input.is_action_pressed(&\\\"run\\\"):\\n\\t\\tspeed = max_run_speed\\n\\telse:\\n\\t\\tspeed = max_walk_speed\\n\\tvar collision_info := move(speed, input_direction)\\n\\nfunc move(p_speed: float, direction: Vector2) -> KinematicCollision2D:\\n\\towner.velocity = direction.normalized() * p_speed\\n\\towner.move_and_slide()\\n\\tif owner.get_slide_collision_count() == 0: return null\\n\\treturn owner.get_slide_collision(0)"}'}}
]));

conversations.push(makeConv("Create a RigidBody2D platformer player with _integrate_forces", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"RigidBody2D","node_name":"Player"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"Sprite2D","node_name":"Sprite2D"}'}},
  {id: "call_3", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player","node_name":"CollisionShape2D","shape_type":"capsule"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"AnimationPlayer","node_name":"AnimationPlayer"}'}},
  {id: "call_5", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Player","node_type":"AudioStreamPlayer2D","node_name":"SoundJump"}'}},
  {id: "call_6", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Player","script_content":"extends RigidBody2D\\n\\nconst WALK_ACCEL = 1000.0\\nconst WALK_MAX_VELOCITY = 200.0\\nconst JUMP_VELOCITY = 380.0\\nconst MAX_FLOOR_AIRBORNE_TIME = 0.15\\n\\nvar jumping: bool = false\\nvar airborne_time: float = 1e20\\n\\nfunc _integrate_forces(state: PhysicsDirectBodyState2D) -> void:\\n\\tvar velocity := state.get_linear_velocity()\\n\\tvar step := state.get_step()\\n\\tvar found_floor: bool = false\\n\\tfor contact_index in state.get_contact_count():\\n\\t\\tvar collision_normal := state.get_contact_local_normal(contact_index)\\n\\t\\tif collision_normal.dot(Vector2(0, -1)) > 0.6: found_floor = true\\n\\tif found_floor: airborne_time = 0.0\\n\\telse: airborne_time += step\\n\\tvar on_floor := airborne_time < MAX_FLOOR_AIRBORNE_TIME\\n\\tif on_floor:\\n\\t\\tif Input.is_action_pressed(&\\\"move_right\\\") and velocity.x < WALK_MAX_VELOCITY:\\n\\t\\t\\tvelocity.x += WALK_ACCEL * step\\n\\t\\tif not jumping and Input.is_action_pressed(&\\\"jump\\\"):\\n\\t\\t\\tvelocity.y = -JUMP_VELOCITY\\n\\t\\t\\tjumping = true\\n\\tvelocity += state.get_total_gravity() * step\\n\\tstate.set_linear_velocity(velocity)"}'}}
]));

conversations.push(makeConv("Create a 3D RigidBody3D enemy with raycast-based AI", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"RigidBody3D","node_name":"Enemy"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Enemy","node_type":"Node3D","node_name":"EnemyModel"}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Enemy/EnemyModel","node_type":"AnimationPlayer","node_name":"AnimationPlayer"}'}},
  {id: "call_4", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Enemy","node_name":"CollisionShape3D","shape_type":"box"}'}},
  {id: "call_5", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Enemy/EnemyModel","node_type":"RayCast3D","node_name":"RayFloor"}'}},
  {id: "call_6", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Enemy/EnemyModel","node_type":"RayCast3D","node_name":"RayWall"}'}},
  {id: "call_7", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Enemy","script_content":"extends RigidBody3D\\n\\nconst ACCEL: float = 5.0\\nconst MAX_SPEED: float = 2.0\\n\\nvar dying: bool = false\\n\\nfunc _integrate_forces(state: PhysicsDirectBodyState3D) -> void:\\n\\tvar delta := state.get_step()\\n\\tvar lin_velocity := state.get_linear_velocity()\\n\\tlin_velocity += state.get_total_gravity() * delta\\n\\tif dying:\\n\\t\\tstate.set_linear_velocity(lin_velocity)\\n\\t\\treturn\\n\\tvar advance: bool = $EnemyModel/RayFloor.is_colliding() and not $EnemyModel/RayWall.is_colliding()\\n\\tif advance:\\n\\t\\tif lin_velocity.length() < MAX_SPEED:\\n\\t\\t\\tlin_velocity += direction * ACCEL * delta\\n\\tstate.set_linear_velocity(lin_velocity)"}'}}
]));

conversations.push(makeConv("Set up a project for a 2D dodge game with WASD movement inputs", [
  {id: "call_1", type: "function", function: {name: "configure_project", arguments: '{"settings":{"application/config/name":"Dodge the Creeps","application/run/main_scene":"res://main.tscn","display/window/size/viewport_width":480,"display/window/size/viewport_height":720,"display/window/stretch/mode":"canvas_items","rendering/renderer/rendering_method":"gl_compatibility"}}'}},
  {id: "call_2", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_left","events":[{"type":"key","physical_keycode":"A"},{"type":"key","physical_keycode":"Left"}]}'}},
  {id: "call_3", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_right","events":[{"type":"key","physical_keycode":"D"},{"type":"key","physical_keycode":"Right"}]}'}},
  {id: "call_4", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_up","events":[{"type":"key","physical_keycode":"W"},{"type":"key","physical_keycode":"Up"}]}'}},
  {id: "call_5", type: "function", function: {name: "create_input_action", arguments: '{"action_name":"move_down","events":[{"type":"key","physical_keycode":"S"},{"type":"key","physical_keycode":"Down"}]}'}}
]));

conversations.push(makeConv("Create a 3D CharacterBody3D with standard first-person controller", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"CharacterBody3D","node_name":"Player"}'}},
  {id: "call_2", type: "function", function: {name: "add_collision_shape", arguments: '{"parent_path":"Player","node_name":"CollisionShape3D","shape_type":"capsule","parameters":{"radius":0.5,"height":1.6}}'}},
  {id: "call_3", type: "function", function: {name: "add_primitive_mesh", arguments: '{"parent_path":"Player","node_name":"Mesh","mesh_type":"capsule"}'}},
  {id: "call_4", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Player","script_content":"extends CharacterBody3D\\n\\nconst SPEED = 5.0\\nconst JUMP_VELOCITY = 4.5\\n\\n@onready var gravity: float = ProjectSettings.get_setting(\\\"physics/3d/default_gravity\\\")\\n\\nfunc _physics_process(delta: float) -> void:\\n\\tif not is_on_floor(): velocity.y -= gravity * delta\\n\\tif Input.is_action_just_pressed(&\\\"jump\\\") and is_on_floor(): velocity.y = JUMP_VELOCITY\\n\\tvar input_dir := Input.get_vector(&\\\"move_left\\\", &\\\"move_right\\\", &\\\"move_forward\\\", &\\\"move_back\\\")\\n\\tvar direction := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()\\n\\tif direction:\\n\\t\\tvelocity.x = direction.x * SPEED\\n\\t\\tvelocity.z = direction.z * SPEED\\n\\telse:\\n\\t\\tvelocity.x = move_toward(velocity.x, 0, SPEED)\\n\\t\\tvelocity.z = move_toward(velocity.z, 0, SPEED)\\n\\tmove_and_slide()"}'}}
]));

conversations.push(makeConv("Create a game with main scene, player, HUD, and mob spawning system", [
  {id: "call_1", type: "function", function: {name: "add_node", arguments: '{"node_type":"Node","node_name":"Main"}'}},
  {id: "call_2", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main","node_type":"ColorRect","node_name":"Background"}'}},
  {id: "call_3", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main","node_type":"Area2D","node_name":"Player"}'}},
  {id: "call_4", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main","node_type":"CanvasLayer","node_name":"HUD"}'}},
  {id: "call_5", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main","node_type":"Timer","node_name":"MobTimer"}'}},
  {id: "call_6", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main","node_type":"Timer","node_name":"ScoreTimer"}'}},
  {id: "call_7", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main","node_type":"Path2D","node_name":"MobPath"}'}},
  {id: "call_8", type: "function", function: {name: "add_child_node", arguments: '{"parent_path":"Main/MobPath","node_type":"PathFollow2D","node_name":"MobSpawnLocation"}'}},
  {id: "call_9", type: "function", function: {name: "attach_script", arguments: '{"node_path":"Main","script_content":"extends Node\\n\\n@export var mob_scene: PackedScene\\nvar score\\n\\nfunc game_over():\\n\\t$ScoreTimer.stop()\\n\\t$MobTimer.stop()\\n\\t$HUD.show_game_over()\\n\\nfunc new_game():\\n\\tget_tree().call_group(&\\\"mobs\\\", &\\\"queue_free\\\")\\n\\tscore = 0\\n\\t$Player.start($StartPosition.position)\\n\\t$StartTimer.start()\\n\\t$HUD.update_score(score)\\n\\nfunc _on_MobTimer_timeout():\\n\\tvar mob = mob_scene.instantiate()\\n\\tvar mob_spawn_location = get_node(^\\\"MobPath/MobSpawnLocation\\\")\\n\\tmob_spawn_location.progress_ratio = randf()\\n\\tmob.position = mob_spawn_location.position\\n\\tadd_child(mob)"}'}}
]));

const jsonl = conversations.map(c => JSON.stringify(c)).join('\n');
fs.writeFileSync(path.join(outDir, 'conversations_from_projects.jsonl'), jsonl);
console.log('conversations_from_projects.jsonl written with', conversations.length, 'conversations');
