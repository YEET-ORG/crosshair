/**************************************************************************/
/*  yeet_ai_project_context_index.cpp                                     */
/**************************************************************************/
/*  Project-wide context index for retrieval over Godot project files.     */
/**************************************************************************/

#include "yeet_ai_project_context_index.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/http_client.h"
#include "core/io/image.h"
#include "core/io/image_loader.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/math/math_funcs.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/string/translation.h"

#include "editor/settings/editor_settings.h"

YeetAIProjectContextIndex *YeetAIProjectContextIndex::singleton = nullptr;

static constexpr int MAX_TEXT_FILE_BYTES = 512 * 1024;
static constexpr int TEXT_CHUNK_TARGET_CHARS = 3500;
static constexpr int TEXT_CHUNK_OVERLAP_CHARS = 350;

static bool yeet_context_parse_url(const String &p_url, String &r_host, int &r_port, String &r_path, bool &r_use_tls) {
	int scheme_end = p_url.find("://");
	if (scheme_end < 0) {
		return false;
	}
	const String scheme = p_url.substr(0, scheme_end).to_lower().strip_edges();
	r_use_tls = (scheme == "https");
	const String rest = p_url.substr(scheme_end + 3);
	int slash = rest.find("/");
	String host_part;
	if (slash < 0) {
		host_part = rest;
		r_path = "/";
	} else {
		host_part = rest.substr(0, slash);
		r_path = rest.substr(slash);
	}
	int colon = host_part.rfind(":");
	if (colon >= 0) {
		r_host = host_part.substr(0, colon).strip_edges();
		r_port = host_part.substr(colon + 1).strip_edges().to_int();
	} else {
		r_host = host_part.strip_edges();
		r_port = r_use_tls ? 443 : 80;
	}
	return !r_host.is_empty();
}

void YeetAIProjectContextIndex::initialize() {
	if (singleton == nullptr) {
		singleton = memnew(YeetAIProjectContextIndex);
		singleton->_load_index_file();
	}
}

void YeetAIProjectContextIndex::finalize() {
	if (singleton != nullptr) {
		singleton->_save_index_file();
		memdelete(singleton);
		singleton = nullptr;
	}
}

YeetAIProjectContextIndex *YeetAIProjectContextIndex::get_singleton() {
	return singleton;
}

String YeetAIProjectContextIndex::_get_index_path() const {
	return "res://.crosshair/project_context_index.json";
}

Dictionary YeetAIProjectContextIndex::_load_index_file() const {
	const String path = _get_index_path();
	if (!FileAccess::exists(path)) {
		return Dictionary();
	}

	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	if (f.is_null()) {
		return Dictionary();
	}
	const String contents = f->get_as_text();
	f->close();

	JSON json;
	if (json.parse(contents) != OK) {
		return Dictionary();
	}

	Variant data = json.get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}

	MutexLock lock(const_cast<Mutex &>(_mutex));
	_index = Dictionary(data);
	return _index;
}

bool YeetAIProjectContextIndex::_save_index_file() const {
	const String path = _get_index_path();
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da.is_valid()) {
		da->make_dir_recursive(path.get_base_dir());
	}

	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_null()) {
		return false;
	}

	Dictionary snapshot;
	{
		MutexLock lock(const_cast<Mutex &>(_mutex));
		snapshot = _index;
	}
	f->store_string(JSON::stringify(snapshot, "\t", false, true));
	f->close();
	return true;
}

Dictionary YeetAIProjectContextIndex::rebuild_project_context(const Dictionary &p_options) {
	Array files = _find_context_files();
	Array chunks;
	Array skipped;
	Array errors;
	int text_files = 0;
	int binary_files = 0;
	int embedded_chunks = 0;
	int embedding_errors = 0;
	String embedding_error;
	const bool force = bool(p_options.get("force", false));
	const bool embed_enabled = p_options.has("embed") ? bool(p_options.get("embed", false)) : _get_editor_setting_bool("yeet_ai/rag/embeddings_enabled", true);
	const String embedding_provider = String(p_options.get("embedding_provider", _get_editor_setting_string("yeet_ai/rag/embedding_provider", "ollama"))).to_lower().strip_edges();
	const String embedding_model = String(p_options.get("embedding_model", _get_editor_setting_string("yeet_ai/rag/embedding_model", "nomic-embed-text"))).strip_edges();
	const String embedding_base_url = String(p_options.get("embedding_base_url", _get_editor_setting_string("yeet_ai/rag/embedding_base_url", "http://127.0.0.1:11434"))).strip_edges();
	const int max_embedded_chunks = CLAMP(int(p_options.get("max_embedded_chunks", _get_editor_setting_int("yeet_ai/rag/max_embedded_chunks", 1000))), 0, 100000);

	for (int i = 0; i < files.size(); i++) {
		const String path = files[i];
		if (_is_text_context_file(path)) {
			const int64_t size = _file_size(path);
			if (size > MAX_TEXT_FILE_BYTES) {
				skipped.push_back(path);
				continue;
			}

			Error read_error = OK;
			const String content = FileAccess::get_file_as_string(path, &read_error);
			if (read_error != OK) {
				errors.push_back(vformat("Failed to read %s (error %d)", path, read_error));
				continue;
			}

			Array text_chunks = _build_text_chunks(path, content);
			for (int j = 0; j < text_chunks.size(); j++) {
				chunks.push_back(text_chunks[j]);
			}
			text_files++;
		} else if (_is_binary_context_file(path)) {
			chunks.push_back(_build_binary_metadata_chunk(path));
			binary_files++;
		}
	}

	if (embed_enabled && !embedding_model.is_empty() && max_embedded_chunks > 0) {
		for (int i = 0; i < chunks.size() && embedded_chunks < max_embedded_chunks; i++) {
			Dictionary chunk = chunks[i];
			String error;
			const Array embedding = _embed_text(String(chunk.get("text", "")), embedding_provider, embedding_model, embedding_base_url, error);
			if (embedding.is_empty()) {
				embedding_errors++;
				if (embedding_error.is_empty()) {
					embedding_error = error;
				}
				if (embedded_chunks == 0 || embedding_errors >= 3) {
					break;
				}
				continue;
			}
			chunk["embedding"] = embedding;
			chunks[i] = chunk;
			embedded_chunks++;
		}
	}

	Dictionary new_index;
	new_index["schema"] = "crosshair.project_context_index.v1";
	new_index["retrieval"] = embedded_chunks > 0 ? "hybrid_exact_cosine" : "lexical";
	new_index["embedding_schema_ready"] = true;
	new_index["embedding_provider"] = embedding_provider;
	new_index["embedding_model"] = embedding_model;
	new_index["embedding_base_url"] = embedding_base_url;
	new_index["embedded_chunk_count"] = embedded_chunks;
	new_index["embedding_error_count"] = embedding_errors;
	if (!embedding_error.is_empty()) {
		new_index["embedding_error"] = embedding_error;
	}
	new_index["last_updated"] = Time::get_singleton()->get_unix_time_from_system();
	new_index["file_count"] = files.size();
	new_index["text_file_count"] = text_files;
	new_index["binary_file_count"] = binary_files;
	new_index["chunk_count"] = chunks.size();
	new_index["chunks"] = chunks;

	{
		MutexLock lock(_mutex);
		_index = new_index;
	}
	_save_index_file();

	Dictionary result;
	result["ok"] = true;
	result["force"] = force;
	result["retrieval"] = new_index["retrieval"];
	result["embedding_provider"] = embedding_provider;
	result["embedding_model"] = embedding_model;
	result["embedded_chunk_count"] = embedded_chunks;
	result["embedding_error_count"] = embedding_errors;
	if (!embedding_error.is_empty()) {
		result["embedding_error"] = embedding_error;
	}
	result["file_count"] = files.size();
	result["text_file_count"] = text_files;
	result["binary_file_count"] = binary_files;
	result["chunk_count"] = chunks.size();
	result["skipped_count"] = skipped.size();
	result["error_count"] = errors.size();
	result["skipped_paths"] = skipped;
	result["errors"] = errors;
	return result;
}

Dictionary YeetAIProjectContextIndex::search_project_context(const Dictionary &p_query) const {
	String query = String(p_query.get("query", "")).to_lower().strip_edges();
	if (query.is_empty()) {
		Dictionary result;
		result["ok"] = false;
		result["error"] = "query is required.";
		return result;
	}

	const int max_results = CLAMP(int(p_query.get("max_results", 12)), 1, 50);
	const int max_chars = CLAMP(int(p_query.get("max_chars_per_result", 1200)), 120, 6000);
	const String kind_filter = String(p_query.get("kind", "")).to_lower();
	const String mode = String(p_query.get("mode", "hybrid")).to_lower().strip_edges();
	const PackedStringArray terms = query.split(" ", false);

	Dictionary snapshot;
	{
		MutexLock lock(const_cast<Mutex &>(_mutex));
		snapshot = _index;
	}

	Array chunks = snapshot.get("chunks", Array());
	Array query_embedding;
	String embedding_warning;
	const bool wants_vector = mode != "lexical";
	if (wants_vector && int(snapshot.get("embedded_chunk_count", 0)) > 0) {
		String error;
		query_embedding = _embed_text(query, String(snapshot.get("embedding_provider", "ollama")), String(snapshot.get("embedding_model", "nomic-embed-text")), String(snapshot.get("embedding_base_url", "http://127.0.0.1:11434")), error);
		if (query_embedding.is_empty()) {
			embedding_warning = error.is_empty() ? "Query embedding failed; using lexical retrieval." : error;
		}
	}
	Array scored;
	for (int i = 0; i < chunks.size(); i++) {
		Dictionary chunk = chunks[i];
		if (!kind_filter.is_empty() && String(chunk.get("kind", "")).to_lower() != kind_filter) {
			continue;
		}
		const int lexical_score = _score_chunk(chunk, query, terms);
		double vector_score = 0.0;
		if (!query_embedding.is_empty()) {
			vector_score = _cosine_similarity(query_embedding, chunk.get("embedding", Array()));
		}
		double score = double(lexical_score);
		if (!query_embedding.is_empty()) {
			if (mode == "vector") {
				score = vector_score * 1000.0;
			} else {
				score = (vector_score * 1000.0) + double(lexical_score);
			}
		}
		if (score <= 0.0) {
			continue;
		}
		chunk["score"] = score;
		chunk["lexical_score"] = lexical_score;
		chunk["vector_score"] = vector_score;
		String text = String(chunk.get("text", ""));
		if (text.length() > max_chars) {
			chunk["text"] = text.substr(0, max_chars) + "...";
		}
		scored.push_back(chunk);
	}

	for (int i = 0; i < scored.size(); i++) {
		for (int j = i + 1; j < scored.size(); j++) {
			Dictionary a = scored[i];
			Dictionary b = scored[j];
			if (double(a.get("score", 0.0)) < double(b.get("score", 0.0))) {
				Variant tmp = scored[i];
				scored[i] = scored[j];
				scored[j] = tmp;
			}
		}
	}

	Array results;
	for (int i = 0; i < MIN(scored.size(), max_results); i++) {
		results.push_back(scored[i]);
	}

	Dictionary result;
	result["ok"] = true;
	result["query"] = query;
	result["count"] = results.size();
	result["mode"] = query_embedding.is_empty() ? "lexical" : mode;
	result["retrieval"] = snapshot.get("retrieval", "lexical");
	result["total_matches"] = scored.size();
	result["total_chunks"] = chunks.size();
	result["embedded_chunk_count"] = int(snapshot.get("embedded_chunk_count", 0));
	result["results"] = results;
	if (!embedding_warning.is_empty()) {
		result["warning"] = embedding_warning;
	}
	if (chunks.is_empty()) {
		result["warning"] = "Project context index is empty. Run index_project_context first.";
	}
	return result;
}

Dictionary YeetAIProjectContextIndex::get_project_context_index() const {
	MutexLock lock(const_cast<Mutex &>(_mutex));
	Dictionary result = _index;
	if (result.is_empty()) {
		result["schema"] = "crosshair.project_context_index.v1";
		result["chunk_count"] = 0;
	}
	return result;
}

Array YeetAIProjectContextIndex::_find_context_files() const {
	Array files;
	_scan_dir_recursive("res://", files);
	return files;
}

void YeetAIProjectContextIndex::_scan_dir_recursive(const String &p_dir, Array &r_files) const {
	Ref<DirAccess> dir = DirAccess::open(p_dir);
	if (dir.is_null()) {
		return;
	}

	dir->set_include_hidden(false);
	dir->set_include_navigational(false);
	if (dir->list_dir_begin() != OK) {
		return;
	}

	while (true) {
		const String name = dir->get_next();
		if (name.is_empty()) {
			break;
		}
		const String full = p_dir.path_join(name);
		if (_should_skip_path(full)) {
			continue;
		}
		if (dir->current_is_dir()) {
			_scan_dir_recursive(full, r_files);
		} else if (_is_text_context_file(full) || _is_binary_context_file(full)) {
			r_files.push_back(full);
		}
	}
	dir->list_dir_end();
}

bool YeetAIProjectContextIndex::_should_skip_path(const String &p_path) const {
	const String path = p_path.to_lower();
	return path.begins_with("res://.crosshair") || path.begins_with("res://.godot") || path.ends_with(".import") || path.ends_with(".uid") || path.contains("/__pycache__/");
}

bool YeetAIProjectContextIndex::_is_text_context_file(const String &p_path) const {
	const String ext = p_path.get_extension().to_lower();
	static const char *text_exts[] = {
		"gd", "cs", "tscn", "tres", "godot", "gdshader", "shader", "md", "txt", "json", "cfg", "ini", "csv", "tsv", "xml", "yaml", "yml", "toml", "rst", "glsl", "hlsl", "inc", "csproj", "sln", nullptr
	};
	for (int i = 0; text_exts[i] != nullptr; i++) {
		if (ext == text_exts[i]) {
			return true;
		}
	}
	return false;
}

bool YeetAIProjectContextIndex::_is_binary_context_file(const String &p_path) const {
	const String ext = p_path.get_extension().to_lower();
	static const char *binary_exts[] = {
		"png", "jpg", "jpeg", "webp", "svg", "bmp", "tga", "exr", "hdr", "wav", "ogg", "mp3", "flac", "glb", "gltf", "fbx", "obj", "dae", "blend", "res", "scn", "mesh", "material", "otf", "ttf", "woff", "woff2", nullptr
	};
	for (int i = 0; binary_exts[i] != nullptr; i++) {
		if (ext == binary_exts[i]) {
			return true;
		}
	}
	return false;
}

String YeetAIProjectContextIndex::_classify_file_kind(const String &p_path) const {
	const String ext = p_path.get_extension().to_lower();
	if (ext == "gd" || ext == "cs" || ext == "gdshader" || ext == "shader" || ext == "glsl" || ext == "hlsl" || ext == "inc") return "code";
	if (ext == "tscn" || ext == "scn") return "scene";
	if (ext == "tres" || ext == "res" || ext == "material" || ext == "mesh") return "resource";
	if (ext == "md" || ext == "txt" || ext == "rst") return "doc";
	if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "svg" || ext == "bmp" || ext == "tga" || ext == "exr" || ext == "hdr") return "image";
	if (ext == "wav" || ext == "ogg" || ext == "mp3" || ext == "flac") return "audio";
	if (ext == "glb" || ext == "gltf" || ext == "fbx" || ext == "obj" || ext == "dae" || ext == "blend") return "model";
	if (ext == "otf" || ext == "ttf" || ext == "woff" || ext == "woff2") return "font";
	return "file";
}

String YeetAIProjectContextIndex::_resource_type_for_path(const String &p_path) const {
	String type = ResourceLoader::get_resource_type(p_path);
	return type.is_empty() ? "unknown" : type;
}

int64_t YeetAIProjectContextIndex::_file_size(const String &p_path) const {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	if (f.is_null()) {
		return 0;
	}
	const int64_t size = f->get_length();
	f->close();
	return size;
}

Array YeetAIProjectContextIndex::_build_text_chunks(const String &p_path, const String &p_content) const {
	Array chunks;
	const PackedStringArray lines = p_content.split("\n");
	String current;
	int start_line = 1;
	int chunk_index = 0;

	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i];
		if (current.is_empty()) {
			start_line = i + 1;
		}
		current += line + "\n";
		if (current.length() >= TEXT_CHUNK_TARGET_CHARS) {
			Dictionary chunk;
			chunk["id"] = _make_chunk_id(p_path, chunk_index);
			chunk["path"] = p_path;
			chunk["kind"] = _classify_file_kind(p_path);
			chunk["resource_type"] = _resource_type_for_path(p_path);
			chunk["chunk_index"] = chunk_index;
			chunk["line_start"] = start_line;
			chunk["line_end"] = i + 1;
			chunk["size_bytes"] = _file_size(p_path);
			chunk["modified_time"] = FileAccess::get_modified_time(p_path);
			chunk["text"] = current;
			chunk["embedding"] = Array();
			chunks.push_back(chunk);
			chunk_index++;

			if (current.length() > TEXT_CHUNK_OVERLAP_CHARS) {
				current = current.substr(current.length() - TEXT_CHUNK_OVERLAP_CHARS);
			} else {
				current = "";
			}
			start_line = MAX(1, i + 1);
		}
	}

	if (!current.strip_edges().is_empty() || chunks.is_empty()) {
		Dictionary chunk;
		chunk["id"] = _make_chunk_id(p_path, chunk_index);
		chunk["path"] = p_path;
		chunk["kind"] = _classify_file_kind(p_path);
		chunk["resource_type"] = _resource_type_for_path(p_path);
		chunk["chunk_index"] = chunk_index;
		chunk["line_start"] = start_line;
		chunk["line_end"] = lines.size();
		chunk["size_bytes"] = _file_size(p_path);
		chunk["modified_time"] = FileAccess::get_modified_time(p_path);
		chunk["text"] = current;
		chunk["embedding"] = Array();
		chunks.push_back(chunk);
	}

	return chunks;
}

Dictionary YeetAIProjectContextIndex::_build_binary_metadata_chunk(const String &p_path) const {
	Dictionary chunk;
	chunk["id"] = _make_chunk_id(p_path, 0);
	chunk["path"] = p_path;
	chunk["kind"] = _classify_file_kind(p_path);
	chunk["resource_type"] = _resource_type_for_path(p_path);
	chunk["chunk_index"] = 0;
	chunk["size_bytes"] = _file_size(p_path);
	chunk["modified_time"] = FileAccess::get_modified_time(p_path);
	chunk["embedding"] = Array();

	String text = "Asset metadata\n";
	text += "Path: " + p_path + "\n";
	text += "Kind: " + String(chunk["kind"]) + "\n";
	text += "Extension: " + p_path.get_extension().to_lower() + "\n";
	text += "Resource type: " + String(chunk["resource_type"]) + "\n";
	text += "File name: " + p_path.get_file().get_basename().replace("_", " ").replace("-", " ") + "\n";

	if (String(chunk["kind"]) == "image") {
		Ref<Image> img;
		img.instantiate();
		if (ImageLoader::load_image(p_path, img) == OK && img.is_valid()) {
			chunk["width"] = img->get_width();
			chunk["height"] = img->get_height();
			chunk["has_alpha"] = img->detect_alpha();
			text += vformat("Image dimensions: %dx%d\n", img->get_width(), img->get_height());
			text += String("Has alpha: ") + (img->detect_alpha() ? "yes" : "no") + "\n";
		}
	}

	chunk["text"] = text;
	return chunk;
}

String YeetAIProjectContextIndex::_make_chunk_id(const String &p_path, int p_chunk_index) const {
	return p_path.md5_text() + ":" + String::num_int64(p_chunk_index);
}

int YeetAIProjectContextIndex::_score_chunk(const Dictionary &p_chunk, const String &p_query, const PackedStringArray &p_terms) const {
	const String text = String(p_chunk.get("text", "")).to_lower();
	const String path = String(p_chunk.get("path", "")).to_lower();
	const String kind = String(p_chunk.get("kind", "")).to_lower();
	int score = 0;
	if (text.contains(p_query)) score += 25;
	if (path.contains(p_query)) score += 15;
	if (kind == p_query) score += 10;
	for (int i = 0; i < p_terms.size(); i++) {
		const String term = String(p_terms[i]).strip_edges();
		if (term.length() < 2) {
			continue;
		}
		if (text.contains(term)) score += 4;
		if (path.contains(term)) score += 3;
		if (kind.contains(term)) score += 2;
	}
	return score;
}

Array YeetAIProjectContextIndex::_embed_text(const String &p_text, const String &p_provider, const String &p_model, const String &p_base_url, String &r_error) const {
	if (p_text.strip_edges().is_empty()) {
		r_error = "Cannot embed empty text.";
		return Array();
	}
	if (p_provider == "ollama") {
		return _embed_text_ollama(p_text, p_model, p_base_url, r_error);
	}
	r_error = "Unsupported embedding provider: " + p_provider;
	return Array();
}

Array YeetAIProjectContextIndex::_embed_text_ollama(const String &p_text, const String &p_model, const String &p_base_url, String &r_error) const {
	String host;
	int port = 0;
	String base_path;
	bool use_tls = false;
	if (!yeet_context_parse_url(p_base_url, host, port, base_path, use_tls)) {
		r_error = "Invalid Ollama embedding base URL: " + p_base_url;
		return Array();
	}
	String path = base_path;
	if (path.ends_with("/")) {
		path = path.substr(0, path.length() - 1);
	}
	path += "/api/embed";

	Dictionary body;
	body["model"] = p_model;
	body["input"] = p_text;
	const String json_body = JSON::stringify(body);

	HTTPClient *client = HTTPClient::create();
	client->set_blocking_mode(false);
	Ref<TLSOptions> tls;
	if (use_tls) {
		tls = TLSOptions::client();
	}

	Error err = client->connect_to_host(host, port, tls);
	if (err != OK) {
		memdelete(client);
		r_error = vformat("Failed to connect to Ollama embedding host %s:%d.", host, port);
		return Array();
	}

	int elapsed_ms = 0;
	const int connect_timeout_ms = 5000;
	while (elapsed_ms < connect_timeout_ms) {
		client->poll();
		const HTTPClient::Status status = client->get_status();
		if (status == HTTPClient::STATUS_CONNECTED) {
			break;
		}
		if (status == HTTPClient::STATUS_DISCONNECTED || status == HTTPClient::STATUS_CONNECTION_ERROR || status == HTTPClient::STATUS_CANT_CONNECT || status == HTTPClient::STATUS_CANT_RESOLVE || status == HTTPClient::STATUS_TLS_HANDSHAKE_ERROR) {
			memdelete(client);
			r_error = "Ollama embedding connection failed.";
			return Array();
		}
		OS::get_singleton()->delay_usec(5000);
		elapsed_ms += 5;
	}
	if (client->get_status() != HTTPClient::STATUS_CONNECTED) {
		memdelete(client);
		r_error = "Ollama embedding connection timed out.";
		return Array();
	}

	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	CharString body_utf8 = json_body.utf8();
	err = client->request(HTTPClient::METHOD_POST, path, headers, reinterpret_cast<const uint8_t *>(body_utf8.get_data()), body_utf8.length());
	if (err != OK) {
		memdelete(client);
		r_error = "Failed to send Ollama embedding request.";
		return Array();
	}

	elapsed_ms = 0;
	const int request_timeout_ms = 60000;
	while (elapsed_ms < request_timeout_ms) {
		client->poll();
		const HTTPClient::Status status = client->get_status();
		if (status == HTTPClient::STATUS_BODY || status == HTTPClient::STATUS_CONNECTED) {
			break;
		}
		if (status == HTTPClient::STATUS_DISCONNECTED || status == HTTPClient::STATUS_CONNECTION_ERROR) {
			memdelete(client);
			r_error = "Ollama embedding request disconnected.";
			return Array();
		}
		OS::get_singleton()->delay_usec(5000);
		elapsed_ms += 5;
	}

	String response_body;
	while (client->get_status() == HTTPClient::STATUS_BODY) {
		client->poll();
		PackedByteArray chunk = client->read_response_body_chunk();
		if (chunk.size() > 0) {
			response_body += String::utf8((const char *)chunk.ptr(), chunk.size());
		}
		OS::get_singleton()->delay_usec(2000);
		elapsed_ms += 2;
		if (elapsed_ms > request_timeout_ms) {
			client->close();
			memdelete(client);
			r_error = "Ollama embedding response timed out.";
			return Array();
		}
	}
	const int response_code = client->get_response_code();
	client->close();
	memdelete(client);

	if (response_code < 200 || response_code >= 300) {
		r_error = vformat("Ollama embedding API returned HTTP %d: %s", response_code, response_body.substr(0, 240));
		return Array();
	}

	JSON json;
	err = json.parse(response_body);
	if (err != OK) {
		r_error = "Failed to parse Ollama embedding response as JSON.";
		return Array();
	}
	Variant data = json.get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		r_error = "Ollama embedding response was not a JSON object.";
		return Array();
	}
	Dictionary dict = data;
	if (dict.has("embeddings")) {
		Array embeddings = dict.get("embeddings", Array());
		if (!embeddings.is_empty() && embeddings[0].get_type() == Variant::ARRAY) {
			return embeddings[0];
		}
	}
	if (dict.has("embedding") && dict["embedding"].get_type() == Variant::ARRAY) {
		return dict["embedding"];
	}
	r_error = "Ollama embedding response did not contain embeddings.";
	return Array();
}

double YeetAIProjectContextIndex::_cosine_similarity(const Array &p_a, const Array &p_b) const {
	const int n = MIN(p_a.size(), p_b.size());
	if (n <= 0) {
		return 0.0;
	}
	double dot = 0.0;
	double norm_a = 0.0;
	double norm_b = 0.0;
	for (int i = 0; i < n; i++) {
		const double a = double(p_a[i]);
		const double b = double(p_b[i]);
		dot += a * b;
		norm_a += a * a;
		norm_b += b * b;
	}
	if (norm_a <= 0.0 || norm_b <= 0.0) {
		return 0.0;
	}
	return dot / (Math::sqrt(norm_a) * Math::sqrt(norm_b));
}

String YeetAIProjectContextIndex::_get_editor_setting_string(const String &p_setting, const String &p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	return String(settings->get_setting(p_setting));
}

int YeetAIProjectContextIndex::_get_editor_setting_int(const String &p_setting, int p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	return int(settings->get_setting(p_setting));
}

bool YeetAIProjectContextIndex::_get_editor_setting_bool(const String &p_setting, bool p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	return bool(settings->get_setting(p_setting));
}
