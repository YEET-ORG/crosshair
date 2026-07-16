/**************************************************************************/
/*  yeet_ai_azure_profiles.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_azure_profiles.h"

#include "core/os/time.h"
#include "core/string/ustring.h"
#include "editor/settings/editor_settings.h"

namespace {

String _new_profile_id() {
	const int64_t t = Time::get_singleton()->get_ticks_usec();
	return "azure_" + itos(t);
}

String _es_string(const String &p_key, const String &p_default) {
	EditorSettings *s = EditorSettings::get_singleton();
	if (s == nullptr || !s->has_setting(p_key)) {
		return p_default;
	}
	bool valid = false;
	const Variant v = s->get(StringName(p_key), &valid);
	if (!valid) {
		return p_default;
	}
	return String(v);
}

int _es_int(const String &p_key, int p_default) {
	EditorSettings *s = EditorSettings::get_singleton();
	if (s == nullptr || !s->has_setting(p_key)) {
		return p_default;
	}
	bool valid = false;
	const Variant v = s->get(StringName(p_key), &valid);
	if (!valid) {
		return p_default;
	}
	return int(v);
}

Dictionary _normalize_profile(const Dictionary &p_in) {
	Dictionary p = p_in;
	if (String(p.get("id", "")).is_empty()) {
		p["id"] = _new_profile_id();
	}
	if (String(p.get("name", "")).strip_edges().is_empty()) {
		p["name"] = "Azure";
	}
	if (!p.has("endpoint")) {
		p["endpoint"] = "https://crosshair-resource.services.ai.azure.com";
	}
	if (!p.has("deployment")) {
		p["deployment"] = "gpt-5.5";
	}
	if (!p.has("api_key")) {
		p["api_key"] = "";
	}
	if (!p.has("api_mode")) {
		p["api_mode"] = 1;
	}
	if (!p.has("api_version")) {
		p["api_version"] = "2024-05-01-preview";
	}
	if (!p.has("model")) {
		const String dep = String(p.get("deployment", "gpt-5.5"));
		p["model"] = dep.is_empty() ? String("gpt-5.5") : dep;
	}
	return p;
}

void _mirror_legacy_from_profile(const Dictionary &p_profile) {
	EditorSettings *s = EditorSettings::get_singleton();
	if (s == nullptr) {
		return;
	}
	s->set_setting("yeet_ai/chat/azure_endpoint", String(p_profile.get("endpoint", "")));
	s->set_setting("yeet_ai/chat/azure_deployment", String(p_profile.get("deployment", "")));
	s->set_setting("yeet_ai/chat/azure_api_key", String(p_profile.get("api_key", "")));
	s->set_setting("yeet_ai/chat/azure_api_version", String(p_profile.get("api_version", "2024-05-01-preview")));
	s->set_setting("yeet_ai/chat/azure_api_mode", int(p_profile.get("api_mode", 1)));
	const String model = String(p_profile.get("model", p_profile.get("deployment", "gpt-5.5")));
	// Only overwrite global model when Azure is the active provider.
	if (_es_int("yeet_ai/chat/provider", 0) == 4) {
		s->set_setting("yeet_ai/chat/model", model);
	}
}

} // namespace

Dictionary yeet_ai_azure_make_profile(const String &p_name) {
	Dictionary p;
	p["id"] = _new_profile_id();
	p["name"] = p_name.strip_edges().is_empty() ? String("Azure") : p_name.strip_edges();
	p["endpoint"] = "https://YOUR-RESOURCE.services.ai.azure.com";
	p["deployment"] = "gpt-5.5";
	p["api_key"] = "";
	p["api_mode"] = 1;
	p["api_version"] = "2024-05-01-preview";
	p["model"] = "gpt-5.5";
	return p;
}

void yeet_ai_azure_profiles_ensure_migrated() {
	EditorSettings *s = EditorSettings::get_singleton();
	if (s == nullptr) {
		return;
	}

	if (s->has_setting("yeet_ai/chat/azure_profiles")) {
		const Variant v = s->get_setting("yeet_ai/chat/azure_profiles");
		if (v.get_type() == Variant::ARRAY && Array(v).size() > 0) {
			// Ensure active id is valid.
			Array profiles = v;
			String active = _es_string("yeet_ai/chat/azure_active_profile", "");
			bool found = false;
			for (int i = 0; i < profiles.size(); i++) {
				if (profiles[i].get_type() != Variant::DICTIONARY) {
					continue;
				}
				if (String(((Dictionary)profiles[i]).get("id", "")) == active) {
					found = true;
					break;
				}
			}
			if (!found && profiles.size() > 0 && profiles[0].get_type() == Variant::DICTIONARY) {
				const String first_id = String(((Dictionary)profiles[0]).get("id", ""));
				s->set_setting("yeet_ai/chat/azure_active_profile", first_id);
				_mirror_legacy_from_profile(profiles[0]);
			}
			return;
		}
	}

	// Migrate legacy single-config keys into one profile.
	Dictionary profile;
	profile["id"] = _new_profile_id();
	profile["name"] = "Azure (default)";
	profile["endpoint"] = _es_string("yeet_ai/chat/azure_endpoint", "https://crosshair-resource.services.ai.azure.com");
	profile["deployment"] = _es_string("yeet_ai/chat/azure_deployment", "gpt-5.5");
	profile["api_key"] = _es_string("yeet_ai/chat/azure_api_key", "");
	profile["api_mode"] = _es_int("yeet_ai/chat/azure_api_mode", 1);
	profile["api_version"] = _es_string("yeet_ai/chat/azure_api_version", "2024-05-01-preview");
	String model = _es_string("yeet_ai/chat/model", "");
	if (model.is_empty() || model == "berrymodel") {
		model = String(profile["deployment"]);
	}
	profile["model"] = model;

	Array profiles;
	profiles.append(_normalize_profile(profile));
	s->set_setting("yeet_ai/chat/azure_profiles", profiles);
	s->set_setting("yeet_ai/chat/azure_active_profile", String(profile["id"]));
	_mirror_legacy_from_profile(profile);
}

Array yeet_ai_azure_profiles_load() {
	yeet_ai_azure_profiles_ensure_migrated();
	EditorSettings *s = EditorSettings::get_singleton();
	if (s == nullptr || !s->has_setting("yeet_ai/chat/azure_profiles")) {
		return Array();
	}
	const Variant v = s->get_setting("yeet_ai/chat/azure_profiles");
	if (v.get_type() != Variant::ARRAY) {
		return Array();
	}
	Array raw = v;
	Array out;
	for (int i = 0; i < raw.size(); i++) {
		if (raw[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		out.append(_normalize_profile(raw[i]));
	}
	if (out.is_empty()) {
		Dictionary p = yeet_ai_azure_make_profile("Azure (default)");
		out.append(p);
		yeet_ai_azure_profiles_save(out, String(p["id"]));
	}
	return out;
}

String yeet_ai_azure_active_profile_id() {
	yeet_ai_azure_profiles_ensure_migrated();
	return _es_string("yeet_ai/chat/azure_active_profile", "");
}

Dictionary yeet_ai_azure_profile_by_id(const String &p_id) {
	const Array profiles = yeet_ai_azure_profiles_load();
	for (int i = 0; i < profiles.size(); i++) {
		if (profiles[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary p = profiles[i];
		if (String(p.get("id", "")) == p_id) {
			return p;
		}
	}
	return Dictionary();
}

Dictionary yeet_ai_azure_active_profile() {
	const Array profiles = yeet_ai_azure_profiles_load();
	const String active = yeet_ai_azure_active_profile_id();
	for (int i = 0; i < profiles.size(); i++) {
		if (profiles[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary p = profiles[i];
		if (String(p.get("id", "")) == active) {
			return p;
		}
	}
	if (profiles.size() > 0 && profiles[0].get_type() == Variant::DICTIONARY) {
		return profiles[0];
	}
	return yeet_ai_azure_make_profile();
}

void yeet_ai_azure_profiles_save(const Array &p_profiles, const String &p_active_id) {
	EditorSettings *s = EditorSettings::get_singleton();
	if (s == nullptr) {
		return;
	}
	Array normalized;
	for (int i = 0; i < p_profiles.size(); i++) {
		if (p_profiles[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		normalized.append(_normalize_profile(p_profiles[i]));
	}
	if (normalized.is_empty()) {
		Dictionary p = yeet_ai_azure_make_profile("Azure (default)");
		normalized.append(p);
	}

	String active = p_active_id;
	bool active_ok = false;
	for (int i = 0; i < normalized.size(); i++) {
		if (String(((Dictionary)normalized[i]).get("id", "")) == active) {
			active_ok = true;
			break;
		}
	}
	if (!active_ok) {
		active = String(((Dictionary)normalized[0]).get("id", ""));
	}

	s->set_setting("yeet_ai/chat/azure_profiles", normalized);
	s->set_setting("yeet_ai/chat/azure_active_profile", active);

	Dictionary active_profile;
	for (int i = 0; i < normalized.size(); i++) {
		if (String(((Dictionary)normalized[i]).get("id", "")) == active) {
			active_profile = normalized[i];
			break;
		}
	}
	if (!active_profile.is_empty()) {
		_mirror_legacy_from_profile(active_profile);
	}
	EditorSettings::save();
}
