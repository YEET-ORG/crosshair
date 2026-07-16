/**************************************************************************/
/*  yeet_ai_tools_tilemap_physics.cpp                                      */
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/config/project_settings.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/2d/tile_map.h"
#include "scene/2d/tile_map_layer.h"
#include "scene/resources/2d/tile_set.h"
#include "scene/resources/2d/tile_set.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/2d/physics/collision_object_2d.h"
#include "scene/3d/physics/physics_body_3d.h"
#include "scene/2d/physics/physics_body_2d.h"
#include "scene/resources/physics_material.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_loader.h"


Dictionary YeetAIDock::_tool_set_tilemap_cells(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	TileMapLayer *layer = Object::cast_to<TileMapLayer>(node);
	if (layer == nullptr) {
		return _make_error("Node is not a TileMapLayer");
	}

	Array cells = _arg_array(p_args, "cells");
	int painted = 0;
	for (int i = 0; i < cells.size(); i++) {
		Dictionary cd = cells[i];
		Vector2i coords(_arg_int(cd, "x", 0), _arg_int(cd, "y", 0));
		int source_id = _arg_int(cd, "source_id", -1);
		Vector2i atlas_coords(_arg_int(cd, "atlas_coords_x", -1), _arg_int(cd, "atlas_coords_y", -1));
		int alternative = _arg_int(cd, "alternative_tile", 0);
		layer->set_cell(coords, source_id, atlas_coords, alternative);
		painted++;
	}

	_mark_unsaved();
	Dictionary extra;
	extra["cells_painted"] = painted;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_clear_tilemap_cells(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	TileMapLayer *layer = Object::cast_to<TileMapLayer>(node);
	if (layer == nullptr) {
		return _make_error("Node is not a TileMapLayer");
	}

	int from_x = _arg_int(p_args, "from_x", 0);
	int from_y = _arg_int(p_args, "from_y", 0);
	int to_x = _arg_int(p_args, "to_x", 0);
	int to_y = _arg_int(p_args, "to_y", 0);

	int cleared = 0;
	for (int y = from_y; y <= to_y; y++) {
		for (int x = from_x; x <= to_x; x++) {
			layer->set_cell(Vector2i(x, y), -1);
			cleared++;
		}
	}

	_mark_unsaved();
	Dictionary extra;
	extra["cells_cleared"] = cleared;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_get_tileset_sources(const Dictionary &p_args) const {
	Ref<TileSet> ts;

	const String tileset_path = _arg_string(p_args, "tileset_path", "");
	if (!tileset_path.is_empty()) {
		ts = ResourceLoader::load(tileset_path, "TileSet");
		if (ts.is_null()) {
			return _make_error("Failed to load TileSet from: " + tileset_path);
		}
	} else {
		String err;
		Node *scene_root;
		Node *node;
		if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
			return _make_error(err);
		}
		TileMapLayer *layer = Object::cast_to<TileMapLayer>(node);
		if (layer == nullptr) {
			TileMap *tm = Object::cast_to<TileMap>(node);
			if (tm == nullptr) {
				return _make_error("Node is not a TileMapLayer or TileMap");
			}
			ts = tm->get_tileset();
		} else {
			ts = layer->get_tile_set();
		}
		if (ts.is_null()) {
			return _make_error("Target node has no TileSet assigned");
		}
	}

	Array sources;
	const int count = ts->get_source_count();
	for (int i = 0; i < count; i++) {
		const int source_id = ts->get_source_id(i);
		Ref<TileSetSource> source = ts->get_source(source_id);
		if (source.is_null()) {
			continue;
		}
		Dictionary entry;
		entry["source_id"] = source_id;
		entry["source_name"] = source->get_name();
		Ref<TileSetAtlasSource> atlas = source;
		if (atlas.is_valid()) {
			Ref<Texture2D> tex = atlas->get_texture();
			entry["texture_path"] = tex.is_valid() ? String(tex->get_path()) : String();
		} else {
			entry["texture_path"] = String();
		}
		sources.push_back(entry);
	}

	Dictionary extra;
	extra["sources"] = sources;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_create_tileset(const Dictionary &p_args) const {
	const String save_path = _arg_string(p_args, "save_path", "");
	if (!save_path.begins_with("res://")) {
		return _make_error("save_path must start with res://");
	}

	const int tile_size = _arg_int(p_args, "tile_size", 16);

	Ref<TileSet> ts;
	ts.instantiate();
	ts->set_tile_size(Size2i(tile_size, tile_size));

	const Error save_err = ResourceSaver::save(ts, save_path);
	if (save_err != OK) {
		return _make_error(vformat("Failed to save TileSet: error %d", save_err));
	}

	Dictionary extra;
	extra["tileset_path"] = save_path;
	extra["tile_size"] = tile_size;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_add_tileset_atlas_source(const Dictionary &p_args) const {
	const String tileset_path = _arg_string(p_args, "tileset_path", "");
	if (!tileset_path.begins_with("res://")) {
		return _make_error("tileset_path must start with res://");
	}

	const String texture_path = _arg_string(p_args, "texture_path", "");
	if (!texture_path.begins_with("res://")) {
		return _make_error("texture_path must start with res://");
	}

	Ref<TileSet> ts = ResourceLoader::load(tileset_path, "TileSet");
	if (ts.is_null()) {
		return _make_error("Failed to load TileSet from: " + tileset_path);
	}

	Ref<Texture2D> texture = ResourceLoader::load(texture_path, "Texture2D");
	if (texture.is_null()) {
		return _make_error("Failed to load texture from: " + texture_path);
	}

	const int tile_size_x = _arg_int(p_args, "tile_size_x", 16);
	const int tile_size_y = _arg_int(p_args, "tile_size_y", 16);
	const int separation_x = _arg_int(p_args, "separation_x", 0);
	const int separation_y = _arg_int(p_args, "separation_y", 0);

	Ref<TileSetAtlasSource> atlas;
	atlas.instantiate();
	atlas->set_texture(texture);
	atlas->set_texture_region_size(Vector2i(tile_size_x, tile_size_y));
	atlas->set_separation(Vector2i(separation_x, separation_y));

	const int source_id = ts->add_source(atlas);

	const Error save_err = ResourceSaver::save(ts, tileset_path);
	if (save_err != OK) {
		ts->remove_source(source_id);
		return _make_error(vformat("Failed to save TileSet after adding atlas source: error %d", save_err));
	}

	Dictionary extra;
	extra["source_id"] = source_id;
	extra["tileset_path"] = tileset_path;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_set_tile_collision_polygon(const Dictionary &p_args) const {
	const String tileset_path = _arg_string(p_args, "tileset_path", "");
	if (!tileset_path.begins_with("res://")) {
		return _make_error("tileset_path must start with res://");
	}

	Ref<TileSet> ts = ResourceLoader::load(tileset_path, "TileSet");
	if (ts.is_null()) {
		return _make_error("Failed to load TileSet from: " + tileset_path);
	}

	const int source_id = _arg_int(p_args, "source_id", 0);
	const int atlas_coords_x = _arg_int(p_args, "atlas_coords_x", 0);
	const int atlas_coords_y = _arg_int(p_args, "atlas_coords_y", 0);
	const Vector2i atlas_coords(atlas_coords_x, atlas_coords_y);
	const int physics_layer = _arg_int(p_args, "physics_layer", 0);

	Ref<TileSetAtlasSource> atlas = ts->get_source(source_id);
	if (atlas.is_null()) {
		return _make_error(vformat("No atlas source with source_id %d", source_id));
	}

	if (!atlas->has_tile(atlas_coords)) {
		return _make_error(vformat("No tile at atlas coords (%d, %d)", atlas_coords_x, atlas_coords_y));
	}

	while (ts->get_physics_layers_count() <= physics_layer) {
		ts->add_physics_layer(ts->get_physics_layers_count());
	}

	TileData *tile_data = atlas->get_tile_data(atlas_coords, 0);
	if (tile_data == nullptr) {
		return _make_error("Failed to get TileData for the specified tile");
	}

	Array points = _arg_array(p_args, "polygon_points");
	Vector<Vector2> polygon;
	for (int i = 0; i < points.size(); i++) {
		Dictionary pd = points[i];
		polygon.push_back(Vector2(_arg_float(pd, "x", 0.0), _arg_float(pd, "y", 0.0)));
	}

	const int poly_idx = tile_data->get_collision_polygons_count(physics_layer);
	tile_data->add_collision_polygon(physics_layer);
	tile_data->set_collision_polygon_points(physics_layer, poly_idx, polygon);

	const Error save_err = ResourceSaver::save(ts, tileset_path);
	if (save_err != OK) {
		return _make_error(vformat("Failed to save TileSet after setting collision polygon: error %d", save_err));
	}

	Dictionary extra;
	extra["source_id"] = source_id;
	extra["atlas_coords"] = vformat("(%d, %d)", atlas_coords_x, atlas_coords_y);
	extra["physics_layer"] = physics_layer;
	extra["polygon_points"] = points.size();
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_paint_terrain(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	TileMapLayer *layer = Object::cast_to<TileMapLayer>(node);
	if (layer == nullptr) {
		return _make_error("Node is not a TileMapLayer");
	}

	const int terrain_set = _arg_int(p_args, "terrain_set", 0);
	const int terrain = _arg_int(p_args, "terrain", 0);
	Array cells = _arg_array(p_args, "cells");

	TypedArray<Vector2i> coords_array;
	for (int i = 0; i < cells.size(); i++) {
		Dictionary cd = cells[i];
		coords_array.push_back(Vector2i(_arg_int(cd, "x", 0), _arg_int(cd, "y", 0)));
	}

	layer->set_cells_terrain_connect(coords_array, terrain_set, terrain);

	_mark_unsaved();
	Dictionary extra;
	extra["cells_painted"] = coords_array.size();
	extra["terrain_set"] = terrain_set;
	extra["terrain"] = terrain;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_create_tilemap_layer(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "Layer0");

	TileMapLayer *layer = memnew(TileMapLayer);
	layer->set_name(node_name);

	// Optional: assign tileset
	const String tile_set_path = _arg_string(p_args, "tileset_path", "");
	if (!tile_set_path.is_empty()) {
		Ref<TileSet> ts = ResourceLoader::load(tile_set_path);
		if (ts.is_valid()) {
			layer->set_tile_set(ts);
		}
	}

	// Find parent: either a TileMap or regular node
	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, layer, scene_root);

	layer->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(layer->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_fill_tilemap_rect(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	TileMapLayer *layer = Object::cast_to<TileMapLayer>(node);
	if (layer == nullptr) {
		return _make_error("Node is not a TileMapLayer");
	}

	const int from_x = _arg_int(p_args, "from_x", 0);
	const int from_y = _arg_int(p_args, "from_y", 0);
	const int to_x = _arg_int(p_args, "to_x", 0);
	const int to_y = _arg_int(p_args, "to_y", 0);
	const int source_id = _arg_int(p_args, "source_id", 0);
	const int atlas_coords_x = _arg_int(p_args, "atlas_coords_x", -1);
	const int atlas_coords_y = _arg_int(p_args, "atlas_coords_y", -1);
	const int alternative_tile = _arg_int(p_args, "alternative_tile", 0);

	Vector2i atlas_coords(atlas_coords_x, atlas_coords_y);

	int painted = 0;
	for (int y = from_y; y <= to_y; y++) {
		for (int x = from_x; x <= to_x; x++) {
			layer->set_cell(Vector2i(x, y), source_id, atlas_coords, alternative_tile);
			painted++;
		}
	}

	_mark_unsaved();
	Dictionary extra;
	extra["cells_painted"] = painted;
	extra["rect"] = vformat("(%d,%d) to (%d,%d)", from_x, from_y, to_x, to_y);
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_set_collision_layer_mask(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	CollisionObject3D *co3 = Object::cast_to<CollisionObject3D>(node);
	CollisionObject2D *co2 = Object::cast_to<CollisionObject2D>(node);
	if (co3 == nullptr && co2 == nullptr) {
		return _make_error("Node is not a CollisionObject2D or CollisionObject3D");
	}

	bool is_3d = (co3 != nullptr);

	uint32_t layer = co3 ? co3->get_collision_layer() : co2->get_collision_layer();
	uint32_t mask = co3 ? co3->get_collision_mask() : co2->get_collision_mask();

	if (p_args.has("layer")) {
		layer = (uint32_t)_arg_int(p_args, "layer", 0);
	}
	if (p_args.has("mask")) {
		mask = (uint32_t)_arg_int(p_args, "mask", 0);
	}

	const String dimension_key = is_3d ? "3d_physics" : "2d_physics";

	Array layer_names = _arg_array(p_args, "layer_names");
	if (layer_names.size() > 0) {
		layer = 0;
		for (int i = 0; i < layer_names.size(); i++) {
			const String name = layer_names[i];
			for (int bit = 1; bit <= 32; bit++) {
				const String setting = vformat("layer_names/%s/layer_%d", dimension_key, bit);
				const String layer_name = GLOBAL_GET(setting);
				if (layer_name == name) {
					layer |= (1u << (bit - 1));
					break;
				}
			}
		}
	}

	Array mask_names = _arg_array(p_args, "mask_names");
	if (mask_names.size() > 0) {
		mask = 0;
		for (int i = 0; i < mask_names.size(); i++) {
			const String name = mask_names[i];
			for (int bit = 1; bit <= 32; bit++) {
				const String setting = vformat("layer_names/%s/layer_%d", dimension_key, bit);
				const String layer_name = GLOBAL_GET(setting);
				if (layer_name == name) {
					mask |= (1u << (bit - 1));
					break;
				}
			}
		}
	}

	if (is_3d) {
		_commit_ai_property_change(co3, SNAME("collision_layer"), co3->get_collision_layer(), layer, "Set Collision Layer");
		_commit_ai_property_change(co3, SNAME("collision_mask"), co3->get_collision_mask(), mask, "Set Collision Mask");
	} else {
		_commit_ai_property_change(co2, SNAME("collision_layer"), co2->get_collision_layer(), layer, "Set Collision Layer");
		_commit_ai_property_change(co2, SNAME("collision_mask"), co2->get_collision_mask(), mask, "Set Collision Mask");
	}

	Dictionary extra;
	extra["collision_layer"] = (int64_t)layer;
	extra["collision_mask"] = (int64_t)mask;
	extra["dimension"] = is_3d ? "3d" : "2d";
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_add_collision_exception(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	const String exception_node_path = _arg_string(p_args, "exception_node_path", "");
	if (exception_node_path.is_empty()) {
		return _make_error("exception_node_path is required");
	}

	Node *exception_node = _resolve_node_target(scene_root, exception_node_path, err);
	if (exception_node == nullptr) {
		return _make_error("Exception node not found: " + err);
	}

	PhysicsBody3D *pb3 = Object::cast_to<PhysicsBody3D>(node);
	PhysicsBody2D *pb2 = Object::cast_to<PhysicsBody2D>(node);
	EditorUndoRedoManager *urm = _get_ai_undo_redo();
	if (pb3) {
		if (urm != nullptr) {
			urm->create_action("AI: Add Collision Exception");
			urm->add_do_method(pb3, "add_collision_exception_with", exception_node);
			urm->add_undo_method(pb3, "remove_collision_exception_with", exception_node);
			urm->commit_action();
			_mark_unsaved();
		} else {
			pb3->add_collision_exception_with(exception_node);
			_mark_unsaved();
		}
	} else if (pb2) {
		if (urm != nullptr) {
			urm->create_action("AI: Add Collision Exception");
			urm->add_do_method(pb2, "add_collision_exception_with", exception_node);
			urm->add_undo_method(pb2, "remove_collision_exception_with", exception_node);
			urm->commit_action();
			_mark_unsaved();
		} else {
			pb2->add_collision_exception_with(exception_node);
			_mark_unsaved();
		}
	} else {
		return _make_error("Node is not a PhysicsBody2D or PhysicsBody3D");
	}

	Dictionary extra;
	extra["exception_node_path"] = exception_node_path;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_remove_collision_exception(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	const String exception_node_path = _arg_string(p_args, "exception_node_path", "");
	if (exception_node_path.is_empty()) {
		return _make_error("exception_node_path is required");
	}

	Node *exception_node = _resolve_node_target(scene_root, exception_node_path, err);
	if (exception_node == nullptr) {
		return _make_error("Exception node not found: " + err);
	}

	PhysicsBody3D *pb3 = Object::cast_to<PhysicsBody3D>(node);
	PhysicsBody2D *pb2 = Object::cast_to<PhysicsBody2D>(node);
	EditorUndoRedoManager *urm = _get_ai_undo_redo();
	if (pb3) {
		if (urm != nullptr) {
			urm->create_action("AI: Remove Collision Exception");
			urm->add_do_method(pb3, "remove_collision_exception_with", exception_node);
			urm->add_undo_method(pb3, "add_collision_exception_with", exception_node);
			urm->commit_action();
			_mark_unsaved();
		} else {
			pb3->remove_collision_exception_with(exception_node);
			_mark_unsaved();
		}
	} else if (pb2) {
		if (urm != nullptr) {
			urm->create_action("AI: Remove Collision Exception");
			urm->add_do_method(pb2, "remove_collision_exception_with", exception_node);
			urm->add_undo_method(pb2, "add_collision_exception_with", exception_node);
			urm->commit_action();
			_mark_unsaved();
		} else {
			pb2->remove_collision_exception_with(exception_node);
			_mark_unsaved();
		}
	} else {
		return _make_error("Node is not a PhysicsBody2D or PhysicsBody3D");
	}

	Dictionary extra;
	extra["exception_node_path"] = exception_node_path;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_get_collision_exceptions(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	Array exceptions;

	PhysicsBody3D *pb3 = Object::cast_to<PhysicsBody3D>(node);
	PhysicsBody2D *pb2 = Object::cast_to<PhysicsBody2D>(node);
	if (pb3) {
		TypedArray<PhysicsBody3D> exc = pb3->get_collision_exceptions();
		for (int i = 0; i < exc.size(); i++) {
			Node *n = Object::cast_to<Node>(exc[i]);
			if (n) {
				exceptions.push_back(String(n->get_path()));
			}
		}
	} else if (pb2) {
		TypedArray<PhysicsBody2D> exc = pb2->get_collision_exceptions();
		for (int i = 0; i < exc.size(); i++) {
			Node *n = Object::cast_to<Node>(exc[i]);
			if (n) {
				exceptions.push_back(String(n->get_path()));
			}
		}
	} else {
		return _make_error("Node is not a PhysicsBody2D or PhysicsBody3D");
	}

	Dictionary extra;
	extra["exceptions"] = exceptions;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_set_physics_material(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	const double friction = _arg_float(p_args, "friction", 1.0);
	const double bounce = _arg_float(p_args, "bounce", 0.0);
	const bool absorbent = _arg_bool(p_args, "absorbent", false);
	const bool rough = _arg_bool(p_args, "rough", false);

	Ref<PhysicsMaterial> mat;
	bool created_new = false;

	if (node->has_method("get_physics_material_override")) {
		Variant v = node->call("get_physics_material_override");
		mat = v;
	}

	if (mat.is_null()) {
		mat.instantiate();
		created_new = true;
	}

	if (node->has_method("set_physics_material_override")) {
		if (created_new) {
			mat->set_friction(friction);
			mat->set_bounce(bounce);
			mat->set_absorbent(absorbent);
			mat->set_rough(rough);
			_commit_ai_property_change(node, SNAME("physics_material_override"), Variant(), mat, "Set Physics Material");
		} else {
			_commit_ai_property_change(mat.ptr(), SNAME("friction"), mat->get_friction(), friction, "Set Physics Material");
			_commit_ai_property_change(mat.ptr(), SNAME("bounce"), mat->get_bounce(), bounce, "Set Physics Material");
			_commit_ai_property_change(mat.ptr(), SNAME("absorbent"), mat->is_absorbent(), absorbent, "Set Physics Material");
			_commit_ai_property_change(mat.ptr(), SNAME("rough"), mat->is_rough(), rough, "Set Physics Material");
		}
	} else {
		return _make_error("Node does not support physics_material_override");
	}

	Dictionary extra;
	extra["friction"] = friction;
	extra["bounce"] = bounce;
	extra["absorbent"] = absorbent;
	extra["rough"] = rough;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_raycast_query(const Dictionary &p_args) const {
	return _make_error("Physics raycast queries can only execute at runtime, not in the editor. Use _tool_run_gdscript_expression to run PhysicsServer2D/3D ray queries while the game is playing, or use a RayCast2D/3D node configured in the editor.");
}

Dictionary YeetAIDock::_tool_shape_cast_query(const Dictionary &p_args) const {
	return _make_error("Physics shape cast queries can only execute at runtime, not in the editor. Use _tool_run_gdscript_expression to run PhysicsServer2D/3D shape queries while the game is playing, or use a ShapeCast2D/3D node configured in the editor.");
}
