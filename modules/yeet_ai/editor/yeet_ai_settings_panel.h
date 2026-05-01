/**************************************************************************/
/*  yeet_ai_settings_panel.h                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#pragma once

#include "scene/gui/box_container.h"

class Button;
class CheckBox;
class HTTPRequest;
class LineEdit;
class OptionButton;
class SpinBox;

/// Editor Settings dialog tab: Crosshair AI (`yeet_ai/*` EditorSettings keys).
class YeetAISettingsPanel : public VBoxContainer {
	CheckBox *settings_enabled = nullptr;
	OptionButton *settings_provider = nullptr;
	VBoxContainer *local_settings_block = nullptr;
	VBoxContainer *gemini_settings_block = nullptr;
	VBoxContainer *openrouter_settings_block = nullptr;
	VBoxContainer *yeet_settings_block = nullptr;
	VBoxContainer *azure_settings_block = nullptr;
	VBoxContainer *model_local_gemini_block = nullptr;
	VBoxContainer *model_gemini_block = nullptr;
	VBoxContainer *model_openrouter_block = nullptr;
	VBoxContainer *model_yeet_block = nullptr;
	VBoxContainer *model_azure_block = nullptr;
	LineEdit *settings_completions_url = nullptr;
	LineEdit *settings_model = nullptr;
	OptionButton *settings_openrouter_model_choice = nullptr;
	LineEdit *settings_openrouter_model_custom = nullptr;
	Button *settings_openrouter_models_refresh = nullptr;
	HTTPRequest *openrouter_models_http = nullptr;
	OptionButton *settings_gemini_model_choice = nullptr;
	LineEdit *settings_gemini_model_custom = nullptr;
	Button *settings_gemini_models_refresh = nullptr;
	HTTPRequest *gemini_models_http = nullptr;
	OptionButton *settings_yeet_model_choice = nullptr;
	LineEdit *settings_yeet_model_custom = nullptr;
	Button *settings_yeet_models_refresh = nullptr;
	HTTPRequest *yeet_models_http = nullptr;
	LineEdit *settings_yeet_chat_url = nullptr;
	LineEdit *settings_yeet_tags_url = nullptr;
	LineEdit *settings_api_key = nullptr;
	LineEdit *settings_gemini_api_key = nullptr;
	LineEdit *settings_openrouter_api_key = nullptr;
	LineEdit *settings_yeet_api_key = nullptr;
	LineEdit *settings_azure_endpoint = nullptr;
	LineEdit *settings_azure_deployment = nullptr;
	LineEdit *settings_azure_api_version = nullptr;
	LineEdit *settings_azure_api_key = nullptr;
	LineEdit *settings_azure_model = nullptr;
	SpinBox *settings_max_tokens = nullptr;
	SpinBox *settings_max_tool_round_trips = nullptr;
	SpinBox *settings_max_base64_chars = nullptr;
	SpinBox *settings_game_screenshot_timeout_ms = nullptr;
	SpinBox *settings_temperature = nullptr;
	CheckBox *settings_vision_enabled = nullptr;
	CheckBox *settings_allow_project_settings_write = nullptr;
	CheckBox *settings_allow_editor_settings_write = nullptr;
	CheckBox *settings_extended_read_extensions = nullptr;
	CheckBox *settings_allow_reimport = nullptr;
	CheckBox *settings_allow_resource_save = nullptr;
	CheckBox *settings_allow_replace_in_files = nullptr;
	SpinBox *settings_vision_default_max_dimension = nullptr;
	bool settings_committing = false;
	int cached_provider_id = 0;

	void _add_settings_labeled_row(VBoxContainer *p_vb, const String &p_label, Control *p_control, const String &p_tooltip = String());
	void _build_ui();
	void _load_from_settings();
	void _commit_to_settings();
	void _commit_text_submitted(const String &p_text);
	void _commit_spinbox_changed(double p_value);
	void _commit_checkbox_toggled(bool p_pressed);
	void _on_external_settings_changed();
	void _update_provider_blocks();
	void _on_provider_selected(int p_index);

	void _rebuild_openrouter_model_dropdown(const Vector<String> &p_api_slugs);
	void _fetch_openrouter_models_list();
	void _on_openrouter_models_http_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _sync_openrouter_model_ui_from_slug(const String &p_slug);
	void _on_openrouter_model_selected(int p_index);
	void _on_openrouter_model_custom_changed(const String &p_new_text);
	void _update_openrouter_custom_line_visibility();

	void _rebuild_gemini_model_dropdown(const Vector<String> &p_api_model_ids);
	void _fetch_gemini_models_list();
	void _on_gemini_models_http_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _sync_gemini_model_ui_from_slug(const String &p_slug);
	void _on_gemini_model_selected(int p_index);
	void _on_gemini_model_custom_changed(const String &p_new_text);
	void _update_gemini_custom_line_visibility();

	void _rebuild_yeet_model_dropdown(const Vector<String> &p_model_ids);
	void _fetch_yeet_models_list();
	void _on_yeet_models_http_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _sync_yeet_model_ui_from_slug(const String &p_slug);
	void _on_yeet_model_selected(int p_index);
	void _on_yeet_model_custom_changed(const String &p_new_text);
	void _update_yeet_custom_line_visibility();

	String _get_openrouter_model_slug_from_ui() const;
	String _get_gemini_model_slug_from_ui() const;
	String _get_yeet_model_slug_from_ui() const;
	String _get_model_value_for_commit() const;

	String _get_setting_string(const String &p_setting, const String &p_default) const;
	int _get_setting_int(const String &p_setting, int p_default) const;
	float _get_setting_float(const String &p_setting, float p_default) const;
	bool _get_setting_bool(const String &p_setting, bool p_default) const;

protected:
	void _notification(int p_what);

public:
	void reload_from_settings();
	YeetAISettingsPanel();
};
