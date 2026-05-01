/**************************************************************************/
/*  yeet_ai_asset_manifest.h                                              */
/**************************************************************************/
/*  Manifest schema, validation, merge rules, and confidence calculation   */
/*  for Crosshair asset indexing.                                         */
/**************************************************************************/

#pragma once

#include "core/io/image.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"

// ═══════════════════════════════════════════════════════════════════════════
// YeetAIAssetManifest — Schema helpers for asset metadata
// ═══════════════════════════════════════════════════════════════════════════

class YeetAIAssetManifest {
public:
	static constexpr const char *SCHEMA_VERSION = "crosshair.asset_manifest.v1";
	static constexpr int DETERMINISTIC_VERSION = 1;
	static constexpr int VISION_VERSION = 1;

	// Supported asset types
	enum AssetType {
		ASSET_UNKNOWN = 0,
		ASSET_SINGLE_SPRITE,
		ASSET_SPRITE_SHEET,
		ASSET_TILESET,
		ASSET_UI_IMAGE,
		ASSET_BACKGROUND,
		ASSET_PORTRAIT,
		ASSET_VFX_SHEET,
		ASSET_TEXTURE,
		ASSET_MAX
	};

	// Status states
	enum Status {
		STATUS_DRAFT = 0,
		STATUS_CONFIRMED,
		STATUS_MISSING,
		STATUS_ERROR
	};

	static String asset_type_to_string(AssetType p_type);
	static AssetType string_to_asset_type(const String &p_str);
	static String status_to_string(Status p_status);
	static Status string_to_status(const String &p_str);

	// ── Manifest Creation ──────────────────────────────────────────────────────
	static Dictionary create_empty();
	static Dictionary from_image_scan(const String &p_path, const Ref<Image> &p_image);

	// ── Deterministic Scan ─────────────────────────────────────────────────────
	static Dictionary scan_image_deterministic(const Ref<Image> &p_image);
	static Array detect_grid_candidates(const Ref<Image> &p_image);
	static bool has_uniform_grid(const Ref<Image> &p_image, int p_frame_w, int p_frame_h, float p_threshold = 0.15f);
	static Array detect_repeated_cells(const Ref<Image> &p_image, int p_cell_w, int p_cell_h);
	static Dictionary analyze_alpha_distribution(const Ref<Image> &p_image);
	static Dictionary extract_dominant_palette(const Ref<Image> &p_image, int p_max_colors = 16);
	static float compute_sheet_likelihood(const Ref<Image> &p_image, const Dictionary &p_grid);

	// ── Validation ─────────────────────────────────────────────────────────────
	static Array validate_manifest(const Dictionary &p_manifest);
	static bool validate_sprite_sheet(const Dictionary &p_manifest, Array &r_errors);
	static bool validate_tileset(const Dictionary &p_manifest, Array &r_errors);
	static bool validate_frame_rects(const Dictionary &p_manifest, Array &r_errors);
	static bool validate_tile_coords(const Dictionary &p_manifest, Array &r_errors);

	// ── Merge & Patch ──────────────────────────────────────────────────────────
	static Dictionary merge_ai_suggestions(const Dictionary &p_existing, const Dictionary &p_ai_suggestions);
	static Dictionary apply_patch(const Dictionary &p_manifest, const Dictionary &p_patch, bool p_confirm = false);
	static bool is_field_user_confirmed(const Dictionary &p_manifest, const String &p_field_path);
	static void mark_field_confirmed(Dictionary &r_manifest, const String &p_field_path);

	// ── Confidence ─────────────────────────────────────────────────────────────
	static float compute_confidence(const Dictionary &p_manifest);
	static float confidence_from_sources(const Array &p_sources);
	static float confidence_from_grid_quality(const Dictionary &p_grid);
	static float confidence_from_alpha_analysis(const Dictionary &p_alpha);

	// ── Hashing ────────────────────────────────────────────────────────────────
	static String compute_file_hash(const String &p_path);
	static String compute_image_hash(const Ref<Image> &p_image);

	// ── Utility ────────────────────────────────────────────────────────────────
	static String generate_asset_id(const String &p_path);
	static String get_sidecar_path(const String &p_asset_path);
	static bool is_sidecar_path(const String &p_path);
	static bool should_skip_file(const String &p_path);
	static bool is_image_file(const String &p_path);

private:
	static float _pixel_variance(const Ref<Image> &p_image, const Rect2i &p_region);
	static bool _is_transparent_frame(const Ref<Image> &p_image, const Rect2i &p_region);
};
