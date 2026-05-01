/**************************************************************************/
/*  yeet_ai_asset_index.cpp                                               */
/**************************************************************************/
/*  Core indexing service: filesystem scanning, sidecar IO, project cache,  */
/*  manifest lifecycle.                                                   */
/**************************************************************************/

#include "yeet_ai_asset_index.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image_loader.h"
#include "core/io/json.h"
#include "core/os/time.h"
#include "core/variant/variant.h"

// Scene includes for resource generation
#include "scene/2d/animated_sprite_2d.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/sprite_frames.h"
#include "scene/resources/tile_set.h"
#include "scene/resources/tile_set_atlas_source.h"

YeetAIAssetIndex *YeetAIAssetIndex::singleton = nullptr;

void YeetAIAssetIndex::initialize() {
	if (singleton == nullptr) {
		singleton = memnew(YeetAIAssetIndex);
		singleton->_load_project_index_file();
	}
}

void YeetAIAssetIndex::finalize() {
	if (singleton != nullptr) {
		singleton->flush_pending_scans();
		memdelete(singleton);
		singleton = nullptr;
	}
}

YeetAIAssetIndex *YeetAIAssetIndex::get_singleton() {
	return singleton;
}

// ═══════════════════════════════════════════════════════════════════════════
// Storage paths
// ═══════════════════════════════════════════════════════════════════════════

String YeetAIAssetIndex::_get_project_index_path() const {
	return "res://.crosshair/asset_index.json";
}

String YeetAIAssetIndex::_get_generated_dir() const {
	return "res://.crosshair/generated/";
}

String YeetAIAssetIndex::_get_sidecar_path(const String &p_asset_path) const {
	return YeetAIAssetManifest::get_sidecar_path(p_asset_path);
}

// ═══════════════════════════════════════════════════════════════════════════
// Sidecar I/O
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIAssetIndex::_load_sidecar(const String &p_path) const {
	String sidecar = _get_sidecar_path(p_path);
	if (!FileAccess::exists(sidecar)) {
		return Dictionary();
	}

	Ref<FileAccess> f = FileAccess::open(sidecar, FileAccess::READ);
	if (f.is_null()) {
		return Dictionary();
	}
	String contents = f->get_as_text();
	f->close();

	JSON json;
	Error err = json.parse(contents);
	if (err == OK) {
		Variant data = json.get_data();
		if (data.get_type() == Variant::DICTIONARY) {
			return Dictionary(data);
		}
	}
	return Dictionary();
}

bool YeetAIAssetIndex::_save_sidecar(const String &p_path, const Dictionary &p_manifest) const {
	String sidecar = _get_sidecar_path(p_path);
	String dir = sidecar.get_base_dir();

	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da.is_valid()) {
		da->make_dir_recursive(dir);
	}

	Ref<FileAccess> f = FileAccess::open(sidecar, FileAccess::WRITE);
	if (f.is_null()) {
		return false;
	}
	f->store_string(JSON::stringify(p_manifest, "\t", false, true));
	f->close();
	return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Project Index I/O
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIAssetIndex::_load_project_index_file() const {
	String path = _get_project_index_path();
	if (!FileAccess::exists(path)) {
		return Dictionary();
	}

	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	if (f.is_null()) {
		return Dictionary();
	}
	String contents = f->get_as_text();
	f->close();

	JSON json;
	Error err = json.parse(contents);
	if (err == OK) {
		Variant data = json.get_data();
		if (data.get_type() == Variant::DICTIONARY) {
			Dictionary index = Dictionary(data);
			MutexLock lock(const_cast<Mutex &>(_mutex));
			_project_index = index.get("manifests", Dictionary());
			// Rebuild path->id map
			_path_to_id.clear();
			for (const Variant *key = _project_index.next(nullptr); key != nullptr; key = _project_index.next(key)) {
				Dictionary manifest = _project_index[*key];
				String asset_path = manifest.get("path", "");
				if (!asset_path.is_empty()) {
					_path_to_id[asset_path] = String(*key);
				}
			}
			return index;
		}
	}
	return Dictionary();
}

bool YeetAIAssetIndex::_save_project_index_file() const {
	String path = _get_project_index_path();
	String dir = path.get_base_dir();

	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da.is_valid()) {
		da->make_dir_recursive(dir);
	}

	Dictionary index;
	{
		MutexLock lock(const_cast<Mutex &>(_mutex));
		index["schema"] = "crosshair.asset_index.v1";
		index["manifests"] = _project_index;
		index["last_updated"] = Time::get_singleton()->get_unix_time_from_system();
		index["asset_count"] = _project_index.size();
	}

	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_null()) {
		return false;
	}
	f->store_string(JSON::stringify(index, "\t", false, true));
	f->close();
	return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Core Scanning
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIAssetIndex::scan_project(bool p_force, bool p_deep) {
	Array image_paths = _find_image_assets();
	return scan_assets_batch(image_paths, p_force, p_deep);
}

Dictionary YeetAIAssetIndex::scan_asset(const String &p_path, bool p_force, bool p_deep) {
	if (!YeetAIAssetManifest::is_image_file(p_path)) {
		Dictionary result;
		result["ok"] = false;
		result["error"] = "Not an image file.";
		return result;
	}
	return _scan_single_asset(p_path, p_force, p_deep);
}

Dictionary YeetAIAssetIndex::scan_assets_batch(const Array &p_paths, bool p_force, bool p_deep) {
	Dictionary result;
	Array scanned;
	Array skipped;
	Array errors;

	for (int i = 0; i < p_paths.size(); i++) {
		String path = p_paths[i];
		if (!YeetAIAssetManifest::is_image_file(path)) {
			continue;
		}
		Dictionary scan_result = _scan_single_asset(path, p_force, p_deep);
		if (scan_result.get("ok", false)) {
			if (scan_result.get("unchanged", false)) {
				skipped.push_back(path);
			} else {
				scanned.push_back(scan_result.get("manifest", Dictionary()));
			}
		} else {
			errors.push_back(scan_result.get("error", "Unknown error"));
		}
	}

	_save_project_index_file();

	result["ok"] = true;
	result["scanned"] = scanned.size();
	result["skipped"] = skipped.size();
	result["errors"] = errors.size();
	result["manifests"] = scanned;
	result["skipped_paths"] = skipped;
	result["error_messages"] = errors;
	return result;
}

Dictionary YeetAIAssetIndex::_scan_single_asset(const String &p_path, bool p_force, bool p_deep) {
	Dictionary result;

	// Check for existing manifest
	Dictionary existing = _load_sidecar(p_path);
	if (!p_force && !existing.is_empty()) {
		if (_is_asset_unchanged(p_path, existing)) {
			result["ok"] = true;
			result["unchanged"] = true;
			result["manifest"] = existing;
			return result;
		}
	}

	// Load image
	Ref<Image> img = ImageLoader::load_image(p_path);
	if (img.is_null()) {
		result["ok"] = false;
		result["error"] = vformat("Failed to load image: %s", p_path);
		return result;
	}

	// Run deterministic scan
	Dictionary manifest = YeetAIAssetManifest::from_image_scan(p_path, img);

	// If deep index requested and available
	if (p_deep) {
		Dictionary deep_result = _call_vision_model(p_path, manifest);
		if (deep_result.get("ok", false)) {
			Dictionary ai_suggestions = deep_result["manifest"];
			manifest = YeetAIAssetManifest::merge_ai_suggestions(manifest, ai_suggestions);
		} else {
			Array warnings = manifest.get("warnings", Array());
			warnings.push_back(vformat("Deep index failed: %s", deep_result.get("error", "Unknown")));
			manifest["warnings"] = warnings;
		}
	}

	// Write sidecar
	_save_sidecar(p_path, manifest);

	// Update project index
	{
		MutexLock lock(_mutex);
		String asset_id = manifest["asset_id"];
		String old_path = existing.get("path", "");
		if (!old_path.is_empty() && old_path != p_path) {
			_path_to_id.erase(old_path);
		}
		_project_index[asset_id] = manifest;
		_path_to_id[p_path] = asset_id;
	}

	result["ok"] = true;
	result["manifest"] = manifest;
	result["deep_indexed"] = p_deep;
	return result;
}

Array YeetAIAssetIndex::_find_image_assets() const {
	Array result;
	Ref<DirAccess> da = DirAccess::open("res://");
	if (da.is_null()) {
		return result;
	}

	Vector<String> dirs;
	dirs.push_back("res://");

	while (!dirs.is_empty()) {
		String current = dirs[0];
		dirs.remove_at(0);

		Ref<DirAccess> sub = DirAccess::open(current);
		if (sub.is_null()) continue;

		sub->list_dir_begin();
		String file = sub->get_next();
		while (!file.is_empty()) {
			if (file == "." || file == "..") {
				file = sub->get_next();
				continue;
			}
			String full = current.path_join(file);
			if (sub->current_is_dir()) {
				if (!full.begins_with("res://.crosshair")) {
					dirs.push_back(full);
				}
			} else {
				if (YeetAIAssetManifest::is_image_file(full) && !YeetAIAssetManifest::should_skip_file(full)) {
					result.push_back(full);
				}
			}
			file = sub->get_next();
		}
		sub->list_dir_end();
	}

	return result;
}

bool YeetAIAssetIndex::_is_asset_unchanged(const String &p_path, const Dictionary &p_existing) const {
	String stored_hash = p_existing.get("hash", "");
	if (stored_hash.is_empty()) return false;

	String current_hash = YeetAIAssetManifest::compute_file_hash(p_path);
	return stored_hash == current_hash;
}

// ═══════════════════════════════════════════════════════════════════════════
// Query
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIAssetIndex::search_assets(const Dictionary &p_query) const {
	MutexLock lock(const_cast<Mutex &>(_mutex));

	String text_query = p_query.get("query", "").to_lower();
	String type_filter = p_query.get("asset_type", "");
	Array tag_filter = p_query.get("tags", Array());
	String status_filter = p_query.get("status", "");
	float min_confidence = p_query.get("min_confidence", 0.0f);
	int max_results = CLAMP(int(p_query.get("max_results", 50)), 1, 500);

	Array results;

	for (const Variant *key = _project_index.next(nullptr); key != nullptr; key = _project_index.next(key)) {
		Dictionary manifest = _project_index[*key];
		bool match = true;

		// Type filter
		if (!type_filter.is_empty()) {
			if (manifest.get("asset_type", "") != type_filter) {
				match = false;
			}
		}

		// Status filter
		if (match && !status_filter.is_empty()) {
			if (manifest.get("status", "") != status_filter) {
				match = false;
			}
		}

		// Confidence filter
		if (match) {
			float confidence = manifest.get("confidence", 0.0f);
			if (confidence < min_confidence) {
				match = false;
			}
		}

		// Tag filter
		if (match && !tag_filter.is_empty()) {
			Array tags = manifest.get("tags", Array());
			bool has_any = false;
			for (int i = 0; i < tag_filter.size(); i++) {
				for (int j = 0; j < tags.size(); j++) {
					if (String(tag_filter[i]).to_lower() == String(tags[j]).to_lower()) {
						has_any = true;
						break;
					}
				}
				if (has_any) break;
			}
			if (!has_any) match = false;
		}

		// Text search (name, tags, path)
		if (match && !text_query.is_empty()) {
			bool text_match = false;
			String name = manifest.get("name", "").to_lower();
			String path = manifest.get("path", "").to_lower();
			if (name.contains(text_query) || path.contains(text_query)) {
				text_match = true;
			}
			if (!text_match) {
				Array tags = manifest.get("tags", Array());
				for (int i = 0; i < tags.size(); i++) {
					if (String(tags[i]).to_lower().contains(text_query)) {
						text_match = true;
						break;
					}
				}
			}
			match = text_match;
		}

		if (match) {
			results.push_back(manifest);
			if (results.size() >= max_results) break;
		}
	}

	Dictionary result;
	result["ok"] = true;
	result["count"] = results.size();
	result["total"] = _project_index.size();
	result["manifests"] = results;
	return result;
}

Dictionary YeetAIAssetIndex::get_asset_manifest(const String &p_path_or_id) const {
	// Try as ID first
	if (_project_index.has(p_path_or_id)) {
		return get_asset_by_id(p_path_or_id);
	}
	// Try as path
	return get_asset_by_path(p_path_or_id);
}

Dictionary YeetAIAssetIndex::get_asset_by_path(const String &p_path) const {
	MutexLock lock(const_cast<Mutex &>(_mutex));
	const String *id = _path_to_id.getptr(p_path);
	if (id != nullptr && _project_index.has(*id)) {
		Dictionary result;
		result["ok"] = true;
		result["found"] = true;
		result["manifest"] = _project_index[*id];
		return result;
	}

	// Fallback: try sidecar directly
	Dictionary sidecar = const_cast<YeetAIAssetIndex *>(this)->_load_sidecar(p_path);
	if (!sidecar.is_empty()) {
		Dictionary result;
		result["ok"] = true;
		result["found"] = true;
		result["manifest"] = sidecar;
		return result;
	}

	Dictionary result;
	result["ok"] = true;
	result["found"] = false;
	result["path"] = p_path;
	return result;
}

Dictionary YeetAIAssetIndex::get_asset_by_id(const String &p_id) const {
	MutexLock lock(const_cast<Mutex &>(_mutex));
	Dictionary result;
	if (_project_index.has(p_id)) {
		result["ok"] = true;
		result["found"] = true;
		result["manifest"] = _project_index[p_id];
	} else {
		result["ok"] = true;
		result["found"] = false;
		result["id"] = p_id;
	}
	return result;
}

Array YeetAIAssetIndex::get_all_manifests() const {
	MutexLock lock(const_cast<Mutex &>(_mutex));
	Array result;
	for (const Variant *key = _project_index.next(nullptr); key != nullptr; key = _project_index.next(key)) {
		result.push_back(_project_index[*key]);
	}
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Mutation
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIAssetIndex::update_manifest(const String &p_path_or_id, const Dictionary &p_patch, bool p_confirm) {
	MutexLock lock(_mutex);

	String id;
	if (_project_index.has(p_path_or_id)) {
		id = p_path_or_id;
	} else if (_path_to_id.has(p_path_or_id)) {
		id = _path_to_id[p_path_or_id];
	} else {
		Dictionary result;
		result["ok"] = false;
		result["error"] = vformat("Asset not found: %s", p_path_or_id);
		return result;
	}

	Dictionary manifest = _project_index[id];
	Dictionary updated = YeetAIAssetManifest::apply_patch(manifest, p_patch, p_confirm);

	String path = updated["path"];
	_project_index[id] = updated;
	_save_sidecar(path, updated);
	_save_project_index_file();

	Dictionary result;
	result["ok"] = true;
	result["updated"] = true;
	result["confirmed"] = p_confirm;
	result["manifest"] = updated;
	return result;
}

Dictionary YeetAIAssetIndex::confirm_manifest(const String &p_path_or_id) {
	MutexLock lock(_mutex);

	String id;
	if (_project_index.has(p_path_or_id)) {
		id = p_path_or_id;
	} else if (_path_to_id.has(p_path_or_id)) {
		id = _path_to_id[p_path_or_id];
	} else {
		Dictionary result;
		result["ok"] = false;
		result["error"] = vformat("Asset not found: %s", p_path_or_id);
		return result;
	}

	Dictionary manifest = _project_index[id];
	manifest["status"] = "confirmed";
	manifest["confidence"] = MIN(float(manifest.get("confidence", 0.0f)) + 0.15f, 1.0f);

	// Mark all key fields as confirmed
	YeetAIAssetManifest::mark_field_confirmed(manifest, "asset_type");
	YeetAIAssetManifest::mark_field_confirmed(manifest, "tags");
	if (manifest.has("sprite_sheet")) YeetAIAssetManifest::mark_field_confirmed(manifest, "sprite_sheet");
	if (manifest.has("tileset")) YeetAIAssetManifest::mark_field_confirmed(manifest, "tileset");

	String path = manifest["path"];
	_project_index[id] = manifest;
	_save_sidecar(path, manifest);
	_save_project_index_file();

	Dictionary result;
	result["ok"] = true;
	result["confirmed"] = true;
	result["manifest"] = manifest;
	return result;
}

bool YeetAIAssetIndex::delete_manifest(const String &p_path_or_id) {
	MutexLock lock(_mutex);

	String id;
	if (_project_index.has(p_path_or_id)) {
		id = p_path_or_id;
	} else if (_path_to_id.has(p_path_or_id)) {
		id = _path_to_id[p_path_or_id];
	} else {
		return false;
	}

	Dictionary manifest = _project_index[id];
	String path = manifest.get("path", "");

	_project_index.erase(id);
	_path_to_id.erase(path);

	// Delete sidecar
	String sidecar = _get_sidecar_path(path);
	if (FileAccess::exists(sidecar)) {
		Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
		if (da.is_valid()) {
			da->remove(sidecar);
		}
	}

	_save_project_index_file();
	return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Project Index
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIAssetIndex::get_project_index() const {
	MutexLock lock(const_cast<Mutex &>(_mutex));
	Dictionary result;
	result["schema"] = "crosshair.asset_index.v1";
	result["manifests"] = _project_index;
	result["asset_count"] = _project_index.size();
	result["last_updated"] = Time::get_singleton()->get_unix_time_from_system();
	return result;
}

void YeetAIAssetIndex::rebuild_project_index() {
	MutexLock lock(_mutex);
	_project_index.clear();
	_path_to_id.clear();

	// Scan all image assets
	Array paths = const_cast<YeetAIAssetIndex *>(this)->_find_image_assets();
	for (int i = 0; i < paths.size(); i++) {
		String path = paths[i];
		Dictionary sidecar = _load_sidecar(path);
		if (!sidecar.is_empty()) {
			String asset_id = sidecar.get("asset_id", YeetAIAssetManifest::generate_asset_id(path));
			sidecar["asset_id"] = asset_id;
			_project_index[asset_id] = sidecar;
			_path_to_id[path] = asset_id;
		}
	}

	_save_project_index_file();
}

Dictionary YeetAIAssetIndex::get_index_issues() const {
	MutexLock lock(const_cast<Mutex &>(_mutex));
	Array issues;
	int missing_count = 0;
	int draft_count = 0;
	int low_confidence = 0;

	for (const Variant *key = _project_index.next(nullptr); key != nullptr; key = _project_index.next(key)) {
		Dictionary manifest = _project_index[*key];
		String status = manifest.get("status", "draft");
		float confidence = manifest.get("confidence", 0.0f);
		String path = manifest.get("path", "");

		if (status == "missing") {
			missing_count++;
			Dictionary issue;
			issue["severity"] = "warning";
			issue["type"] = "missing_file";
			issue["asset_id"] = String(*key);
			issue["path"] = path;
			issue["message"] = vformat("Indexed asset file missing: %s", path);
			issues.push_back(issue);
		}

		if (status == "draft") {
			draft_count++;
		}

		if (confidence < 0.3f) {
			low_confidence++;
			Dictionary issue;
			issue["severity"] = "info";
			issue["type"] = "low_confidence";
			issue["asset_id"] = String(*key);
			issue["path"] = path;
			issue["message"] = vformat("Low confidence (%0.2f): %s", confidence, path);
			issues.push_back(issue);
		}
	}

	Dictionary result;
	result["ok"] = true;
	result["total"] = _project_index.size();
	result["missing"] = missing_count;
	result["draft"] = draft_count;
	result["low_confidence"] = low_confidence;
	result["issues"] = issues;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Deep Index (AI Vision)
// ═══════════════════════════════════════════════════════════════════════════

bool YeetAIAssetIndex::is_deep_index_available() const {
	return _deep_index_enabled;
}

void YeetAIAssetIndex::set_deep_index_enabled(bool p_enabled) {
	_deep_index_enabled = p_enabled;
}

Dictionary YeetAIAssetIndex::run_deep_index(const String &p_path) {
	return _scan_single_asset(p_path, true, true);
}

Dictionary YeetAIAssetIndex::_call_vision_model(const String &p_path, const Dictionary &p_deterministic_facts) {
	Dictionary result;
	result["ok"] = false;
	result["error"] = "Deep indexing (AI vision) is not yet implemented in v1. Configure a vision-capable model and re-enable.";
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════

void YeetAIAssetIndex::on_filesystem_changed() {
	_last_scan_request_time = Time::get_singleton()->get_ticks_msec();
}

void YeetAIAssetIndex::on_resources_reimported(const Array &p_files) {
	MutexLock lock(_mutex);
	for (int i = 0; i < p_files.size(); i++) {
		String path = p_files[i];
		if (YeetAIAssetManifest::is_image_file(path) && !YeetAIAssetManifest::should_skip_file(path)) {
			_pending_scan_paths.insert(path);
		}
	}
	_last_scan_request_time = Time::get_singleton()->get_ticks_msec();
}

void YeetAIAssetIndex::debounced_scan() {
	uint64_t now = Time::get_singleton()->get_ticks_msec();
	if (now - _last_scan_request_time < SCAN_DEBOUNCE_MS) {
		return;
	}
	flush_pending_scans();
}

void YeetAIAssetIndex::flush_pending_scans() {
	MutexLock lock(_mutex);
	if (_pending_scan_paths.is_empty()) {
		return;
	}

	Array paths;
	for (const String &path : _pending_scan_paths) {
		paths.push_back(path);
	}
	_pending_scan_paths.clear();

	// Release lock during scan
	lock.temp_unlock();
	scan_assets_batch(paths, false, false);
}

// ═══════════════════════════════════════════════════════════════════════════
// Resource Generation
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIAssetIndex::create_sprite_frames_from_manifest(const String &p_asset_id, const String &p_save_path) {
	MutexLock lock(_mutex);

	Dictionary result;
	if (!_project_index.has(p_asset_id)) {
		result["ok"] = false;
		result["error"] = vformat("Asset not found: %s", p_asset_id);
		return result;
	}

	Dictionary manifest = _project_index[p_asset_id];
	String asset_path = manifest.get("path", "");
	Dictionary sprite_sheet = manifest.get("sprite_sheet", Dictionary());
	if (sprite_sheet.is_empty()) {
		result["ok"] = false;
		result["error"] = "Asset is not a sprite sheet.";
		return result;
	}

	Dictionary grid = sprite_sheet.get("grid", Dictionary());
	int frame_w = grid.get("frame_width", 0);
	int frame_h = grid.get("frame_height", 0);
	int cols = grid.get("columns", 0);
	int rows = grid.get("rows", 0);

	if (frame_w <= 0 || frame_h <= 0 || cols <= 0 || rows <= 0) {
		result["ok"] = false;
		result["error"] = "Invalid grid dimensions in manifest.";
		return result;
	}

	// Load texture
	Ref<Texture2D> tex = ResourceLoader::load(asset_path);
	if (tex.is_null()) {
		result["ok"] = false;
		result["error"] = vformat("Failed to load texture: %s", asset_path);
		return result;
	}

	// Create SpriteFrames
	Ref<SpriteFrames> sprite_frames;
	sprite_frames.instantiate();

	Dictionary animations = sprite_sheet.get("animations", Dictionary());
	if (animations.is_empty()) {
		// Create default animation with all frames
		Array default_frames;
		for (int i = 0; i < cols * rows; i++) {
			default_frames.push_back(i);
		}
		Dictionary default_anim;
		default_anim["frames"] = default_frames;
		default_anim["fps"] = 8;
		default_anim["loop"] = true;
		animations["default"] = default_anim;
	}

	for (const Variant *key = animations.next(nullptr); key != nullptr; key = animations.next(key)) {
		String anim_name = *key;
		Dictionary anim = animations[*key];
		Array frames = anim.get("frames", Array());
		float fps = anim.get("fps", 8.0f);
		bool loop = anim.get("loop", true);

		if (frames.is_empty()) continue;

		// Add animation to SpriteFrames
		sprite_frames->add_animation(anim_name);
		sprite_frames->set_animation_speed(anim_name, fps);
		sprite_frames->set_animation_loop(anim_name, loop);

		for (int i = 0; i < frames.size(); i++) {
			int frame_id = frames[i];
			int col = frame_id % cols;
			int row = frame_id / cols;

			Ref<AtlasTexture> atlas_tex;
			atlas_tex.instantiate();
			atlas_tex->set_atlas(tex);
			atlas_tex->set_region(Rect2(col * frame_w, row * frame_h, frame_w, frame_h));

			sprite_frames->add_frame(anim_name, atlas_tex);
		}
	}

	// Save
	String save_path = p_save_path;
	if (save_path.is_empty()) {
		save_path = _get_generated_dir().path_join("sprite_frames/" + p_asset_id + ".tres");
	}

	String dir = save_path.get_base_dir();
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da.is_valid()) {
		da->make_dir_recursive(dir);
	}

	Error err = ResourceSaver::save(sprite_frames, save_path);
	if (err != OK) {
		result["ok"] = false;
		result["error"] = vformat("Failed to save SpriteFrames: %s", save_path);
		return result;
	}

	result["ok"] = true;
	result["save_path"] = save_path;
	result["animations"] = sprite_frames->get_animation_names();
	return result;
}

Dictionary YeetAIAssetIndex::create_tileset_from_manifest(const String &p_asset_id, const String &p_save_path) {
	MutexLock lock(_mutex);

	Dictionary result;
	if (!_project_index.has(p_asset_id)) {
		result["ok"] = false;
		result["error"] = vformat("Asset not found: %s", p_asset_id);
		return result;
	}

	Dictionary manifest = _project_index[p_asset_id];
	String asset_path = manifest.get("path", "");
	Dictionary tileset_data = manifest.get("tileset", Dictionary());
	if (tileset_data.is_empty()) {
		result["ok"] = false;
		result["error"] = "Asset is not a tileset.";
		return result;
	}

	int tile_size = tileset_data.get("tile_size", 0);
	int cols = tileset_data.get("columns", 0);
	int rows = tileset_data.get("rows", 0);

	if (tile_size <= 0 || cols <= 0 || rows <= 0) {
		result["ok"] = false;
		result["error"] = "Invalid tileset dimensions in manifest.";
		return result;
	}

	// Load texture
	Ref<Texture2D> tex = ResourceLoader::load(asset_path);
	if (tex.is_null()) {
		result["ok"] = false;
		result["error"] = vformat("Failed to load texture: %s", asset_path);
		return result;
	}

	// Create TileSet
	Ref<TileSet> tileset;
	tileset.instantiate();
	tileset->set_tile_size(Vector2i(tile_size, tile_size));

	// Add atlas source
	Ref<TileSetAtlasSource> source;
	source.instantiate();
	source->set_texture(tex);
	source->set_texture_region_size(Vector2i(tile_size, tile_size));

	int source_id = tileset->add_source(source);

	// Create tiles
	Array tiles = tileset_data.get("tiles", Array());
	for (int row = 0; row < rows; row++) {
		for (int col = 0; col < cols; col++) {
			Vector2i atlas_coords(col, row);
			if (!source->has_tile(atlas_coords)) {
				source->create_tile(atlas_coords);
			}
		}
	}

	// Apply tile metadata if present
	for (int i = 0; i < tiles.size(); i++) {
		Dictionary tile_info = tiles[i];
		int tile_id = tile_info.get("id", -1);
		if (tile_id < 0) continue;

		int col = tile_id % cols;
		int row = tile_id / cols;
		Vector2i atlas_coords(col, row);

		if (!source->has_tile(atlas_coords)) continue;

		// Apply terrain/semantic tags
		if (tile_info.has("collision_type")) {
			String collision = tile_info["collision_type"];
			if (collision == "solid" || collision == "full") {
				// Add full-tile collision polygon
				source->set_tile_animation_columns(atlas_coords, 0);
			}
		}
		if (tile_info.has("terrain_set")) {
			int terrain_set = tile_info["terrain_set"];
			source->set_tile_terrain_set(atlas_coords, terrain_set);
		}
		if (tile_info.has("terrain")) {
			int terrain = tile_info["terrain"];
			source->set_tile_terrain(atlas_coords, terrain);
		}
	}

	// Save
	String save_path = p_save_path;
	if (save_path.is_empty()) {
		save_path = _get_generated_dir().path_join("tilesets/" + p_asset_id + ".tres");
	}

	String dir = save_path.get_base_dir();
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da.is_valid()) {
		da->make_dir_recursive(dir);
	}

	Error err = ResourceSaver::save(tileset, save_path);
	if (err != OK) {
		result["ok"] = false;
		result["error"] = vformat("Failed to save TileSet: %s", save_path);
		return result;
	}

	result["ok"] = true;
	result["save_path"] = save_path;
	result["tile_count"] = cols * rows;
	result["source_id"] = source_id;
	return result;
}
