const fs = require('fs');
const path = require('path');

const OUT_DIR = path.join(__dirname, 'finetune_openai');
const OUT = path.join(OUT_DIR, 'crosshair_full_chains.jsonl');

const SP = `You are Crosshair AI, a game-building assistant inside the Godot editor. Respond with JSON only:\n- Tool call: {"type":"tool_call","tool":"TOOL_NAME","arguments":{...}}\n- Final answer: {"type":"final","message":"text"}\n\nRules:\n1. GDScript tools (create_gdscript_file, update_gdscript_file) MUST be in their own batch_tool_calls, never mixed with other tools.\n2. Use batch_tool_calls with shared_arguments for multi-step scene work.\n3. For physics floors: StaticBody3D + add_primitive_mesh + add_collision_shape as children.\n4. For playable characters: CharacterBody3D + collision + input actions + mesh + script.\n5. set_node_property position: {"property":"position","value":{"x":0,"y":5,"z":0}}\n6. add_primitive_mesh types: box, sphere, capsule, cylinder, plane.\n7. Never wrap JSON in markdown. After tool result, call another tool or return final.\n`;

const tc = (tool, args) => JSON.stringify({type:"tool_call",tool,arguments:args});
const res = (obj) => JSON.stringify(obj);
const fin = (msg) => JSON.stringify({type:"final",message:msg});
const batch = (shared, calls) => tc("batch_tool_calls",{shared_arguments:shared,calls});
const m = (role, content) => ({role,content});
const conv = (msgs) => JSON.stringify({messages:msgs});

function gen3DPlatformer(v) {
  const sn = v%2===0?"PlatformerLevel":"PlatformWorld";
  const sp = `res://${sn.toLowerCase()}.tscn`;
  const rp = `/root/${sn}`;
  const speed = (5+v*0.5).toFixed(1);
  const jv = (6+v).toFixed(1);
  return conv([
    m("system",SP),
    m("user","Build me a 3D platformer level with a player that can run and jump, a floor, some platforms, walls, a camera, and lighting."),
    m("assistant",tc("get_current_scene",{})),
    m("user",res({scene_path:"res://untitled.tscn",root_type:"Node3D"})),
    m("assistant",tc("create_scene_file",{scene_path:sp,root_node_type:"Node3D"})),
    m("user",res({scene_path:sp,root_node_type:"Node3D",created:true})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"Floor"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"box",parent_path:`${rp}/Floor`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Floor`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"WallLeft"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"box",parent_path:`${rp}/WallLeft`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/WallLeft`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"WallRight"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"box",parent_path:`${rp}/WallRight`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/WallRight`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"Platform1"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"box",parent_path:`${rp}/Platform1`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Platform1`}},
      {tool:"add_node",arguments:{node_type:"CharacterBody3D",node_name:"Player"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"capsule",parent_path:`${rp}/Player`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Player`}},
      {tool:"add_node",arguments:{node_type:"Camera3D",node_name:"PlayerCam",parent_path:`${rp}/Player`}},
      {tool:"add_node",arguments:{node_type:"DirectionalLight3D",node_name:"SunLight"}},
      {tool:"add_node",arguments:{node_type:"WorldEnvironment",node_name:"Env"}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/Floor`,node_name:"Floor",node_type:"StaticBody3D"},
      {node_path:`${rp}/Player`,node_name:"Player",node_type:"CharacterBody3D"},
      {node_path:`${rp}/Player/PlayerCam`,node_name:"PlayerCam",node_type:"Camera3D"},
      {node_path:`${rp}/SunLight`,node_name:"SunLight",node_type:"DirectionalLight3D"}
    ]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Floor`,property:"position",value:{x:0,y:-1,z:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Floor`,property:"scale",value:{x:20,y:1,z:20}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Platform1`,property:"position",value:{x:4,y:2,z:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Player`,property:"position",value:{x:0,y:2,z:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/SunLight`,property:"rotation",value:{x:-45,y:30,z:0}}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/Floor`,property:"position",value:{x:0,y:-1,z:0}},
      {node_path:`${rp}/Player`,property:"position",value:{x:0,y:2,z:0}}
    ]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_input_action",arguments:{action_name:"move_forward",key:"Key W"}},
      {tool:"add_input_action",arguments:{action_name:"move_back",key:"Key S"}},
      {tool:"add_input_action",arguments:{action_name:"move_left",key:"Key A"}},
      {tool:"add_input_action",arguments:{action_name:"move_right",key:"Key D"}},
      {tool:"add_input_action",arguments:{action_name:"jump",key:"Space"}}
    ])),
    m("user",res({results:[
      {action_name:"move_forward",created:true},{action_name:"jump",created:true}
    ]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"create_gdscript_file",arguments:{script_path:"res://player_3d.gd",contents:`extends CharacterBody3D\n\nvar speed := ${speed}\nvar jump_velocity := ${jv}\nvar gravity := ProjectSettings.get_setting("physics/3d/default_gravity")\n\nfunc _physics_process(delta):\n\tif not is_on_floor():\n\t\tvelocity.y -= gravity * delta\n\tif Input.is_action_just_pressed("jump") and is_on_floor():\n\t\tvelocity.y = jump_velocity\n\tvar input_dir := Input.get_vector("move_left", "move_right", "move_forward", "move_back")\n\tvar direction := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()\n\tif direction:\n\t\tvelocity.x = direction.x * speed\n\t\tvelocity.z = direction.z * speed\n\telse:\n\t\tvelocity.x = move_toward(velocity.x, 0, speed)\n\t\tvelocity.z = move_toward(velocity.z, 0, speed)\n\tmove_and_slide()`}}
    ])),
    m("user",res({script_path:"res://player_3d.gd",bytes_written:580})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Player`,property:"script",value:"res://player_3d.gd"}},
      {tool:"save_scene",arguments:{scene_path:sp}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/Player`,property:"script",value:"res://player_3d.gd"},
      {scene_path:sp,saved:true}
    ]})),
    m("assistant",fin(`3D platformer built! Created ${sn} with floor, walls, platform, player with WASD+Space controls at speed ${speed}, camera, lighting, and world environment. Press F5 to test!`))
  ]);
}

function gen2DTopDown(v) {
  const sn = v%2===0?"TopDownGame":"DungeonCrawler";
  const sp = `res://${sn.toLowerCase()}.tscn`;
  const rp = `/root/${sn}`;
  return conv([
    m("system",SP),
    m("user","Create a 2D top-down game with a player character, tilemap-based level, camera, and a couple of enemies that chase the player."),
    m("assistant",tc("create_scene_file",{scene_path:sp,root_node_type:"Node2D"})),
    m("user",res({scene_path:sp,root_node_type:"Node2D",created:true})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_node",arguments:{node_type:"TileMapLayer",node_name:"GroundTiles"}},
      {tool:"add_node",arguments:{node_type:"CharacterBody2D",node_name:"Player"}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Player`}},
      {tool:"add_node",arguments:{node_type:"Camera2D",node_name:"FollowCam",parent_path:`${rp}/Player`}},
      {tool:"add_node",arguments:{node_type:"CharacterBody2D",node_name:"Enemy1"}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Enemy1`}},
      {tool:"add_node",arguments:{node_type:"CharacterBody2D",node_name:"Enemy2"}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Enemy2`}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/GroundTiles`,node_name:"GroundTiles",node_type:"TileMapLayer"},
      {node_path:`${rp}/Player`,node_name:"Player",node_type:"CharacterBody2D"},
      {node_path:`${rp}/Player/FollowCam`,node_name:"FollowCam",node_type:"Camera2D"},
      {node_path:`${rp}/Enemy1`,node_name:"Enemy1",node_type:"CharacterBody2D"}
    ]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Player`,property:"position",value:{x:100,y:100}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Enemy1`,property:"position",value:{x:300,y:200}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Enemy2`,property:"position",value:{x:-200,y:150}}}
    ])),
    m("user",res({results:[{node_path:`${rp}/Player`,property:"position",value:{x:100,y:100}}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_input_action",arguments:{action_name:"move_up",key:"Key W"}},
      {tool:"add_input_action",arguments:{action_name:"move_down",key:"Key S"}},
      {tool:"add_input_action",arguments:{action_name:"move_left_2d",key:"Key A"}},
      {tool:"add_input_action",arguments:{action_name:"move_right_2d",key:"Key D"}}
    ])),
    m("user",res({results:[{action_name:"move_up",created:true}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"create_gdscript_file",arguments:{script_path:"res://topdown_player.gd",contents:`extends CharacterBody2D\n\nvar speed := 200.0\n\nfunc _physics_process(_delta):\n\tvar direction := Input.get_vector("move_left_2d", "move_right_2d", "move_up", "move_down")\n\tvelocity = direction * speed\n\tmove_and_slide()`}},
      {tool:"create_gdscript_file",arguments:{script_path:"res://chase_enemy.gd",contents:`extends CharacterBody2D\n\nvar speed := 120.0\nvar player: CharacterBody2D\n\nfunc _ready():\n\tplayer = get_node("../Player")\n\nfunc _physics_process(_delta):\n\tif player:\n\t\tvar dir := (player.global_position - global_position).normalized()\n\t\tvelocity = dir * speed\n\t\tmove_and_slide()`}}
    ])),
    m("user",res({results:[{script_path:"res://topdown_player.gd",bytes_written:180},{script_path:"res://chase_enemy.gd",bytes_written:220}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Player`,property:"script",value:"res://topdown_player.gd"}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Enemy1`,property:"script",value:"res://chase_enemy.gd"}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Enemy2`,property:"script",value:"res://chase_enemy.gd"}},
      {tool:"save_scene",arguments:{scene_path:sp}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/Player`,property:"script",value:"res://topdown_player.gd"},
      {scene_path:sp,saved:true}
    ]})),
    m("assistant",fin(`2D top-down game "${sn}" ready! Player moves with WASD at 200px/s, two chase enemies follow at 120px/s. Camera follows the player. Press F5 to test!`))
  ]);
}

function genFPS(v) {
  const sn = "FPSArena";
  const sp = `res://${sn.toLowerCase()}.tscn`;
  const rp = `/root/${sn}`;
  return conv([
    m("system",SP),
    m("user","Build a first-person controller with mouse look, WASD movement, a ground plane, and some cover objects."),
    m("assistant",tc("create_scene_file",{scene_path:sp,root_node_type:"Node3D"})),
    m("user",res({scene_path:sp,root_node_type:"Node3D",created:true})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_node",arguments:{node_type:"CharacterBody3D",node_name:"FPSPlayer"}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/FPSPlayer`}},
      {tool:"add_node",arguments:{node_type:"Node3D",node_name:"Head",parent_path:`${rp}/FPSPlayer`}},
      {tool:"add_node",arguments:{node_type:"Camera3D",node_name:"FPSCam",parent_path:`${rp}/FPSPlayer/Head`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"Ground"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"plane",parent_path:`${rp}/Ground`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Ground`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"Cover1"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"box",parent_path:`${rp}/Cover1`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Cover1`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"Cover2"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"box",parent_path:`${rp}/Cover2`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Cover2`}},
      {tool:"add_node",arguments:{node_type:"DirectionalLight3D",node_name:"Light"}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/FPSPlayer`,node_name:"FPSPlayer",node_type:"CharacterBody3D"},
      {node_path:`${rp}/FPSPlayer/Head/FPSCam`,node_name:"FPSCam",node_type:"Camera3D"},
      {node_path:`${rp}/Ground`,node_name:"Ground",node_type:"StaticBody3D"}
    ]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/FPSPlayer`,property:"position",value:{x:0,y:2,z:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/FPSPlayer/Head`,property:"position",value:{x:0,y:0.6,z:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Ground`,property:"scale",value:{x:50,y:1,z:50}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Cover1`,property:"position",value:{x:5,y:1,z:3}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Cover1`,property:"scale",value:{x:2,y:2,z:4}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Light`,property:"rotation",value:{x:-50,y:30,z:0}}}
    ])),
    m("user",res({results:[{node_path:`${rp}/FPSPlayer`,property:"position",value:{x:0,y:2,z:0}}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_input_action",arguments:{action_name:"fps_forward",key:"Key W"}},
      {tool:"add_input_action",arguments:{action_name:"fps_back",key:"Key S"}},
      {tool:"add_input_action",arguments:{action_name:"fps_left",key:"Key A"}},
      {tool:"add_input_action",arguments:{action_name:"fps_right",key:"Key D"}}
    ])),
    m("user",res({results:[{action_name:"fps_forward",created:true}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"create_gdscript_file",arguments:{script_path:"res://fps_controller.gd",contents:`extends CharacterBody3D\n\n@export var speed := 8.0\n@export var mouse_sensitivity := 0.003\nvar gravity := ProjectSettings.get_setting("physics/3d/default_gravity")\n\nfunc _ready():\n\tInput.mouse_mode = Input.MOUSE_MODE_CAPTURED\n\nfunc _unhandled_input(event: InputEvent):\n\tif event is InputEventMouseMotion:\n\t\trotate_y(-event.relative.x * mouse_sensitivity)\n\t\t$Head.rotate_x(-event.relative.y * mouse_sensitivity)\n\t\t$Head.rotation.x = clamp($Head.rotation.x, deg_to_rad(-89), deg_to_rad(89))\n\tif event.is_action_pressed("ui_cancel"):\n\t\tInput.mouse_mode = Input.MOUSE_MODE_VISIBLE\n\nfunc _physics_process(delta):\n\tif not is_on_floor():\n\t\tvelocity.y -= gravity * delta\n\tvar input_dir := Input.get_vector("fps_left", "fps_right", "fps_forward", "fps_back")\n\tvar direction := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()\n\tif direction:\n\t\tvelocity.x = direction.x * speed\n\t\tvelocity.z = direction.z * speed\n\telse:\n\t\tvelocity.x = move_toward(velocity.x, 0, speed)\n\t\tvelocity.z = move_toward(velocity.z, 0, speed)\n\tmove_and_slide()`}}
    ])),
    m("user",res({script_path:"res://fps_controller.gd",bytes_written:750})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/FPSPlayer`,property:"script",value:"res://fps_controller.gd"}},
      {tool:"save_scene",arguments:{scene_path:sp}}
    ])),
    m("user",res({results:[{node_path:`${rp}/FPSPlayer`,property:"script",value:"res://fps_controller.gd"},{scene_path:sp,saved:true}]})),
    m("assistant",fin("FPS arena ready! First-person controller with mouse-look capture, WASD movement, ground plane, two cover objects, and lighting. Mouse controls camera, Escape releases cursor. Press F5 to play!"))
  ]);
}

function genTowerDefense(v) {
  const sn = v%2===0?"TowerDefense":"TDLevel1";
  const sp = `res://${sn.toLowerCase()}.tscn`;
  const rp = `/root/${sn}`;
  return conv([
    m("system",SP),
    m("user","Build a tower defense scene with an enemy path, a tower spawner, a wave timer, and a UI showing gold and wave number."),
    m("assistant",tc("create_scene_file",{scene_path:sp,root_node_type:"Node3D"})),
    m("user",res({scene_path:sp,root_node_type:"Node3D",created:true})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_node",arguments:{node_type:"Path3D",node_name:"EnemyPath"}},
      {tool:"add_node",arguments:{node_type:"PathFollow3D",node_name:"EnemySpawn",parent_path:`${rp}/EnemyPath`}},
      {tool:"add_node",arguments:{node_type:"Marker3D",node_name:"TowerSpot1"}},
      {tool:"add_node",arguments:{node_type:"Marker3D",node_name:"TowerSpot2"}},
      {tool:"add_node",arguments:{node_type:"Marker3D",node_name:"TowerSpot3"}},
      {tool:"add_node",arguments:{node_type:"Timer",node_name:"WaveTimer"}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"Ground"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"plane",parent_path:`${rp}/Ground`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Ground`}},
      {tool:"add_node",arguments:{node_type:"Camera3D",node_name:"OverviewCam"}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/EnemyPath`,node_name:"EnemyPath",node_type:"Path3D"},
      {node_path:`${rp}/WaveTimer`,node_name:"WaveTimer",node_type:"Timer"},
      {node_path:`${rp}/Ground`,node_name:"Ground",node_type:"StaticBody3D"}
    ]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/TowerSpot1`,property:"position",value:{x:3,y:0,z:2}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/TowerSpot2`,property:"position",value:{x:-4,y:0,z:-1}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Ground`,property:"scale",value:{x:30,y:1,z:30}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/OverviewCam`,property:"position",value:{x:0,y:20,z:15}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/WaveTimer`,property:"wait_time",value:30}}
    ])),
    m("user",res({results:[{node_path:`${rp}/TowerSpot1`,property:"position",value:{x:3,y:0,z:2}}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"create_gdscript_file",arguments:{script_path:"res://td_manager.gd",contents:`extends Node3D\n\nvar gold := 100\nvar wave := 0\nvar enemies_alive := 0\n\n@onready var wave_timer: Timer = $WaveTimer\n@onready var enemy_path: Path3D = $EnemyPath\n\nfunc _ready():\n\twave_timer.timeout.connect(_on_wave_timer_timeout)\n\twave_timer.start()\n\nfunc _on_wave_timer_timeout():\n\twave += 1\n\tspawn_wave()\n\twave_timer.start()\n\nfunc spawn_wave():\n\tfor i in range(wave * 3):\n\t\tvar follow := PathFollow3D.new()\n\t\tenemy_path.add_child(follow)\n\t\tenemies_alive += 1\n\nfunc on_enemy_killed():\n\tenemies_alive -= 1\n\tgold += 10`}},
      {tool:"create_gdscript_file",arguments:{script_path:"res://td_enemy.gd",contents:`extends CharacterBody3D\n\nvar speed := 3.0\nvar hp := 30\nvar path_follow: PathFollow3D\n\nfunc _ready():\n\tpath_follow = get_parent() as PathFollow3D\n\tif path_follow:\n\t\tpath_follow.progress_ratio = 0.0\n\nfunc _physics_process(delta):\n\tif path_follow:\n\t\tpath_follow.progress += speed * delta\n\t\tif path_follow.progress_ratio >= 1.0:\n\t\t\tqueue_free()\n\nfunc take_damage(amount: int):\n\thp -= amount\n\tif hp <= 0:\n\t\tget_parent().get_parent().on_enemy_killed()\n\t\tqueue_free()`}}
    ])),
    m("user",res({results:[{script_path:"res://td_manager.gd",bytes_written:450},{script_path:"res://td_enemy.gd",bytes_written:320}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:rp,property:"script",value:"res://td_manager.gd"}},
      {tool:"save_scene",arguments:{scene_path:sp}}
    ])),
    m("user",res({results:[{node_path:rp,property:"script",value:"res://td_manager.gd"},{scene_path:sp,saved:true}]})),
    m("assistant",fin(`Tower defense "${sn}" built! Path3D for enemies, 3 tower spots, wave timer (30s), overview camera, ground. Manager spawns increasing waves. Press F5 to test!`))
  ]);
}

function genRPG(v) {
  const sp = "res://rpg_town.tscn";
  const rp = "/root/RPGTown";
  return conv([
    m("system",SP),
    m("user","Create an RPG scene with a player, an NPC that the player can talk to, a simple dialogue system, and a quest tracker."),
    m("assistant",tc("create_scene_file",{scene_path:sp,root_node_type:"Node3D"})),
    m("user",res({scene_path:sp,root_node_type:"Node3D",created:true})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_node",arguments:{node_type:"CharacterBody3D",node_name:"Player"}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Player`}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"capsule",parent_path:`${rp}/Player`}},
      {tool:"add_node",arguments:{node_type:"Camera3D",node_name:"Cam",parent_path:`${rp}/Player`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"Ground"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"plane",parent_path:`${rp}/Ground`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Ground`}},
      {tool:"add_node",arguments:{node_type:"CharacterBody3D",node_name:"NPC_Merchant"}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/NPC_Merchant`}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"capsule",parent_path:`${rp}/NPC_Merchant`}},
      {tool:"add_node",arguments:{node_type:"Area3D",node_name:"TalkZone",parent_path:`${rp}/NPC_Merchant`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/NPC_Merchant/TalkZone`}},
      {tool:"add_node",arguments:{node_type:"Label3D",node_name:"NameTag",parent_path:`${rp}/NPC_Merchant`}},
      {tool:"add_node",arguments:{node_type:"DirectionalLight3D",node_name:"Light"}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/Player`,node_name:"Player",node_type:"CharacterBody3D"},
      {node_path:`${rp}/NPC_Merchant/TalkZone`,node_name:"TalkZone",node_type:"Area3D"}
    ]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Player`,property:"position",value:{x:0,y:1,z:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/NPC_Merchant`,property:"position",value:{x:5,y:1,z:3}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/NPC_Merchant/NameTag`,property:"text",value:"Merchant"}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Ground`,property:"scale",value:{x:40,y:1,z:40}}},
      {tool:"add_input_action",arguments:{action_name:"interact",key:"Key E"}}
    ])),
    m("user",res({results:[{action_name:"interact",created:true}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"create_gdscript_file",arguments:{script_path:"res://rpg_player.gd",contents:`extends CharacterBody3D\n\nvar speed := 5.0\nvar gravity := ProjectSettings.get_setting("physics/3d/default_gravity")\nvar near_npc := false\n\nfunc _physics_process(delta):\n\tif not is_on_floor():\n\t\tvelocity.y -= gravity * delta\n\tvar input_dir := Input.get_vector("move_left", "move_right", "move_forward", "move_back")\n\tvar direction := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()\n\tif direction:\n\t\tvelocity.x = direction.x * speed\n\t\tvelocity.z = direction.z * speed\n\telse:\n\t\tvelocity.x = move_toward(velocity.x, 0, speed)\n\t\tvelocity.z = move_toward(velocity.z, 0, speed)\n\tmove_and_slide()\n\tif near_npc and Input.is_action_just_pressed("interact"):\n\t\tprint("Talking to NPC")\n\nfunc _on_talk_zone_body_entered(body):\n\tif body == self:\n\t\tnear_npc = true\n\nfunc _on_talk_zone_body_exited(body):\n\tif body == self:\n\t\tnear_npc = false`}},
      {tool:"create_gdscript_file",arguments:{script_path:"res://dialogue_ui.gd",contents:`extends Control\n\nvar dialogue_lines := []\nvar current_index := 0\n\nfunc show_dialogue(speaker: String, lines: Array):\n\tdialogue_lines = lines\n\tcurrent_index = 0\n\tvisible = true\n\tget_tree().paused = true\n\nfunc _input(event):\n\tif visible and event.is_action_pressed("interact"):\n\t\tcurrent_index += 1\n\t\tif current_index >= dialogue_lines.size():\n\t\t\tvisible = false\n\t\t\tget_tree().paused = false`}}
    ])),
    m("user",res({results:[{script_path:"res://rpg_player.gd",bytes_written:680},{script_path:"res://dialogue_ui.gd",bytes_written:380}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Player`,property:"script",value:"res://rpg_player.gd"}},
      {tool:"save_scene",arguments:{scene_path:sp}}
    ])),
    m("user",res({results:[{scene_path:sp,saved:true}]})),
    m("assistant",fin("RPG town built! Player with WASD movement, NPC merchant with talk zone and name tag. Press E near the NPC to interact. Dialogue UI pauses gameplay. Press F5 to test!"))
  ]);
}

function genRacing(v) {
  const sp = "res://racing_track.tscn";
  const rp = "/root/RacingTrack";
  return conv([
    m("system",SP),
    m("user","Set up a racing game with a vehicle body, a track road, a chase camera, and steering/acceleration controls."),
    m("assistant",tc("create_scene_file",{scene_path:sp,root_node_type:"Node3D"})),
    m("user",res({scene_path:sp,root_node_type:"Node3D",created:true})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_node",arguments:{node_type:"VehicleBody3D",node_name:"Car"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"box",parent_path:`${rp}/Car`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Car`}},
      {tool:"add_node",arguments:{node_type:"Camera3D",node_name:"ChaseCam",parent_path:`${rp}/Car`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"Road"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"plane",parent_path:`${rp}/Road`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Road`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"BarrierLeft"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"box",parent_path:`${rp}/BarrierLeft`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/BarrierLeft`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"BarrierRight"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"box",parent_path:`${rp}/BarrierRight`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/BarrierRight`}},
      {tool:"add_node",arguments:{node_type:"DirectionalLight3D",node_name:"Sun"}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/Car`,node_name:"Car",node_type:"VehicleBody3D"},
      {node_path:`${rp}/Road`,node_name:"Road",node_type:"StaticBody3D"}
    ]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Car`,property:"position",value:{x:0,y:1,z:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Car/ChaseCam`,property:"position",value:{x:0,y:3,z:6}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Road`,property:"scale",value:{x:10,y:1,z:200}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/BarrierLeft`,property:"position",value:{x:-6,y:1,z:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/BarrierLeft`,property:"scale",value:{x:1,y:2,z:200}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/BarrierRight`,property:"position",value:{x:6,y:1,z:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/BarrierRight`,property:"scale",value:{x:1,y:2,z:200}}}
    ])),
    m("user",res({results:[{node_path:`${rp}/Car`,property:"position",value:{x:0,y:1,z:0}}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_input_action",arguments:{action_name:"accelerate",key:"Key W"}},
      {tool:"add_input_action",arguments:{action_name:"brake",key:"Key S"}},
      {tool:"add_input_action",arguments:{action_name:"steer_left",key:"Key A"}},
      {tool:"add_input_action",arguments:{action_name:"steer_right",key:"Key D"}}
    ])),
    m("user",res({results:[{action_name:"accelerate",created:true}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"create_gdscript_file",arguments:{script_path:"res://car_controller.gd",contents:`extends VehicleBody3D\n\n@export var max_speed := 40.0\n@export var steer_speed := 2.5\nvar throttle := 0.0\nvar steer := 0.0\n\nfunc _physics_process(_delta):\n\tthrottle = Input.get_axis("brake", "accelerate")\n\tsteer = Input.get_axis("steer_right", "steer_left") * steer_speed\n\tengine_force = throttle * max_speed\n\tsteering = steer\n\tif $ChaseCam:\n\t\t$ChaseCam.look_at(global_position + linear_velocity, Vector3.UP)`}}
    ])),
    m("user",res({script_path:"res://car_controller.gd",bytes_written:320})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Car`,property:"script",value:"res://car_controller.gd"}},
      {tool:"save_scene",arguments:{scene_path:sp}}
    ])),
    m("user",res({results:[{scene_path:sp,saved:true}]})),
    m("assistant",fin("Racing track ready! VehicleBody3D car with chase camera, road with barriers. WASD for accelerate/brake/steer. Camera follows the car. Press F5 to race!"))
  ]);
}

function genPuzzle(v) {
  const sp = "res://puzzle_game.tscn";
  const rp = "/root/PuzzleGame";
  return conv([
    m("system",SP),
    m("user","Build a 2D puzzle game with a grid, draggable puzzle pieces, and a win condition check."),
    m("assistant",tc("create_scene_file",{scene_path:sp,root_node_type:"Node2D"})),
    m("user",res({scene_path:sp,root_node_type:"Node2D",created:true})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_node",arguments:{node_type:"Node2D",node_name:"Grid"}},
      {tool:"add_node",arguments:{node_type:"ColorRect",node_name:"Background",parent_path:`${rp}/Grid`}},
      {tool:"add_node",arguments:{node_type:"Area2D",node_name:"Slot1",parent_path:`${rp}/Grid`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Grid/Slot1`}},
      {tool:"add_node",arguments:{node_type:"Area2D",node_name:"Slot2",parent_path:`${rp}/Grid`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Grid/Slot2`}},
      {tool:"add_node",arguments:{node_type:"Area2D",node_name:"Slot3",parent_path:`${rp}/Grid`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Grid/Slot3`}},
      {tool:"add_node",arguments:{node_type:"Area2D",node_name:"Slot4",parent_path:`${rp}/Grid`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Grid/Slot4`}},
      {tool:"add_node",arguments:{node_type:"RigidBody2D",node_name:"Piece1"}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Piece1`}},
      {tool:"add_node",arguments:{node_type:"RigidBody2D",node_name:"Piece2"}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Piece2`}},
      {tool:"add_node",arguments:{node_type:"Camera2D",node_name:"Cam"}},
      {tool:"add_node",arguments:{node_type:"Label",node_name:"StatusText"}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/Grid`,node_name:"Grid",node_type:"Node2D"},
      {node_path:`${rp}/Piece1`,node_name:"Piece1",node_type:"RigidBody2D"}
    ]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Grid/Slot1`,property:"position",value:{x:0,y:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Grid/Slot2`,property:"position",value:{x:64,y:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Grid/Slot3`,property:"position",value:{x:0,y:64}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Grid/Slot4`,property:"position",value:{x:64,y:64}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Piece1`,property:"position",value:{x:200,y:100}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Piece2`,property:"position",value:{x:300,y:150}}}
    ])),
    m("user",res({results:[{node_path:`${rp}/Grid/Slot1`,property:"position",value:{x:0,y:0}}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"create_gdscript_file",arguments:{script_path:"res://puzzle_piece.gd",contents:`extends RigidBody2D\n\nvar dragging := false\nvar offset := Vector2.ZERO\nvar correct_slot: Area2D\nvar snapped := false\n\nfunc _input(event):\n\tif event is InputEventMouseButton:\n\t\tif event.button_index == MOUSE_BUTTON_LEFT:\n\t\t\tif event.pressed and get_rect().has_point(to_local(event.position)):\n\t\t\t\tdragging = true\n\t\t\t\toffset = position - event.position\n\t\t\telif dragging:\n\t\t\t\tdragging = false\n\t\t\t\tcheck_snap()\n\telif event is InputEventMouseMotion and dragging:\n\t\tposition = event.position + offset\n\nfunc check_snap():\n\tif correct_slot and global_position.distance_to(correct_slot.global_position) < 20:\n\t\tglobal_position = correct_slot.global_position\n\t\tsnapped = true\n\t\tget_parent().check_win()\n\nfunc get_rect():\n\treturn Rect2(-32, -32, 64, 64)`}},
      {tool:"create_gdscript_file",arguments:{script_path:"res://puzzle_manager.gd",contents:`extends Node2D\n\nfunc check_win():\n\tvar pieces := get_children().filter(func(c): return c is RigidBody2D)\n\tif pieces.all(func(p): return p.snapped):\n\t\t$StatusText.text = "You Win!"\n\t\tprint("Puzzle Complete!")`}}
    ])),
    m("user",res({results:[{script_path:"res://puzzle_piece.gd",bytes_written:450},{script_path:"res://puzzle_manager.gd",bytes_written:180}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Piece1`,property:"script",value:"res://puzzle_piece.gd"}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Piece2`,property:"script",value:"res://puzzle_piece.gd"}},
      {tool:"set_node_property",arguments:{node_path:rp,property:"script",value:"res://puzzle_manager.gd"}},
      {tool:"save_scene",arguments:{scene_path:sp}}
    ])),
    m("user",res({results:[{scene_path:sp,saved:true}]})),
    m("assistant",fin("Puzzle game built! 4-slot grid with 2 draggable pieces. Click and drag pieces to their correct slots. When all pieces snap into place, the win condition triggers. Press F5 to play!"))
  ]);
}

function genSurvival(v) {
  const sp = "res://survival_game.tscn";
  const rp = "/root/SurvivalGame";
  return conv([
    m("system",SP),
    m("user","Create a 3D survival game with a player, a procedural world, a crafting system, and an inventory UI."),
    m("assistant",tc("create_scene_file",{scene_path:sp,root_node_type:"Node3D"})),
    m("user",res({scene_path:sp,root_node_type:"Node3D",created:true})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_node",arguments:{node_type:"CharacterBody3D",node_name:"Player"}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Player`}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"capsule",parent_path:`${rp}/Player`}},
      {tool:"add_node",arguments:{node_type:"Camera3D",node_name:"Cam",parent_path:`${rp}/Player`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"Ground"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"plane",parent_path:`${rp}/Ground`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Ground`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"Tree1"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"cylinder",parent_path:`${rp}/Tree1`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Tree1`}},
      {tool:"add_node",arguments:{node_type:"StaticBody3D",node_name:"Rock1"}},
      {tool:"add_primitive_mesh",arguments:{mesh_type:"sphere",parent_path:`${rp}/Rock1`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Rock1`}},
      {tool:"add_node",arguments:{node_type:"DirectionalLight3D",node_name:"Sun"}},
      {tool:"add_node",arguments:{node_type:"Timer",node_name:"HungerTimer"}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/Player`,node_name:"Player",node_type:"CharacterBody3D"},
      {node_path:`${rp}/Tree1`,node_name:"Tree1",node_type:"StaticBody3D"},
      {node_path:`${rp}/HungerTimer`,node_name:"HungerTimer",node_type:"Timer"}
    ]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Player`,property:"position",value:{x:0,y:2,z:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Ground`,property:"scale",value:{x:100,y:1,z:100}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Tree1`,property:"position",value:{x:10,y:2,z:5}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Tree1`,property:"scale",value:{x:0.5,y:4,z:0.5}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Rock1`,property:"position",value:{x:-8,y:0.5,z:12}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/HungerTimer`,property:"wait_time",value:5}},
      {tool:"add_input_action",arguments:{action_name:"gather",key:"Key F"}},
      {tool:"add_input_action",arguments:{action_name:"open_inventory",key:"Key I"}}
    ])),
    m("user",res({results:[{action_name:"gather",created:true},{action_name:"open_inventory",created:true}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"create_gdscript_file",arguments:{script_path:"res://survival_player.gd",contents:`extends CharacterBody3D\n\nvar speed := 6.0\nvar gravity := ProjectSettings.get_setting("physics/3d/default_gravity")\nvar inventory := {}\nvar health := 100.0\nvar hunger := 100.0\n\nfunc _physics_process(delta):\n\tif not is_on_floor():\n\t\tvelocity.y -= gravity * delta\n\tvar input_dir := Input.get_vector("move_left", "move_right", "move_forward", "move_back")\n\tvar direction := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()\n\tif direction:\n\t\tvelocity.x = direction.x * speed\n\t\tvelocity.z = direction.z * speed\n\telse:\n\t\tvelocity.x = move_toward(velocity.x, 0, speed)\n\t\tvelocity.z = move_toward(velocity.z, 0, speed)\n\tmove_and_slide()\n\nfunc gather_resource(resource_name: String, amount: int):\n\tinventory[resource_name] = (inventory.get(resource_name, 0) + amount)\n\tprint("Gathered " + str(amount) + " " + resource_name)`}},
      {tool:"create_gdscript_file",arguments:{script_path:"res://survival_manager.gd",contents:`extends Node3D\n\n@onready var player: CharacterBody3D = $Player\n@onready var hunger_timer: Timer = $HungerTimer\n\nfunc _ready():\n\thunger_timer.timeout.connect(_on_hunger_tick)\n\thunger_timer.start()\n\nfunc _on_hunger_tick():\n\tplayer.hunger = max(0, player.hunger - 2)\n\tif player.hunger <= 0:\n\t\tplayer.health = max(0, player.health - 5)\n\tif player.health <= 0:\n\t\tprint("Game Over!")\n\t\tget_tree().paused = true\n\nfunc craft_item(recipe_name: String):\n\tmatch recipe_name:\n\t\t"campfire":\n\t\t\tif player.inventory.get("wood", 0) >= 3:\n\t\t\t\tplayer.inventory["wood"] -= 3\n\t\t\t\tplayer.inventory["campfire"] = 1\n\t\t\t\tprint("Crafted campfire!")`}}
    ])),
    m("user",res({results:[{script_path:"res://survival_player.gd",bytes_written:620},{script_path:"res://survival_manager.gd",bytes_written:450}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Player`,property:"script",value:"res://survival_player.gd"}},
      {tool:"set_node_property",arguments:{node_path:rp,property:"script",value:"res://survival_manager.gd"}},
      {tool:"save_scene",arguments:{scene_path:sp}}
    ])),
    m("user",res({results:[{scene_path:sp,saved:true}]})),
    m("assistant",fin("Survival game built! Player with WASD movement, hunger/health system with timer drain, trees and rocks for gathering (F key), inventory system, and campfire crafting recipe. Hunger drains 2/5s, health drains when starving. Press F5 to survive!"))
  ]);
}

function genMultiplayer(v) {
  const sp = "res://lobby.tscn";
  const rp = "/root/Lobby";
  return conv([
    m("system",SP),
    m("user","Build a multiplayer lobby scene with connection UI, a player list, and chat functionality using Godot's multiplayer API."),
    m("assistant",tc("create_scene_file",{scene_path:sp,root_node_type:"Control"})),
    m("user",res({scene_path:sp,root_node_type:"Control",created:true})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_node",arguments:{node_type:"VBoxContainer",node_name:"Layout"}},
      {tool:"add_node",arguments:{node_type:"Label",node_name:"TitleLabel",parent_path:`${rp}/Layout`}},
      {tool:"add_node",arguments:{node_type:"HBoxContainer",node_name:"ConnectionRow",parent_path:`${rp}/Layout`}},
      {tool:"add_node",arguments:{node_type:"LineEdit",node_name:"IPInput",parent_path:`${rp}/Layout/ConnectionRow`}},
      {tool:"add_node",arguments:{node_type:"Button",node_name:"HostBtn",parent_path:`${rp}/Layout/ConnectionRow`}},
      {tool:"add_node",arguments:{node_type:"Button",node_name:"JoinBtn",parent_path:`${rp}/Layout/ConnectionRow`}},
      {tool:"add_node",arguments:{node_type:"ItemList",node_name:"PlayerList",parent_path:`${rp}/Layout`}},
      {tool:"add_node",arguments:{node_type:"RichTextLabel",node_name:"ChatBox",parent_path:`${rp}/Layout`}},
      {tool:"add_node",arguments:{node_type:"HBoxContainer",node_name:"ChatRow",parent_path:`${rp}/Layout`}},
      {tool:"add_node",arguments:{node_type:"LineEdit",node_name:"ChatInput",parent_path:`${rp}/Layout/ChatRow`}},
      {tool:"add_node",arguments:{node_type:"Button",node_name:"SendBtn",parent_path:`${rp}/Layout/ChatRow`}},
      {tool:"add_node",arguments:{node_type:"Button",node_name:"StartGameBtn",parent_path:`${rp}/Layout`}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/Layout`,node_name:"Layout",node_type:"VBoxContainer"},
      {node_path:`${rp}/Layout/PlayerList`,node_name:"PlayerList",node_type:"ItemList"}
    ]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Layout/TitleLabel`,property:"text",value:"Multiplayer Lobby"}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Layout/ConnectionRow/HostBtn`,property:"text",value:"Host"}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Layout/ConnectionRow/JoinBtn`,property:"text",value:"Join"}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Layout/ConnectionRow/IPInput`,property:"placeholder_text",value:"127.0.0.1"}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Layout/ChatRow/SendBtn`,property:"text",value:"Send"}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Layout/StartGameBtn`,property:"text",value:"Start Game"}}
    ])),
    m("user",res({results:[{node_path:`${rp}/Layout/TitleLabel`,property:"text",value:"Multiplayer Lobby"}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"create_gdscript_file",arguments:{script_path:"res://lobby.gd",contents:`extends Control\n\nconst PORT := 7777\nvar peer: ENetMultiplayerPeer\n\nfunc _ready():\n\t$Layout/ConnectionRow/HostBtn.pressed.connect(_on_host)\n\t$Layout/ConnectionRow/JoinBtn.pressed.connect(_on_join)\n\t$Layout/ChatRow/SendBtn.pressed.connect(_on_send)\n\t$Layout/StartGameBtn.pressed.connect(_on_start)\n\tmultiplayer.peer_connected.connect(_on_player_connected)\n\tmultiplayer.peer_disconnected.connect(_on_player_disconnected)\n\nfunc _on_host():\n\tpeer = ENetMultiplayerPeer.new()\n\tpeer.create_server(PORT)\n\tmultiplayer.multiplayer_peer = peer\n\tadd_player(1)\n\nfunc _on_join():\n\tvar ip := $Layout/ConnectionRow/IPInput.text\n\tpeer = ENetMultiplayerPeer.new()\n\tpeer.create_client(ip, PORT)\n\tmultiplayer.multiplayer_peer = peer\n\nfunc _on_player_connected(id: int):\n\tadd_player(id)\n\nfunc _on_player_disconnected(id: int):\n\tremove_player(id)\n\nfunc add_player(id: int):\n\t$Layout/PlayerList.add_item("Player " + str(id))\n\nfunc remove_player(id: int):\n\tfor i in range($Layout/PlayerList.item_count):\n\t\tif $Layout/PlayerList.get_item_text(i) == "Player " + str(id):\n\t\t\t$Layout/PlayerList.remove_item(i)\n\t\t\tbreak\n\nfunc _on_send():\n\tvar msg := $Layout/ChatRow/ChatInput.text\n\tif msg:\n\t\tsend_chat.rpc(msg)\n\t\t$Layout/ChatRow/ChatInput.text = ""\n\n@rpc("any_peer", "call_local")\nfunc send_chat(msg: String):\n\t$Layout/ChatBox.add_text("Player: " + msg + "\\n")\n\nfunc _on_start():\n\tif multiplayer.is_server():\n\t\tprint("Starting game...")`}}
    ])),
    m("user",res({script_path:"res://lobby.gd",bytes_written:1100})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:rp,property:"script",value:"res://lobby.gd"}},
      {tool:"save_scene",arguments:{scene_path:sp}}
    ])),
    m("user",res({results:[{scene_path:sp,saved:true}]})),
    m("assistant",fin("Multiplayer lobby built! Host/Join UI with IP input, player list, chat system with RPC, and start game button. Uses ENetMultiplayerPeer for networking. Chat messages sync across all clients. Press F5 to test!"))
  ]);
}

function genRhythm(v) {
  const sp = "res://rhythm_game.tscn";
  const rp = "/root/RhythmGame";
  return conv([
    m("system",SP),
    m("user","Create a music rhythm game with falling notes, input timing, a scoring system, and an audio player."),
    m("assistant",tc("create_scene_file",{scene_path:sp,root_node_type:"Node2D"})),
    m("user",res({scene_path:sp,root_node_type:"Node2D",created:true})),
    m("assistant",batch({scene_path:sp},[
      {tool:"add_node",arguments:{node_type:"ColorRect",node_name:"Background"}},
      {tool:"add_node",arguments:{node_type:"Node2D",node_name:"Lane1"}},
      {tool:"add_node",arguments:{node_type:"Node2D",node_name:"Lane2"}},
      {tool:"add_node",arguments:{node_type:"Node2D",node_name:"Lane3"}},
      {tool:"add_node",arguments:{node_type:"Node2D",node_name:"Lane4"}},
      {tool:"add_node",arguments:{node_type:"Area2D",node_name:"HitZone1",parent_path:`${rp}/Lane1`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Lane1/HitZone1`}},
      {tool:"add_node",arguments:{node_type:"Area2D",node_name:"HitZone2",parent_path:`${rp}/Lane2`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Lane2/HitZone2`}},
      {tool:"add_node",arguments:{node_type:"Area2D",node_name:"HitZone3",parent_path:`${rp}/Lane3`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Lane3/HitZone3`}},
      {tool:"add_node",arguments:{node_type:"Area2D",node_name:"HitZone4",parent_path:`${rp}/Lane4`}},
      {tool:"add_collision_shape",arguments:{parent_path:`${rp}/Lane4/HitZone4`}},
      {tool:"add_node",arguments:{node_type:"Label",node_name:"ScoreLabel"}},
      {tool:"add_node",arguments:{node_type:"Label",node_name:"ComboLabel"}},
      {tool:"add_node",arguments:{node_type:"Timer",node_name:"NoteSpawner"}},
      {tool:"add_node",arguments:{node_type:"AudioStreamPlayer",node_name:"MusicPlayer"}}
    ])),
    m("user",res({results:[
      {node_path:`${rp}/Lane1`,node_name:"Lane1",node_type:"Node2D"},
      {node_path:`${rp}/ScoreLabel`,node_name:"ScoreLabel",node_type:"Label"},
      {node_path:`${rp}/NoteSpawner`,node_name:"NoteSpawner",node_type:"Timer"}
    ]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:`${rp}/Lane1`,property:"position",value:{x:100,y:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Lane2`,property:"position",value:{x:200,y:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Lane3`,property:"position",value:{x:300,y:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/Lane4`,property:"position",value:{x:400,y:0}}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/ScoreLabel`,property:"text",value:"Score: 0"}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/ComboLabel`,property:"text",value:"Combo: 0"}},
      {tool:"set_node_property",arguments:{node_path:`${rp}/NoteSpawner`,property:"wait_time",value:0.5}},
      {tool:"add_input_action",arguments:{action_name:"hit1",key:"Key D"}},
      {tool:"add_input_action",arguments:{action_name:"hit2",key:"Key F"}},
      {tool:"add_input_action",arguments:{action_name:"hit3",key:"Key J"}},
      {tool:"add_input_action",arguments:{action_name:"hit4",key:"Key K"}}
    ])),
    m("user",res({results:[{action_name:"hit1",created:true}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"create_gdscript_file",arguments:{script_path:"res://note.gd",contents:`extends RigidBody2D\n\nvar speed := 300.0\nvar lane := 0\n\nfunc _physics_process(delta):\n\tposition.y += speed * delta\n\tif position.y > 700:\n\t\tqueue_free()\n\t\tget_parent().get_parent().on_miss()`}},
      {tool:"create_gdscript_file",arguments:{script_path:"res://rhythm_manager.gd",contents:`extends Node2D\n\nvar score := 0\nvar combo := 0\nvar best_combo := 0\n\n@onready var spawner: Timer = $NoteSpawner\nvar lane_scenes := []\n\nfunc _ready():\n\tspawner.timeout.connect(_spawn_note)\n\tspawner.start()\n\nfunc _spawn_note():\n\tvar lane := randi() % 4\n\tvar note := RigidBody2D.new()\n\tnote.set_script(preload("res://note.gd"))\n\tvar lane_node = get_child(lane)\n\tlane_node.add_child(note)\n\tnote.position = Vector2(0, -50)\n\nfunc _input(event):\n\tfor i in range(4):\n\t\tvar action := "hit" + str(i + 1)\n\t\tif event.is_action_pressed(action):\n\t\t\t_check_hit(i)\n\nfunc _check_hit(lane_idx: int):\n\tvar lane_node = get_child(lane_idx)\n\tfor child in lane_node.get_children():\n\t\tif child is RigidBody2D and abs(child.position.y - 500) < 40:\n\t\t\tscore += 100 + combo * 10\n\t\t\tcombo += 1\n\t\t\tbest_combo = max(best_combo, combo)\n\t\t\tchild.queue_free()\n\t\t\t_update_ui()\n\t\t\treturn\n\tcombo = 0\n\t_update_ui()\n\nfunc on_miss():\n\tcombo = 0\n\t_update_ui()\n\nfunc _update_ui():\n\t$ScoreLabel.text = "Score: " + str(score)\n\t$ComboLabel.text = "Combo: " + str(combo)`}}
    ])),
    m("user",res({results:[{script_path:"res://note.gd",bytes_written:150},{script_path:"res://rhythm_manager.gd",bytes_written:680}]})),
    m("assistant",batch({scene_path:sp},[
      {tool:"set_node_property",arguments:{node_path:rp,property:"script",value:"res://rhythm_manager.gd"}},
      {tool:"save_scene",arguments:{scene_path:sp}}
    ])),
    m("user",res({results:[{scene_path:sp,saved:true}]})),
    m("assistant",fin("Rhythm game built! 4 lanes with falling notes (D, F, J, K keys), hit zones, scoring with combo multiplier, and note spawner timer. Notes fall at 300px/s and spawn every 0.5s. Score 100+combo*10 per hit. Press F5 to rock!"))
  ]);
}

const generators = [
  gen3DPlatformer, gen2DTopDown, genFPS, genTowerDefense,
  genRPG, genRacing, genPuzzle, genSurvival,
  genMultiplayer, genRhythm
];

const lines = [];
for (let g = 0; g < generators.length; g++) {
  for (let v = 0; v < 8; v++) {
    try {
      const line = generators[g](v);
      JSON.parse(line);
      lines.push(line);
    } catch(e) {
      console.error(`Error in generator ${g} variant ${v}: ${e.message}`);
    }
  }
}

fs.writeFileSync(OUT, lines.join('\n'), 'utf8');
console.log(`Wrote ${lines.length} conversations to crosshair_full_chains.jsonl`);

let bad = 0;
for (const line of lines) {
  try { JSON.parse(line); } catch { bad++; console.error('Invalid JSON line'); }
}
if (bad > 0) console.error(`${bad} invalid JSONL lines!`);
else console.log('All JSONL lines valid!');
