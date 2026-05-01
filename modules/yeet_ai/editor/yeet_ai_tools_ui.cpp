/**************************************************************************/
/*  yeet_ai_tools_ui.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/resource_loader.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/nine_patch_rect.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/texture_progress_bar.h"
#include "scene/gui/texture_rect.h"

// ── Helper to parse color from args ──────────────────────────────────────────
static Color _parse_color_arg(const Dictionary &p_args, const String &p_key, const Color &p_default) {
	if (!p_args.has(p_key)) {
		return p_default;
	}
	const Variant v = p_args[p_key];
	if (v.get_type() == Variant::STRING) {
		const String s = String(v);
		if (s.begins_with("#")) {
			return Color(s);
		}
	}
	if (v.get_type() == Variant::DICTIONARY) {
		const Dictionary d = v;
		return Color(d.get("r", 1.0), d.get("g", 1.0), d.get("b", 1.0), d.get("a", 1.0));
	}
	return p_default;
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 1: Essential UI Display
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_create_texture_rect(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "TextureRect");

	TextureRect *tr = memnew(TextureRect);
	tr->set_name(node_name);

	const String texture_path = _arg_string(p_args, "texture_path", "");
	if (!texture_path.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(texture_path);
		if (tex.is_valid()) {
			tr->set_texture(tex);
		}
	}

	// Stretch mode: 0=scale, 1=tile, 2=keep, 3=keep_centered, 4=keep_aspect, 5=keep_aspect_centered, 6=keep_aspect_covered
	const int stretch_mode = _arg_int(p_args, "stretch_mode", 0);
	tr->set_stretch_mode(static_cast<TextureRect::StretchMode>(stretch_mode));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, tr, scene_root);
	tr->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	tr->set_size(Vector2(_arg_float(p_args, "width", 64.0), _arg_float(p_args, "height", 64.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(tr->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_nine_patch_rect(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "NinePatchRect");

	NinePatchRect *npr = memnew(NinePatchRect);
	npr->set_name(node_name);

	const String texture_path = _arg_string(p_args, "texture_path", "");
	if (!texture_path.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(texture_path);
		if (tex.is_valid()) {
			npr->set_texture(tex);
		}
	}

	// Region rect
	Dictionary region = _arg_dict(p_args, "region_rect");
	if (!region.is_empty()) {
		npr->set_region_rect(Rect2(
			region.get("x", 0.0), region.get("y", 0.0),
			region.get("w", 0.0), region.get("h", 0.0)));
	}

	// Patch margins
	Dictionary margins = _arg_dict(p_args, "patch_margin");
	if (!margins.is_empty()) {
		npr->set_patch_margin(SIDE_LEFT, margins.get("left", 0));
		npr->set_patch_margin(SIDE_TOP, margins.get("top", 0));
		npr->set_patch_margin(SIDE_RIGHT, margins.get("right", 0));
		npr->set_patch_margin(SIDE_BOTTOM, margins.get("bottom", 0));
	}

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, npr, scene_root);
	npr->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	npr->set_size(Vector2(_arg_float(p_args, "width", 64.0), _arg_float(p_args, "height", 64.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(npr->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_color_rect(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "ColorRect");

	ColorRect *cr = memnew(ColorRect);
	cr->set_name(node_name);
	cr->set_color(_parse_color_arg(p_args, "color", Color(1, 1, 1)));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, cr, scene_root);
	cr->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	cr->set_size(Vector2(_arg_float(p_args, "width", 64.0), _arg_float(p_args, "height", 64.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(cr->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_rich_text_label(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "RichTextLabel");

	RichTextLabel *rtl = memnew(RichTextLabel);
	rtl->set_name(node_name);
	rtl->set_text(_arg_string(p_args, "text", ""));
	rtl->set_fit_content(_arg_bool(p_args, "fit_content", false));
	rtl->set_scroll_active(_arg_bool(p_args, "scroll_active", true));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, rtl, scene_root);
	rtl->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	rtl->set_size(Vector2(_arg_float(p_args, "width", 200.0), _arg_float(p_args, "height", 100.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(rtl->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_texture_progress_bar(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "TextureProgressBar");

	TextureProgressBar *tpb = memnew(TextureProgressBar);
	tpb->set_name(node_name);

	// Under texture
	const String under_path = _arg_string(p_args, "texture_under", "");
	if (!under_path.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(under_path);
		if (tex.is_valid()) {
			tpb->set_under_texture(tex);
		}
	}
	// Over texture
	const String over_path = _arg_string(p_args, "texture_over", "");
	if (!over_path.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(over_path);
		if (tex.is_valid()) {
			tpb->set_over_texture(tex);
		}
	}
	// Progress texture
	const String progress_path = _arg_string(p_args, "texture_progress", "");
	if (!progress_path.is_empty()) {
		Ref<Texture2D> tex = ResourceLoader::load(progress_path);
		if (tex.is_valid()) {
			tpb->set_progress_texture(tex);
		}
	}

	// Fill mode: 0=left-right, 1=right-left, 2=top-bottom, 3=bottom-top, 4=clockwise, 5=counter-clockwise
	const int fill_mode = _arg_int(p_args, "fill_mode", 0);
	tpb->set_fill_mode(fill_mode);

	// Value range
	tpb->set_min(_arg_float(p_args, "min", 0.0));
	tpb->set_max(_arg_float(p_args, "max", 100.0));
	tpb->set_value(_arg_float(p_args, "value", 0.0));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, tpb, scene_root);
	tpb->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	tpb->set_size(Vector2(_arg_float(p_args, "width", 64.0), _arg_float(p_args, "height", 64.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(tpb->get_path());
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 2: Input & Settings
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_create_line_edit(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "LineEdit");

	LineEdit *le = memnew(LineEdit);
	le->set_name(node_name);
	le->set_text(_arg_string(p_args, "text", ""));
	le->set_placeholder(_arg_string(p_args, "placeholder_text", ""));
	le->set_editable(_arg_bool(p_args, "editable", true));
	le->set_secret(_arg_bool(p_args, "secret", false));
	le->set_max_length(_arg_int(p_args, "max_length", 0)); // 0 = unlimited
	le->set_expand_to_text_length_enabled(_arg_bool(p_args, "expand_to_text_length", false));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, le, scene_root);
	le->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	le->set_size(Vector2(_arg_float(p_args, "width", 150.0), _arg_float(p_args, "height", 30.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(le->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_text_edit(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "TextEdit");

	TextEdit *te = memnew(TextEdit);
	te->set_name(node_name);
	te->set_text(_arg_string(p_args, "text", ""));
	te->set_placeholder(_arg_string(p_args, "placeholder_text", ""));
	te->set_editable(_arg_bool(p_args, "editable", true));
	// Wrap mode: 0=off, 1=boundary, 2=char
	const int wrap_mode = _arg_int(p_args, "wrap_mode", 0);
	te->set_line_wrapping_mode(static_cast<TextEdit::LineWrappingMode>(wrap_mode));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, te, scene_root);
	te->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	te->set_size(Vector2(_arg_float(p_args, "width", 200.0), _arg_float(p_args, "height", 100.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(te->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_check_box(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "CheckBox");

	CheckBox *cb = memnew(CheckBox);
	cb->set_name(node_name);
	cb->set_text(_arg_string(p_args, "text", ""));
	cb->set_pressed(_arg_bool(p_args, "pressed", false));
	cb->set_disabled(_arg_bool(p_args, "disabled", false));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, cb, scene_root);
	cb->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(cb->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_spin_box(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "SpinBox");

	SpinBox *sb = memnew(SpinBox);
	sb->set_name(node_name);
	sb->set_min(_arg_float(p_args, "min", 0.0));
	sb->set_max(_arg_float(p_args, "max", 100.0));
	sb->set_step(_arg_float(p_args, "step", 1.0));
	sb->set_value(_arg_float(p_args, "value", 0.0));
	sb->set_prefix(_arg_string(p_args, "prefix", ""));
	sb->set_suffix(_arg_string(p_args, "suffix", ""));
	sb->set_editable(_arg_bool(p_args, "editable", true));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, sb, scene_root);
	sb->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	sb->set_size(Vector2(_arg_float(p_args, "width", 80.0), _arg_float(p_args, "height", 30.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(sb->get_path());
	return result;
}

Dictionary YeetAIDock::_tool_create_panel_container(const Dictionary &p_args) const {
	Dictionary result;
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}
	const String node_name = _arg_string(p_args, "name", "PanelContainer");

	PanelContainer *pc = memnew(PanelContainer);
	pc->set_name(node_name);

	// Optional custom stylebox
	const String stylebox_path = _arg_string(p_args, "stylebox_path", "");
	if (!stylebox_path.is_empty()) {
		Ref<StyleBox> sb = ResourceLoader::load(stylebox_path);
		if (sb.is_valid()) {
			pc->add_theme_style_override("panel", sb);
		}
	}

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, pc, scene_root);
	pc->set_position(Vector2(_arg_float(p_args, "x", 0.0), _arg_float(p_args, "y", 0.0)));
	pc->set_size(Vector2(_arg_float(p_args, "width", 200.0), _arg_float(p_args, "height", 150.0)));

	_mark_unsaved();
	result["ok"] = true;
	result["node_path"] = String(pc->get_path());
	return result;
}
