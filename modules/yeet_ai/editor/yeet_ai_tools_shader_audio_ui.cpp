/**************************************************************************/
/*  yeet_ai_tools_shader_audio_ui.cpp                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "scene/2d/audio_stream_player_2d.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/center_container.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/graph_node.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/slider.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/texture.h"
#include "servers/audio/audio_server.h"
#include "servers/audio/effects/audio_effect_chorus.h"
#include "servers/audio/effects/audio_effect_compressor.h"
#include "servers/audio/effects/audio_effect_delay.h"
#include "servers/audio/effects/audio_effect_distortion.h"
#include "servers/audio/effects/audio_effect_eq.h"
#include "servers/audio/effects/audio_effect_limiter.h"
#include "servers/audio/effects/audio_effect_panner.h"
#include "servers/audio/effects/audio_effect_record.h"
#include "servers/audio/effects/audio_effect_reverb.h"

// ═══════════════════════════════════════════════════════════════════════════
// G. Shader Authoring
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_create_shader_material(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	const String shader_name = _arg_string(p_args, "shader_name", "NewShader");
	const String vertex_code = _arg_string(p_args, "vertex_code", "");
	const String fragment_code = _arg_string(p_args, "fragment_code", "");

	String shader_source = "shader_type spatial;\n";
	if (!vertex_code.is_empty()) {
		shader_source += "\nvertex();\n" + vertex_code + "\n";
	}
	if (!fragment_code.is_empty()) {
		shader_source += "\nfragment();\n" + fragment_code + "\n";
	}

	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(shader_source);

	Ref<ShaderMaterial> mat;
	mat.instantiate();
	mat->set_shader(shader);

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(node);
	if (mesh_instance != nullptr) {
		mesh_instance->set_material_override(mat);
	} else {
		return _make_error("Shader material assignment requires a MeshInstance3D target.");
	}

	Dictionary result;
	result["shader_name"] = shader_name;
	result["shader_code"] = shader_source;
	result["node_path"] = String(node->get_path());
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_set_shader_uniform(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	ShaderMaterial *mat = nullptr;
	Variant mat_var = node->get("material_override");
	if (mat_var.get_type() == Variant::OBJECT) {
		mat = Object::cast_to<ShaderMaterial>(mat_var);
	}
	if (mat == nullptr) {
		MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(node);
		if (mesh_instance != nullptr && mesh_instance->get_surface_override_material_count() > 0) {
			Ref<Material> sm = mesh_instance->get_surface_override_material(0);
			mat = Object::cast_to<ShaderMaterial>(sm.ptr());
		}
	}
	if (mat == nullptr) {
		return _make_error("Target node does not have a ShaderMaterial assigned.");
	}

	Ref<Shader> shader = mat->get_shader();
	if (shader.is_null()) {
		return _make_error("ShaderMaterial has no shader assigned.");
	}

	const String uniform_name = _arg_string(p_args, "uniform_name", "");
	if (uniform_name.is_empty()) {
		return _make_error("uniform_name is required.");
	}

	const String type_hint = _arg_string(p_args, "type_hint", "").to_lower();
	const Variant value_raw = p_args.get("value", Variant());

	Variant converted;
	if (type_hint == "float") {
		converted = _variant_from_json(value_raw, Variant::FLOAT);
	} else if (type_hint == "color") {
		converted = _variant_from_json(value_raw, Variant::COLOR);
	} else if (type_hint == "vector3") {
		converted = _variant_from_json(value_raw, Variant::VECTOR3);
	} else if (type_hint == "bool") {
		converted = _variant_from_json(value_raw, Variant::BOOL);
	} else if (type_hint == "int") {
		converted = _variant_from_json(value_raw, Variant::INT);
	} else if (type_hint == "texture") {
		const String tex_path = _arg_string(p_args, "value", "");
		if (tex_path.is_empty()) {
			return _make_error("texture uniform requires a resource path as value.");
		}
		Ref<Texture2D> tex = ResourceLoader::load(tex_path);
		if (tex.is_null()) {
			return _make_error("Failed to load texture from: " + tex_path);
		}
		converted = tex;
	} else {
		converted = value_raw;
	}

	mat->set_shader_parameter(uniform_name, converted);

	Dictionary result;
	result["uniform_name"] = uniform_name;
	result["type_hint"] = type_hint;
	result["node_path"] = String(node->get_path());
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_get_shader_uniforms(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	ShaderMaterial *mat = nullptr;
	Variant mat_var = node->get("material_override");
	if (mat_var.get_type() == Variant::OBJECT) {
		mat = Object::cast_to<ShaderMaterial>(mat_var);
	}
	if (mat == nullptr) {
		MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(node);
		if (mesh_instance != nullptr && mesh_instance->get_surface_override_material_count() > 0) {
			Ref<Material> sm = mesh_instance->get_surface_override_material(0);
			mat = Object::cast_to<ShaderMaterial>(sm.ptr());
		}
	}
	if (mat == nullptr) {
		return _make_error("Target node does not have a ShaderMaterial assigned.");
	}

	Ref<Shader> shader = mat->get_shader();
	if (shader.is_null()) {
		return _make_error("ShaderMaterial has no shader assigned.");
	}

	const String code = shader->get_code();
	Array uniform_lines;
	const PackedStringArray lines = code.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i].strip_edges();
		if (line.begins_with("uniform ")) {
			Dictionary uniform_info;
			uniform_info["line"] = i + 1;
			uniform_info["declaration"] = line;

			PackedStringArray tokens = line.split(" ", false);
			if (tokens.size() >= 3) {
				uniform_info["type"] = tokens[1];
				String name_part = tokens[2];
				int semi = name_part.find(";");
				if (semi >= 0) {
					name_part = name_part.substr(0, semi);
				}
				int colon = name_part.find(":");
				if (colon >= 0) {
					name_part = name_part.substr(0, colon);
				}
				uniform_info["name"] = name_part.strip_edges();

				Variant current_val = mat->get_shader_parameter(name_part.strip_edges());
				if (current_val.get_type() != Variant::NIL) {
					uniform_info["current_value"] = _json_safe_variant(current_val);
				}
			}

			uniform_lines.push_back(uniform_info);
		}
	}

	Dictionary result;
	result["shader_code"] = code;
	result["uniforms"] = uniform_lines;
	result["node_path"] = String(node->get_path());
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// H. Audio
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_create_audio_player(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String player_type = _arg_string(p_args, "player_type", "audio").to_lower();
	const String node_name = _arg_string(p_args, "name", "AudioStreamPlayer");
	const String stream_path = _arg_string(p_args, "stream_path", "");
	const String bus = _arg_string(p_args, "bus", "Master");
	const double volume_db = _arg_float(p_args, "volume_db", 0.0);
	const bool autoplay = _arg_bool(p_args, "autoplay", false);

	Node *player = nullptr;
	if (player_type == "3d") {
		AudioStreamPlayer3D *p3d = memnew(AudioStreamPlayer3D);
		p3d->set_max_distance(_arg_float(p_args, "max_distance", 2000.0));
		p3d->set_attenuation_model(AudioStreamPlayer3D::ATTENUATION_INVERSE_DISTANCE);
		player = p3d;
	} else if (player_type == "2d") {
		AudioStreamPlayer2D *p2d = memnew(AudioStreamPlayer2D);
		p2d->set_max_distance(_arg_float(p_args, "max_distance", 2000.0));
		player = p2d;
	} else {
		player = memnew(AudioStreamPlayer);
	}

	player->set_name(node_name);

	if (!stream_path.is_empty()) {
		Ref<AudioStream> stream = ResourceLoader::load(stream_path);
		if (stream.is_valid()) {
			player->set("stream", stream);
		}
	}

	player->set("bus", bus);
	player->set("volume_db", volume_db);
	player->set("autoplay", autoplay);

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, player, scene_root);

	Dictionary result;
	result["node_path"] = String(player->get_path());
	result["player_type"] = player_type;
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_play_audio(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	AudioStreamPlayer *asp = Object::cast_to<AudioStreamPlayer>(node);
	AudioStreamPlayer2D *asp2d = Object::cast_to<AudioStreamPlayer2D>(node);
	AudioStreamPlayer3D *asp3d = Object::cast_to<AudioStreamPlayer3D>(node);

	if (asp != nullptr) {
		asp->play();
	} else if (asp2d != nullptr) {
		asp2d->play();
	} else if (asp3d != nullptr) {
		asp3d->play();
	} else {
		return _make_error("Target node is not an AudioStreamPlayer, AudioStreamPlayer2D, or AudioStreamPlayer3D.");
	}

	Dictionary result;
	result["node_path"] = String(node->get_path());
	result["playing"] = true;
	return result;
}

Dictionary YeetAIDock::_tool_stop_audio(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	AudioStreamPlayer *asp = Object::cast_to<AudioStreamPlayer>(node);
	AudioStreamPlayer2D *asp2d = Object::cast_to<AudioStreamPlayer2D>(node);
	AudioStreamPlayer3D *asp3d = Object::cast_to<AudioStreamPlayer3D>(node);

	if (asp != nullptr) {
		asp->stop();
	} else if (asp2d != nullptr) {
		asp2d->stop();
	} else if (asp3d != nullptr) {
		asp3d->stop();
	} else {
		return _make_error("Target node is not an AudioStreamPlayer, AudioStreamPlayer2D, or AudioStreamPlayer3D.");
	}

	Dictionary result;
	result["node_path"] = String(node->get_path());
	result["playing"] = false;
	return result;
}

Dictionary YeetAIDock::_tool_get_audio_buses(const Dictionary &p_args) const {
	AudioServer *audio = AudioServer::get_singleton();
	if (audio == nullptr) {
		return _make_error("AudioServer is not available.");
	}

	const int bus_count = audio->get_bus_count();
	Array buses;
	for (int i = 0; i < bus_count; i++) {
		Dictionary bus_info;
		bus_info["index"] = i;
		bus_info["name"] = audio->get_bus_name(i);
		bus_info["volume_db"] = audio->get_bus_volume_db(i);
		bus_info["solo"] = audio->is_bus_solo(i);
		bus_info["mute"] = audio->is_bus_mute(i);
		bus_info["bypass"] = audio->is_bus_bypassing_effects(i);

		int effect_count = audio->get_bus_effect_count(i);
		Array effects;
		for (int j = 0; j < effect_count; j++) {
			Dictionary eff;
			Ref<AudioEffect> effect = audio->get_bus_effect(i, j);
			if (effect.is_valid()) {
				eff["class"] = effect->get_class();
				eff["enabled"] = audio->is_bus_effect_enabled(i, j);
			}
			effects.push_back(eff);
		}
		bus_info["effects"] = effects;
		buses.push_back(bus_info);
	}

	Dictionary result;
	result["bus_count"] = bus_count;
	result["buses"] = buses;
	return result;
}

Dictionary YeetAIDock::_tool_add_audio_bus_effect(const Dictionary &p_args) const {
	AudioServer *audio = AudioServer::get_singleton();
	if (audio == nullptr) {
		return _make_error("AudioServer is not available.");
	}

	const String bus_name = _arg_string(p_args, "bus_name", "Master");
	const String effect_type = _arg_string(p_args, "effect_type", "reverb").to_lower();
	const String effect_name = _arg_string(p_args, "effect_name", effect_type.capitalize());

	int bus_idx = -1;
	for (int i = 0; i < audio->get_bus_count(); i++) {
		if (audio->get_bus_name(i) == bus_name) {
			bus_idx = i;
			break;
		}
	}
	if (bus_idx < 0) {
		return _make_error("Audio bus not found: " + bus_name);
	}

	Ref<AudioEffect> effect;
	if (effect_type == "reverb") {
		effect = memnew(AudioEffectReverb);
	} else if (effect_type == "eq") {
		effect = memnew(AudioEffectEQ);
	} else if (effect_type == "compressor") {
		effect = memnew(AudioEffectCompressor);
	} else if (effect_type == "chorus") {
		effect = memnew(AudioEffectChorus);
	} else if (effect_type == "delay") {
		effect = memnew(AudioEffectDelay);
	} else if (effect_type == "distortion") {
		effect = memnew(AudioEffectDistortion);
	} else if (effect_type == "limiter") {
		effect = memnew(AudioEffectLimiter);
	} else if (effect_type == "panner") {
		effect = memnew(AudioEffectPanner);
	} else if (effect_type == "recording") {
		effect = memnew(AudioEffectRecord);
	} else {
		return _make_error("Unknown effect_type. Use: reverb, eq, compressor, chorus, delay, distortion, limiter, panner, recording");
	}

	if (effect.is_null()) {
		return _make_error("Failed to create audio effect.");
	}

	effect->set_name(effect_name);
	audio->add_bus_effect(bus_idx, effect);

	Dictionary result;
	result["bus_name"] = bus_name;
	result["effect_type"] = effect_type;
	result["effect_name"] = effect_name;
	return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// I. UI / HUD Construction
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_create_control_node(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String control_type = _arg_string(p_args, "control_type", "Control");
	const String node_name = _arg_string(p_args, "name", control_type);
	const String text = _arg_string(p_args, "text", "");
	const String placeholder_text = _arg_string(p_args, "placeholder_text", "");

	Object *obj = ClassDB::instantiate(control_type);
	if (obj == nullptr) {
		return _make_error("Cannot instantiate class: " + control_type + ". Is it a valid class name?");
	}

	Control *ctrl = Object::cast_to<Control>(obj);
	if (ctrl == nullptr) {
		memdelete(obj);
		return _make_error(control_type + " is not a Control-derived class.");
	}

	ctrl->set_name(node_name);

	Button *button = Object::cast_to<Button>(ctrl);
	Label *label = Object::cast_to<Label>(ctrl);
	LineEdit *line_edit = Object::cast_to<LineEdit>(ctrl);
	TextEdit *text_edit = Object::cast_to<TextEdit>(ctrl);

	if (button != nullptr && !text.is_empty()) {
		button->set_text(text);
	} else if (label != nullptr && !text.is_empty()) {
		label->set_text(text);
	}
	if (line_edit != nullptr && !placeholder_text.is_empty()) {
		line_edit->set_placeholder(placeholder_text);
	}
	if (text_edit != nullptr && !placeholder_text.is_empty()) {
		text_edit->set_placeholder(placeholder_text);
	}

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, ctrl, scene_root);

	Dictionary result;
	result["node_path"] = String(ctrl->get_path());
	result["control_type"] = control_type;
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_set_control_layout(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	Control *ctrl = Object::cast_to<Control>(node);
	if (ctrl == nullptr) {
		return _make_error("Target node is not a Control-derived node.");
	}

	const String anchor_preset = _arg_string(p_args, "anchor_preset", "").to_lower();
	if (!anchor_preset.is_empty()) {
		if (anchor_preset == "full_rect" || anchor_preset == "full") {
			ctrl->set_anchors_preset(Control::PRESET_FULL_RECT);
		} else if (anchor_preset == "top_left") {
			ctrl->set_anchors_preset(Control::PRESET_TOP_LEFT);
		} else if (anchor_preset == "top_right") {
			ctrl->set_anchors_preset(Control::PRESET_TOP_RIGHT);
		} else if (anchor_preset == "bottom_left") {
			ctrl->set_anchors_preset(Control::PRESET_BOTTOM_LEFT);
		} else if (anchor_preset == "bottom_right") {
			ctrl->set_anchors_preset(Control::PRESET_BOTTOM_RIGHT);
		} else if (anchor_preset == "center" || anchor_preset == "center_center") {
			ctrl->set_anchors_preset(Control::PRESET_CENTER);
		} else if (anchor_preset == "left_wide") {
			ctrl->set_anchors_preset(Control::PRESET_LEFT_WIDE);
		} else if (anchor_preset == "top_wide") {
			ctrl->set_anchors_preset(Control::PRESET_TOP_WIDE);
		} else if (anchor_preset == "right_wide") {
			ctrl->set_anchors_preset(Control::PRESET_RIGHT_WIDE);
		} else if (anchor_preset == "bottom_wide") {
			ctrl->set_anchors_preset(Control::PRESET_BOTTOM_WIDE);
		} else if (anchor_preset == "vcenter_wide") {
			ctrl->set_anchors_preset(Control::PRESET_VCENTER_WIDE);
		} else if (anchor_preset == "hcenter_wide") {
			ctrl->set_anchors_preset(Control::PRESET_HCENTER_WIDE);
		} else {
			return _make_error("Unknown anchor_preset. Use: full_rect, top_left, top_right, bottom_left, bottom_right, center, left_wide, top_wide, right_wide, bottom_wide, vcenter_wide, hcenter_wide");
		}
	} else {
		ctrl->set_anchor(SIDE_LEFT, _arg_float(p_args, "anchor_left", 0.0));
		ctrl->set_anchor(SIDE_RIGHT, _arg_float(p_args, "anchor_right", 0.0));
		ctrl->set_anchor(SIDE_TOP, _arg_float(p_args, "anchor_top", 0.0));
		ctrl->set_anchor(SIDE_BOTTOM, _arg_float(p_args, "anchor_bottom", 0.0));
	}

	ctrl->set_offset(SIDE_LEFT, _arg_float(p_args, "offset_left", 0.0));
	ctrl->set_offset(SIDE_RIGHT, _arg_float(p_args, "offset_right", 0.0));
	ctrl->set_offset(SIDE_TOP, _arg_float(p_args, "offset_top", 0.0));
	ctrl->set_offset(SIDE_BOTTOM, _arg_float(p_args, "offset_bottom", 0.0));

	const String grow_h = _arg_string(p_args, "grow_horizontal", "").to_lower();
	if (grow_h == "begin") {
		ctrl->set_h_grow_direction(Control::GROW_DIRECTION_BEGIN);
	} else if (grow_h == "end") {
		ctrl->set_h_grow_direction(Control::GROW_DIRECTION_END);
	} else if (grow_h == "both") {
		ctrl->set_h_grow_direction(Control::GROW_DIRECTION_BOTH);
	}

	const String grow_v = _arg_string(p_args, "grow_vertical", "").to_lower();
	if (grow_v == "begin") {
		ctrl->set_v_grow_direction(Control::GROW_DIRECTION_BEGIN);
	} else if (grow_v == "end") {
		ctrl->set_v_grow_direction(Control::GROW_DIRECTION_END);
	} else if (grow_v == "both") {
		ctrl->set_v_grow_direction(Control::GROW_DIRECTION_BOTH);
	}

	const int size_flags_h = _arg_int(p_args, "size_flags_horizontal", -1);
	if (size_flags_h >= 0) {
		ctrl->set_h_size_flags(size_flags_h);
	}
	const int size_flags_v = _arg_int(p_args, "size_flags_vertical", -1);
	if (size_flags_v >= 0) {
		ctrl->set_v_size_flags(size_flags_v);
	}

	Dictionary result;
	result["node_path"] = String(ctrl->get_path());
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_set_control_theme_override(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	Control *ctrl = Object::cast_to<Control>(node);
	if (ctrl == nullptr) {
		return _make_error("Target node is not a Control-derived node.");
	}

	const String override_type = _arg_string(p_args, "override_type", "").to_lower();
	const String name = _arg_string(p_args, "name", "");
	if (name.is_empty()) {
		return _make_error("name (theme item name) is required.");
	}

	const StringName theme_name = StringName(name);

	if (override_type == "color") {
		Color color = _arg_color(p_args, "value", Color());
		ctrl->add_theme_color_override(theme_name, color);
	} else if (override_type == "font") {
		const String font_path = _arg_string(p_args, "value", "");
		if (font_path.is_empty()) {
			return _make_error("font override requires a resource path as value.");
		}
		Ref<Font> font = ResourceLoader::load(font_path);
		if (font.is_null()) {
			return _make_error("Failed to load font from: " + font_path);
		}
		ctrl->add_theme_font_override(theme_name, font);
	} else if (override_type == "font_size") {
		int size = _arg_int(p_args, "value", 16);
		ctrl->add_theme_font_size_override(theme_name, size);
	} else if (override_type == "icon") {
		const String icon_path = _arg_string(p_args, "value", "");
		if (icon_path.is_empty()) {
			return _make_error("icon override requires a resource path as value.");
		}
		Ref<Texture2D> tex = ResourceLoader::load(icon_path);
		if (tex.is_null()) {
			return _make_error("Failed to load icon texture from: " + icon_path);
		}
		ctrl->add_theme_icon_override(theme_name, tex);
	} else if (override_type == "stylebox") {
		Dictionary sb_dict = _arg_dict(p_args, "value");
		Ref<StyleBoxFlat> sb;
		sb.instantiate();
		if (sb_dict.has("bg_color")) {
			sb->set_bg_color(_arg_color(sb_dict, "bg_color", Color()));
		}
		if (sb_dict.has("border_color")) {
			sb->set_border_color(_arg_color(sb_dict, "border_color", Color()));
		}
		if (sb_dict.has("border_width")) {
			const int bw = _arg_int(sb_dict, "border_width", 0);
			sb->set_border_width_all(bw);
		}
		if (sb_dict.has("corner_radius")) {
			const int cr = _arg_int(sb_dict, "corner_radius", 0);
			sb->set_corner_radius_all(cr);
		}
		if (sb_dict.has("content_margin")) {
			const int cm = _arg_int(sb_dict, "content_margin", 0);
			sb->set_content_margin_all(cm);
		}
		ctrl->add_theme_style_override(theme_name, sb);
	} else {
		return _make_error("Unknown override_type. Use: color, font, font_size, icon, stylebox");
	}

	Dictionary result;
	result["node_path"] = String(ctrl->get_path());
	result["override_type"] = override_type;
	result["name"] = name;
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_create_container_layout(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String container_type = _arg_string(p_args, "container_type", "vbox").to_lower();
	const String node_name = _arg_string(p_args, "name", container_type + "_container");
	const int spacing = _arg_int(p_args, "spacing", 4);

	Container *container = nullptr;
	if (container_type == "hbox") {
		container = memnew(HBoxContainer);
	} else if (container_type == "vbox") {
		container = memnew(VBoxContainer);
	} else if (container_type == "grid") {
		GridContainer *gc = memnew(GridContainer);
		gc->set_columns(_arg_int(p_args, "columns", 2));
		container = gc;
	} else if (container_type == "hflow") {
		container = memnew(HFlowContainer);
	} else if (container_type == "vflow") {
		container = memnew(VFlowContainer);
	} else if (container_type == "hsplit") {
		container = memnew(HSplitContainer);
	} else if (container_type == "vsplit") {
		container = memnew(VSplitContainer);
	} else if (container_type == "center") {
		container = memnew(CenterContainer);
	} else if (container_type == "margin") {
		container = memnew(MarginContainer);
	} else {
		return _make_error("Unknown container_type. Use: hbox, vbox, grid, hflow, vflow, hsplit, vsplit, center, margin");
	}

	container->set_name(node_name);
	container->add_theme_constant_override("separation", spacing);

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, container, scene_root);

	Dictionary result;
	result["node_path"] = String(container->get_path());
	result["container_type"] = container_type;
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_create_scroll_container(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "ScrollContainer");

	ScrollContainer *sc = memnew(ScrollContainer);
	sc->set_name(node_name);
	sc->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
	sc->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, sc, scene_root);

	const String content_node_type = _arg_string(p_args, "content_node_type", "");
	if (!content_node_type.is_empty()) {
		Object *obj = ClassDB::instantiate(content_node_type);
		if (obj != nullptr) {
			Control *content = Object::cast_to<Control>(obj);
			if (content != nullptr) {
				content->set_name(content_node_type);
				content->set_anchors_preset(Control::PRESET_FULL_RECT);
				_add_to_scene(sc, content, scene_root);
			} else {
				memdelete(obj);
			}
		}
	}

	Dictionary result;
	result["node_path"] = String(sc->get_path());
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_create_progress_bar(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "ProgressBar");

	ProgressBar *pb = memnew(ProgressBar);
	pb->set_name(node_name);
	pb->set_value(_arg_float(p_args, "value", 0.0));
	pb->set_min(_arg_float(p_args, "min_value", 0.0));
	pb->set_max(_arg_float(p_args, "max_value", 100.0));
	pb->set_show_percentage(_arg_bool(p_args, "show_percentage", true));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, pb, scene_root);

	Dictionary result;
	result["node_path"] = String(pb->get_path());
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_create_slider(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String slider_type = _arg_string(p_args, "slider_type", "horizontal").to_lower();
	const String node_name = _arg_string(p_args, "name", slider_type == "vertical" ? "VSlider" : "HSlider");

	Slider *slider = nullptr;
	if (slider_type == "vertical") {
		slider = memnew(VSlider);
	} else {
		slider = memnew(HSlider);
	}

	slider->set_name(node_name);
	slider->set_min(_arg_float(p_args, "min_value", 0.0));
	slider->set_max(_arg_float(p_args, "max_value", 100.0));
	slider->set_step(_arg_float(p_args, "step", 1.0));
	slider->set_value(_arg_float(p_args, "value", 0.0));

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, slider, scene_root);

	Dictionary result;
	result["node_path"] = String(slider->get_path());
	result["slider_type"] = slider_type;
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_create_item_list(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "ItemList");
	const String select_mode_str = _arg_string(p_args, "select_mode", "single").to_lower();

	ItemList *il = memnew(ItemList);
	il->set_name(node_name);

	if (select_mode_str == "multi") {
		il->set_select_mode(ItemList::SELECT_MULTI);
	}

	Array items = _arg_array(p_args, "items");
	for (int i = 0; i < items.size(); i++) {
		il->add_item(String(items[i]));
	}

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, il, scene_root);

	Dictionary result;
	result["node_path"] = String(il->get_path());
	result["item_count"] = items.size();
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_create_option_button(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "OptionButton");
	const int selected = _arg_int(p_args, "selected", 0);

	OptionButton *ob = memnew(OptionButton);
	ob->set_name(node_name);

	Array items = _arg_array(p_args, "items");
	for (int i = 0; i < items.size(); i++) {
		ob->add_item(String(items[i]));
	}
	if (selected >= 0 && selected < items.size()) {
		ob->select(selected);
	}

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, ob, scene_root);

	Dictionary result;
	result["node_path"] = String(ob->get_path());
	result["item_count"] = items.size();
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_create_tab_container(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "TabContainer");

	TabContainer *tc = memnew(TabContainer);
	tc->set_name(node_name);

	Array tabs = _arg_array(p_args, "tabs");
	for (int i = 0; i < tabs.size(); i++) {
		Dictionary tab_info;
		if (tabs[i].get_type() == Variant::DICTIONARY) {
			tab_info = tabs[i];
		} else {
			continue;
		}

		const String title = String(tab_info.get("title", "Tab " + itos(i)));
		const String ctrl_type = String(tab_info.get("control_type", "Control"));

		Object *obj = ClassDB::instantiate(ctrl_type);
		if (obj != nullptr) {
			Control *tab_ctrl = Object::cast_to<Control>(obj);
			if (tab_ctrl != nullptr) {
				tab_ctrl->set_name(title);
				tab_ctrl->set_anchors_preset(Control::PRESET_FULL_RECT);
				_add_to_scene(tc, tab_ctrl, scene_root);
			} else {
				memdelete(obj);
			}
		}
	}

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, tc, scene_root);

	Dictionary result;
	result["node_path"] = String(tc->get_path());
	result["tab_count"] = tabs.size();
	_mark_unsaved();
	return result;
}

Dictionary YeetAIDock::_tool_create_graph_node(const Dictionary &p_args) const {
	return _make_error("GraphNode must be added as a child of a GraphEdit. Use add_node with type=GraphEdit first, then add GraphNode children.");
}

Dictionary YeetAIDock::_tool_create_tree_widget(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String node_name = _arg_string(p_args, "name", "Tree");
	const int columns = _arg_int(p_args, "columns", 1);

	Tree *tree = memnew(Tree);
	tree->set_name(node_name);
	tree->set_columns(columns);

	Array items = _arg_array(p_args, "items");

	std::function<void(const Array &, TreeItem *)> build_items;
	build_items = [&](const Array &p_items, TreeItem *p_parent) {
		for (int i = 0; i < p_items.size(); i++) {
			Dictionary item_dict;
			if (p_items[i].get_type() == Variant::DICTIONARY) {
				item_dict = p_items[i];
			} else {
				continue;
			}

			TreeItem *item = tree->create_item(p_parent);
			const String text = String(item_dict.get("text", ""));
			if (!text.is_empty()) {
				item->set_text(0, text);
			}

			Array children = item_dict.get("children", Array());
			if (children.size() > 0) {
				build_items(children, item);
			}
		}
	};

	TreeItem *root_item = tree->create_item();
	if (items.size() > 0) {
		build_items(items, root_item);
	}

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}
	_add_to_scene(parent, tree, scene_root);

	Dictionary result;
	result["node_path"] = String(tree->get_path());
	result["columns"] = columns;
	_mark_unsaved();
	return result;
}
