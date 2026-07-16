/**************************************************************************/
/*  yeet_ai_project_context_index.h                                       */
/**************************************************************************/
/*  Project-wide context index for code, docs, scenes, resources, and      */
/*  asset metadata. This is the retrieval foundation for Yeet AI RAG.       */
/**************************************************************************/

#pragma once

#include "core/os/mutex.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class YeetAIProjectContextIndex {
public:
	static YeetAIProjectContextIndex *get_singleton();
	static void initialize();
	static void finalize();

	Dictionary rebuild_project_context(const Dictionary &p_options = Dictionary());
	Dictionary search_project_context(const Dictionary &p_query) const;
	Dictionary get_project_context_index() const;

private:
	static YeetAIProjectContextIndex *singleton;

	mutable Mutex _mutex;
	mutable Dictionary _index;

	String _get_index_path() const;
	Dictionary _load_index_file() const;
	bool _save_index_file() const;

	Array _find_context_files() const;
	void _scan_dir_recursive(const String &p_dir, Array &r_files) const;
	bool _should_skip_path(const String &p_path) const;
	bool _is_text_context_file(const String &p_path) const;
	bool _is_binary_context_file(const String &p_path) const;
	String _classify_file_kind(const String &p_path) const;
	String _resource_type_for_path(const String &p_path) const;
	int64_t _file_size(const String &p_path) const;

	Array _build_text_chunks(const String &p_path, const String &p_content) const;
	Dictionary _build_binary_metadata_chunk(const String &p_path) const;
	String _make_chunk_id(const String &p_path, int p_chunk_index) const;
	int _score_chunk(const Dictionary &p_chunk, const String &p_query, const PackedStringArray &p_terms) const;
	Array _embed_text(const String &p_text, const String &p_provider, const String &p_model, const String &p_base_url, String &r_error) const;
	Array _embed_text_ollama(const String &p_text, const String &p_model, const String &p_base_url, String &r_error) const;
	double _cosine_similarity(const Array &p_a, const Array &p_b) const;
	String _get_editor_setting_string(const String &p_setting, const String &p_default) const;
	int _get_editor_setting_int(const String &p_setting, int p_default) const;
	bool _get_editor_setting_bool(const String &p_setting, bool p_default) const;
};
