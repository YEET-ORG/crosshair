const fs = require('fs');
const path = require('path');
const outDir = __dirname;

const scenePatterns = [
  {
    pattern_name: "2D Dodge Game Player (Area2D + AnimatedSprite2D + Collision)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/dodge_the_creeps/player.tscn",
    node_tree: {root_type: "Area2D", root_name: "Player", children: [
      {type: "AnimatedSprite2D", name: "AnimatedSprite2D", properties: {}},
      {type: "CollisionShape2D", name: "CollisionShape2D", properties: {shape: "CircleShape2D"}},
      {type: "Sprite2D", name: "Trail", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "Area2D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AnimatedSprite2D", node_name: "AnimatedSprite2D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "circle", parameters: {radius: 20}}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Sprite2D", node_name: "Trail"}}
    ]
  },
  {
    pattern_name: "2D RigidBody2D Mob with VisibilityNotifier",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/dodge_the_creeps/mob.tscn",
    node_tree: {root_type: "RigidBody2D", root_name: "Mob", children: [
      {type: "AnimatedSprite2D", name: "AnimatedSprite2D", properties: {}},
      {type: "CollisionShape2D", name: "CollisionShape2D", properties: {shape: "CapsuleShape2D"}},
      {type: "VisibleOnScreenNotifier2D", name: "VisibilityNotifier2D", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "RigidBody2D", node_name: "Mob"}},
      {tool: "add_child_node", arguments: {parent_path: "Mob", node_type: "AnimatedSprite2D", node_name: "AnimatedSprite2D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Mob", node_name: "CollisionShape2D", shape_type: "capsule"}},
      {tool: "add_child_node", arguments: {parent_path: "Mob", node_type: "VisibleOnScreenNotifier2D", node_name: "VisibilityNotifier2D"}}
    ]
  },
  {
    pattern_name: "2D HUD (CanvasLayer + Labels + Button + Timer)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/dodge_the_creeps/hud.tscn",
    node_tree: {root_type: "CanvasLayer", root_name: "HUD", children: [
      {type: "Label", name: "ScoreLabel", properties: {}},
      {type: "Label", name: "MessageLabel", properties: {}},
      {type: "Button", name: "StartButton", properties: {}},
      {type: "Timer", name: "MessageTimer", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "CanvasLayer", node_name: "HUD"}},
      {tool: "add_child_node", arguments: {parent_path: "HUD", node_type: "Label", node_name: "ScoreLabel"}},
      {tool: "add_child_node", arguments: {parent_path: "HUD", node_type: "Label", node_name: "MessageLabel"}},
      {tool: "add_child_node", arguments: {parent_path: "HUD", node_type: "Button", node_name: "StartButton"}},
      {tool: "add_child_node", arguments: {parent_path: "HUD", node_type: "Timer", node_name: "MessageTimer"}}
    ]
  },
  {
    pattern_name: "3D Platformer Player (CharacterBody3D + Camera + AnimationTree)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/platformer/player/player.tscn",
    node_tree: {root_type: "CharacterBody3D", root_name: "Player", children: [
      {type: "Node3D", name: "Target", properties: {}, children: [
        {type: "Camera3D", name: "Camera3D", properties: {current: true}}
      ]},
      {type: "CollisionShape3D", name: "CollisionShape3D", properties: {shape: "CapsuleShape3D"}},
      {type: "AnimationTree", name: "AnimationTree", properties: {}},
      {type: "Node3D", name: "Player", properties: {}, children: [
        {type: "Skeleton3D", name: "Skeleton", properties: {}}
      ]}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Node3D", node_name: "Target"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/Target", node_type: "Camera3D", node_name: "Camera3D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "capsule", parameters: {radius: 0.5, height: 1.6}}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AnimationTree", node_name: "AnimationTree"}}
    ]
  },
  {
    pattern_name: "3D Kinematic Character (CharacterBody3D + Camera on pivot)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/kinematic_character/player/player.tscn",
    node_tree: {root_type: "CharacterBody3D", root_name: "Player", children: [
      {type: "Node3D", name: "Target", properties: {}, children: [
        {type: "Camera3D", name: "Camera3D", properties: {current: true}}
      ]},
      {type: "CollisionShape3D", name: "CollisionShape3D", properties: {shape: "BoxShape3D"}},
      {type: "MeshInstance3D", name: "Mesh", properties: {mesh: "BoxMesh"}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Node3D", node_name: "Target"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/Target", node_type: "Camera3D", node_name: "Camera3D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "box"}},
      {tool: "add_primitive_mesh", arguments: {parent_path: "Player", node_name: "Mesh", mesh_type: "box"}}
    ]
  },
  {
    pattern_name: "3D RigidBody Character (RigidBody3D + ShapeCast + Camera)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/rigidbody_character/player/player.tscn",
    node_tree: {root_type: "RigidBody3D", root_name: "Player", children: [
      {type: "ShapeCast3D", name: "ShapeCast3D", properties: {}},
      {type: "Node3D", name: "Target", properties: {}, children: [
        {type: "Camera3D", name: "Camera3D", properties: {}}
      ]},
      {type: "CollisionShape3D", name: "CollisionShape3D", properties: {shape: "CapsuleShape3D"}},
      {type: "MeshInstance3D", name: "Mesh", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "RigidBody3D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "ShapeCast3D", node_name: "ShapeCast3D"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Node3D", node_name: "Target"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/Target", node_type: "Camera3D", node_name: "Camera3D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "capsule"}},
      {tool: "add_primitive_mesh", arguments: {parent_path: "Player", node_name: "Mesh", mesh_type: "box"}}
    ]
  },
  {
    pattern_name: "3D Squash The Creeps Player (CharacterBody3D + MobDetector + AnimationPlayer)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/squash_the_creeps/Player.tscn",
    node_tree: {root_type: "CharacterBody3D", root_name: "Player", children: [
      {type: "AnimationPlayer", name: "AnimationPlayer", properties: {}},
      {type: "CollisionShape3D", name: "CollisionShape3D", properties: {shape: "CapsuleShape3D"}},
      {type: "Area3D", name: "MobDetector", properties: {}, children: [
        {type: "CollisionShape3D", name: "CollisionShape3D", properties: {shape: "SphereShape3D"}}
      ]}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AnimationPlayer", node_name: "AnimationPlayer"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "capsule"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Area3D", node_name: "MobDetector"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player/MobDetector", node_name: "CollisionShape3D", shape_type: "sphere"}}
    ]
  },
  {
    pattern_name: "3D Mob (CharacterBody3D + AnimationPlayer + VisibleOnScreenNotifier3D)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/squash_the_creeps/Mob.tscn",
    node_tree: {root_type: "CharacterBody3D", root_name: "Mob", children: [
      {type: "AnimationPlayer", name: "AnimationPlayer", properties: {}},
      {type: "CollisionShape3D", name: "CollisionShape3D", properties: {shape: "BoxShape3D"}},
      {type: "VisibleOnScreenNotifier3D", name: "VisibleOnScreenNotifier3D", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Mob"}},
      {tool: "add_child_node", arguments: {parent_path: "Mob", node_type: "AnimationPlayer", node_name: "AnimationPlayer"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Mob", node_name: "CollisionShape3D", shape_type: "box"}},
      {tool: "add_child_node", arguments: {parent_path: "Mob", node_type: "VisibleOnScreenNotifier3D", node_name: "VisibleOnScreenNotifier3D"}}
    ]
  },
  {
    pattern_name: "3D Navigation Character (Marker3D + NavigationAgent3D)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/navigation/character.tscn",
    node_tree: {root_type: "Marker3D", root_name: "Character", children: [
      {type: "NavigationAgent3D", name: "NavigationAgent3D", properties: {}},
      {type: "MeshInstance3D", name: "Robot", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "Marker3D", node_name: "Character"}},
      {tool: "add_child_node", arguments: {parent_path: "Character", node_type: "NavigationAgent3D", node_name: "NavigationAgent3D"}},
      {tool: "add_primitive_mesh", arguments: {parent_path: "Character", node_name: "Robot", mesh_type: "capsule"}}
    ]
  },
  {
    pattern_name: "VehicleBody3D Car (VehicleBody3D + Wheel + Camera + Audio)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/truck_town/vehicles/vehicle.tscn",
    node_tree: {root_type: "VehicleBody3D", root_name: "Vehicle", children: [
      {type: "MeshInstance3D", name: "Body", properties: {}},
      {type: "CollisionShape3D", name: "CollisionShape3D", properties: {shape: "BoxShape3D"}},
      {type: "Node3D", name: "CameraTarget", properties: {}, children: [
        {type: "Camera3D", name: "Camera3D", properties: {}}
      ]},
      {type: "AudioStreamPlayer3D", name: "EngineSound", properties: {}},
      {type: "VehicleWheel3D", name: "FrontLeftWheel", properties: {}},
      {type: "VehicleWheel3D", name: "FrontRightWheel", properties: {}},
      {type: "VehicleWheel3D", name: "RearLeftWheel", properties: {}},
      {type: "VehicleWheel3D", name: "RearRightWheel", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "VehicleBody3D", node_name: "Vehicle"}},
      {tool: "add_primitive_mesh", arguments: {parent_path: "Vehicle", node_name: "Body", mesh_type: "box"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Vehicle", node_name: "CollisionShape3D", shape_type: "box"}},
      {tool: "add_child_node", arguments: {parent_path: "Vehicle", node_type: "Node3D", node_name: "CameraTarget"}},
      {tool: "add_child_node", arguments: {parent_path: "Vehicle/CameraTarget", node_type: "Camera3D", node_name: "Camera3D"}},
      {tool: "add_child_node", arguments: {parent_path: "Vehicle", node_type: "VehicleWheel3D", node_name: "FrontLeftWheel"}},
      {tool: "add_child_node", arguments: {parent_path: "Vehicle", node_type: "VehicleWheel3D", node_name: "FrontRightWheel"}},
      {tool: "add_child_node", arguments: {parent_path: "Vehicle", node_type: "VehicleWheel3D", node_name: "RearLeftWheel"}},
      {tool: "add_child_node", arguments: {parent_path: "Vehicle", node_type: "VehicleWheel3D", node_name: "RearRightWheel"}},
      {tool: "add_child_node", arguments: {parent_path: "Vehicle", node_type: "AudioStreamPlayer3D", node_name: "EngineSound"}}
    ]
  },
  {
    pattern_name: "2D FSM Player (CharacterBody2D + AnimationPlayer + StateMachine)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/finite_state_machine/player/Player.tscn",
    node_tree: {root_type: "CharacterBody2D", root_name: "Player", children: [
      {type: "Sprite2D", name: "Body", properties: {}},
      {type: "CollisionShape2D", name: "CollisionShape2D", properties: {shape: "RectangleShape2D"}},
      {type: "AnimationPlayer", name: "AnimationPlayer", properties: {}},
      {type: "Node", name: "StateMachine", properties: {}, children: [
        {type: "Node", name: "Idle", properties: {}},
        {type: "Node", name: "Move", properties: {}},
        {type: "Node", name: "Jump", properties: {}},
        {type: "Node", name: "Attack", properties: {}}
      ]}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "CharacterBody2D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Sprite2D", node_name: "Body"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "rectangle"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AnimationPlayer", node_name: "AnimationPlayer"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Node", node_name: "StateMachine"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/StateMachine", node_type: "Node", node_name: "Idle"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/StateMachine", node_type: "Node", node_name: "Move"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/StateMachine", node_type: "Node", node_name: "Jump"}}
    ]
  },
  {
    pattern_name: "2D Navigation Character (CharacterBody2D + NavigationAgent2D)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/navigation/character.tscn",
    node_tree: {root_type: "CharacterBody2D", root_name: "Character", children: [
      {type: "NavigationAgent2D", name: "NavigationAgent2D", properties: {}},
      {type: "Sprite2D", name: "Sprite2D", properties: {}},
      {type: "CollisionShape2D", name: "CollisionShape2D", properties: {shape: "CircleShape2D"}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "CharacterBody2D", node_name: "Character"}},
      {tool: "add_child_node", arguments: {parent_path: "Character", node_type: "NavigationAgent2D", node_name: "NavigationAgent2D"}},
      {tool: "add_child_node", arguments: {parent_path: "Character", node_type: "Sprite2D", node_name: "Sprite2D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Character", node_name: "CollisionShape2D", shape_type: "circle"}}
    ]
  },
  {
    pattern_name: "2D Physics Platformer Player (RigidBody2D + AnimationPlayer + Audio)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/physics_platformer/player/player.tscn",
    node_tree: {root_type: "RigidBody2D", root_name: "Player", children: [
      {type: "Sprite2D", name: "Sprite2D", properties: {}},
      {type: "CollisionShape2D", name: "CollisionShape2D", properties: {}},
      {type: "AnimationPlayer", name: "AnimationPlayer", properties: {}},
      {type: "AudioStreamPlayer2D", name: "SoundJump", properties: {}},
      {type: "AudioStreamPlayer2D", name: "SoundShoot", properties: {}},
      {type: "Marker2D", name: "BulletShoot", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "RigidBody2D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Sprite2D", node_name: "Sprite2D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "capsule"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AnimationPlayer", node_name: "AnimationPlayer"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AudioStreamPlayer2D", node_name: "SoundJump"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AudioStreamPlayer2D", node_name: "SoundShoot"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Marker2D", node_name: "BulletShoot"}}
    ]
  },
  {
    pattern_name: "Multiplayer Bomber Player (CharacterBody2D + Inputs + AnimatedSprite2D)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "networking/multiplayer_bomber/player.tscn",
    node_tree: {root_type: "CharacterBody2D", root_name: "Player", children: [
      {type: "AnimatedSprite2D", name: "anim", properties: {}},
      {type: "CollisionShape2D", name: "CollisionShape2D", properties: {}},
      {type: "Label", name: "label", properties: {}},
      {type: "Sprite2D", name: "sprite", properties: {}},
      {type: "Node", name: "Inputs", properties: {}, children: [
        {type: "Node", name: "InputsSync", properties: {}}
      ]}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "CharacterBody2D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "AnimatedSprite2D", node_name: "anim"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "circle"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Label", node_name: "label"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Sprite2D", node_name: "sprite"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Node", node_name: "Inputs"}}
    ]
  },
  {
    pattern_name: "Dynamic Split Screen (Node3D + 2x SubViewport + TextureRect)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "viewport/dynamic_split_screen/camera_controller.tscn",
    node_tree: {root_type: "Node3D", root_name: "CameraController", children: [
      {type: "SubViewport", name: "Viewport1", properties: {}, children: [
        {type: "Camera3D", name: "Camera1", properties: {}}
      ]},
      {type: "SubViewport", name: "Viewport2", properties: {}, children: [
        {type: "Camera3D", name: "Camera2", properties: {}}
      ]},
      {type: "TextureRect", name: "View", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "Node3D", node_name: "CameraController"}},
      {tool: "add_child_node", arguments: {parent_path: "CameraController", node_type: "SubViewport", node_name: "Viewport1"}},
      {tool: "add_child_node", arguments: {parent_path: "CameraController/Viewport1", node_type: "Camera3D", node_name: "Camera1"}},
      {tool: "add_child_node", arguments: {parent_path: "CameraController", node_type: "SubViewport", node_name: "Viewport2"}},
      {tool: "add_child_node", arguments: {parent_path: "CameraController/Viewport2", node_type: "Camera3D", node_name: "Camera2"}},
      {tool: "add_child_node", arguments: {parent_path: "CameraController", node_type: "TextureRect", node_name: "View"}}
    ]
  },
  {
    pattern_name: "FPS Voxel Player (CharacterBody3D + Head + Camera + RayCast3D)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/voxel/player/player.tscn",
    node_tree: {root_type: "CharacterBody3D", root_name: "Player", children: [
      {type: "Node3D", name: "Head", properties: {}, children: [
        {type: "Camera3D", name: "Camera3D", properties: {current: true}},
        {type: "RayCast3D", name: "RayCast3D", properties: {}}
      ]},
      {type: "CollisionShape3D", name: "CollisionShape3D", properties: {shape: "CapsuleShape3D"}},
      {type: "MeshInstance3D", name: "AimPreview", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Node3D", node_name: "Head"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/Head", node_type: "Camera3D", node_name: "Camera3D"}},
      {tool: "add_child_node", arguments: {parent_path: "Player/Head", node_type: "RayCast3D", node_name: "RayCast3D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "capsule", parameters: {radius: 0.5, height: 1.8}}}
    ]
  },
  {
    pattern_name: "2D Platformer Player with TileMapLayer interaction",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/dynamic_tilemap_layers/player/player.tscn",
    node_tree: {root_type: "CharacterBody2D", root_name: "Player", children: [
      {type: "Sprite2D", name: "Sprite2D", properties: {}},
      {type: "CollisionShape2D", name: "CollisionShape2D", properties: {shape: "CircleShape2D"}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "CharacterBody2D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Sprite2D", node_name: "Sprite2D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "circle", parameters: {radius: 16}}}
    ]
  },
  {
    pattern_name: "Audio Effects Panel (Control + VBoxContainer + AudioStreamPlayers)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "audio/audio_effects/audio_effects.tscn",
    node_tree: {root_type: "Control", root_name: "AudioEffects", children: [
      {type: "VBoxContainer", name: "SoundEffects", properties: {}, children: [
        {type: "AudioStreamPlayer", name: "Music", properties: {}},
        {type: "AudioStreamPlayer", name: "Ding", properties: {}},
        {type: "AudioStreamPlayer", name: "Glass", properties: {}},
        {type: "AudioStreamPlayer", name: "Reverb", properties: {}}
      ]},
      {type: "CheckButton", name: "ToggleReverb", properties: {}},
      {type: "CheckButton", name: "ToggleChorus", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "Control", node_name: "AudioEffects"}},
      {tool: "add_child_node", arguments: {parent_path: "AudioEffects", node_type: "VBoxContainer", node_name: "SoundEffects"}},
      {tool: "add_child_node", arguments: {parent_path: "AudioEffects/SoundEffects", node_type: "AudioStreamPlayer", node_name: "Music"}},
      {tool: "add_child_node", arguments: {parent_path: "AudioEffects/SoundEffects", node_type: "AudioStreamPlayer", node_name: "Ding"}},
      {tool: "add_child_node", arguments: {parent_path: "AudioEffects", node_type: "CheckButton", node_name: "ToggleReverb"}},
      {tool: "add_child_node", arguments: {parent_path: "AudioEffects", node_type: "CheckButton", node_name: "ToggleChorus"}}
    ]
  },
  {
    pattern_name: "Drag and Drop ColorPicker (ColorPickerButton with drag preview)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "gui/drag_and_drop/drag_drop_script.tscn",
    node_tree: {root_type: "ColorPickerButton", root_name: "ColorPicker", children: []},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "ColorPickerButton", node_name: "ColorPicker"}}
    ]
  },
  {
    pattern_name: "2D Enemy (RigidBody2D + RayCast2D + AnimatedSprite2D)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/physics_platformer/enemy/enemy.tscn",
    node_tree: {root_type: "RigidBody2D", root_name: "Enemy", children: [
      {type: "Sprite2D", name: "Sprite2D", properties: {}},
      {type: "CollisionShape2D", name: "CollisionShape2D", properties: {}},
      {type: "RayCast2D", name: "RaycastLeft", properties: {}},
      {type: "RayCast2D", name: "RaycastRight", properties: {}},
      {type: "AnimationPlayer", name: "AnimationPlayer", properties: {}},
      {type: "AudioStreamPlayer2D", name: "SoundHit", properties: {}},
      {type: "AudioStreamPlayer2D", name: "SoundExplode", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "RigidBody2D", node_name: "Enemy"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "Sprite2D", node_name: "Sprite2D"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Enemy", node_name: "CollisionShape2D", shape_type: "rectangle"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "RayCast2D", node_name: "RaycastLeft"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "RayCast2D", node_name: "RaycastRight"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "AnimationPlayer", node_name: "AnimationPlayer"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "AudioStreamPlayer2D", node_name: "SoundHit"}}
    ]
  },
  {
    pattern_name: "3D Enemy (RigidBody3D + RayCast3D + AnimationPlayer + Audio)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "3d/platformer/enemy/enemy.tscn",
    node_tree: {root_type: "RigidBody3D", root_name: "Enemy", children: [
      {type: "Node3D", name: "Enemy", properties: {}, children: [
        {type: "Skeleton3D", name: "Skeleton", properties: {}, children: [
          {type: "RayCast3D", name: "RayFloor", properties: {}},
          {type: "RayCast3D", name: "RayWall", properties: {}}
        ]},
        {type: "AnimationPlayer", name: "AnimationPlayer", properties: {}}
      ]},
      {type: "CollisionShape3D", name: "CollisionShape3D", properties: {}},
      {type: "AudioStreamPlayer3D", name: "SoundHit", properties: {}},
      {type: "AudioStreamPlayer3D", name: "SoundWalkLoop", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "RigidBody3D", node_name: "Enemy"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "Node3D", node_name: "EnemyModel"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy/EnemyModel", node_type: "AnimationPlayer", node_name: "AnimationPlayer"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Enemy", node_name: "CollisionShape3D", shape_type: "box"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy/EnemyModel", node_type: "RayCast3D", node_name: "RayFloor"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy/EnemyModel", node_type: "RayCast3D", node_name: "RayWall"}},
      {tool: "add_child_node", arguments: {parent_path: "Enemy", node_type: "AudioStreamPlayer3D", node_name: "SoundHit"}}
    ]
  },
  {
    pattern_name: "PhantomCamera2D Player (CharacterBody2D + Area2D + PhantomCamera2D)",
    source_repo: "ramokz/phantom-camera",
    source_path: "addons/phantom_camera/examples/example_scenes/2D/sub_scenes/playable_character_2d.tscn",
    node_tree: {root_type: "CharacterBody2D", root_name: "Player", children: [
      {type: "CollisionShape2D", name: "CollisionShape2D", properties: {}},
      {type: "Sprite2D", name: "PlayerSprite", properties: {}},
      {type: "Area2D", name: "PlayerArea2D", properties: {}},
      {type: "PhantomCamera2D", name: "PlayerPhantomCamera2D", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "CharacterBody2D", node_name: "Player"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape2D", shape_type: "capsule"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Sprite2D", node_name: "PlayerSprite"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "Area2D", node_name: "PlayerArea2D"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "PhantomCamera2D", node_name: "PlayerPhantomCamera2D"}}
    ]
  },
  {
    pattern_name: "PhantomCamera3D Third Person Player (CharacterBody3D + PhantomCamera3D)",
    source_repo: "ramokz/phantom-camera",
    source_path: "addons/phantom_camera/examples/example_scenes/3D/sub_scenes/playable_character_third_person_3d.tscn",
    node_tree: {root_type: "CharacterBody3D", root_name: "Player", children: [
      {type: "CollisionShape3D", name: "CollisionShape3D", properties: {}},
      {type: "MeshInstance3D", name: "Mesh", properties: {}},
      {type: "PhantomCamera3D", name: "PlayerPhantomCamera3D", properties: {follow_mode: "ThirdPerson"}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "CharacterBody3D", node_name: "Player"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Player", node_name: "CollisionShape3D", shape_type: "capsule"}},
      {tool: "add_primitive_mesh", arguments: {parent_path: "Player", node_name: "Mesh", mesh_type: "capsule"}},
      {tool: "add_child_node", arguments: {parent_path: "Player", node_type: "PhantomCamera3D", node_name: "PlayerPhantomCamera3D"}}
    ]
  },
  {
    pattern_name: "Save/Load UI (Control + FileDialog + Button)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "loading/serialization/serialization.tscn",
    node_tree: {root_type: "Control", root_name: "SaveLoadUI", children: [
      {type: "MarginContainer", name: "MarginContainer", properties: {}, children: [
        {type: "VBoxContainer", name: "VBoxContainer", properties: {}, children: [
          {type: "Button", name: "SaveJSON", properties: {}},
          {type: "Button", name: "LoadJSON", properties: {}},
          {type: "FileDialog", name: "FileDialog", properties: {}}
        ]}
      ]}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "Control", node_name: "SaveLoadUI"}},
      {tool: "add_child_node", arguments: {parent_path: "SaveLoadUI", node_type: "MarginContainer", node_name: "MarginContainer"}},
      {tool: "add_child_node", arguments: {parent_path: "SaveLoadUI/MarginContainer", node_type: "VBoxContainer", node_name: "VBoxContainer"}},
      {tool: "add_child_node", arguments: {parent_path: "SaveLoadUI/MarginContainer/VBoxContainer", node_type: "Button", node_name: "SaveJSON"}},
      {tool: "add_child_node", arguments: {parent_path: "SaveLoadUI/MarginContainer/VBoxContainer", node_type: "Button", node_name: "LoadJSON"}},
      {tool: "add_child_node", arguments: {parent_path: "SaveLoadUI/MarginContainer/VBoxContainer", node_type: "FileDialog", node_name: "FileDialog"}}
    ]
  },
  {
    pattern_name: "2D TileMapLayer with secret area detector",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/dynamic_tilemap_layers/level/level.tscn",
    node_tree: {root_type: "Node2D", root_name: "Level", children: [
      {type: "TileMapLayer", name: "Ground", properties: {}},
      {type: "TileMapLayer", name: "SecretLayer", properties: {}, children: [
        {type: "Area2D", name: "SecretDetector", properties: {}, children: [
          {type: "CollisionShape2D", name: "CollisionShape2D", properties: {}}
        ]}
      ]},
      {type: "CharacterBody2D", name: "Player", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "Node2D", node_name: "Level"}},
      {tool: "add_child_node", arguments: {parent_path: "Level", node_type: "TileMapLayer", node_name: "Ground"}},
      {tool: "add_child_node", arguments: {parent_path: "Level", node_type: "TileMapLayer", node_name: "SecretLayer"}},
      {tool: "add_child_node", arguments: {parent_path: "Level/SecretLayer", node_type: "Area2D", node_name: "SecretDetector"}},
      {tool: "add_collision_shape", arguments: {parent_path: "Level/SecretLayer/SecretDetector", node_name: "CollisionShape2D", shape_type: "rectangle"}}
    ]
  },
  {
    pattern_name: "Bullet system (Node2D with custom drawing and PhysicsServer2D)",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/bullet_shower/shower.tscn",
    node_tree: {root_type: "Node2D", root_name: "Shower", children: [
      {type: "Area2D", name: "Player", properties: {}},
      {type: "Node2D", name: "Bullets", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "Node2D", node_name: "Shower"}},
      {tool: "add_child_node", arguments: {parent_path: "Shower", node_type: "Area2D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Shower", node_type: "Node2D", node_name: "Bullets"}}
    ]
  },
  {
    pattern_name: "Main game scene with timers and Path2D mob spawning",
    source_repo: "godotengine/godot-demo-projects",
    source_path: "2d/dodge_the_creeps/main.tscn",
    node_tree: {root_type: "Node", root_name: "Main", children: [
      {type: "ColorRect", name: "Background", properties: {}},
      {type: "Area2D", name: "Player", properties: {}},
      {type: "CanvasLayer", name: "HUD", properties: {}},
      {type: "Timer", name: "MobTimer", properties: {}},
      {type: "Timer", name: "ScoreTimer", properties: {}},
      {type: "Timer", name: "StartTimer", properties: {}},
      {type: "Path2D", name: "MobPath", properties: {}, children: [
        {type: "PathFollow2D", name: "MobSpawnLocation", properties: {}}
      ]},
      {type: "AudioStreamPlayer", name: "Music", properties: {}},
      {type: "AudioStreamPlayer", name: "DeathSound", properties: {}},
      {type: "Marker2D", name: "StartPosition", properties: {}}
    ]},
    tool_calls_to_recreate: [
      {tool: "add_node", arguments: {node_type: "Node", node_name: "Main"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "ColorRect", node_name: "Background"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "Area2D", node_name: "Player"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "CanvasLayer", node_name: "HUD"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "Timer", node_name: "MobTimer"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "Timer", node_name: "ScoreTimer"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "Timer", node_name: "StartTimer"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "Path2D", node_name: "MobPath"}},
      {tool: "add_child_node", arguments: {parent_path: "Main/MobPath", node_type: "PathFollow2D", node_name: "MobSpawnLocation"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "AudioStreamPlayer", node_name: "Music"}},
      {tool: "add_child_node", arguments: {parent_path: "Main", node_type: "Marker2D", node_name: "StartPosition"}}
    ]
  }
];

fs.writeFileSync(path.join(outDir, 'scene_patterns.json'), JSON.stringify(scenePatterns, null, 2));
console.log('scene_patterns.json written with', scenePatterns.length, 'patterns');

const projectConfigs = [
  {source_repo: "godotengine/godot-demo-projects", project_name: "Dodge the Creeps", settings: {"application/config/name": "Dodge the Creeps", "application/run/main_scene": "res://main.tscn", "display/window/size/viewport_width": 480, "display/window/size/viewport_height": 720, "display/window/stretch/mode": "canvas_items", "rendering/renderer/rendering_method": "gl_compatibility"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Platformer 3D", settings: {"application/config/name": "Platformer 3D", "application/run/main_scene": "res://game.tscn", "display/window/stretch/mode": "canvas_items", "display/window/stretch/aspect": "expand", "physics/common/physics_ticks_per_second": 120, "physics/3d/physics_engine": "Jolt Physics", "physics/3d/default_gravity": 22.0, "physics/common/physics_interpolation": true, "rendering/textures/vram_compression/import_s3tc_bptc": true, "rendering/anti_aliasing/quality/msaa_3d": 2}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Squash the Creeps 3D", settings: {"application/config/name": "Squash the Creeps (3D)", "application/run/main_scene": "res://Main.tscn", "application/config/features": "PackedStringArray(\"4.6\")", "autoload/MusicPlayer": "*res://MusicPlayer.tscn", "display/window/stretch/mode": "canvas_items", "display/window/stretch/aspect": "expand", "filesystem/import/blender/enabled": false, "layer_names/3d_physics/layer_1": "player", "layer_names/3d_physics/layer_2": "enemies", "layer_names/3d_physics/layer_3": "world", "physics/common/physics_ticks_per_second": 120, "physics/3d/physics_engine": "Jolt Physics", "physics/common/physics_interpolation": true}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Hierarchical Finite State Machine", settings: {"application/config/name": "Hierarchical Finite State Machine", "application/run/main_scene": "res://Demo.tscn", "display/window/size/viewport_width": 1280, "display/window/size/viewport_height": 720, "display/window/stretch/mode": "canvas_items", "display/window/stretch/aspect": "expand", "physics/common/physics_ticks_per_second": 120, "physics/common/physics_interpolation": true, "rendering/renderer/rendering_method": "gl_compatibility"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Multiplayer Bomber", settings: {"application/config/name": "Multiplayer Bomber", "application/run/main_scene": "res://lobby.tscn", "autoload/gamestate": "*res://gamestate.gd", "display/window/stretch/mode": "canvas_items", "display/window/stretch/aspect": "expand", "rendering/renderer/rendering_method": "gl_compatibility", "rendering/2d/snap/snap_2d_transforms_to_pixel": true, "rendering/2d/snap/snap_2d_vertices_to_pixel": true}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Custom Drawing", settings: {"application/config/name": "Custom Drawing", "application/run/main_scene": "res://custom_drawing.tscn", "application/config/features": "PackedStringArray(\"4.6\")"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Bullet Shower", settings: {"application/config/name": "Bullet Shower", "application/run/main_scene": "res://shower.tscn", "application/config/features": "PackedStringArray(\"4.6\")"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Dynamic TileMap Layers", settings: {"application/config/name": "Dynamic TileMap Layers", "application/run/main_scene": "res://world.tscn", "application/config/features": "PackedStringArray(\"4.6\")"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "2D Navigation", settings: {"application/config/name": "2D Navigation", "application/run/main_scene": "res://navigation.tscn"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "2D Physics Platformer", settings: {"application/config/name": "2D Physics Platformer", "application/run/main_scene": "res://stage.tscn"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "2D Kinematic Character", settings: {"application/config/name": "2D Kinematic Character", "application/run/main_scene": "res://level.tscn"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "3D Kinematic Character", settings: {"application/config/name": "3D Kinematic Character", "application/run/main_scene": "res://level.tscn"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "3D RigidBody Character", settings: {"application/config/name": "3D RigidBody Character", "application/run/main_scene": "res://level.tscn"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "3D Navigation", settings: {"application/config/name": "3D Navigation", "application/run/main_scene": "res://NavigationRegion.tscn"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Truck Town", settings: {"application/config/name": "Truck Town", "application/run/main_scene": "res://car_select/car_select.tscn"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Audio Effects", settings: {"application/config/name": "Audio Effects", "application/run/main_scene": "res://audio_effects.tscn"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Runtime Save Load", settings: {"application/config/name": "Runtime Save/Load", "application/run/main_scene": "res://runtime_save_load.tscn"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Drag and Drop", settings: {"application/config/name": "Drag and Drop", "application/run/main_scene": "res://drag_drop_script.tscn"}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Voxel Game", settings: {"application/config/name": "Voxel Game", "application/run/main_scene": "res://menu/main/main_menu.tscn", "display/window/size/mode": 2}},
  {source_repo: "godotengine/godot-demo-projects", project_name: "Dynamic Split Screen", settings: {"application/config/name": "Dynamic Split Screen", "application/run/main_scene": "res://split_screen.tscn"}}
];

fs.writeFileSync(path.join(outDir, 'project_configs.json'), JSON.stringify(projectConfigs, null, 2));
console.log('project_configs.json written with', projectConfigs.length, 'configs');
