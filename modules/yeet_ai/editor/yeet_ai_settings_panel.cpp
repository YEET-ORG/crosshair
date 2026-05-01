/**************************************************************************/
/*  yeet_ai_settings_panel.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_settings_panel.h"

#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/string/string_name.h"
#include "core/string/translation.h"
#include "core/templates/hash_set.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/scene_string_names.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/option_button.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/main/http_request.h"
#include "servers/text/text_server.h"

namespace {
/// Fixed model id for Berry (OpenAI-compatible) — matches Ollama `/api/tags` `models[].model` / `data[].id`.
constexpr const char *k_berry_model_id = "berrymodel";

constexpr const char *k_openrouter_custom_meta = "__crosshair_openrouter_custom__";
constexpr const char *k_gemini_custom_meta = "__crosshair_gemini_custom__";
constexpr const char *k_yeet_custom_meta = "__crosshair_yeet_custom__";

static const char *k_openrouter_pinned_slug[] = {
	"qwen/qwen3.6-plus:free",
	"qwen/qwen3.6-plus-preview:free",
};

static const char *k_gemini_pinned_slug[] = {
	"gemini-2.0-flash",
	"gemini-2.0-flash-lite",
	"gemini-1.5-flash",
	"gemini-1.5-pro",
};

String _gemini_strip_models_prefix(const String &p_name) {
	static const String k_prefix = "models/";
	if (p_name.begins_with(k_prefix)) {
		return p_name.substr(k_prefix.length());
	}
	return p_name;
}

bool _gemini_item_supports_chat(const Dictionary &p_item) {
	if (!p_item.has("supportedGenerationMethods")) {
		return true;
	}
	const Array methods = p_item["supportedGenerationMethods"];
	for (int i = 0; i < methods.size(); i++) {
		if (String(methods[i]) == String("generateContent")) {
			return true;
		}
	}
	return false;
}
} // namespace

void YeetAISettingsPanel::_update_provider_blocks() {
	ERR_FAIL_NULL(settings_provider);
	ERR_FAIL_NULL(local_settings_block);
	ERR_FAIL_NULL(gemini_settings_block);
	ERR_FAIL_NULL(openrouter_settings_block);
	ERR_FAIL_NULL(yeet_settings_block);
	ERR_FAIL_NULL(azure_settings_block);
	ERR_FAIL_NULL(model_local_gemini_block);
	ERR_FAIL_NULL(model_gemini_block);
	ERR_FAIL_NULL(model_openrouter_block);
	ERR_FAIL_NULL(model_yeet_block);
	ERR_FAIL_NULL(model_azure_block);
	const int id = settings_provider->get_selected_id();
	local_settings_block->set_visible(id == 0);
	gemini_settings_block->set_visible(id == 1);
	openrouter_settings_block->set_visible(id == 2);
	yeet_settings_block->set_visible(id == 3);
	azure_settings_block->set_visible(id == 4);
	model_local_gemini_block->set_visible(id == 0);
	model_gemini_block->set_visible(id == 1);
	model_openrouter_block->set_visible(id == 2);
	model_yeet_block->set_visible(id == 3);
	model_azure_block->set_visible(id == 4);
}

void YeetAISettingsPanel::_on_provider_selected(int p_index) {
	(void)p_index;

	const int new_id = settings_provider->get_selected_id();
	const int prev_id = cached_provider_id;
	cached_provider_id = new_id;

	if (!settings_committing) {
		EditorSettings *s = EditorSettings::get_singleton();
		if (s != nullptr) {
			String model_val;
			if (prev_id == 1) {
				model_val = _get_gemini_model_slug_from_ui();
			} else if (prev_id == 2) {
				model_val = _get_openrouter_model_slug_from_ui();
			} else if (prev_id == 3) {
				model_val = _get_yeet_model_slug_from_ui();
			} else if (prev_id == 4) {
				model_val = settings_azure_model->get_text();
			} else {
				model_val = String::utf8(k_berry_model_id);
			}
			s->set_setting("yeet_ai/chat/model", model_val);
		}
	}

	_update_provider_blocks();

	if (new_id == 1) {
		_fetch_gemini_models_list();
		_sync_gemini_model_ui_from_slug(_get_setting_string("yeet_ai/chat/model", ""));
	}
	if (new_id == 2) {
		_fetch_openrouter_models_list();
		_sync_openrouter_model_ui_from_slug(_get_setting_string("yeet_ai/chat/model", ""));
	}
	if (new_id == 3) {
		_fetch_yeet_models_list();
		_sync_yeet_model_ui_from_slug(_get_setting_string("yeet_ai/chat/model", ""));
	}
	if (new_id == 4) {
		settings_azure_model->set_text(_get_setting_string("yeet_ai/chat/model", "gpt-4o"));
	}

	_commit_to_settings();
}

YeetAISettingsPanel::YeetAISettingsPanel() {
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	set_v_size_flags(Control::SIZE_EXPAND_FILL);
	_build_ui();
}

void YeetAISettingsPanel::_add_settings_labeled_row(VBoxContainer *p_vb, const String &p_label, Control *p_control, const String &p_tooltip) {
	HBoxContainer *row = memnew(HBoxContainer);
	row->add_theme_constant_override("separation", int(8.0f * EDSCALE));
	row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	Label *l = memnew(Label);
	l->set_text(p_label);
	l->set_custom_minimum_size(Vector2(132 * EDSCALE, 0));
	l->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	p_control->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	if (!p_tooltip.is_empty()) {
		l->set_tooltip_text(p_tooltip);
		p_control->set_tooltip_text(p_tooltip);
	}
	row->add_child(l);
	row->add_child(p_control);
	p_vb->add_child(row);
}

void YeetAISettingsPanel::_build_ui() {
	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_SHOW_NEVER);
	scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
	add_child(scroll);

	MarginContainer *smc = memnew(MarginContainer);
	smc->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	smc->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	smc->add_theme_constant_override("margin_left", int(8.0f * EDSCALE));
	smc->add_theme_constant_override("margin_right", int(12.0f * EDSCALE));
	smc->add_theme_constant_override("margin_top", int(8.0f * EDSCALE));
	smc->add_theme_constant_override("margin_bottom", int(12.0f * EDSCALE));
	scroll->add_child(smc);

	VBoxContainer *vb = memnew(VBoxContainer);
	vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	vb->add_theme_constant_override("separation", int(10.0f * EDSCALE));
	smc->add_child(vb);

	Label *intro = memnew(Label);
	intro->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	intro->set_text(TTR("Configure the Crosshair AI chat endpoint and generation limits. Values are stored in your editor user profile (shared across all projects), not in project.godot. The same keys appear under Editor Settings → General when you filter by yeet_ai."));
	vb->add_child(intro);

	settings_enabled = memnew(CheckBox);
	settings_enabled->set_text(TTR("Enable Crosshair AI dock"));
	vb->add_child(settings_enabled);

	HSeparator *sep0 = memnew(HSeparator);
	vb->add_child(sep0);

	Label *provider_label = memnew(Label);
	provider_label->set_text(TTR("Provider"));
	vb->add_child(provider_label);

	settings_provider = memnew(OptionButton);
	settings_provider->add_item(TTR("Berry Model"), 0);
	settings_provider->add_item(TTR("Gemini"), 1);
	settings_provider->add_item(TTR("OpenRouter"), 2);
	settings_provider->add_item(TTR("Yeet Models"), 3);
	settings_provider->add_item(TTR("Azure OpenAI"), 4);
	settings_provider->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	settings_provider->connect(SceneStringName(item_selected), callable_mp(this, &YeetAISettingsPanel::_on_provider_selected));
	vb->add_child(settings_provider);

	local_settings_block = memnew(VBoxContainer);
	local_settings_block->add_theme_constant_override("separation", int(10.0f * EDSCALE));
	vb->add_child(local_settings_block);

	settings_completions_url = memnew(LineEdit);
	settings_completions_url->set_placeholder(TTR("https://host/v1/chat/completions"));
	_add_settings_labeled_row(local_settings_block, TTR("API URL"), settings_completions_url, TTR("OpenAI-compatible chat completions endpoint for your Berry Model server."));

	settings_api_key = memnew(LineEdit);
	settings_api_key->set_secret(true);
	settings_api_key->set_placeholder(TTR("Optional Bearer token"));
	_add_settings_labeled_row(local_settings_block, TTR("API key"), settings_api_key, TTR("Sent as Authorization: Bearer if non-empty."));

	gemini_settings_block = memnew(VBoxContainer);
	gemini_settings_block->add_theme_constant_override("separation", int(10.0f * EDSCALE));
	vb->add_child(gemini_settings_block);

	settings_gemini_api_key = memnew(LineEdit);
	settings_gemini_api_key->set_secret(true);
	settings_gemini_api_key->set_placeholder(TTR("Google AI Studio API key"));
	_add_settings_labeled_row(gemini_settings_block, TTR("Gemini API key"), settings_gemini_api_key, TTR("Create a key at Google AI Studio. Uses the Gemini OpenAI-compatible endpoint; not sent to your local server."));

	Label *gemini_hint = memnew(Label);
	gemini_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	gemini_hint->set_text(TTR("Below, pick a model from the Google AI list (requires API key) or Custom."));
	gemini_settings_block->add_child(gemini_hint);

	openrouter_settings_block = memnew(VBoxContainer);
	openrouter_settings_block->add_theme_constant_override("separation", int(10.0f * EDSCALE));
	vb->add_child(openrouter_settings_block);

	settings_openrouter_api_key = memnew(LineEdit);
	settings_openrouter_api_key->set_secret(true);
	settings_openrouter_api_key->set_placeholder(TTR("OpenRouter API key"));
	_add_settings_labeled_row(openrouter_settings_block, TTR("OpenRouter API key"), settings_openrouter_api_key, TTR("Create a key at openrouter.ai. Uses the OpenRouter OpenAI-compatible endpoint."));

	Label *openrouter_hint = memnew(Label);
	openrouter_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	openrouter_hint->set_text(TTR("Below, pick an OpenRouter model or Custom. The list loads from OpenRouter; Qwen 3.6 free tiers are pinned at the top."));
	openrouter_settings_block->add_child(openrouter_hint);

	yeet_settings_block = memnew(VBoxContainer);
	yeet_settings_block->add_theme_constant_override("separation", int(10.0f * EDSCALE));
	vb->add_child(yeet_settings_block);

	settings_yeet_chat_url = memnew(LineEdit);
	settings_yeet_chat_url->set_placeholder(TTR("https://gpt.yeetlabs.fun/v1/chat/completions"));
	_add_settings_labeled_row(yeet_settings_block, TTR("Yeet chat URL"), settings_yeet_chat_url, TTR("OpenAI-compatible /v1/chat/completions endpoint (Ollama-compatible API)."));

	settings_yeet_tags_url = memnew(LineEdit);
	settings_yeet_tags_url->set_placeholder(TTR("https://gpt.yeetlabs.fun/api/tags"));
	_add_settings_labeled_row(yeet_settings_block, TTR("Yeet tags URL"), settings_yeet_tags_url, TTR("Ollama-style /api/tags URL used to list models for the dropdown."));

	settings_yeet_api_key = memnew(LineEdit);
	settings_yeet_api_key->set_secret(true);
	settings_yeet_api_key->set_placeholder(TTR("Optional Bearer token"));
	_add_settings_labeled_row(yeet_settings_block, TTR("Yeet API key"), settings_yeet_api_key, TTR("Sent as Authorization: Bearer if non-empty."));

	Label *yeet_hint = memnew(Label);
	yeet_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	yeet_hint->set_text(TTR("Below, choose a model from the Yeet tags list or Custom."));
	yeet_settings_block->add_child(yeet_hint);

	azure_settings_block = memnew(VBoxContainer);
	azure_settings_block->add_theme_constant_override("separation", int(10.0f * EDSCALE));
	vb->add_child(azure_settings_block);

	settings_azure_endpoint = memnew(LineEdit);
	settings_azure_endpoint->set_placeholder(TTR("https://crosshair-resource.services.ai.azure.com"));
	_add_settings_labeled_row(azure_settings_block, TTR("Azure endpoint"), settings_azure_endpoint, TTR("Azure OpenAI resource endpoint (e.g. https://crosshair-resource.services.ai.azure.com)."));

	settings_azure_deployment = memnew(LineEdit);
	settings_azure_deployment->set_placeholder(TTR("gpt-4o"));
	_add_settings_labeled_row(azure_settings_block, TTR("Azure deployment"), settings_azure_deployment, TTR("Deployment name configured in Azure AI Foundry / OpenAI Studio."));

	settings_azure_api_version = memnew(LineEdit);
	settings_azure_api_version->set_placeholder(TTR("2024-05-01-preview"));
	_add_settings_labeled_row(azure_settings_block, TTR("API version"), settings_azure_api_version, TTR("Azure AI API version (e.g. 2024-05-01-preview)."));

	settings_azure_api_key = memnew(LineEdit);
	settings_azure_api_key->set_secret(true);
	settings_azure_api_key->set_placeholder(TTR("Azure API key"));
	_add_settings_labeled_row(azure_settings_block, TTR("Azure API key"), settings_azure_api_key, TTR("Key from Azure Portal or AI Foundry. Sent as Authorization: Bearer."));

	Label *azure_hint = memnew(Label);
	azure_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	azure_hint->set_text(TTR("Below, enter the model name matching your Azure deployment (e.g. gpt-4o)."));
	azure_settings_block->add_child(azure_hint);

	model_local_gemini_block = memnew(VBoxContainer);
	model_local_gemini_block->add_theme_constant_override("separation", int(10.0f * EDSCALE));
	vb->add_child(model_local_gemini_block);

	settings_model = memnew(LineEdit);
	settings_model->set_text(String::utf8(k_berry_model_id));
	settings_model->set_editable(false);
	settings_model->set_focus_mode(Control::FOCUS_NONE);
	settings_model->set_tooltip_text(TTR("Locked to `berrymodel` (the id your Berry/Ollama-compatible server exposes in /api/tags)."));
	_add_settings_labeled_row(model_local_gemini_block, TTR("Model"), settings_model, TTR("Berry Model uses the fixed model id `berrymodel` for chat completions (same as tags list `model` / `id`)."));

	model_gemini_block = memnew(VBoxContainer);
	model_gemini_block->add_theme_constant_override("separation", int(6.0f * EDSCALE));
	vb->add_child(model_gemini_block);

	HBoxContainer *gm_row = memnew(HBoxContainer);
	gm_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	Label *gm_ml = memnew(Label);
	gm_ml->set_text(TTR("Model"));
	gm_ml->set_custom_minimum_size(Vector2(132 * EDSCALE, 0));
	gm_ml->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	gm_ml->set_tooltip_text(TTR("Gemini model id for the OpenAI-compatible chat API (from list or Custom)."));
	VBoxContainer *gm_col = memnew(VBoxContainer);
	gm_col->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	settings_gemini_model_choice = memnew(OptionButton);
	settings_gemini_model_choice->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	settings_gemini_model_choice->set_tooltip_text(TTR("Listed via Google generativelanguage v1beta/models; includes Custom…"));
	gm_col->add_child(settings_gemini_model_choice);
	settings_gemini_model_custom = memnew(LineEdit);
	settings_gemini_model_custom->set_placeholder(TTR("Custom model id"));
	settings_gemini_model_custom->set_visible(false);
	settings_gemini_model_custom->set_tooltip_text(TTR("Used when Custom… is selected (e.g. gemini-2.0-flash)."));
	gm_col->add_child(settings_gemini_model_custom);
	settings_gemini_models_refresh = memnew(Button);
	settings_gemini_models_refresh->set_text(TTR("Refresh model list"));
	settings_gemini_models_refresh->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	gm_col->add_child(settings_gemini_models_refresh);
	gm_row->add_child(gm_ml);
	gm_row->add_child(gm_col);
	model_gemini_block->add_child(gm_row);

	model_openrouter_block = memnew(VBoxContainer);
	model_openrouter_block->add_theme_constant_override("separation", int(6.0f * EDSCALE));
	vb->add_child(model_openrouter_block);

	HBoxContainer *or_row = memnew(HBoxContainer);
	or_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	Label *or_ml = memnew(Label);
	or_ml->set_text(TTR("Model"));
	or_ml->set_custom_minimum_size(Vector2(132 * EDSCALE, 0));
	or_ml->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	or_ml->set_tooltip_text(TTR("OpenRouter model slug (from list or Custom)."));
	VBoxContainer *or_col = memnew(VBoxContainer);
	or_col->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	settings_openrouter_model_choice = memnew(OptionButton);
	settings_openrouter_model_choice->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	settings_openrouter_model_choice->set_tooltip_text(TTR("Fetched from OpenRouter; includes Custom… for manual slugs."));
	or_col->add_child(settings_openrouter_model_choice);
	settings_openrouter_model_custom = memnew(LineEdit);
	settings_openrouter_model_custom->set_placeholder(TTR("Custom model slug"));
	settings_openrouter_model_custom->set_visible(false);
	settings_openrouter_model_custom->set_tooltip_text(TTR("Used when Custom… is selected."));
	or_col->add_child(settings_openrouter_model_custom);
	settings_openrouter_models_refresh = memnew(Button);
	settings_openrouter_models_refresh->set_text(TTR("Refresh model list"));
	settings_openrouter_models_refresh->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	or_col->add_child(settings_openrouter_models_refresh);
	or_row->add_child(or_ml);
	or_row->add_child(or_col);
	model_openrouter_block->add_child(or_row);

	model_yeet_block = memnew(VBoxContainer);
	model_yeet_block->add_theme_constant_override("separation", int(6.0f * EDSCALE));
	vb->add_child(model_yeet_block);

	HBoxContainer *yt_row = memnew(HBoxContainer);
	yt_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	Label *yt_ml = memnew(Label);
	yt_ml->set_text(TTR("Model"));
	yt_ml->set_custom_minimum_size(Vector2(132 * EDSCALE, 0));
	yt_ml->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	yt_ml->set_tooltip_text(TTR("Yeet Labs model name (from tags or Custom)."));
	VBoxContainer *yt_col = memnew(VBoxContainer);
	yt_col->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	settings_yeet_model_choice = memnew(OptionButton);
	settings_yeet_model_choice->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	settings_yeet_model_choice->set_tooltip_text(TTR("Fetched from the Yeet tags URL; includes Custom… for manual names."));
	yt_col->add_child(settings_yeet_model_choice);
	settings_yeet_model_custom = memnew(LineEdit);
	settings_yeet_model_custom->set_placeholder(TTR("Custom model name"));
	settings_yeet_model_custom->set_visible(false);
	settings_yeet_model_custom->set_tooltip_text(TTR("Used when Custom… is selected."));
	yt_col->add_child(settings_yeet_model_custom);
	settings_yeet_models_refresh = memnew(Button);
	settings_yeet_models_refresh->set_text(TTR("Refresh model list"));
	settings_yeet_models_refresh->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	yt_col->add_child(settings_yeet_models_refresh);
	yt_row->add_child(yt_ml);
	yt_row->add_child(yt_col);
	model_yeet_block->add_child(yt_row);

	model_azure_block = memnew(VBoxContainer);
	model_azure_block->add_theme_constant_override("separation", int(10.0f * EDSCALE));
	vb->add_child(model_azure_block);

	settings_azure_model = memnew(LineEdit);
	settings_azure_model->set_placeholder(TTR("gpt-4o"));
	settings_azure_model->set_tooltip_text(TTR("Model name for this Azure deployment (e.g. gpt-4o). Used in chat payload."));
	_add_settings_labeled_row(model_azure_block, TTR("Model"), settings_azure_model, TTR("Model name matching your Azure deployment (e.g. gpt-4o)."));

	openrouter_models_http = memnew(HTTPRequest);
	openrouter_models_http->set_use_threads(true);
	openrouter_models_http->set_timeout(60.0);
	openrouter_models_http->connect("request_completed", callable_mp(this, &YeetAISettingsPanel::_on_openrouter_models_http_completed));
	add_child(openrouter_models_http);

	yeet_models_http = memnew(HTTPRequest);
	yeet_models_http->set_use_threads(true);
	yeet_models_http->set_timeout(60.0);
	yeet_models_http->connect("request_completed", callable_mp(this, &YeetAISettingsPanel::_on_yeet_models_http_completed));
	add_child(yeet_models_http);

	gemini_models_http = memnew(HTTPRequest);
	gemini_models_http->set_use_threads(true);
	gemini_models_http->set_timeout(60.0);
	gemini_models_http->connect("request_completed", callable_mp(this, &YeetAISettingsPanel::_on_gemini_models_http_completed));
	add_child(gemini_models_http);

	_rebuild_openrouter_model_dropdown(Vector<String>());
	_rebuild_gemini_model_dropdown(Vector<String>());
	_rebuild_yeet_model_dropdown(Vector<String>());

	settings_max_tokens = memnew(SpinBox);
	settings_max_tokens->set_min(256);
	settings_max_tokens->set_max(262144);
	settings_max_tokens->set_step(256);
	_add_settings_labeled_row(vb, TTR("Max tokens"), settings_max_tokens, TTR("Max completion tokens per request (raise on server too)."));

	settings_temperature = memnew(SpinBox);
	settings_temperature->set_min(-1.0);
	settings_temperature->set_max(2.0);
	settings_temperature->set_step(0.01);
	settings_temperature->set_allow_greater(true);
	settings_temperature->set_allow_lesser(true);
	_add_settings_labeled_row(vb, TTR("Temperature"), settings_temperature, TTR("Sampling temperature. Use -1 to omit the field (server default)."));

	settings_max_tool_round_trips = memnew(SpinBox);
	settings_max_tool_round_trips->set_min(1);
	settings_max_tool_round_trips->set_max(500);
	settings_max_tool_round_trips->set_step(1);
	_add_settings_labeled_row(vb, TTR("Tool round-trips"), settings_max_tool_round_trips, TTR("Maximum assistant tool-call loops per user message."));

	settings_vision_enabled = memnew(CheckBox);
	settings_vision_enabled->set_text(TTR("Include viewport/screenshot images in requests (vision models)"));
	vb->add_child(settings_vision_enabled);

	settings_vision_default_max_dimension = memnew(SpinBox);
	settings_vision_default_max_dimension->set_min(64);
	settings_vision_default_max_dimension->set_max(4096);
	settings_vision_default_max_dimension->set_step(1);
	_add_settings_labeled_row(vb, TTR("Vision default max px"), settings_vision_default_max_dimension, TTR("Default max width/height for captures when the model omits max_width/max_height."));

	settings_allow_project_settings_write = memnew(CheckBox);
	settings_allow_project_settings_write->set_text(TTR("Allow AI to patch project settings (patch_project_settings)"));
	vb->add_child(settings_allow_project_settings_write);

	settings_allow_editor_settings_write = memnew(CheckBox);
	settings_allow_editor_settings_write->set_text(TTR("Allow AI to patch editor user settings (patch_editor_settings; off by default)"));
	vb->add_child(settings_allow_editor_settings_write);

	settings_extended_read_extensions = memnew(CheckBox);
	settings_extended_read_extensions->set_text(TTR("Extended read allowlist (more text extensions for read_project_file)"));
	vb->add_child(settings_extended_read_extensions);

	settings_allow_reimport = memnew(CheckBox);
	settings_allow_reimport->set_text(TTR("Allow AI reimport + full filesystem scan (reimport_project_files, scan_project_filesystem)"));
	vb->add_child(settings_allow_reimport);

	settings_allow_resource_save = memnew(CheckBox);
	settings_allow_resource_save->set_text(TTR("Allow AI save_resource (serialize loaded resources to disk)"));
	vb->add_child(settings_allow_resource_save);

	settings_allow_replace_in_files = memnew(CheckBox);
	settings_allow_replace_in_files->set_text(TTR("Allow AI replace_in_project_files (bulk text replace; off by default)"));
	vb->add_child(settings_allow_replace_in_files);

	settings_max_base64_chars = memnew(SpinBox);
	settings_max_base64_chars->set_min(10000);
	settings_max_base64_chars->set_max(10000000);
	settings_max_base64_chars->set_step(1000);
	_add_settings_labeled_row(vb, TTR("Max image chars"), settings_max_base64_chars, TTR("Cap base64 image payload size for vision."));

	settings_game_screenshot_timeout_ms = memnew(SpinBox);
	settings_game_screenshot_timeout_ms->set_min(1000);
	settings_game_screenshot_timeout_ms->set_max(120000);
	settings_game_screenshot_timeout_ms->set_step(100);
	_add_settings_labeled_row(vb, TTR("Game shot wait (ms)"), settings_game_screenshot_timeout_ms, TTR("How long to wait when capturing the running game view."));

	settings_enabled->connect(SceneStringName(toggled), callable_mp(this, &YeetAISettingsPanel::_commit_checkbox_toggled));
	settings_gemini_api_key->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_gemini_api_key->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_openrouter_api_key->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_openrouter_api_key->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_yeet_chat_url->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_yeet_chat_url->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_yeet_tags_url->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_yeet_tags_url->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_yeet_api_key->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_yeet_api_key->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_completions_url->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_completions_url->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_model->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_model->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_gemini_model_choice->connect(SceneStringName(item_selected), callable_mp(this, &YeetAISettingsPanel::_on_gemini_model_selected));
	settings_gemini_model_custom->connect(SceneStringName(text_changed), callable_mp(this, &YeetAISettingsPanel::_on_gemini_model_custom_changed));
	settings_gemini_model_custom->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_gemini_model_custom->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_gemini_models_refresh->connect(SceneStringName(pressed), callable_mp(this, &YeetAISettingsPanel::_fetch_gemini_models_list));
	settings_openrouter_model_choice->connect(SceneStringName(item_selected), callable_mp(this, &YeetAISettingsPanel::_on_openrouter_model_selected));
	settings_openrouter_model_custom->connect(SceneStringName(text_changed), callable_mp(this, &YeetAISettingsPanel::_on_openrouter_model_custom_changed));
	settings_openrouter_model_custom->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_openrouter_model_custom->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_openrouter_models_refresh->connect(SceneStringName(pressed), callable_mp(this, &YeetAISettingsPanel::_fetch_openrouter_models_list));
	settings_yeet_model_choice->connect(SceneStringName(item_selected), callable_mp(this, &YeetAISettingsPanel::_on_yeet_model_selected));
	settings_yeet_model_custom->connect(SceneStringName(text_changed), callable_mp(this, &YeetAISettingsPanel::_on_yeet_model_custom_changed));
	settings_yeet_model_custom->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_yeet_model_custom->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_yeet_models_refresh->connect(SceneStringName(pressed), callable_mp(this, &YeetAISettingsPanel::_fetch_yeet_models_list));
	settings_api_key->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_api_key->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_max_tokens->connect(SceneStringName(value_changed), callable_mp(this, &YeetAISettingsPanel::_commit_spinbox_changed));
	settings_temperature->connect(SceneStringName(value_changed), callable_mp(this, &YeetAISettingsPanel::_commit_spinbox_changed));
	settings_max_tool_round_trips->connect(SceneStringName(value_changed), callable_mp(this, &YeetAISettingsPanel::_commit_spinbox_changed));
	settings_vision_enabled->connect(SceneStringName(toggled), callable_mp(this, &YeetAISettingsPanel::_commit_checkbox_toggled));
	settings_vision_default_max_dimension->connect(SceneStringName(value_changed), callable_mp(this, &YeetAISettingsPanel::_commit_spinbox_changed));
	settings_allow_project_settings_write->connect(SceneStringName(toggled), callable_mp(this, &YeetAISettingsPanel::_commit_checkbox_toggled));
	settings_allow_editor_settings_write->connect(SceneStringName(toggled), callable_mp(this, &YeetAISettingsPanel::_commit_checkbox_toggled));
	settings_extended_read_extensions->connect(SceneStringName(toggled), callable_mp(this, &YeetAISettingsPanel::_commit_checkbox_toggled));
	settings_allow_reimport->connect(SceneStringName(toggled), callable_mp(this, &YeetAISettingsPanel::_commit_checkbox_toggled));
	settings_allow_resource_save->connect(SceneStringName(toggled), callable_mp(this, &YeetAISettingsPanel::_commit_checkbox_toggled));
	settings_allow_replace_in_files->connect(SceneStringName(toggled), callable_mp(this, &YeetAISettingsPanel::_commit_checkbox_toggled));
	settings_max_base64_chars->connect(SceneStringName(value_changed), callable_mp(this, &YeetAISettingsPanel::_commit_spinbox_changed));
	settings_game_screenshot_timeout_ms->connect(SceneStringName(value_changed), callable_mp(this, &YeetAISettingsPanel::_commit_spinbox_changed));

	settings_azure_endpoint->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_azure_endpoint->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_azure_deployment->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_azure_deployment->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_azure_api_version->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_azure_api_version->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_azure_api_key->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_azure_api_key->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
	settings_azure_model->connect(SceneStringName(text_submitted), callable_mp(this, &YeetAISettingsPanel::_commit_text_submitted));
	settings_azure_model->connect("focus_exited", callable_mp(this, &YeetAISettingsPanel::_commit_to_settings));
}

void YeetAISettingsPanel::_rebuild_openrouter_model_dropdown(const Vector<String> &p_api_slugs) {
	ERR_FAIL_NULL(settings_openrouter_model_choice);

	settings_committing = true;
	settings_openrouter_model_choice->clear();

	HashSet<String> seen;
	const int n_pinned = int(sizeof(k_openrouter_pinned_slug) / sizeof(k_openrouter_pinned_slug[0]));
	for (int i = 0; i < n_pinned; i++) {
		const String slug = String::utf8(k_openrouter_pinned_slug[i]);
		seen.insert(slug);
		String friendly;
		if (slug == "qwen/qwen3.6-plus:free") {
			friendly = TTR("Qwen 3.6 Plus (free)");
		} else if (slug == "qwen/qwen3.6-plus-preview:free") {
			friendly = TTR("Qwen 3.6 Plus Preview (free)");
		} else {
			friendly = slug;
		}
		const String label = vformat("%s — %s", friendly, slug);
		const int idx = settings_openrouter_model_choice->get_item_count();
		settings_openrouter_model_choice->add_item(label);
		settings_openrouter_model_choice->set_item_metadata(idx, slug);
	}

	Vector<String> extras;
	for (int i = 0; i < p_api_slugs.size(); i++) {
		const String s = p_api_slugs[i];
		if (!seen.has(s)) {
			extras.push_back(s);
		}
	}
	extras.sort();
	for (const String &slug : extras) {
		const int idx = settings_openrouter_model_choice->get_item_count();
		settings_openrouter_model_choice->add_item(slug);
		settings_openrouter_model_choice->set_item_metadata(idx, slug);
	}

	const int idx_custom = settings_openrouter_model_choice->get_item_count();
	settings_openrouter_model_choice->add_item(TTR("Custom…"));
	settings_openrouter_model_choice->set_item_metadata(idx_custom, String(k_openrouter_custom_meta));

	settings_committing = false;
}

void YeetAISettingsPanel::_rebuild_gemini_model_dropdown(const Vector<String> &p_api_model_ids) {
	ERR_FAIL_NULL(settings_gemini_model_choice);

	settings_committing = true;
	settings_gemini_model_choice->clear();

	HashSet<String> seen;
	const int n_pinned = int(sizeof(k_gemini_pinned_slug) / sizeof(k_gemini_pinned_slug[0]));
	for (int i = 0; i < n_pinned; i++) {
		const String slug = String::utf8(k_gemini_pinned_slug[i]);
		seen.insert(slug);
		const String label = vformat("%s — %s", TTR("Popular"), slug);
		const int idx = settings_gemini_model_choice->get_item_count();
		settings_gemini_model_choice->add_item(label);
		settings_gemini_model_choice->set_item_metadata(idx, slug);
	}

	Vector<String> extras;
	for (int i = 0; i < p_api_model_ids.size(); i++) {
		const String s = p_api_model_ids[i];
		if (!seen.has(s) && !s.strip_edges().is_empty()) {
			extras.push_back(s);
		}
	}
	extras.sort();
	for (const String &slug : extras) {
		seen.insert(slug);
		const int idx = settings_gemini_model_choice->get_item_count();
		settings_gemini_model_choice->add_item(slug);
		settings_gemini_model_choice->set_item_metadata(idx, slug);
	}

	const int idx_custom = settings_gemini_model_choice->get_item_count();
	settings_gemini_model_choice->add_item(TTR("Custom…"));
	settings_gemini_model_choice->set_item_metadata(idx_custom, String(k_gemini_custom_meta));

	settings_committing = false;
}

void YeetAISettingsPanel::_rebuild_yeet_model_dropdown(const Vector<String> &p_model_ids) {
	ERR_FAIL_NULL(settings_yeet_model_choice);

	settings_committing = true;
	settings_yeet_model_choice->clear();

	Vector<String> sorted = p_model_ids;
	sorted.sort();
	HashSet<String> seen;
	for (const String &slug : sorted) {
		if (slug.strip_edges().is_empty() || seen.has(slug)) {
			continue;
		}
		seen.insert(slug);
		const int idx = settings_yeet_model_choice->get_item_count();
		settings_yeet_model_choice->add_item(slug);
		settings_yeet_model_choice->set_item_metadata(idx, slug);
	}

	const int idx_custom = settings_yeet_model_choice->get_item_count();
	settings_yeet_model_choice->add_item(TTR("Custom…"));
	settings_yeet_model_choice->set_item_metadata(idx_custom, String(k_yeet_custom_meta));

	settings_committing = false;
}

void YeetAISettingsPanel::_fetch_openrouter_models_list() {
	ERR_FAIL_NULL(openrouter_models_http);
	ERR_FAIL_NULL(settings_openrouter_models_refresh);

	if (openrouter_models_http->get_http_client_status() != HTTPClient::STATUS_DISCONNECTED) {
		openrouter_models_http->cancel_request();
	}

	settings_openrouter_models_refresh->set_disabled(true);
	settings_openrouter_models_refresh->set_text(TTR("Loading…"));

	Vector<String> headers;
	headers.push_back("Accept: application/json");
	const String key = settings_openrouter_api_key->get_text().strip_edges();
	if (!key.is_empty()) {
		headers.push_back("Authorization: Bearer " + key);
	}

	const Error err = openrouter_models_http->request(
			"https://openrouter.ai/api/v1/models",
			headers,
			HTTPClient::METHOD_GET);

	if (err != OK) {
		WARN_PRINT("OpenRouter model list: failed to start HTTP request.");
		settings_openrouter_models_refresh->set_disabled(false);
		settings_openrouter_models_refresh->set_text(TTR("Refresh model list"));
	}
}

void YeetAISettingsPanel::_fetch_yeet_models_list() {
	ERR_FAIL_NULL(yeet_models_http);
	ERR_FAIL_NULL(settings_yeet_models_refresh);
	ERR_FAIL_NULL(settings_yeet_tags_url);

	if (yeet_models_http->get_http_client_status() != HTTPClient::STATUS_DISCONNECTED) {
		yeet_models_http->cancel_request();
	}

	settings_yeet_models_refresh->set_disabled(true);
	settings_yeet_models_refresh->set_text(TTR("Loading…"));

	Vector<String> headers;
	headers.push_back("Accept: application/json");
	const String key = settings_yeet_api_key->get_text().strip_edges();
	if (!key.is_empty()) {
		headers.push_back("Authorization: Bearer " + key);
	}

	String tags_url = settings_yeet_tags_url->get_text().strip_edges();
	if (tags_url.is_empty()) {
		tags_url = "https://gpt.yeetlabs.fun/api/tags";
	}

	const Error err = yeet_models_http->request(
			tags_url,
			headers,
			HTTPClient::METHOD_GET);

	if (err != OK) {
		WARN_PRINT("Yeet model list: failed to start HTTP request.");
		settings_yeet_models_refresh->set_disabled(false);
		settings_yeet_models_refresh->set_text(TTR("Refresh model list"));
	}
}

void YeetAISettingsPanel::_on_openrouter_models_http_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	(void)p_headers;
	ERR_FAIL_NULL(settings_openrouter_models_refresh);
	settings_openrouter_models_refresh->set_disabled(false);
	settings_openrouter_models_refresh->set_text(TTR("Refresh model list"));

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code < 200 || p_response_code >= 300) {
		WARN_PRINT(vformat("OpenRouter model list: HTTP error (result=%d, code=%d).", p_result, p_response_code));
		return;
	}

	const String body_text = String::utf8(reinterpret_cast<const char *>(p_body.ptr()), p_body.size());
	const Variant parsed = JSON::parse_string(body_text);
	if (parsed.get_type() != Variant::DICTIONARY) {
		WARN_PRINT("OpenRouter model list: response was not a JSON object.");
		return;
	}

	const Dictionary root = parsed;
	if (!root.has("data")) {
		WARN_PRINT("OpenRouter model list: missing \"data\" array.");
		return;
	}

	const Array openrouter_data = root["data"];
	Vector<String> slugs;
	for (int i = 0; i < openrouter_data.size(); i++) {
		const Variant &row = openrouter_data[i];
		if (row.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary item = row;
		if (!item.has("id")) {
			continue;
		}
		slugs.push_back(String(item["id"]));
	}

	const String preserved = _get_setting_string("yeet_ai/chat/model", "");
	_rebuild_openrouter_model_dropdown(slugs);
	_sync_openrouter_model_ui_from_slug(preserved);
}

void YeetAISettingsPanel::_on_yeet_models_http_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	(void)p_headers;
	ERR_FAIL_NULL(settings_yeet_models_refresh);
	settings_yeet_models_refresh->set_disabled(false);
	settings_yeet_models_refresh->set_text(TTR("Refresh model list"));

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code < 200 || p_response_code >= 300) {
		WARN_PRINT(vformat("Yeet model list: HTTP error (result=%d, code=%d).", p_result, p_response_code));
		return;
	}

	const String body_text = String::utf8(reinterpret_cast<const char *>(p_body.ptr()), p_body.size());
	const Variant parsed = JSON::parse_string(body_text);
	if (parsed.get_type() != Variant::DICTIONARY) {
		WARN_PRINT("Yeet model list: response was not a JSON object.");
		return;
	}

	const Dictionary root = parsed;
	if (!root.has("models")) {
		WARN_PRINT("Yeet model list: missing \"models\" array.");
		return;
	}

	const Array models = root["models"];
	Vector<String> ids;
	for (int i = 0; i < models.size(); i++) {
		const Variant &row = models[i];
		if (row.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary item = row;
		if (item.has("model")) {
			ids.push_back(String(item["model"]));
		} else if (item.has("name")) {
			ids.push_back(String(item["name"]));
		}
	}

	const String preserved = _get_setting_string("yeet_ai/chat/model", "");
	_rebuild_yeet_model_dropdown(ids);
	_sync_yeet_model_ui_from_slug(preserved);
}

void YeetAISettingsPanel::_fetch_gemini_models_list() {
	ERR_FAIL_NULL(gemini_models_http);
	ERR_FAIL_NULL(settings_gemini_models_refresh);
	ERR_FAIL_NULL(settings_gemini_api_key);

	if (gemini_models_http->get_http_client_status() != HTTPClient::STATUS_DISCONNECTED) {
		gemini_models_http->cancel_request();
	}

	const String key = settings_gemini_api_key->get_text().strip_edges();
	if (key.is_empty()) {
		WARN_PRINT("Gemini model list: set Gemini API key first.");
		return;
	}

	settings_gemini_models_refresh->set_disabled(true);
	settings_gemini_models_refresh->set_text(TTR("Loading…"));

	Vector<String> headers;
	headers.push_back("Accept: application/json");
	const String url = "https://generativelanguage.googleapis.com/v1beta/models?pageSize=100&key=" + key.uri_encode();

	const Error err = gemini_models_http->request(
			url,
			headers,
			HTTPClient::METHOD_GET);

	if (err != OK) {
		WARN_PRINT("Gemini model list: failed to start HTTP request.");
		settings_gemini_models_refresh->set_disabled(false);
		settings_gemini_models_refresh->set_text(TTR("Refresh model list"));
	}
}

void YeetAISettingsPanel::_on_gemini_models_http_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	(void)p_headers;
	ERR_FAIL_NULL(settings_gemini_models_refresh);
	settings_gemini_models_refresh->set_disabled(false);
	settings_gemini_models_refresh->set_text(TTR("Refresh model list"));

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code < 200 || p_response_code >= 300) {
		WARN_PRINT(vformat("Gemini model list: HTTP error (result=%d, code=%d).", p_result, p_response_code));
		return;
	}

	const String body_text = String::utf8(reinterpret_cast<const char *>(p_body.ptr()), p_body.size());
	const Variant parsed = JSON::parse_string(body_text);
	if (parsed.get_type() != Variant::DICTIONARY) {
		WARN_PRINT("Gemini model list: response was not a JSON object.");
		return;
	}

	const Dictionary root = parsed;
	if (!root.has("models")) {
		WARN_PRINT("Gemini model list: missing \"models\" array.");
		return;
	}

	const Array models = root["models"];
	Vector<String> ids;
	for (int i = 0; i < models.size(); i++) {
		const Variant &row = models[i];
		if (row.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary item = row;
		if (!item.has("name")) {
			continue;
		}
		if (!_gemini_item_supports_chat(item)) {
			continue;
		}
		const String short_id = _gemini_strip_models_prefix(String(item["name"]));
		const String lower = short_id.to_lower();
		if (lower.find("embed") >= 0) {
			continue;
		}
		if (!short_id.strip_edges().is_empty()) {
			ids.push_back(short_id);
		}
	}

	const String preserved = _get_setting_string("yeet_ai/chat/model", "");
	_rebuild_gemini_model_dropdown(ids);
	_sync_gemini_model_ui_from_slug(preserved);
}

void YeetAISettingsPanel::_sync_gemini_model_ui_from_slug(const String &p_slug) {
	ERR_FAIL_NULL(settings_gemini_model_choice);
	ERR_FAIL_NULL(settings_gemini_model_custom);

	settings_committing = true;

	const String m = p_slug.strip_edges();
	for (int i = 0; i < settings_gemini_model_choice->get_item_count(); i++) {
		const Variant meta = settings_gemini_model_choice->get_item_metadata(i);
		if (meta.get_type() != Variant::STRING) {
			continue;
		}
		const String s = meta;
		if (s == String(k_gemini_custom_meta)) {
			continue;
		}
		if (s == m) {
			settings_gemini_model_choice->select(i);
			settings_gemini_model_custom->set_visible(false);
			settings_committing = false;
			return;
		}
	}

	for (int i = 0; i < settings_gemini_model_choice->get_item_count(); i++) {
		const Variant meta = settings_gemini_model_choice->get_item_metadata(i);
		if (meta.get_type() == Variant::STRING && meta.operator String() == String(k_gemini_custom_meta)) {
			settings_gemini_model_choice->select(i);
			settings_gemini_model_custom->set_text(m);
			settings_gemini_model_custom->set_visible(true);
			break;
		}
	}

	settings_committing = false;
}

void YeetAISettingsPanel::_sync_openrouter_model_ui_from_slug(const String &p_slug) {
	ERR_FAIL_NULL(settings_openrouter_model_choice);
	ERR_FAIL_NULL(settings_openrouter_model_custom);

	settings_committing = true;

	const String m = p_slug.strip_edges();
	for (int i = 0; i < settings_openrouter_model_choice->get_item_count(); i++) {
		const Variant meta = settings_openrouter_model_choice->get_item_metadata(i);
		if (meta.get_type() != Variant::STRING) {
			continue;
		}
		const String s = meta;
		if (s == String(k_openrouter_custom_meta)) {
			continue;
		}
		if (s == m) {
			settings_openrouter_model_choice->select(i);
			settings_openrouter_model_custom->set_visible(false);
			settings_committing = false;
			return;
		}
	}

	for (int i = 0; i < settings_openrouter_model_choice->get_item_count(); i++) {
		const Variant meta = settings_openrouter_model_choice->get_item_metadata(i);
		if (meta.get_type() == Variant::STRING && meta.operator String() == String(k_openrouter_custom_meta)) {
			settings_openrouter_model_choice->select(i);
			settings_openrouter_model_custom->set_text(m);
			settings_openrouter_model_custom->set_visible(true);
			break;
		}
	}

	settings_committing = false;
}

void YeetAISettingsPanel::_sync_yeet_model_ui_from_slug(const String &p_slug) {
	ERR_FAIL_NULL(settings_yeet_model_choice);
	ERR_FAIL_NULL(settings_yeet_model_custom);

	settings_committing = true;

	const String m = p_slug.strip_edges();
	for (int i = 0; i < settings_yeet_model_choice->get_item_count(); i++) {
		const Variant meta = settings_yeet_model_choice->get_item_metadata(i);
		if (meta.get_type() != Variant::STRING) {
			continue;
		}
		const String s = meta;
		if (s == String(k_yeet_custom_meta)) {
			continue;
		}
		if (s == m) {
			settings_yeet_model_choice->select(i);
			settings_yeet_model_custom->set_visible(false);
			settings_committing = false;
			return;
		}
	}

	for (int i = 0; i < settings_yeet_model_choice->get_item_count(); i++) {
		const Variant meta = settings_yeet_model_choice->get_item_metadata(i);
		if (meta.get_type() == Variant::STRING && meta.operator String() == String(k_yeet_custom_meta)) {
			settings_yeet_model_choice->select(i);
			settings_yeet_model_custom->set_text(m);
			settings_yeet_model_custom->set_visible(true);
			break;
		}
	}

	settings_committing = false;
}

void YeetAISettingsPanel::_update_openrouter_custom_line_visibility() {
	ERR_FAIL_NULL(settings_openrouter_model_choice);
	ERR_FAIL_NULL(settings_openrouter_model_custom);
	const int idx = settings_openrouter_model_choice->get_selected();
	if (idx < 0) {
		settings_openrouter_model_custom->set_visible(true);
		return;
	}
	const Variant meta = settings_openrouter_model_choice->get_item_metadata(idx);
	const bool is_custom = (meta.get_type() == Variant::STRING && meta.operator String() == String(k_openrouter_custom_meta));
	settings_openrouter_model_custom->set_visible(is_custom);
}

void YeetAISettingsPanel::_update_yeet_custom_line_visibility() {
	ERR_FAIL_NULL(settings_yeet_model_choice);
	ERR_FAIL_NULL(settings_yeet_model_custom);
	const int idx = settings_yeet_model_choice->get_selected();
	if (idx < 0) {
		settings_yeet_model_custom->set_visible(true);
		return;
	}
	const Variant meta = settings_yeet_model_choice->get_item_metadata(idx);
	const bool is_custom = (meta.get_type() == Variant::STRING && meta.operator String() == String(k_yeet_custom_meta));
	settings_yeet_model_custom->set_visible(is_custom);
}

void YeetAISettingsPanel::_update_gemini_custom_line_visibility() {
	ERR_FAIL_NULL(settings_gemini_model_choice);
	ERR_FAIL_NULL(settings_gemini_model_custom);
	const int idx = settings_gemini_model_choice->get_selected();
	if (idx < 0) {
		settings_gemini_model_custom->set_visible(true);
		return;
	}
	const Variant meta = settings_gemini_model_choice->get_item_metadata(idx);
	const bool is_custom = (meta.get_type() == Variant::STRING && meta.operator String() == String(k_gemini_custom_meta));
	settings_gemini_model_custom->set_visible(is_custom);
}

void YeetAISettingsPanel::_on_openrouter_model_selected(int p_index) {
	(void)p_index;
	_update_openrouter_custom_line_visibility();
	_commit_to_settings();
}

void YeetAISettingsPanel::_on_yeet_model_selected(int p_index) {
	(void)p_index;
	_update_yeet_custom_line_visibility();
	_commit_to_settings();
}

void YeetAISettingsPanel::_on_gemini_model_selected(int p_index) {
	(void)p_index;
	_update_gemini_custom_line_visibility();
	_commit_to_settings();
}

void YeetAISettingsPanel::_on_openrouter_model_custom_changed(const String &p_new_text) {
	(void)p_new_text;
	_commit_to_settings();
}

void YeetAISettingsPanel::_on_yeet_model_custom_changed(const String &p_new_text) {
	(void)p_new_text;
	_commit_to_settings();
}

void YeetAISettingsPanel::_on_gemini_model_custom_changed(const String &p_new_text) {
	(void)p_new_text;
	_commit_to_settings();
}

String YeetAISettingsPanel::_get_gemini_model_slug_from_ui() const {
	ERR_FAIL_COND_V(settings_gemini_model_choice == nullptr, String());
	ERR_FAIL_COND_V(settings_gemini_model_custom == nullptr, String());

	const int idx = settings_gemini_model_choice->get_selected();
	if (idx < 0) {
		return settings_gemini_model_custom->get_text().strip_edges();
	}
	const Variant meta = settings_gemini_model_choice->get_item_metadata(idx);
	if (meta.get_type() == Variant::STRING && meta.operator String() == String(k_gemini_custom_meta)) {
		return settings_gemini_model_custom->get_text().strip_edges();
	}
	if (meta.get_type() == Variant::STRING) {
		return meta.operator String();
	}
	return String();
}

String YeetAISettingsPanel::_get_openrouter_model_slug_from_ui() const {
	ERR_FAIL_COND_V(settings_openrouter_model_choice == nullptr, String());
	ERR_FAIL_COND_V(settings_openrouter_model_custom == nullptr, String());

	const int idx = settings_openrouter_model_choice->get_selected();
	if (idx < 0) {
		return settings_openrouter_model_custom->get_text().strip_edges();
	}
	const Variant meta = settings_openrouter_model_choice->get_item_metadata(idx);
	if (meta.get_type() == Variant::STRING && meta.operator String() == String(k_openrouter_custom_meta)) {
		return settings_openrouter_model_custom->get_text().strip_edges();
	}
	if (meta.get_type() == Variant::STRING) {
		return meta.operator String();
	}
	return String();
}

String YeetAISettingsPanel::_get_yeet_model_slug_from_ui() const {
	ERR_FAIL_COND_V(settings_yeet_model_choice == nullptr, String());
	ERR_FAIL_COND_V(settings_yeet_model_custom == nullptr, String());

	const int idx = settings_yeet_model_choice->get_selected();
	if (idx < 0) {
		return settings_yeet_model_custom->get_text().strip_edges();
	}
	const Variant meta = settings_yeet_model_choice->get_item_metadata(idx);
	if (meta.get_type() == Variant::STRING && meta.operator String() == String(k_yeet_custom_meta)) {
		return settings_yeet_model_custom->get_text().strip_edges();
	}
	if (meta.get_type() == Variant::STRING) {
		return meta.operator String();
	}
	return String();
}

String YeetAISettingsPanel::_get_model_value_for_commit() const {
	ERR_FAIL_COND_V(settings_provider == nullptr, String());
	const int id = settings_provider->get_selected_id();
	if (id == 1) {
		return _get_gemini_model_slug_from_ui();
	}
	if (id == 2) {
		return _get_openrouter_model_slug_from_ui();
	}
	if (id == 3) {
		return _get_yeet_model_slug_from_ui();
	}
	if (id == 4) {
		return settings_azure_model->get_text();
	}
	return String::utf8(k_berry_model_id);
}

String YeetAISettingsPanel::_get_setting_string(const String &p_setting, const String &p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	// Use get(), not get_setting(): get_setting() merges ProjectSettings editor overrides per project,
	// which hides globally stored keys when opening another project.
	bool valid = false;
	const Variant v = settings->get(StringName(p_setting), &valid);
	if (!valid) {
		return p_default;
	}
	return String(v);
}

int YeetAISettingsPanel::_get_setting_int(const String &p_setting, int p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	bool valid = false;
	const Variant v = settings->get(StringName(p_setting), &valid);
	if (!valid) {
		return p_default;
	}
	return int(v);
}

float YeetAISettingsPanel::_get_setting_float(const String &p_setting, float p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	bool valid = false;
	const Variant v = settings->get(StringName(p_setting), &valid);
	if (!valid) {
		return p_default;
	}
	return float(v);
}

bool YeetAISettingsPanel::_get_setting_bool(const String &p_setting, bool p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings == nullptr || !settings->has_setting(p_setting)) {
		return p_default;
	}
	bool valid = false;
	const Variant v = settings->get(StringName(p_setting), &valid);
	if (!valid) {
		return p_default;
	}
	return bool(v);
}

void YeetAISettingsPanel::reload_from_settings() {
	_load_from_settings();
}

void YeetAISettingsPanel::_load_from_settings() {
	EditorSettings *s = EditorSettings::get_singleton();
	if (s == nullptr) {
		return;
	}
	ERR_FAIL_NULL(settings_enabled);

	settings_committing = true;

	settings_enabled->set_pressed_no_signal(_get_setting_bool("yeet_ai/enabled", true));

	const int prov = _get_setting_int("yeet_ai/chat/provider", 0);
	const int prov_idx = settings_provider->get_item_index(prov);
	settings_provider->select(prov_idx >= 0 ? prov_idx : 0);
	cached_provider_id = settings_provider->get_selected_id();

	settings_completions_url->set_text(_get_setting_string("yeet_ai/chat/completions_url", ""));
	const String model_setting = _get_setting_string("yeet_ai/chat/model", "");
	settings_model->set_text(String::utf8(k_berry_model_id));
	settings_api_key->set_text(_get_setting_string("yeet_ai/chat/api_key", ""));
	settings_gemini_api_key->set_text(_get_setting_string("yeet_ai/chat/gemini_api_key", ""));
	settings_openrouter_api_key->set_text(_get_setting_string("yeet_ai/chat/openrouter_api_key", ""));
	settings_yeet_chat_url->set_text(_get_setting_string("yeet_ai/chat/yeet_chat_url", "https://gpt.yeetlabs.fun/v1/chat/completions"));
	settings_yeet_tags_url->set_text(_get_setting_string("yeet_ai/chat/yeet_tags_url", "https://gpt.yeetlabs.fun/api/tags"));
	settings_yeet_api_key->set_text(_get_setting_string("yeet_ai/chat/yeet_api_key", ""));
	settings_azure_endpoint->set_text(_get_setting_string("yeet_ai/chat/azure_endpoint", "https://crosshair-resource.services.ai.azure.com"));
	settings_azure_deployment->set_text(_get_setting_string("yeet_ai/chat/azure_deployment", ""));
	settings_azure_api_version->set_text(_get_setting_string("yeet_ai/chat/azure_api_version", "2024-05-01-preview"));
	settings_azure_api_key->set_text(_get_setting_string("yeet_ai/chat/azure_api_key", ""));
	settings_azure_model->set_text(_get_setting_string("yeet_ai/chat/model", "gpt-4o"));
	_update_provider_blocks();
	if (cached_provider_id == 1) {
		_sync_gemini_model_ui_from_slug(model_setting);
	}
	if (cached_provider_id == 2) {
		_sync_openrouter_model_ui_from_slug(model_setting);
	}
	if (cached_provider_id == 3) {
		_sync_yeet_model_ui_from_slug(model_setting);
	}
	if (cached_provider_id == 4) {
		settings_azure_model->set_text(model_setting);
	}
	settings_max_tokens->set_value_no_signal(_get_setting_int("yeet_ai/chat/max_tokens", 32768));
	settings_temperature->set_value_no_signal((double)_get_setting_float("yeet_ai/chat/temperature", 0.25f));
	settings_max_tool_round_trips->set_value_no_signal(_get_setting_int("yeet_ai/chat/max_tool_round_trips", 100));
	settings_vision_enabled->set_pressed_no_signal(_get_setting_bool("yeet_ai/chat/vision_enabled", false));
	settings_vision_default_max_dimension->set_value_no_signal(_get_setting_int("yeet_ai/chat/vision_default_max_dimension", 1280));
	settings_allow_project_settings_write->set_pressed_no_signal(_get_setting_bool("yeet_ai/tools/allow_project_settings_write", true));
	settings_allow_editor_settings_write->set_pressed_no_signal(_get_setting_bool("yeet_ai/tools/allow_editor_settings_write", false));
	settings_extended_read_extensions->set_pressed_no_signal(_get_setting_bool("yeet_ai/tools/extended_read_extensions", false));
	settings_allow_reimport->set_pressed_no_signal(_get_setting_bool("yeet_ai/tools/allow_reimport", true));
	settings_allow_resource_save->set_pressed_no_signal(_get_setting_bool("yeet_ai/tools/allow_resource_save", true));
	settings_allow_replace_in_files->set_pressed_no_signal(_get_setting_bool("yeet_ai/tools/allow_replace_in_files", false));
	settings_max_base64_chars->set_value_no_signal(_get_setting_int("yeet_ai/chat/max_base64_chars", 2000000));
	settings_game_screenshot_timeout_ms->set_value_no_signal(_get_setting_int("yeet_ai/chat/game_screenshot_timeout_ms", 8000));

	settings_committing = false;
}

void YeetAISettingsPanel::_commit_to_settings() {
	if (settings_committing) {
		return;
	}
	EditorSettings *s = EditorSettings::get_singleton();
	if (s == nullptr) {
		return;
	}
	ERR_FAIL_NULL(settings_enabled);

	settings_committing = true;
	s->set_setting("yeet_ai/enabled", settings_enabled->is_pressed());
	s->set_setting("yeet_ai/chat/provider", int(settings_provider->get_selected_id()));
	s->set_setting("yeet_ai/chat/completions_url", settings_completions_url->get_text());
	s->set_setting("yeet_ai/chat/model", _get_model_value_for_commit());
	s->set_setting("yeet_ai/chat/api_key", settings_api_key->get_text());
	s->set_setting("yeet_ai/chat/gemini_api_key", settings_gemini_api_key->get_text());
	s->set_setting("yeet_ai/chat/openrouter_api_key", settings_openrouter_api_key->get_text());
	s->set_setting("yeet_ai/chat/yeet_chat_url", settings_yeet_chat_url->get_text());
	s->set_setting("yeet_ai/chat/yeet_tags_url", settings_yeet_tags_url->get_text());
	s->set_setting("yeet_ai/chat/yeet_api_key", settings_yeet_api_key->get_text());
	s->set_setting("yeet_ai/chat/azure_endpoint", settings_azure_endpoint->get_text());
	s->set_setting("yeet_ai/chat/azure_deployment", settings_azure_deployment->get_text());
	s->set_setting("yeet_ai/chat/azure_api_version", settings_azure_api_version->get_text());
	s->set_setting("yeet_ai/chat/azure_api_key", settings_azure_api_key->get_text());
	s->set_setting("yeet_ai/chat/max_tokens", int(settings_max_tokens->get_value()));
	s->set_setting("yeet_ai/chat/temperature", float(settings_temperature->get_value()));
	s->set_setting("yeet_ai/chat/max_tool_round_trips", int(settings_max_tool_round_trips->get_value()));
	s->set_setting("yeet_ai/chat/vision_enabled", settings_vision_enabled->is_pressed());
	s->set_setting("yeet_ai/chat/vision_default_max_dimension", int(settings_vision_default_max_dimension->get_value()));
	s->set_setting("yeet_ai/tools/allow_project_settings_write", settings_allow_project_settings_write->is_pressed());
	s->set_setting("yeet_ai/tools/allow_editor_settings_write", settings_allow_editor_settings_write->is_pressed());
	s->set_setting("yeet_ai/tools/extended_read_extensions", settings_extended_read_extensions->is_pressed());
	s->set_setting("yeet_ai/tools/allow_reimport", settings_allow_reimport->is_pressed());
	s->set_setting("yeet_ai/tools/allow_resource_save", settings_allow_resource_save->is_pressed());
	s->set_setting("yeet_ai/tools/allow_replace_in_files", settings_allow_replace_in_files->is_pressed());
	s->set_setting("yeet_ai/chat/max_base64_chars", int(settings_max_base64_chars->get_value()));
	s->set_setting("yeet_ai/chat/game_screenshot_timeout_ms", int(settings_game_screenshot_timeout_ms->get_value()));
	EditorSettings::save();
	settings_committing = false;
}

void YeetAISettingsPanel::_on_external_settings_changed() {
	if (settings_committing) {
		return;
	}
	_load_from_settings();
}

void YeetAISettingsPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		EditorSettings *s = EditorSettings::get_singleton();
		if (s != nullptr) {
			s->connect("settings_changed", callable_mp(this, &YeetAISettingsPanel::_on_external_settings_changed));
		}
		_load_from_settings();
		const int pid = settings_provider->get_selected_id();
		if (pid == 1) {
			_fetch_gemini_models_list();
		} else if (pid == 2) {
			_fetch_openrouter_models_list();
		} else if (pid == 3) {
			_fetch_yeet_models_list();
		}
	}
	if (p_what == NOTIFICATION_EXIT_TREE) {
		if (gemini_models_http != nullptr && gemini_models_http->get_http_client_status() != HTTPClient::STATUS_DISCONNECTED) {
			gemini_models_http->cancel_request();
		}
		if (openrouter_models_http != nullptr && openrouter_models_http->get_http_client_status() != HTTPClient::STATUS_DISCONNECTED) {
			openrouter_models_http->cancel_request();
		}
		if (yeet_models_http != nullptr && yeet_models_http->get_http_client_status() != HTTPClient::STATUS_DISCONNECTED) {
			yeet_models_http->cancel_request();
		}
	EditorSettings *s = EditorSettings::get_singleton();
	if (s != nullptr && s->is_connected("settings_changed", callable_mp(this, &YeetAISettingsPanel::_on_external_settings_changed))) {
		s->disconnect("settings_changed", callable_mp(this, &YeetAISettingsPanel::_on_external_settings_changed));
	}
}
}

void YeetAISettingsPanel::_commit_text_submitted(const String &p_text) {
	(void)p_text;
	_commit_to_settings();
}

void YeetAISettingsPanel::_commit_spinbox_changed(double p_value) {
	(void)p_value;
	_commit_to_settings();
}

void YeetAISettingsPanel::_commit_checkbox_toggled(bool p_pressed) {
	(void)p_pressed;
	_commit_to_settings();
}
