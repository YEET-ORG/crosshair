/**************************************************************************/
/*  yeet_ai_editor_plugin.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#pragma once

#include "editor/plugins/editor_plugin.h"

class YeetAIDock;
class EditorDock;

class YeetAIEditorPlugin : public EditorPlugin {
	GDCLASS(YeetAIEditorPlugin, EditorPlugin);

	YeetAIDock *dock = nullptr;
	EditorDock *editor_dock = nullptr;

	void _ensure_editor_settings();
	void _register_crosshair_editor_setting_hints();
	bool _is_ai_enabled() const;
	void _deferred_add_ai_dock();
	void _add_ai_dock();
	void _remove_ai_dock();
	void _open_ai_dock_from_menu();
	void _on_filesystem_changed();
	void _on_resources_reimported(const PackedStringArray &p_files);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override;
	virtual bool has_main_screen() const override;

	YeetAIEditorPlugin();
	~YeetAIEditorPlugin();
};
