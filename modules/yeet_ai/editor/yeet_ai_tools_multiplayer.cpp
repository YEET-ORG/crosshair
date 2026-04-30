/**************************************************************************/
/*  yeet_ai_tools_multiplayer.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "modules/multiplayer/multiplayer_spawner.h"
#include "modules/multiplayer/multiplayer_synchronizer.h"
#include "modules/multiplayer/scene_replication_config.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/packed_scene.h"

Dictionary YeetAIDock::_tool_create_multiplayer_spawner(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "MultiplayerSpawner");

	MultiplayerSpawner *spawner = memnew(MultiplayerSpawner);
	spawner->set_name(node_name);

	Array spawn_scenes = _arg_array(p_args, "spawnable_scenes");
	for (int i = 0; i < spawn_scenes.size(); i++) {
		const String path = String(spawn_scenes[i]);
		if (path.begins_with("res://")) {
			spawner->add_spawnable_scene(path);
		}
	}

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, spawner, scene_root);

	result["ok"] = true;
	result["node_path"] = String(spawner->get_path());
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_create_multiplayer_synchronizer(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "MultiplayerSynchronizer");

	MultiplayerSynchronizer *sync = memnew(MultiplayerSynchronizer);
	sync->set_name(node_name);

	Ref<SceneReplicationConfig> rep_cfg;
	rep_cfg.instantiate();
	Array rep = _arg_array(p_args, "replication_config");
	for (int i = 0; i < rep.size(); i++) {
		const Variant &rv = rep[i];
		if (rv.get_type() == Variant::DICTIONARY) {
			const Dictionary rd = rv;
			const String prop_path = _arg_string(rd, "property", "");
			if (prop_path.is_empty()) {
				continue;
			}
			rep_cfg->add_property(NodePath(prop_path));
		}
	}
	sync->set_replication_config(rep_cfg);

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, sync, scene_root);

	result["ok"] = true;
	result["node_path"] = String(sync->get_path());
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_get_network_state(const Dictionary &p_args) const {
	(void)p_args;
	Dictionary result;
	SceneTree *st = get_tree();
	if (st == nullptr) {
		result["connected"] = false;
		result["reason"] = "Not in scene tree";
		return result;
	}
	const Ref<MultiplayerAPI> mp = st->get_multiplayer();
	if (mp.is_null()) {
		result["connected"] = false;
		result["reason"] = "No multiplayer API available";
		return result;
	}

	const bool connected = mp->has_multiplayer_peer() && mp->get_multiplayer_peer()->get_connection_status() != MultiplayerPeer::CONNECTION_DISCONNECTED;
	result["connected"] = connected;
	result["server_peer_id"] = mp->get_unique_id() == 1 ? 1 : 0;
	result["peer_id"] = mp->get_unique_id();
	result["has_server"] = mp->is_server();
	const Vector<int> peer_ids = mp->get_peer_ids();
	Array peers_array;
	for (int i = 0; i < peer_ids.size(); i++) {
		peers_array.push_back(peer_ids[i]);
	}
	result["connected_peers"] = peers_array;

	return result;
}
