/**************************************************************************/
/*  yeet_ai_asset_index.cpp                                               */
/**************************************************************************/
/*  Core indexing service: filesystem scanning, sidecar IO, project cache,  */
/*  manifest lifecycle.                                                   */
/**************************************************************************/

#include "yeet_ai_asset_index.h"

#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/http_client.h"
#include "core/io/image_loader.h"
#include "core/io/json.h"
#include "core/io/tls_options.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/variant/variant.h"

#include "editor/settings/editor_settings.h"

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

		EditorSettings *settings = EditorSettings::get_singleton();
		if (settings != nullptr) {
			singleton->_deep_index_enabled = bool(settings->get_setting("yeet_ai/asset_index/deep_index_enabled", false));
		}
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

// ═══════════════════════════════════════════════════════════════════════════
// URL parsing helper
// ═══════════════════════════════════════════════════════════════════════════

YeetAIAssetIndex::ParsedURL YeetAIAssetIndex::_parse_url(const String &p_url) {
	ParsedURL result;
	String url = p_url;

	if (url.begins_with("https://")) {
		result.use_tls = true;
		result.port = 443;
		url = url.substr(8);
	} else if (url.begins_with("http://")) {
		result.use_tls = false;
		result.port = 80;
		url = url.substr(7);
	}

	int slash = url.find("/");
	if (slash >= 0) {
		result.path = url.substr(slash);
		url = url.substr(0, slash);
	} else {
		result.path = "/";
	}

	int colon = url.find(":");
	if (colon >= 0) {
		result.port = url.substr(colon + 1).to_int();
		url = url.substr(0, colon);
	}

	result.host = url;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Image encoding for vision
// ═══════════════════════════════════════════════════════════════════════════

String YeetAIAssetIndex::_encode_image_for_vision(const String &p_path) const {
	Ref<Image> img = ImageLoader::load_image(p_path);
	if (img.is_null()) {
		return String();
	}

	// Scale down if too large (max 1024px on longest dimension)
	static constexpr int MAX_DIM = 1024;
	int w = img->get_width();
	int h = img->get_height();
	if (w > MAX_DIM || h > MAX_DIM) {
		float scale = float(MAX_DIM) / MAX(w, h);
		img->resize(int(w * scale), int(h * scale), Image::INTERPOLATE_LANCZOS);
	}

	// Convert to PNG buffer
	PackedByteArray png_buffer = img->save_png_to_buffer();
	if (png_buffer.is_empty()) {
		return String();
	}

	// Base64 encode
	int len = 0;
	int buf_len = ((png_buffer.size() + 2) / 3) * 4 + 1;
	char *buf = (char *)memalloc(buf_len);
	CryptoCore::b64_encode((unsigned char *)buf, &len, png_buffer.ptr(), png_buffer.size());
	String encoded = String::utf8(buf, len);
	memfree(buf);
	return encoded;
}

// ═══════════════════════════════════════════════════════════════════════════
// Vision prompt builder
// ═══════════════════════════════════════════════════════════════════════════

String YeetAIAssetIndex::_build_vision_prompt(const Dictionary &p_deterministic_facts) const {
	String prompt =
		"Analyze this game asset image for Godot 4. Return ONLY a JSON object. No markdown, no explanation text.\n\n";

	prompt += "Image facts:\n";
	prompt += "- Dimensions: " + String::num_int64(int(p_deterministic_facts.get("width", 0))) + "x" + String::num_int64(int(p_deterministic_facts.get("height", 0))) + "\n";
	prompt += "- Has alpha: " + String(bool(p_deterministic_facts.get("has_alpha", false)) ? "yes" : "no") + "\n";

	Array candidates = p_deterministic_facts.get("grid_candidates", Array());
	if (!candidates.is_empty()) {
		prompt += "- Detected grid candidates:\n";
		for (int i = 0; i < MIN(candidates.size(), 3); i++) {
			Dictionary c = candidates[i];
			prompt += "  * " + String::num_int64(int(c.get("frame_width", 0))) + "x" + String::num_int64(int(c.get("frame_height", 0)));
			prompt += " (" + String::num_int64(int(c.get("columns", 0))) + "x" + String::num_int64(int(c.get("rows", 0))) + ")";
			prompt += " confidence=" + String::num(float(c.get("confidence", 0.0f))) + "\n";
		}
	} else {
		prompt += "- No grid detected\n";
	}

	Dictionary alpha = p_deterministic_facts.get("alpha_analysis", Dictionary());
	if (!alpha.is_empty()) {
		prompt += "- Alpha ratio: " + String::num(float(alpha.get("alpha_ratio", 0.0f))) + "\n";
		if (bool(alpha.get("has_transparent_gutters", false))) {
			prompt += "- Has transparent gutters (padding)\n";
		}
	}

	prompt +=
		"\nReturn JSON matching this schema:\n"
		"{\n"
		"  \"asset_type\": \"sprite_sheet\" | \"tileset\" | \"single_sprite\" | \"ui_image\" | \"background\" | \"portrait\" | \"vfx_sheet\" | \"texture\",\n"
		"  \"tags\": [\"tag1\", \"tag2\", ...],\n";

	prompt +=
		"  \"sprite_sheet\": {\n"
		"    \"grid\": {\"frame_width\": N, \"frame_height\": N, \"columns\": N, \"rows\": N},\n"
		"    \"animations\": {\"idle\": {\"frames\": [0,1,2,3], \"fps\": 8, \"loop\": true}, ...},\n"
		"    \"pivot\": [x, y],\n"
		"    \"collision\": {\"x\": N, \"y\": N, \"w\": N, \"h\": N}\n"
		"  },\n";

	prompt +=
		"  \"tileset\": {\n"
		"    \"tile_size\": N,\n"
		"    \"columns\": N,\n"
		"    \"rows\": N,\n"
		"    \"tiles\": [{\"id\": 0, \"tags\": [\"solid\"], \"collision_type\": \"full|half|slope\", \"terrain\": 0}]\n"
		"  }\n"
		"}\n";

	prompt +=
		"\nRules:\n"
		"- Only include sprite_sheet OR tileset, not both.\n"
		"- Frame/tile indices are 0-based, left-to-right, top-to-bottom.\n"
		"- Count carefully. Verify against the provided grid candidates.\n"
		"- For tilesets, describe each tile's semantic meaning (solid, platform, hazard, decorative, etc.).\n"
		"- If single sprite with no grid, omit sprite_sheet and tileset.\n"
		"- Animation names should be descriptive (idle, run, jump, attack, die, etc.).\n"
		"- fps values: idle=4-8, run=10-16, jump=8-12, attack=10-20.\n"
		"- pivot should be near bottom-center for characters (e.g., [frame_w/2, frame_h]).\n"
		"- collision rect should match the visible body, not the whole frame.\n";

	return prompt;
}

// ═══════════════════════════════════════════════════════════════════════════
// Vision model call
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIAssetIndex::_call_vision_model(const String &p_path, const Dictionary &p_deterministic_facts) {
	Dictionary result;
	result["ok"] = false;

	if (!_deep_index_enabled) {
		result["error"] = "Deep indexing is disabled. Enable yeet_ai/asset_index/deep_index_enabled in Editor Settings.";
		return result;
	}

	// 1. Encode image
	String base64_image = _encode_image_for_vision(p_path);
	if (base64_image.is_empty()) {
		result["error"] = "Failed to encode image for vision analysis.";
		return result;
	}

	// 2. Read API settings
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr) {
		result["error"] = "EditorSettings not available.";
		return result;
	}

	int provider = int(settings->get_setting("yeet_ai/chat/provider", 0));
	String url;
	String api_key;
	String model = String(settings->get_setting("yeet_ai/chat/model", "gpt-4o"));
	float temperature = float(settings->get_setting("yeet_ai/chat/temperature", 0.2));

	if (provider == 4) {
		// Azure OpenAI
		url = String(settings->get_setting("yeet_ai/chat/azure_endpoint", ""));
		String deployment = String(settings->get_setting("yeet_ai/chat/azure_deployment", ""));
		String api_version = String(settings->get_setting("yeet_ai/chat/azure_api_version", "2024-06-01"));
		api_key = String(settings->get_setting("yeet_ai/chat/azure_api_key", ""));
		if (!url.is_empty() && !deployment.is_empty()) {
			url = url.path_join("openai/deployments/" + deployment + "/chat/completions?api-version=" + api_version);
		}
	} else {
		url = String(settings->get_setting("yeet_ai/chat/completions_url", ""));
		api_key = String(settings->get_setting("yeet_ai/chat/api_key", ""));
	}

	if (url.is_empty()) {
		result["error"] = "Vision model URL not configured. Set yeet_ai/chat/completions_url or Azure endpoint in Editor Settings.";
		return result;
	}
	if (api_key.is_empty()) {
		result["error"] = "Vision API key not configured. Set yeet_ai/chat/api_key or Azure API key in Editor Settings.";
		return result;
	}

	// 3. Build prompt and request body
	String prompt = _build_vision_prompt(p_deterministic_facts);

	Dictionary body;
	body["model"] = model;
	body["temperature"] = temperature;
	body["max_tokens"] = 4096;

	Array messages;
	Dictionary user_msg;
	user_msg["role"] = "user";

	Array content;
	Dictionary text_part;
	text_part["type"] = "text";
	text_part["text"] = prompt;
	content.push_back(text_part);

	Dictionary image_part;
	image_part["type"] = "image_url";
	Dictionary image_url;
	image_url["url"] = "data:image/png;base64," + base64_image;
	image_part["image_url"] = image_url;
	content.push_back(image_part);

	user_msg["content"] = content;
	messages.push_back(user_msg);
	body["messages"] = messages;

	String json_body = JSON::stringify(body);

	// 4. Parse URL and connect
	ParsedURL parsed = _parse_url(url);

	HTTPClient client;
	Ref<TLSOptions> tls;
	if (parsed.use_tls) {
		tls = TLSOptions::client();
	}

	Error err = client.connect_to_host(parsed.host, parsed.port, tls);
	if (err != OK) {
		result["error"] = vformat("Failed to connect to vision API host: %s", parsed.host);
		return result;
	}

	// Poll until connected (or TLS handshake complete)
	int elapsed = 0;
	static constexpr int CONNECT_TIMEOUT_MS = 10000;
	while (elapsed < CONNECT_TIMEOUT_MS) {
		client.poll();
		HTTPClient::Status status = client.get_status();
		if (status == HTTPClient::STATUS_CONNECTED) {
			break;
		}
		if (status == HTTPClient::STATUS_DISCONNECTED || status == HTTPClient::STATUS_CONNECTION_ERROR) {
			result["error"] = "Vision API connection failed.";
			return result;
		}
		OS::get_singleton()->delay_usec(5000);
		elapsed += 5;
	}

	if (client.get_status() != HTTPClient::STATUS_CONNECTED) {
		result["error"] = "Vision API connection timed out.";
		return result;
	}

	// 5. Send request
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("Authorization: Bearer " + api_key);

	err = client.request(HTTPClient::METHOD_POST, parsed.path, headers, json_body);
	if (err != OK) {
		result["error"] = "Failed to send vision request.";
		return result;
	}

	// 6. Poll for response
	elapsed = 0;
	static constexpr int REQUEST_TIMEOUT_MS = 60000;
	while (elapsed < REQUEST_TIMEOUT_MS) {
		client.poll();
		HTTPClient::Status status = client.get_status();
		if (status == HTTPClient::STATUS_BODY) {
			break;
		}
		if (status == HTTPClient::STATUS_CONNECTED) {
			// Empty response
			break;
		}
		if (status == HTTPClient::STATUS_DISCONNECTED || status == HTTPClient::STATUS_CONNECTION_ERROR) {
			result["error"] = "Vision API disconnected during request.";
			return result;
		}
		OS::get_singleton()->delay_usec(5000);
		elapsed += 5;
	}

	// 7. Read response body
	String response_body;
	while (client.get_status() == HTTPClient::STATUS_BODY) {
		client.poll();
		PackedByteArray chunk = client.read_response_body_chunk();
		if (chunk.size() > 0) {
			response_body += String::utf8((const char *)chunk.ptr(), chunk.size());
		}
		OS::get_singleton()->delay_usec(2000);
		elapsed += 2;
		if (elapsed > REQUEST_TIMEOUT_MS) {
			result["error"] = "Vision API response read timed out.";
			client.close();
			return result;
		}
	}

	client.close();

	// Check HTTP status
	int response_code = client.get_response_code();
	if (response_code != 200) {
		result["error"] = vformat("Vision API returned HTTP %d", response_code);
		result["raw_response"] = response_body.substr(0, 500);
		return result;
	}

	// 8. Parse JSON response
	JSON json;
	err = json.parse(response_body);
	if (err != OK) {
		result["error"] = "Failed to parse vision API response as JSON.";
		result["raw_response"] = response_body.substr(0, 500);
		return result;
	}

	Dictionary response = json.get_data();
	if (response.has("error")) {
		Dictionary error = response["error"];
		result["error"] = error.get("message", "Vision API returned an error.");
		return result;
	}

	Array choices = response.get("choices", Array());
	if (choices.is_empty()) {
		result["error"] = "No choices in vision API response.";
		return result;
	}

	Dictionary choice = choices[0];
	Dictionary message = choice.get("message", Dictionary());
	String content_str = message.get("content", "");

	if (content_str.is_empty()) {
		result["error"] = "Empty content in vision response.";
		return result;
	}

	// 9. Extract JSON from content (might be wrapped in markdown)
	String json_str = content_str;
	if (json_str.contains("```json")) {
		int start = json_str.find("```json") + 7;
		int end = json_str.find("```", start);
		if (end > start) {
			json_str = json_str.substr(start, end - start).strip_edges();
		}
	} else if (json_str.contains("```")) {
		int start = json_str.find("```") + 3;
		int end = json_str.find("```", start);
		if (end > start) {
			json_str = json_str.substr(start, end - start).strip_edges();
		}
	}

	JSON json2;
	err = json2.parse(json_str);
	if (err != OK) {
		result["error"] = "Failed to parse vision content as JSON manifest.";
		result["raw_content"] = content_str.substr(0, 500);
		return result;
	}

	Dictionary ai_manifest = json2.get_data();

	// 10. Validate
	Array validation_errors = YeetAIAssetManifest::validate_manifest(ai_manifest);
	if (!validation_errors.is_empty()) {
		result["error"] = "Vision response failed validation: " + String(validation_errors[0]);
		result["validation_errors"] = validation_errors;
		return result;
	}

	result["ok"] = true;
	result["manifest"] = ai_manifest;
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
