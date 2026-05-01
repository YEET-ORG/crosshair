/**************************************************************************/
/*  yeet_ai_asset_tools.cpp                                               */
/**************************************************************************/
/*  YeetAI tool handlers for asset indexing operations.                    */
/**************************************************************************/

#include "yeet_ai_dock.h"
#include "yeet_ai_asset_index.h"

// ═══════════════════════════════════════════════════════════════════════════
// Tool: index_project_assets
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_index_project_assets(const Dictionary &p_args) const {
	bool force = _arg_bool(p_args, "force", false);
	bool deep = _arg_bool(p_args, "deep", false);

	YeetAIAssetIndex *index = YeetAIAssetIndex::get_singleton();
	if (index == nullptr) {
		YeetAIAssetIndex::initialize();
		index = YeetAIAssetIndex::get_singleton();
	}

	return index->scan_project(force, deep);
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: index_asset
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_index_asset(const Dictionary &p_args) const {
	String path = _arg_string(p_args, "path", "");
	if (path.is_empty()) {
		return _make_error("'path' is required to index an asset.");
	}

	bool force = _arg_bool(p_args, "force", false);
	bool deep = _arg_bool(p_args, "deep", false);

	YeetAIAssetIndex *index = YeetAIAssetIndex::get_singleton();
	if (index == nullptr) {
		YeetAIAssetIndex::initialize();
		index = YeetAIAssetIndex::get_singleton();
	}

	return index->scan_asset(path, force, deep);
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: search_assets
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_search_assets(const Dictionary &p_args) const {
	YeetAIAssetIndex *index = YeetAIAssetIndex::get_singleton();
	if (index == nullptr) {
		YeetAIAssetIndex::initialize();
		index = YeetAIAssetIndex::get_singleton();
	}

	return index->search_assets(p_args);
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: get_asset_manifest
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_get_asset_manifest(const Dictionary &p_args) const {
	String path_or_id = _arg_string(p_args, "path_or_id", "");
	if (path_or_id.is_empty()) {
		return _make_error("'path_or_id' is required to get an asset manifest.");
	}

	YeetAIAssetIndex *index = YeetAIAssetIndex::get_singleton();
	if (index == nullptr) {
		YeetAIAssetIndex::initialize();
		index = YeetAIAssetIndex::get_singleton();
	}

	return index->get_asset_manifest(path_or_id);
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: update_asset_manifest
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_update_asset_manifest(const Dictionary &p_args) const {
	String path_or_id = _arg_string(p_args, "path_or_id", "");
	if (path_or_id.is_empty()) {
		return _make_error("'path_or_id' is required to update an asset manifest.");
	}

	Dictionary patch = _arg_dict(p_args, "patch");
	if (patch.is_empty()) {
		return _make_error("'patch' is required to update an asset manifest.");
	}

	bool confirm = _arg_bool(p_args, "confirm", false);

	YeetAIAssetIndex *index = YeetAIAssetIndex::get_singleton();
	if (index == nullptr) {
		YeetAIAssetIndex::initialize();
		index = YeetAIAssetIndex::get_singleton();
	}

	return index->update_manifest(path_or_id, patch, confirm);
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: confirm_asset_manifest
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_confirm_asset_manifest(const Dictionary &p_args) const {
	String path_or_id = _arg_string(p_args, "path_or_id", "");
	if (path_or_id.is_empty()) {
		return _make_error("'path_or_id' is required to confirm an asset manifest.");
	}

	YeetAIAssetIndex *index = YeetAIAssetIndex::get_singleton();
	if (index == nullptr) {
		YeetAIAssetIndex::initialize();
		index = YeetAIAssetIndex::get_singleton();
	}

	return index->confirm_manifest(path_or_id);
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: list_asset_index_issues
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_list_asset_index_issues(const Dictionary &p_args) const {
	YeetAIAssetIndex *index = YeetAIAssetIndex::get_singleton();
	if (index == nullptr) {
		YeetAIAssetIndex::initialize();
		index = YeetAIAssetIndex::get_singleton();
	}

	return index->get_index_issues();
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: create_sprite_frames_from_manifest
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_create_sprite_frames_from_manifest(const Dictionary &p_args) const {
	String asset_id = _arg_string(p_args, "asset_id", "");
	if (asset_id.is_empty()) {
		return _make_error("'asset_id' is required to create SpriteFrames.");
	}

	String save_path = _arg_string(p_args, "save_path", "");

	YeetAIAssetIndex *index = YeetAIAssetIndex::get_singleton();
	if (index == nullptr) {
		YeetAIAssetIndex::initialize();
		index = YeetAIAssetIndex::get_singleton();
	}

	return index->create_sprite_frames_from_manifest(asset_id, save_path);
}

// ═══════════════════════════════════════════════════════════════════════════
// Tool: create_tileset_from_manifest
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_create_tileset_from_manifest(const Dictionary &p_args) const {
	String asset_id = _arg_string(p_args, "asset_id", "");
	if (asset_id.is_empty()) {
		return _make_error("'asset_id' is required to create TileSet.");
	}

	String save_path = _arg_string(p_args, "save_path", "");

	YeetAIAssetIndex *index = YeetAIAssetIndex::get_singleton();
	if (index == nullptr) {
		YeetAIAssetIndex::initialize();
		index = YeetAIAssetIndex::get_singleton();
	}

	return index->create_tileset_from_manifest(asset_id, save_path);
}
