/**************************************************************************/
/*  yeet_ai_asset_manifest.cpp                                            */
/**************************************************************************/
/*  Manifest schema, validation, merge rules, and confidence calculation    */
/*  for Crosshair asset indexing.                                         */
/**************************************************************************/

#include "yeet_ai_asset_manifest.h"

#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image_loader.h"
#include "core/math/rect2i.h"
#include "core/os/time.h"
#include "core/string/translation.h"
#include "core/variant/variant.h"

// ═══════════════════════════════════════════════════════════════════════════
// Asset Type <-> String
// ═══════════════════════════════════════════════════════════════════════════

String YeetAIAssetManifest::asset_type_to_string(AssetType p_type) {
	switch (p_type) {
		case ASSET_SINGLE_SPRITE: return "single_sprite";
		case ASSET_SPRITE_SHEET: return "sprite_sheet";
		case ASSET_TILESET: return "tileset";
		case ASSET_UI_IMAGE: return "ui_image";
		case ASSET_BACKGROUND: return "background";
		case ASSET_PORTRAIT: return "portrait";
		case ASSET_VFX_SHEET: return "vfx_sheet";
		case ASSET_TEXTURE: return "texture";
		case ASSET_UNKNOWN:
		default: return "unknown";
	}
}

YeetAIAssetManifest::AssetType YeetAIAssetManifest::string_to_asset_type(const String &p_str) {
	static const HashMap<String, AssetType> map = []() {
		HashMap<String, AssetType> m;
		m["single_sprite"] = ASSET_SINGLE_SPRITE;
		m["sprite_sheet"] = ASSET_SPRITE_SHEET;
		m["tileset"] = ASSET_TILESET;
		m["ui_image"] = ASSET_UI_IMAGE;
		m["background"] = ASSET_BACKGROUND;
		m["portrait"] = ASSET_PORTRAIT;
		m["vfx_sheet"] = ASSET_VFX_SHEET;
		m["texture"] = ASSET_TEXTURE;
		return m;
	}();
	const AssetType *found = map.getptr(p_str);
	return found ? *found : ASSET_UNKNOWN;
}

String YeetAIAssetManifest::status_to_string(Status p_status) {
	switch (p_status) {
		case STATUS_CONFIRMED: return "confirmed";
		case STATUS_MISSING: return "missing";
		case STATUS_ERROR: return "error";
		case STATUS_DRAFT:
		default: return "draft";
	}
}

YeetAIAssetManifest::Status YeetAIAssetManifest::string_to_status(const String &p_str) {
	if (p_str == "confirmed") return STATUS_CONFIRMED;
	if (p_str == "missing") return STATUS_MISSING;
	if (p_str == "error") return STATUS_ERROR;
	return STATUS_DRAFT;
}

// ═══════════════════════════════════════════════════════════════════════════
// Empty manifest creation
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIAssetManifest::create_empty() {
	Dictionary m;
	m[String("schema")] = SCHEMA_VERSION;
	m[String("asset_id")] = "";
	m[String("path")] = "";
	m[String("hash")] = "";
	m[String("asset_type")] = "unknown";
	m[String("status")] = "draft";
	m[String("confidence")] = 0.0;
	m[String("tags")] = Array();
	m[String("image")] = Dictionary();
	Dictionary analysis;
	analysis[String("deterministic_version")] = DETERMINISTIC_VERSION;
	analysis[String("vision_version")] = 0;
	analysis[String("sources")] = Array();
	m[String("analysis")] = analysis;
	m[String("sprite_sheet")] = Variant(); // null
	m[String("tileset")] = Variant(); // null
	m[String("warnings")] = Array();
	m[String("confirmed_fields")] = Array();
	m[String("last_scanned")] = 0;
	return m;
}

// ═══════════════════════════════════════════════════════════════════════════
// Manifest from image scan
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIAssetManifest::from_image_scan(const String &p_path, const Ref<Image> &p_image) {
	Dictionary manifest = create_empty();
	manifest["path"] = p_path;
	manifest["asset_id"] = generate_asset_id(p_path);
	manifest["hash"] = compute_image_hash(p_image);
	manifest["last_scanned"] = Time::get_singleton()->get_unix_time_from_system();

	Dictionary image_info;
	image_info["width"] = p_image->get_width();
	image_info["height"] = p_image->get_height();
	image_info["has_alpha"] = p_image->detect_alpha();
	image_info["format"] = Image::get_format_name(p_image->get_format());
	manifest["image"] = image_info;

	Dictionary deterministic = scan_image_deterministic(p_image);
	Dictionary img_info = manifest["image"];
	img_info[String("alpha_bounds")] = deterministic.has("alpha_bounds") ? Dictionary(deterministic["alpha_bounds"]) : Dictionary();
	img_info[String("transparent_gutters")] = deterministic.has("transparent_gutters") ? bool(deterministic["transparent_gutters"]) : false;
	manifest[String("image")] = img_info;

	Array grid_candidates = deterministic.get("grid_candidates", Array());
	Array sources;
	sources.push_back("heuristic");

	float best_confidence = 0.0;
	String best_type = "unknown";
	Dictionary best_grid;

	// Analyze grid candidates and determine most likely type
	for (int i = 0; i < grid_candidates.size(); i++) {
		Dictionary candidate = grid_candidates[i];
		float conf = candidate.get("confidence", 0.0);
		if (conf > best_confidence) {
			best_confidence = conf;
			best_grid = candidate;
			int cell_w = candidate.get("frame_width", 0);
			int cell_h = candidate.get("frame_height", 0);
			int cols = candidate.get("columns", 0);
			int rows = candidate.get("rows", 0);

			if (cell_w > 0 && cell_h > 0 && cols > 0 && rows > 0) {
				// Determine type based on dimensions and patterns
				if (rows >= 1 && cols >= 4) {
					best_type = "sprite_sheet";
				} else if (rows >= 2 && cols >= 2) {
					// Could be tileset or sprite sheet
					if (cell_w == cell_h && cols >= 4) {
						best_type = "tileset";
					} else {
						best_type = "sprite_sheet";
					}
				} else if (cols == 1 && rows == 1) {
					best_type = "single_sprite";
				}
			}
		}
	}

	manifest["asset_type"] = best_type;
	manifest["confidence"] = compute_confidence(deterministic);

	if (best_type == "sprite_sheet" || best_type == "vfx_sheet") {
		Dictionary sprite_sheet;
		sprite_sheet[String("grid")] = best_grid;
		sprite_sheet[String("animations")] = Dictionary();
		Array pivot;
		pivot.push_back(best_grid.has("frame_width") ? int(best_grid["frame_width"]) / 2 : 0);
		pivot.push_back(best_grid.has("frame_height") ? int(best_grid["frame_height"]) : 0);
		sprite_sheet[String("pivot")] = pivot;
		sprite_sheet[String("collision")] = Variant();
		manifest[String("sprite_sheet")] = sprite_sheet;
	} else if (best_type == "tileset") {
		Dictionary tileset;
		tileset[String("tile_size")] = best_grid.has("frame_width") ? int(best_grid["frame_width"]) : 0;
		tileset[String("columns")] = best_grid.has("columns") ? int(best_grid["columns"]) : 0;
		tileset[String("rows")] = best_grid.has("rows") ? int(best_grid["rows"]) : 0;
		tileset[String("tiles")] = Array();
		manifest[String("tileset")] = tileset;
	}

	Dictionary det_analysis = deterministic.has("analysis") ? Dictionary(deterministic["analysis"]) : Dictionary();
	det_analysis[String("sources")] = sources;
	manifest[String("analysis")] = det_analysis;
	manifest[String("warnings")] = deterministic.has("warnings") ? Array(deterministic["warnings"]) : Array();

	return manifest;
}

// ═══════════════════════════════════════════════════════════════════════════
// Deterministic Image Scan
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIAssetManifest::scan_image_deterministic(const Ref<Image> &p_image) {
	Dictionary result;
	result["width"] = p_image->get_width();
	result["height"] = p_image->get_height();
	result["has_alpha"] = p_image->detect_alpha();

	// Alpha analysis
	Dictionary alpha = analyze_alpha_distribution(p_image);
	result["alpha_bounds"] = alpha.get("bounds", Dictionary());
	result["alpha_ratio"] = alpha.get("alpha_ratio", 0.0);
	result["transparent_gutters"] = alpha.get("has_transparent_gutters", false);

	// Grid candidates
	Array grid_candidates = detect_grid_candidates(p_image);
	result["grid_candidates"] = grid_candidates;

	// Dominant palette
	Dictionary palette = extract_dominant_palette(p_image);
	result["palette"] = palette;

	// Warnings
	Array warnings;
	if (grid_candidates.is_empty()) {
		warnings.push_back("No grid candidates detected — may be single sprite, UI image, or irregular layout.");
	}
	float alpha_ratio = alpha.has("alpha_ratio") ? float(alpha["alpha_ratio"]) : 1.0f;
	if (alpha_ratio < 0.01f && p_image->detect_alpha()) {
		warnings.push_back("Image has alpha channel but very few transparent pixels.");
	}
	result["warnings"] = warnings;

	// Analysis summary
	Dictionary analysis;
	analysis[String("deterministic_version")] = DETERMINISTIC_VERSION;
	analysis[String("vision_version")] = 0;
	result[String("analysis")] = analysis;

	return result;
}

Array YeetAIAssetManifest::detect_grid_candidates(const Ref<Image> &p_image) {
	Array candidates;
	int w = p_image->get_width();
	int h = p_image->get_height();

	if (w < 8 || h < 8) {
		return candidates;
	}

	// Common sprite/tile sizes to try
	static const int common_sizes[] = { 8, 16, 24, 32, 48, 64, 96, 128, 256 };
	int size_count = sizeof(common_sizes) / sizeof(common_sizes[0]);

	for (int s = 0; s < size_count; s++) {
		int cw = common_sizes[s];
		int ch = cw;
		if (cw > w || ch > h) continue;
		if (w % cw != 0 || h % ch != 0) continue;

		int cols = w / cw;
		int rows = h / ch;
		if (cols < 1 || rows < 1) continue;
		if (cols == 1 && rows == 1) continue; // Single sprite

		bool uniform = has_uniform_grid(p_image, cw, ch);
		if (!uniform) continue;

		// Check for transparent gutters
		Dictionary gutter_info;
		bool has_gutters = false;
		if (w > cw * cols) {
			has_gutters = true;
		}

		Dictionary candidate;
		candidate["frame_width"] = cw;
		candidate["frame_height"] = ch;
		candidate["columns"] = cols;
		candidate["rows"] = rows;
		candidate["total_frames"] = cols * rows;
		candidate["confidence"] = confidence_from_grid_quality(candidate);
		candidate["has_gutters"] = has_gutters;
		candidates.push_back(candidate);
	}

	// Also try non-square sizes for UI backgrounds etc.
	for (int i = 0; i < size_count; i++) {
		for (int j = 0; j < size_count; j++) {
			int cw = common_sizes[i];
			int ch = common_sizes[j];
			if (cw > w || ch > h) continue;
			if (w % cw != 0 || h % ch != 0) continue;

			int cols = w / cw;
			int rows = h / ch;
			if (cols < 2 || rows < 1) continue;

			// Check for duplicate cells (suggests sprite sheet vs tileset)
			Array repeated = detect_repeated_cells(p_image, cw, ch);
			if (repeated.is_empty() && cols * rows > 16) {
				// Many unique tiles = likely tileset
				continue;
			}

			bool uniform = has_uniform_grid(p_image, cw, ch);
			if (!uniform) continue;

			Dictionary candidate;
			candidate["frame_width"] = cw;
			candidate["frame_height"] = ch;
			candidate["columns"] = cols;
			candidate["rows"] = rows;
			candidate["total_frames"] = cols * rows;
			candidate["confidence"] = confidence_from_grid_quality(candidate) * 0.9f;
			candidate["has_gutters"] = false;
			candidates.push_back(candidate);
		}
	}

	// Sort by confidence descending (bubble sort since Array sort_custom requires callable)
	for (int i = 0; i < candidates.size(); i++) {
		for (int j = i + 1; j < candidates.size(); j++) {
			Dictionary da = candidates[i];
			Dictionary db = candidates[j];
			float ca = da.has("confidence") ? float(da["confidence"]) : 0.0f;
			float cb = db.has("confidence") ? float(db["confidence"]) : 0.0f;
			if (ca < cb) {
				Variant tmp = candidates[i];
				candidates[i] = candidates[j];
				candidates[j] = tmp;
			}
		}
	}

	return candidates;
}

bool YeetAIAssetManifest::has_uniform_grid(const Ref<Image> &p_image, int p_frame_w, int p_frame_h, float p_threshold) {
	int w = p_image->get_width();
	int h = p_image->get_height();

	int cols = w / p_frame_w;
	int rows = h / p_frame_h;
	if (cols < 1 || rows < 1) return false;

	// Sample variance across all cells
	int total_cells = cols * rows;
	if (total_cells < 2) return true;

	int sample_count = MIN(total_cells, 8);
	float total_variance = 0.0f;

	for (int i = 0; i < sample_count; i++) {
		int col = i % cols;
		int row = i / cols;
		Rect2i region(col * p_frame_w, row * p_frame_h, p_frame_w, p_frame_h);
		total_variance += _pixel_variance(p_image, region);
	}

	float avg_variance = total_variance / sample_count;
	// Low variance = uniform = likely grid
	return avg_variance < p_threshold;
}

Array YeetAIAssetManifest::detect_repeated_cells(const Ref<Image> &p_image, int p_cell_w, int p_cell_h) {
	Array repeated;
	int w = p_image->get_width();
	int h = p_image->get_height();

	int cols = w / p_cell_w;
	int rows = h / p_cell_h;
	if (cols < 2 || rows < 1) return repeated;

	// Hash each cell's top-left corner pixel
	HashMap<uint32_t, Array> cell_hashes;
	for (int row = 0; row < rows; row++) {
		for (int col = 0; col < cols; col++) {
			int x = col * p_cell_w;
			int y = row * p_cell_h;
			Color c = p_image->get_pixel(x, y);
			uint32_t hash = ((uint32_t)(c.r * 255.0f) << 24) | ((uint32_t)(c.g * 255.0f) << 16) | ((uint32_t)(c.b * 255.0f) << 8) | (uint32_t)(c.a * 255.0f);
			Array list;
			if (cell_hashes.has(hash)) {
				list = cell_hashes[hash];
			}
			Dictionary cell;
			cell[String("col")] = col;
			cell[String("row")] = row;
			list.push_back(cell);
			cell_hashes[hash] = list;
		}
	}

	// Report cells that share hashes (suggest duplicates)
	for (const KeyValue<uint32_t, Array> &kv : cell_hashes) {
		if (kv.value.size() > 1) {
			Dictionary group;
			group["count"] = kv.value.size();
			group["cells"] = kv.value;
			repeated.push_back(group);
		}
	}

	return repeated;
}

Dictionary YeetAIAssetManifest::analyze_alpha_distribution(const Ref<Image> &p_image) {
	Dictionary result;
	int w = p_image->get_width();
	int h = p_image->get_height();

	int total = w * h;
	int transparent = 0;
	int opaque = 0;
	int min_x = w, max_x = -1, min_y = h, max_y = -1;

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			Color c = p_image->get_pixel(x, y);
			if (c.a < 0.01f) {
				transparent++;
			} else {
				opaque++;
				if (x < min_x) min_x = x;
				if (x > max_x) max_x = x;
				if (y < min_y) min_y = y;
				if (y > max_y) max_y = y;
			}
		}
	}

	result["alpha_ratio"] = float(transparent) / float(total);
	result["opaque_ratio"] = float(opaque) / float(total);

	Dictionary bounds;
	if (max_x >= 0) {
		bounds["x"] = min_x;
		bounds["y"] = min_y;
		bounds["w"] = max_x - min_x + 1;
		bounds["h"] = max_y - min_y + 1;
	} else {
		bounds["x"] = 0;
		bounds["y"] = 0;
		bounds["w"] = w;
		bounds["h"] = h;
	}
	result["bounds"] = bounds;

	// Check for transparent gutters (padding around content)
	bool top_gutter = false, bottom_gutter = false, left_gutter = false, right_gutter = false;
	if (min_y > 0) top_gutter = true;
	if (max_y < h - 1) bottom_gutter = true;
	if (min_x > 0) left_gutter = true;
	if (max_x < w - 1) right_gutter = true;
	result["has_transparent_gutters"] = (top_gutter && bottom_gutter) || (left_gutter && right_gutter);
	result["gutter_top"] = top_gutter;
	result["gutter_bottom"] = bottom_gutter;
	result["gutter_left"] = left_gutter;
	result["gutter_right"] = right_gutter;

	return result;
}

Dictionary YeetAIAssetManifest::extract_dominant_palette(const Ref<Image> &p_image, int p_max_colors) {
	Dictionary result;
	HashMap<uint32_t, int> color_counts;
	int w = p_image->get_width();
	int h = p_image->get_height();

	// Sample every Nth pixel for performance
	int step = MAX(1, int(sqrtf(float(w * h) / 4096.0f)));

	for (int y = 0; y < h; y += step) {
		for (int x = 0; x < w; x += step) {
			Color c = p_image->get_pixel(x, y);
			// Quantize to 6 bits per channel for clustering
			uint8_t r = uint8_t(c.r * 255.0f) >> 2;
			uint8_t g = uint8_t(c.g * 255.0f) >> 2;
			uint8_t b = uint8_t(c.b * 255.0f) >> 2;
			uint8_t a = uint8_t(c.a * 255.0f) >> 2;
			uint32_t key = (uint32_t(r) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) | uint32_t(a);
			int count = 0;
			if (color_counts.has(key)) {
				count = color_counts[key];
			}
			color_counts[key] = count + 1;
		}
	}

	// Sort by count (manual bubble sort)
	Array palette;
	Vector<Pair<int, uint32_t>> sorted;
	for (const KeyValue<uint32_t, int> &kv : color_counts) {
		sorted.push_back(Pair<int, uint32_t>(kv.value, kv.key));
	}
	for (int i = 0; i < sorted.size(); i++) {
		for (int j = i + 1; j < sorted.size(); j++) {
			if (sorted[j].first > sorted[i].first) {
				Pair<int, uint32_t> tmp = sorted[i];
				sorted.write[i] = sorted[j];
				sorted.write[j] = tmp;
			}
		}
	}

	for (int i = 0; i < MIN(sorted.size(), p_max_colors); i++) {
		uint32_t key = sorted[i].second;
		Dictionary color;
		color[String("r")] = ((key >> 24) & 0x3F) << 2;
		color[String("g")] = ((key >> 16) & 0x3F) << 2;
		color[String("b")] = ((key >> 8) & 0x3F) << 2;
		color[String("a")] = (key & 0x3F) << 2;
		color[String("count")] = sorted[i].first;
		palette.push_back(color);
	}

	result["colors"] = palette;
	result["total_unique"] = color_counts.size();
	result["sampled_pixels"] = (w / step) * (h / step);
	return result;
}

float YeetAIAssetManifest::compute_sheet_likelihood(const Ref<Image> &p_image, const Dictionary &p_grid) {
	int frame_w = p_grid.get("frame_width", 0);
	int frame_h = p_grid.get("frame_height", 0);
	int cols = p_grid.get("columns", 0);
	int rows = p_grid.get("rows", 0);

	if (frame_w <= 0 || frame_h <= 0 || cols <= 0 || rows <= 0) {
		return 0.0f;
	}

	bool uniform = has_uniform_grid(p_image, frame_w, frame_h);
	if (!uniform) return 0.0f;

	int total_frames = cols * rows;
	if (total_frames < 2) return 0.0f;
	if (total_frames > 256) return 0.3f; // Too many frames = less likely sprite sheet

	// Sprite sheets typically have repeating patterns
	Array repeated = detect_repeated_cells(p_image, frame_w, frame_h);
	bool has_repeats = !repeated.is_empty();

	float score = 0.5f;
	if (has_repeats) score += 0.2f;
	if (cols >= 4) score += 0.1f;
	if (rows <= 8) score += 0.1f; // Reasonable number of rows
	if (frame_w == frame_h) score += 0.05f; // Square frames common for sprites

	return MIN(score, 1.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// Validation
// ═══════════════════════════════════════════════════════════════════════════

Array YeetAIAssetManifest::validate_manifest(const Dictionary &p_manifest) {
	Array errors;

	String schema = p_manifest.get("schema", "");
	if (schema != SCHEMA_VERSION) {
		errors.push_back(vformat("Unknown schema version: %s (expected %s)", schema, SCHEMA_VERSION));
	}

	String path = p_manifest.get("path", "");
	if (path.is_empty()) {
		errors.push_back("'path' is required.");
	}

	String asset_type = p_manifest.get("asset_type", "");
	if (asset_type == "sprite_sheet") {
		validate_sprite_sheet(p_manifest, errors);
	} else if (asset_type == "tileset") {
		validate_tileset(p_manifest, errors);
	}

	return errors;
}

bool YeetAIAssetManifest::validate_sprite_sheet(const Dictionary &p_manifest, Array &r_errors) {
	bool ok = true;
	Dictionary sprite_sheet = p_manifest.get("sprite_sheet", Dictionary());
	if (sprite_sheet.is_empty()) {
		r_errors.push_back("Sprite sheet manifest missing 'sprite_sheet' data.");
		return false;
	}

	Dictionary grid = sprite_sheet.get("grid", Dictionary());
	int frame_w = grid.get("frame_width", 0);
	int frame_h = grid.get("frame_height", 0);
	int cols = grid.get("columns", 0);
	int rows = grid.get("rows", 0);

	if (frame_w <= 0 || frame_h <= 0) {
		r_errors.push_back("Invalid frame dimensions in sprite sheet grid.");
		ok = false;
	}

	Dictionary image = p_manifest.get("image", Dictionary());
	int img_w = image.get("width", 0);
	int img_h = image.get("height", 0);

	if (cols * frame_w > img_w || rows * frame_h > img_h) {
		r_errors.push_back("Grid dimensions exceed image bounds.");
		ok = false;
	}

	// Validate animations
	Dictionary animations = sprite_sheet.get("animations", Dictionary());
	for (const Variant *key = animations.next(nullptr); key != nullptr; key = animations.next(key)) {
		Dictionary anim = animations[*key];
		Array frames = anim.get("frames", Array());
		int total = cols * rows;
		for (int i = 0; i < frames.size(); i++) {
			int frame_id = frames[i];
			if (frame_id < 0 || frame_id >= total) {
				r_errors.push_back(vformat("Animation '%s' references out-of-bounds frame %d (max %d)",
					String(*key), frame_id, total - 1));
				ok = false;
			}
		}
	}

	return ok;
}

bool YeetAIAssetManifest::validate_tileset(const Dictionary &p_manifest, Array &r_errors) {
	bool ok = true;
	Dictionary tileset = p_manifest.get("tileset", Dictionary());
	if (tileset.is_empty()) {
		r_errors.push_back("Tileset manifest missing 'tileset' data.");
		return false;
	}

	int tile_size = tileset.get("tile_size", 0);
	int cols = tileset.get("columns", 0);
	int rows = tileset.get("rows", 0);

	if (tile_size <= 0) {
		r_errors.push_back("Invalid tile_size in tileset.");
		ok = false;
	}

	Dictionary image = p_manifest.get("image", Dictionary());
	int img_w = image.get("width", 0);
	int img_h = image.get("height", 0);

	if (cols * tile_size > img_w || rows * tile_size > img_h) {
		r_errors.push_back("Tileset dimensions exceed image bounds.");
		ok = false;
	}

	return ok;
}

bool YeetAIAssetManifest::validate_frame_rects(const Dictionary &p_manifest, Array &r_errors) {
	bool ok = true;
	Dictionary sprite_sheet = p_manifest.get("sprite_sheet", Dictionary());
	if (sprite_sheet.is_empty()) return true;

	Dictionary image = p_manifest.get("image", Dictionary());
	int img_w = image.get("width", 0);
	int img_h = image.get("height", 0);

	if (sprite_sheet.has("frames")) {
		Array frames = sprite_sheet["frames"];
		for (int i = 0; i < frames.size(); i++) {
			Dictionary frame = frames[i];
			int x = frame.get("x", 0);
			int y = frame.get("y", 0);
			int fw = frame.get("w", 0);
			int fh = frame.get("h", 0);
			if (x + fw > img_w || y + fh > img_h) {
				r_errors.push_back(vformat("Frame %d rect (%d,%d,%d,%d) exceeds image bounds (%d,%d)",
					i, x, y, fw, fh, img_w, img_h));
				ok = false;
			}
		}
	}

	return ok;
}

bool YeetAIAssetManifest::validate_tile_coords(const Dictionary &p_manifest, Array &r_errors) {
	bool ok = true;
	Dictionary tileset = p_manifest.get("tileset", Dictionary());
	if (tileset.is_empty()) return true;

	int tile_size = tileset.get("tile_size", 0);
	int cols = tileset.get("columns", 0);
	int rows = tileset.get("rows", 0);
	int total = cols * rows;

	Array tiles = tileset.get("tiles", Array());
	for (int i = 0; i < tiles.size(); i++) {
		Dictionary tile = tiles[i];
		int id = tile.get("id", -1);
		if (id < 0 || id >= total) {
			r_errors.push_back(vformat("Tile id %d out of bounds (max %d)", id, total - 1));
			ok = false;
		}
	}

	return ok;
}

// ═══════════════════════════════════════════════════════════════════════════
// Merge & Patch
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIAssetManifest::merge_ai_suggestions(const Dictionary &p_existing, const Dictionary &p_ai_suggestions) {
	Dictionary result = p_existing.duplicate(true);

	Array confirmed_fields = result.get("confirmed_fields", Array());
	HashSet<String> confirmed_set;
	for (int i = 0; i < confirmed_fields.size(); i++) {
		confirmed_set.insert(String(confirmed_fields[i]));
	}

	// Merge tags if not confirmed
	if (!confirmed_set.has("tags") && p_ai_suggestions.has("tags")) {
		Array existing_tags = result.get("tags", Array());
		Array new_tags = p_ai_suggestions["tags"];
		HashSet<String> tag_set;
		for (int i = 0; i < existing_tags.size(); i++) tag_set.insert(String(existing_tags[i]));
		for (int i = 0; i < new_tags.size(); i++) tag_set.insert(String(new_tags[i]));
		Array merged;
		for (const String &tag : tag_set) merged.push_back(tag);
		result["tags"] = merged;
	}

	// Merge asset_type if not confirmed and confidence is higher
	if (!confirmed_set.has("asset_type") && p_ai_suggestions.has("asset_type")) {
		float existing_conf = result.get("confidence", 0.0);
		float ai_conf = p_ai_suggestions.get("confidence", 0.0);
		if (ai_conf > existing_conf || ai_conf > 0.7) {
			result["asset_type"] = p_ai_suggestions["asset_type"];
		}
	}

	// Merge sprite_sheet data if not confirmed
	if (!confirmed_set.has("sprite_sheet") && p_ai_suggestions.has("sprite_sheet")) {
		Dictionary ai_sheet = p_ai_suggestions["sprite_sheet"];
		Dictionary existing_sheet = result.get("sprite_sheet", Dictionary());
		if (!existing_sheet.is_empty() && ai_sheet.has("animations")) {
			// Merge animations, preferring AI names if existing is empty
			Dictionary existing_anim = existing_sheet.get("animations", Dictionary());
			Dictionary ai_anim = ai_sheet["animations"];
			for (const Variant *key = ai_anim.next(nullptr); key != nullptr; key = ai_anim.next(key)) {
				if (!existing_anim.has(*key)) {
					existing_anim[*key] = ai_anim[*key];
				}
			}
			existing_sheet["animations"] = existing_anim;
			result["sprite_sheet"] = existing_sheet;
		} else {
			result["sprite_sheet"] = ai_sheet;
		}
	}

	// Merge tileset data if not confirmed
	if (!confirmed_set.has("tileset") && p_ai_suggestions.has("tileset")) {
		Dictionary ai_tileset = p_ai_suggestions["tileset"];
		Dictionary existing_tileset = result.get("tileset", Dictionary());
		if (!existing_tileset.is_empty() && ai_tileset.has("tiles")) {
			Array existing_tiles = existing_tileset.get("tiles", Array());
			Array ai_tiles = ai_tileset["tiles"];
			// Merge tile metadata
			for (int i = 0; i < ai_tiles.size() && i < existing_tiles.size(); i++) {
				Dictionary ai_tile = ai_tiles[i];
				Dictionary ex_tile = existing_tiles[i];
				for (const Variant *key = ai_tile.next(nullptr); key != nullptr; key = ai_tile.next(key)) {
					if (!ex_tile.has(*key)) {
						ex_tile[*key] = ai_tile[*key];
					}
				}
				existing_tiles[i] = ex_tile;
			}
			existing_tileset["tiles"] = existing_tiles;
			result["tileset"] = existing_tileset;
		} else {
			result["tileset"] = ai_tileset;
		}
	}

	// Update sources
	Dictionary result_analysis = result.has("analysis") ? Dictionary(result["analysis"]) : Dictionary();
	Array sources = result_analysis.has("sources") ? Array(result_analysis["sources"]) : Array();
	bool has_vision = false;
	for (int i = 0; i < sources.size(); i++) {
		if (String(sources[i]) == "vision") has_vision = true;
	}
	if (!has_vision) {
		sources.push_back("vision");
		Dictionary analysis = result["analysis"];
		analysis["sources"] = sources;
		analysis["vision_version"] = VISION_VERSION;
		result["analysis"] = analysis;
	}

	result["confidence"] = compute_confidence(result);
	return result;
}

Dictionary YeetAIAssetManifest::apply_patch(const Dictionary &p_manifest, const Dictionary &p_patch, bool p_confirm) {
	Dictionary result = p_manifest.duplicate(true);

	Array allowed_top_level;
	allowed_top_level.push_back("asset_type");
	allowed_top_level.push_back("tags");
	allowed_top_level.push_back("sprite_sheet");
	allowed_top_level.push_back("tileset");
	allowed_top_level.push_back("warnings");
	allowed_top_level.push_back("status");

	Array confirmed_fields = result.get("confirmed_fields", Array());
	HashSet<String> confirmed_set;
	for (int i = 0; i < confirmed_fields.size(); i++) {
		confirmed_set.insert(String(confirmed_fields[i]));
	}

	for (int i = 0; i < allowed_top_level.size(); i++) {
		String key = allowed_top_level[i];
		if (p_patch.has(key)) {
			result[key] = p_patch[key];
			if (p_confirm) {
				confirmed_set.insert(key);
			}
		}
	}

	// Convert confirmed_set back to array
	Array new_confirmed;
	for (const String &field : confirmed_set) {
		new_confirmed.push_back(field);
	}
	result["confirmed_fields"] = new_confirmed;
	result["last_scanned"] = Time::get_singleton()->get_unix_time_from_system();
	result["confidence"] = compute_confidence(result);

	return result;
}

bool YeetAIAssetManifest::is_field_user_confirmed(const Dictionary &p_manifest, const String &p_field_path) {
	Array confirmed = p_manifest.get("confirmed_fields", Array());
	for (int i = 0; i < confirmed.size(); i++) {
		if (String(confirmed[i]) == p_field_path) return true;
	}
	return false;
}

void YeetAIAssetManifest::mark_field_confirmed(Dictionary &r_manifest, const String &p_field_path) {
	Array confirmed = r_manifest.get("confirmed_fields", Array());
	bool found = false;
	for (int i = 0; i < confirmed.size(); i++) {
		if (String(confirmed[i]) == p_field_path) {
			found = true;
			break;
		}
	}
	if (!found) {
		confirmed.push_back(p_field_path);
		r_manifest["confirmed_fields"] = confirmed;
	}
	if (r_manifest.get("status", "draft") != "confirmed") {
		r_manifest["status"] = "confirmed";
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// Confidence
// ═══════════════════════════════════════════════════════════════════════════

float YeetAIAssetManifest::compute_confidence(const Dictionary &p_manifest) {
	float base = 0.3f;

	String asset_type = p_manifest.get("asset_type", "unknown");
	if (asset_type == "unknown") {
		base = 0.1f;
	}

	// Grid quality bonus
	Dictionary sprite_sheet = p_manifest.get("sprite_sheet", Dictionary());
	if (!sprite_sheet.is_empty() && sprite_sheet.has("grid")) {
		base += confidence_from_grid_quality(sprite_sheet["grid"]);
	}

	Dictionary tileset = p_manifest.get("tileset", Dictionary());
	if (!tileset.is_empty()) {
		base += 0.2f;
	}

	// Source bonus
	Dictionary manifest_analysis = p_manifest.has("analysis") ? Dictionary(p_manifest["analysis"]) : Dictionary();
	Array sources = manifest_analysis.has("sources") ? Array(manifest_analysis["sources"]) : Array();
	base += confidence_from_sources(sources);

	// Status bonus
	String status = p_manifest.get("status", "draft");
	if (status == "confirmed") base += 0.15f;

	return MIN(base, 1.0f);
}

float YeetAIAssetManifest::confidence_from_sources(const Array &p_sources) {
	float score = 0.0f;
	for (int i = 0; i < p_sources.size(); i++) {
		String src = p_sources[i];
		if (src == "heuristic") score += 0.15f;
		if (src == "vision") score += 0.2f;
		if (src == "user_confirmed") score += 0.25f;
	}
	return MIN(score, 0.5f);
}

float YeetAIAssetManifest::confidence_from_grid_quality(const Dictionary &p_grid) {
	float score = 0.0f;
	int cols = p_grid.get("columns", 0);
	int rows = p_grid.get("rows", 0);
	int total = cols * rows;

	if (total >= 2 && total <= 64) score += 0.15f;
	if (cols >= 2 && rows >= 1) score += 0.05f;
	if (p_grid.has("has_gutters") && bool(p_grid["has_gutters"])) score += 0.05f;

	return score;
}

float YeetAIAssetManifest::confidence_from_alpha_analysis(const Dictionary &p_alpha) {
	float score = 0.0f;
	bool gutters = p_alpha.get("has_transparent_gutters", false);
	if (gutters) score += 0.1f;
	float ratio = p_alpha.get("alpha_ratio", 0.0);
	if (ratio > 0.01f && ratio < 0.5f) score += 0.05f;
	return score;
}

// ═══════════════════════════════════════════════════════════════════════════
// Hashing
// ═══════════════════════════════════════════════════════════════════════════

String YeetAIAssetManifest::compute_file_hash(const String &p_path) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) return String();

	CryptoCore::MD5Context ctx;
	ctx.start();

	uint8_t buf[4096];
	while (!f->eof_reached()) {
		uint64_t read = f->get_buffer(buf, 4096);
		if (read > 0) {
			ctx.update(buf, read);
		}
	}
	f->close();

	unsigned char hash[16];
	ctx.finish(hash);

	String hex;
	for (int i = 0; i < 16; i++) {
		hex += String::num_int64(hash[i], 16).pad_zeros(2);
	}
	return hex;
}

String YeetAIAssetManifest::compute_image_hash(const Ref<Image> &p_image) {
	if (p_image.is_null()) return String();

	// Use image dimensions + a few sampled pixels for fast hash
	String data = vformat("%dx%d:%d:", p_image->get_width(), p_image->get_height(), int(p_image->get_format()));
	int w = p_image->get_width();
	int h = p_image->get_height();

	// Sample corners and center
	static const Vector2i samples[] = { Vector2i(0, 0), Vector2i(w-1, 0), Vector2i(0, h-1), Vector2i(w-1, h-1), Vector2i(w/2, h/2) };
	for (int i = 0; i < 5; i++) {
		if (samples[i].x >= 0 && samples[i].x < w && samples[i].y >= 0 && samples[i].y < h) {
			Color c = p_image->get_pixel(samples[i].x, samples[i].y);
			data += vformat("%02x%02x%02x%02x", uint8_t(c.r*255), uint8_t(c.g*255), uint8_t(c.b*255), uint8_t(c.a*255));
		}
	}

	CryptoCore::MD5Context ctx;
	ctx.start();
	CharString utf8 = data.utf8();
	ctx.update((const unsigned char *)utf8.get_data(), utf8.size());

	unsigned char hash[16];
	ctx.finish(hash);

	String hex;
	for (int i = 0; i < 16; i++) {
		hex += String::num_int64(hash[i], 16).pad_zeros(2);
	}
	return hex;
}

// ═══════════════════════════════════════════════════════════════════════════
// Utility
// ═══════════════════════════════════════════════════════════════════════════

String YeetAIAssetManifest::generate_asset_id(const String &p_path) {
	String base = p_path.get_basename().replace("/", "_").replace("\\", "_").replace(".", "_");
	// Remove res:// prefix
	if (base.begins_with("res://")) base = base.substr(6);
	// Remove common prefixes
	if (base.begins_with("assets_")) base = base.substr(7);
	if (base.begins_with("textures_")) base = base.substr(9);
	if (base.begins_with("sprites_")) base = base.substr(8);
	return base;
}

String YeetAIAssetManifest::get_sidecar_path(const String &p_asset_path) {
	return p_asset_path + ".crosshair.json";
}

bool YeetAIAssetManifest::is_sidecar_path(const String &p_path) {
	return p_path.ends_with(".crosshair.json");
}

bool YeetAIAssetManifest::should_skip_file(const String &p_path) {
	return is_sidecar_path(p_path) || p_path.ends_with(".import") || p_path.begins_with("res://.crosshair/generated/");
}

bool YeetAIAssetManifest::is_image_file(const String &p_path) {
	String ext = p_path.get_extension().to_lower();
	return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "svg";
}

// ═══════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════════════════

float YeetAIAssetManifest::_pixel_variance(const Ref<Image> &p_image, const Rect2i &p_region) {
	int count = 0;
	float r_sum = 0, g_sum = 0, b_sum = 0;

	for (int y = p_region.position.y; y < p_region.position.y + p_region.size.y && y < p_image->get_height(); y++) {
		for (int x = p_region.position.x; x < p_region.position.x + p_region.size.x && x < p_image->get_width(); x++) {
			Color c = p_image->get_pixel(x, y);
			r_sum += c.r;
			g_sum += c.g;
			b_sum += c.b;
			count++;
		}
	}

	if (count == 0) return 0.0f;

	float r_mean = r_sum / count;
	float g_mean = g_sum / count;
	float b_mean = b_sum / count;

	float variance = 0.0f;
	for (int y = p_region.position.y; y < p_region.position.y + p_region.size.y && y < p_image->get_height(); y++) {
		for (int x = p_region.position.x; x < p_region.position.x + p_region.size.x && x < p_image->get_width(); x++) {
			Color c = p_image->get_pixel(x, y);
			float dr = c.r - r_mean;
			float dg = c.g - g_mean;
			float db = c.b - b_mean;
			variance += (dr * dr + dg * dg + db * db) / 3.0f;
		}
	}

	return variance / count;
}

bool YeetAIAssetManifest::_is_transparent_frame(const Ref<Image> &p_image, const Rect2i &p_region) {
	int total = p_region.size.x * p_region.size.y;
	int transparent = 0;
	for (int y = p_region.position.y; y < p_region.position.y + p_region.size.y && y < p_image->get_height(); y++) {
		for (int x = p_region.position.x; x < p_region.position.x + p_region.size.x && x < p_image->get_width(); x++) {
			if (p_image->get_pixel(x, y).a < 0.01f) transparent++;
		}
	}
	return float(transparent) / float(total) > 0.95f;
}
