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
#include "editor/docks/editor_dock.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_interface.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"

#include "scene/resources/theme.h"

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
		settings->set_initial_value("yeet_ai/chat/max_tokens", 16384, true);
		settings->set_setting("yeet_ai/chat/max_tokens", 16384);
	}
	// One-time bump: the old default (2048) truncates large batch_tool_calls JSON.
	if (!settings->has_setting("yeet_ai/chat/max_tokens_legacy_bump_v1")) {
		settings->set_initial_value("yeet_ai/chat/max_tokens_legacy_bump_v1", true, true);
		if (settings->has_setting("yeet_ai/chat/max_tokens") && int(settings->get_setting("yeet_ai/chat/max_tokens")) == 2048) {
			settings->set_setting("yeet_ai/chat/max_tokens", 16384);
		}
		settings->set_setting("yeet_ai/chat/max_tokens_legacy_bump_v1", true);
	}
	if (!settings->has_setting("yeet_ai/chat/max_tool_round_trips") || int(settings->get_setting("yeet_ai/chat/max_tool_round_trips")) < 100) {
		settings->set_initial_value("yeet_ai/chat/max_tool_round_trips", 100, true);
		settings->set_setting("yeet_ai/chat/max_tool_round_trips", 100);
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
