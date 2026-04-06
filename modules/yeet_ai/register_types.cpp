/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "register_types.h"

#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/yeet_ai_editor_plugin.h"

#include "editor/settings/editor_settings.h"
#endif

void initialize_yeet_ai_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		_EDITOR_DEF("yeet_ai/enabled", true);
		_EDITOR_DEF("yeet_ai/chat/completions_url", "https://llm.adityaberry.me/v1/chat/completions");
		_EDITOR_DEF("yeet_ai/chat/model", "berrymodel");
		_EDITOR_DEF("yeet_ai/chat/api_key", "");
		_EDITOR_DEF("yeet_ai/chat/max_tokens", 16384);
		_EDITOR_DEF("yeet_ai/chat/max_tool_round_trips", 100);

		GDREGISTER_CLASS(YeetAIEditorPlugin);
		EditorPlugins::add_by_type<YeetAIEditorPlugin>();
	}
#endif
}

void uninitialize_yeet_ai_module(ModuleInitializationLevel p_level) {
	(void)p_level;
}
