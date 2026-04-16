/**************************************************************************/
/*  yeet_ai_editor_plugin.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_editor_plugin.h"

#include "yeet_ai_dock.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/property_info.h"
#include "editor/docks/editor_dock.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_interface.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"

#include "scene/resources/theme.h"

void YeetAIEditorPlugin::_register_crosshair_editor_setting_hints() {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr) {
		return;
	}

	// Makes Crosshair keys readable in Editor → Editor Settings (same backing store as the dock panel).
	settings->add_property_hint(PropertyInfo(Variant::BOOL, "yeet_ai/enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::INT, "yeet_ai/chat/provider", PROPERTY_HINT_ENUM, "Berry Model:0,Gemini:1,OpenRouter:2,Yeet Models:3", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::STRING, "yeet_ai/chat/completions_url", PROPERTY_HINT_PLACEHOLDER_TEXT, "https://host/v1/chat/completions", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::STRING, "yeet_ai/chat/model", PROPERTY_HINT_PLACEHOLDER_TEXT, "berrymodel", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::STRING, "yeet_ai/chat/api_key", PROPERTY_HINT_PASSWORD, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::STRING, "yeet_ai/chat/gemini_api_key", PROPERTY_HINT_PASSWORD, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::STRING, "yeet_ai/chat/openrouter_api_key", PROPERTY_HINT_PASSWORD, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::STRING, "yeet_ai/chat/yeet_chat_url", PROPERTY_HINT_PLACEHOLDER_TEXT, "https://gpt.yeetlabs.fun/v1/chat/completions", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::STRING, "yeet_ai/chat/yeet_tags_url", PROPERTY_HINT_PLACEHOLDER_TEXT, "https://gpt.yeetlabs.fun/api/tags", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::STRING, "yeet_ai/chat/yeet_api_key", PROPERTY_HINT_PASSWORD, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::INT, "yeet_ai/chat/max_tokens", PROPERTY_HINT_RANGE, "256,262144,1", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::INT, "yeet_ai/chat/max_tool_round_trips", PROPERTY_HINT_RANGE, "1,500,1", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::BOOL, "yeet_ai/chat/vision_enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::INT, "yeet_ai/chat/max_base64_chars", PROPERTY_HINT_RANGE, "10000,10000000,1000", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::INT, "yeet_ai/chat/game_screenshot_timeout_ms", PROPERTY_HINT_RANGE, "1000,120000,100", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::FLOAT, "yeet_ai/chat/temperature", PROPERTY_HINT_RANGE, "-1,2,0.01,or_less,or_greater", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::INT, "yeet_ai/chat/vision_default_max_dimension", PROPERTY_HINT_RANGE, "64,4096,1", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::BOOL, "yeet_ai/tools/allow_project_settings_write", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::BOOL, "yeet_ai/tools/allow_editor_settings_write", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::BOOL, "yeet_ai/tools/extended_read_extensions", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::BOOL, "yeet_ai/tools/allow_binary_metadata_read", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::BOOL, "yeet_ai/tools/allow_resource_inspector_write", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::INT, "yeet_ai/tools/inspector_max_properties", PROPERTY_HINT_RANGE, "8,2000,1", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::BOOL, "yeet_ai/tools/allow_reimport", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::BOOL, "yeet_ai/tools/allow_resource_save", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT));
	settings->add_property_hint(PropertyInfo(Variant::BOOL, "yeet_ai/tools/allow_replace_in_files", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT));
}

void YeetAIEditorPlugin::_bind_methods() {
}

YeetAIEditorPlugin::YeetAIEditorPlugin() {
	dock = memnew(YeetAIDock);
	editor_dock = memnew(EditorDock);
	editor_dock->set_name(TTR("Crosshair AI"));
	editor_dock->set_title(TTR("Crosshair AI"));
	editor_dock->set_layout_key("CrosshairAI");
	editor_dock->set_icon_name("Search");
	editor_dock->set_default_slot(EditorDock::DOCK_SLOT_RIGHT_UL);
	editor_dock->add_child(dock);
}

YeetAIEditorPlugin::~YeetAIEditorPlugin() {
}

void YeetAIEditorPlugin::_ensure_editor_settings() {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr) {
		return;
	}

	if (!settings->has_setting("yeet_ai/enabled")) {
		settings->set_initial_value("yeet_ai/enabled", true, true);
		settings->set_setting("yeet_ai/enabled", true);
	}
	if (!settings->has_setting("yeet_ai/chat/provider")) {
		settings->set_initial_value("yeet_ai/chat/provider", 0, true);
		settings->set_setting("yeet_ai/chat/provider", 0);
	}
	if (!settings->has_setting("yeet_ai/chat/gemini_api_key")) {
		settings->set_initial_value("yeet_ai/chat/gemini_api_key", "", true);
		settings->set_setting("yeet_ai/chat/gemini_api_key", "");
	}
	if (!settings->has_setting("yeet_ai/chat/openrouter_api_key")) {
		settings->set_initial_value("yeet_ai/chat/openrouter_api_key", "", true);
		settings->set_setting("yeet_ai/chat/openrouter_api_key", "");
	}
	if (!settings->has_setting("yeet_ai/chat/yeet_chat_url")) {
		settings->set_initial_value("yeet_ai/chat/yeet_chat_url", "https://gpt.yeetlabs.fun/v1/chat/completions", true);
		settings->set_setting("yeet_ai/chat/yeet_chat_url", "https://gpt.yeetlabs.fun/v1/chat/completions");
	}
	if (!settings->has_setting("yeet_ai/chat/yeet_tags_url")) {
		settings->set_initial_value("yeet_ai/chat/yeet_tags_url", "https://gpt.yeetlabs.fun/api/tags", true);
		settings->set_setting("yeet_ai/chat/yeet_tags_url", "https://gpt.yeetlabs.fun/api/tags");
	}
	if (!settings->has_setting("yeet_ai/chat/yeet_api_key")) {
		settings->set_initial_value("yeet_ai/chat/yeet_api_key", "", true);
		settings->set_setting("yeet_ai/chat/yeet_api_key", "");
	}
	if (!settings->has_setting("yeet_ai/chat/completions_url")) {
		settings->set_initial_value("yeet_ai/chat/completions_url", "https://llm.adityaberry.me/v1/chat/completions", true);
		settings->set_setting("yeet_ai/chat/completions_url", "https://llm.adityaberry.me/v1/chat/completions");
	}
	if (!settings->has_setting("yeet_ai/chat/model")) {
		settings->set_initial_value("yeet_ai/chat/model", "berrymodel", true);
		settings->set_setting("yeet_ai/chat/model", "berrymodel");
	}
	if (!settings->has_setting("yeet_ai/chat/api_key")) {
		settings->set_initial_value("yeet_ai/chat/api_key", "", true);
		settings->set_setting("yeet_ai/chat/api_key", "");
	}
	if (!settings->has_setting("yeet_ai/chat/max_tokens")) {
		settings->set_initial_value("yeet_ai/chat/max_tokens", 32768, true);
		settings->set_setting("yeet_ai/chat/max_tokens", 32768);
	}
	// One-time bump: the old default (2048) truncates large batch_tool_calls JSON.
	if (!settings->has_setting("yeet_ai/chat/max_tokens_legacy_bump_v1")) {
		settings->set_initial_value("yeet_ai/chat/max_tokens_legacy_bump_v1", true, true);
		if (settings->has_setting("yeet_ai/chat/max_tokens") && int(settings->get_setting("yeet_ai/chat/max_tokens")) == 2048) {
			settings->set_setting("yeet_ai/chat/max_tokens", 16384);
		}
		settings->set_setting("yeet_ai/chat/max_tokens_legacy_bump_v1", true);
	}
	// One-time bump: 16k still truncates long batch_tool_calls (inputs + GDScript in one JSON).
	if (!settings->has_setting("yeet_ai/chat/max_tokens_legacy_bump_v2")) {
		settings->set_initial_value("yeet_ai/chat/max_tokens_legacy_bump_v2", true, true);
		if (settings->has_setting("yeet_ai/chat/max_tokens")) {
			const int v = int(settings->get_setting("yeet_ai/chat/max_tokens"));
			if (v == 16384 || v == 2048) {
				settings->set_setting("yeet_ai/chat/max_tokens", 32768);
			}
		}
		settings->set_setting("yeet_ai/chat/max_tokens_legacy_bump_v2", true);
	}
	if (!settings->has_setting("yeet_ai/chat/max_tool_round_trips") || int(settings->get_setting("yeet_ai/chat/max_tool_round_trips")) < 100) {
		settings->set_initial_value("yeet_ai/chat/max_tool_round_trips", 100, true);
		settings->set_setting("yeet_ai/chat/max_tool_round_trips", 100);
	}
	if (!settings->has_setting("yeet_ai/chat/vision_enabled")) {
		settings->set_initial_value("yeet_ai/chat/vision_enabled", false, true);
		settings->set_setting("yeet_ai/chat/vision_enabled", false);
	}
	if (!settings->has_setting("yeet_ai/chat/max_base64_chars")) {
		settings->set_initial_value("yeet_ai/chat/max_base64_chars", 2000000, true);
		settings->set_setting("yeet_ai/chat/max_base64_chars", 2000000);
	}
	if (!settings->has_setting("yeet_ai/chat/game_screenshot_timeout_ms")) {
		settings->set_initial_value("yeet_ai/chat/game_screenshot_timeout_ms", 8000, true);
		settings->set_setting("yeet_ai/chat/game_screenshot_timeout_ms", 8000);
	}
	if (!settings->has_setting("yeet_ai/chat/temperature")) {
		// Lower than 1.0 helps instruction models (e.g. Qwen) stick to JSON tool output.
		settings->set_initial_value("yeet_ai/chat/temperature", 0.25, true);
		settings->set_setting("yeet_ai/chat/temperature", 0.25);
	}
	if (!settings->has_setting("yeet_ai/chat/vision_default_max_dimension")) {
		settings->set_initial_value("yeet_ai/chat/vision_default_max_dimension", 1280, true);
		settings->set_setting("yeet_ai/chat/vision_default_max_dimension", 1280);
	}
	if (!settings->has_setting("yeet_ai/tools/allow_project_settings_write")) {
		settings->set_initial_value("yeet_ai/tools/allow_project_settings_write", true, true);
		settings->set_setting("yeet_ai/tools/allow_project_settings_write", true);
	}
	if (!settings->has_setting("yeet_ai/tools/allow_editor_settings_write")) {
		settings->set_initial_value("yeet_ai/tools/allow_editor_settings_write", false, true);
		settings->set_setting("yeet_ai/tools/allow_editor_settings_write", false);
	}
	if (!settings->has_setting("yeet_ai/tools/extended_read_extensions")) {
		settings->set_initial_value("yeet_ai/tools/extended_read_extensions", false, true);
		settings->set_setting("yeet_ai/tools/extended_read_extensions", false);
	}
	if (!settings->has_setting("yeet_ai/tools/allow_binary_metadata_read")) {
		settings->set_initial_value("yeet_ai/tools/allow_binary_metadata_read", false, true);
		settings->set_setting("yeet_ai/tools/allow_binary_metadata_read", false);
	}
	if (!settings->has_setting("yeet_ai/tools/allow_resource_inspector_write")) {
		settings->set_initial_value("yeet_ai/tools/allow_resource_inspector_write", true, true);
		settings->set_setting("yeet_ai/tools/allow_resource_inspector_write", true);
	}
	if (!settings->has_setting("yeet_ai/tools/inspector_max_properties")) {
		settings->set_initial_value("yeet_ai/tools/inspector_max_properties", 120, true);
		settings->set_setting("yeet_ai/tools/inspector_max_properties", 120);
	}
	if (!settings->has_setting("yeet_ai/tools/allow_reimport")) {
		settings->set_initial_value("yeet_ai/tools/allow_reimport", true, true);
		settings->set_setting("yeet_ai/tools/allow_reimport", true);
	}
	if (!settings->has_setting("yeet_ai/tools/allow_resource_save")) {
		settings->set_initial_value("yeet_ai/tools/allow_resource_save", true, true);
		settings->set_setting("yeet_ai/tools/allow_resource_save", true);
	}
	if (!settings->has_setting("yeet_ai/tools/allow_replace_in_files")) {
		settings->set_initial_value("yeet_ai/tools/allow_replace_in_files", false, true);
		settings->set_setting("yeet_ai/tools/allow_replace_in_files", false);
	}
}

bool YeetAIEditorPlugin::_is_ai_enabled() const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr) {
		return true;
	}
	if (!settings->has_setting("yeet_ai/enabled")) {
		return true;
	}
	return bool(settings->get_setting("yeet_ai/enabled"));
}

void YeetAIEditorPlugin::_deferred_add_ai_dock() {
	if (!_is_ai_enabled()) {
		return;
	}
	_add_ai_dock();
}

void YeetAIEditorPlugin::_add_ai_dock() {
	if (editor_dock == nullptr || editor_dock->is_inside_tree()) {
		return;
	}

	add_dock(editor_dock);

	EditorInterface *iface = get_editor_interface();
	if (iface != nullptr) {
		Ref<Theme> ed_theme = iface->get_editor_theme();
		if (ed_theme.is_valid()) {
			editor_dock->set_dock_icon(ed_theme->get_icon(SNAME("Search"), EditorStringName(EditorIcons)));
		}
	}

	editor_dock->make_visible();

	EditorDockManager *dock_mgr = EditorDockManager::get_singleton();
	if (dock_mgr != nullptr) {
		dock_mgr->focus_dock(editor_dock);
	}
}

void YeetAIEditorPlugin::_remove_ai_dock() {
	if (editor_dock == nullptr || !editor_dock->is_inside_tree()) {
		return;
	}

	remove_dock(editor_dock);
}

void YeetAIEditorPlugin::_open_ai_dock_from_menu() {
	_add_ai_dock();
	EditorDockManager *dock_mgr = EditorDockManager::get_singleton();
	if (editor_dock != nullptr && dock_mgr != nullptr) {
		dock_mgr->focus_dock(editor_dock);
	}
}

void YeetAIEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_ensure_editor_settings();
			_register_crosshair_editor_setting_hints();
			add_tool_menu_item(TTR("Open Crosshair AI Dock"), callable_mp(this, &YeetAIEditorPlugin::_open_ai_dock_from_menu));
			// Defer so EditorDockManager and editor theme are fully ready (dock tab + focus).
			callable_mp(this, &YeetAIEditorPlugin::_deferred_add_ai_dock).call_deferred();
		} break;
		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (!EditorSettings::get_singleton()->check_changed_settings_in_group("yeet_ai")) {
				break;
			}

			_remove_ai_dock();
			if (_is_ai_enabled()) {
				_add_ai_dock();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			remove_tool_menu_item(TTR("Open Crosshair AI Dock"));
			_remove_ai_dock();
		} break;
	}
}

String YeetAIEditorPlugin::get_plugin_name() const {
	return "Crosshair AI";
}

bool YeetAIEditorPlugin::has_main_screen() const {
	return false;
}
