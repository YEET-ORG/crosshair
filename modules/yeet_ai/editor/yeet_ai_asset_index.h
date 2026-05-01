/**************************************************************************/
/*  yeet_ai_asset_index.h                                                 */
/**************************************************************************/
/*  Core indexing service: filesystem scanning, sidecar IO, project cache,  */
/*  manifest lifecycle. Singleton-style accessor for YeetAI tools.        */
/**************************************************************************/

#pragma once

#include "yeet_ai_asset_manifest.h"

#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

class YeetAIAssetIndex {
public:
	static YeetAIAssetIndex *get_singleton();
	static void initialize();
	static void finalize();

	// ── Core Scanning ──────────────────────────────────────────────────────────
	Dictionary scan_project(bool p_force = false, bool p_deep = false);
	Dictionary scan_asset(const String &p_path, bool p_force = false, bool p_deep = false);
	Dictionary scan_assets_batch(const Array &p_paths, bool p_force = false, bool p_deep = false);

	// ── Query ──────────────────────────────────────────────────────────────────
	Dictionary search_assets(const Dictionary &p_query) const;
	Dictionary get_asset_manifest(const String &p_path_or_id) const;
	Dictionary get_asset_by_path(const String &p_path) const;
	Dictionary get_asset_by_id(const String &p_id) const;
	Array get_all_manifests() const;

	// ── Mutation ───────────────────────────────────────────────────────────────
	Dictionary update_manifest(const String &p_path_or_id, const Dictionary &p_patch, bool p_confirm = false);
	Dictionary confirm_manifest(const String &p_path_or_id);
	bool delete_manifest(const String &p_path_or_id);

	// ── Project Index ──────────────────────────────────────────────────────────
	Dictionary get_project_index() const;
	void rebuild_project_index();
	Dictionary get_index_issues() const;

	// ── Deep Index (AI Vision) ─────────────────────────────────────────────────
	bool is_deep_index_available() const;
	Dictionary run_deep_index(const String &p_path);
	void set_deep_index_enabled(bool p_enabled);

	// ── Lifecycle ──────────────────────────────────────────────────────────────
	void on_filesystem_changed();
	void on_resources_reimported(const Array &p_files);
	void debounced_scan();
	void flush_pending_scans();

	// ── Resource Generation ────────────────────────────────────────────────────
	Dictionary create_sprite_frames_from_manifest(const String &p_asset_id, const String &p_save_path = "");
	Dictionary create_tileset_from_manifest(const String &p_asset_id, const String &p_save_path = "");

private:
	static YeetAIAssetIndex *singleton;

	mutable Mutex _mutex;
	Dictionary _project_index; // asset_id -> manifest
	HashMap<String, String> _path_to_id; // path -> asset_id
	HashSet<String> _pending_scan_paths;
	bool _deep_index_enabled = false;
	bool _scan_in_progress = false;

	// Debounce timer state
	uint64_t _last_scan_request_time = 0;
	static constexpr uint64_t SCAN_DEBOUNCE_MS = 500;

	// Storage paths
	String _get_project_index_path() const;
	String _get_generated_dir() const;
	String _get_sidecar_path(const String &p_asset_path) const;

	// Internal I/O
	Dictionary _load_sidecar(const String &p_path) const;
	bool _save_sidecar(const String &p_path, const Dictionary &p_manifest) const;
	Dictionary _load_project_index_file() const;
	bool _save_project_index_file() const;

	// Internal scan
	Dictionary _scan_single_asset(const String &p_path, bool p_force, bool p_deep);
	Array _find_image_assets() const;
	bool _is_asset_unchanged(const String &p_path, const Dictionary &p_existing) const;
	void _update_path_index(const String &p_old_path, const String &p_new_path);
	void _mark_missing(const String &p_path);

	// Vision integration
	Dictionary _call_vision_model(const String &p_path, const Dictionary &p_deterministic_facts);
	String _build_vision_prompt(const Dictionary &p_deterministic_facts) const;
	String _encode_image_for_vision(const String &p_path) const;

	struct ParsedURL {
		String host;
		int port = 80;
		bool use_tls = false;
		String path = "/";
	};
	static ParsedURL _parse_url(const String &p_url);

	YeetAIAssetIndex() = default;
	~YeetAIAssetIndex() = default;
};
