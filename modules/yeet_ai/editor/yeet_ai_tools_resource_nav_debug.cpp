/**************************************************************************/
/*  yeet_ai_tools_resource_nav_debug.cpp                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                           CROSSHAIR ENGINE                             */
/**************************************************************************/

#include "yeet_ai_dock.h"

#include "core/io/file_access.h"
#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/os/os.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/editor_interface.h"
#include "scene/2d/navigation/navigation_link_2d.h"
#include "scene/2d/navigation/navigation_obstacle_2d.h"
#include "scene/3d/navigation/navigation_link_3d.h"
#include "scene/3d/navigation/navigation_obstacle_3d.h"
#include "scene/3d/navigation/navigation_region_3d.h"
#include "scene/resources/atlas_texture.h"
#include "scene/resources/curve.h"
#include "scene/resources/font.h"
#include "scene/resources/gradient.h"
#include "scene/resources/gradient_texture.h"
#include "modules/noise/fastnoise_lite.h"
#include "modules/noise/noise_texture_2d.h"
#include "scene/resources/placeholder_textures.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/style_box_line.h"
#include "scene/resources/style_box_texture.h"
#include "scene/resources/theme.h"
#include "scene/resources/texture.h"
#include "main/performance.h"

// ═══════════════════════════════════════════════════════════════════════════
// J. Resources & Assets
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_create_gradient(const Dictionary &p_args) const {
	const String save_path = _arg_string(p_args, "save_path", "");
	if (!save_path.begins_with("res://")) {
		return _make_error("save_path must start with res://");
	}

	const String gradient_type = _arg_string(p_args, "gradient_type", "gradient").to_lower();
	const String fill = _arg_string(p_args, "fill", "linear").to_lower();
	Ref<Gradient> gradient;
	gradient.instantiate();

	if (fill == "cubicspline") {
		gradient->set_interpolation_mode(Gradient::GRADIENT_INTERPOLATE_CUBIC);
	} else {
		gradient->set_interpolation_mode(Gradient::GRADIENT_INTERPOLATE_LINEAR);
	}

	Array stops = _arg_array(p_args, "stops");
	if (stops.is_empty()) {
		gradient->add_point(0.0, Color(0, 0, 0));
		gradient->add_point(1.0, Color(1, 1, 1));
	} else {
		for (int i = 0; i < stops.size(); i++) {
			Dictionary sd = stops[i];
			const double pos = sd.get("position", double(i) / double(MAX(stops.size() - 1, 1)));
			const double r = sd.get("r", 0.0);
			const double g = sd.get("g", 0.0);
			const double b = sd.get("b", 0.0);
			const double a = sd.get("a", 1.0);
			gradient->add_point(pos, Color(r, g, b, a));
		}
	}

	Ref<Resource> resource_to_save;
	Dictionary extra;
	extra["gradient_type"] = gradient_type;

	if (gradient_type == "texture1d") {
		Ref<GradientTexture1D> tex;
		tex.instantiate();
		tex->set_gradient(gradient);
		tex->set_width(_arg_int(p_args, "width", 256));
		resource_to_save = tex;
	} else if (gradient_type == "texture2d") {
		Ref<GradientTexture2D> tex;
		tex.instantiate();
		tex->set_gradient(gradient);
		tex->set_width(_arg_int(p_args, "width", 256));
		tex->set_height(_arg_int(p_args, "height", 256));
		resource_to_save = tex;
	} else {
		resource_to_save = gradient;
	}

	const Error save_err = ResourceSaver::save(resource_to_save, save_path);
	if (save_err != OK) {
		return _make_error(vformat("Failed to save gradient resource: error %d", save_err));
	}

	extra["save_path"] = save_path;
	extra["stop_count"] = stops.size();
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_create_curve(const Dictionary &p_args) const {
	const String save_path = _arg_string(p_args, "save_path", "");
	if (!save_path.begins_with("res://")) {
		return _make_error("save_path must start with res://");
	}

	const double min_value = _arg_float(p_args, "min_value", -1.0);
	const double max_value = _arg_float(p_args, "max_value", 1.0);
	const int bake_resolution = _arg_int(p_args, "bake_resolution", 256);

	Ref<Curve> curve;
	curve.instantiate();
	curve->set_min_value(min_value);
	curve->set_max_value(max_value);
	curve->set_bake_resolution(bake_resolution);

	Array points = _arg_array(p_args, "points");
	if (points.is_empty()) {
		curve->add_point(Vector2(0, 0));
		curve->add_point(Vector2(1, 1));
	} else {
		for (int i = 0; i < points.size(); i++) {
			Dictionary pd = points[i];
			const double pos = pd.get("position", 0.0);
			const double val = pd.get("value", 0.0);
			const double lt = pd.get("left_tangent", 0.0);
			const double rt = pd.get("right_tangent", 0.0);
			curve->add_point(Vector2(pos, val), lt, rt);
		}
	}

	curve->bake();

	const Error save_err = ResourceSaver::save(curve, save_path);
	if (save_err != OK) {
		return _make_error(vformat("Failed to save curve resource: error %d", save_err));
	}

	Dictionary extra;
	extra["save_path"] = save_path;
	extra["point_count"] = points.is_empty() ? 2 : points.size();
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_create_stylebox(const Dictionary &p_args) const {
	const String save_path = _arg_string(p_args, "save_path", "");
	if (!save_path.begins_with("res://")) {
		return _make_error("save_path must start with res://");
	}

	const String sb_type = _arg_string(p_args, "stylebox_type", "flat").to_lower();
	const Color bg_color = _arg_color(p_args, "bg_color", Color());
	const Color border_color = _arg_color(p_args, "border_color", Color());
	const Color shadow_color = _arg_color(p_args, "shadow_color", Color(0, 0, 0, 0.6));

	Ref<StyleBox> stylebox;

	if (sb_type == "flat") {
		Ref<StyleBoxFlat> sb;
		sb.instantiate();
		sb->set_bg_color(bg_color);
		sb->set_border_color(border_color);
		sb->set_shadow_color(shadow_color);
		sb->set_shadow_offset(Size2(_arg_float(p_args, "shadow_offset_x", 0.0), _arg_float(p_args, "shadow_offset_y", 3.0)));
		sb->set_shadow_size(_arg_int(p_args, "shadow_size", 0));
		sb->set_anti_aliased(_arg_bool(p_args, "anti_aliased", true));

		const Variant bw = p_args.get("border_width", Variant());
		if (bw.get_type() == Variant::INT || bw.get_type() == Variant::FLOAT) {
			const int w = int(bw);
			sb->set_border_width(SIDE_LEFT, w);
			sb->set_border_width(SIDE_RIGHT, w);
			sb->set_border_width(SIDE_TOP, w);
			sb->set_border_width(SIDE_BOTTOM, w);
		} else if (bw.get_type() == Variant::DICTIONARY) {
			const Dictionary bwd = bw;
			sb->set_border_width(SIDE_LEFT, bwd.get("left", 0));
			sb->set_border_width(SIDE_RIGHT, bwd.get("right", 0));
			sb->set_border_width(SIDE_TOP, bwd.get("top", 0));
			sb->set_border_width(SIDE_BOTTOM, bwd.get("bottom", 0));
		}

		const Variant cr = p_args.get("corner_radius", Variant());
		if (cr.get_type() == Variant::INT || cr.get_type() == Variant::FLOAT) {
			const int r = int(cr);
			sb->set_corner_radius(CORNER_TOP_LEFT, r);
			sb->set_corner_radius(CORNER_TOP_RIGHT, r);
			sb->set_corner_radius(CORNER_BOTTOM_RIGHT, r);
			sb->set_corner_radius(CORNER_BOTTOM_LEFT, r);
		} else if (cr.get_type() == Variant::DICTIONARY) {
			const Dictionary crd = cr;
			sb->set_corner_radius(CORNER_TOP_LEFT, crd.get("top_left", 0));
			sb->set_corner_radius(CORNER_TOP_RIGHT, crd.get("top_right", 0));
			sb->set_corner_radius(CORNER_BOTTOM_RIGHT, crd.get("bottom_right", 0));
			sb->set_corner_radius(CORNER_BOTTOM_LEFT, crd.get("bottom_left", 0));
		}

		const Variant cm = p_args.get("content_margin", Variant());
		if (cm.get_type() == Variant::INT || cm.get_type() == Variant::FLOAT) {
			const int m = int(cm);
			sb->set_content_margin(SIDE_LEFT, m);
			sb->set_content_margin(SIDE_RIGHT, m);
			sb->set_content_margin(SIDE_TOP, m);
			sb->set_content_margin(SIDE_BOTTOM, m);
		} else if (cm.get_type() == Variant::DICTIONARY) {
			const Dictionary cmd = cm;
			sb->set_content_margin(SIDE_LEFT, cmd.get("left", 0));
			sb->set_content_margin(SIDE_RIGHT, cmd.get("right", 0));
			sb->set_content_margin(SIDE_TOP, cmd.get("top", 0));
			sb->set_content_margin(SIDE_BOTTOM, cmd.get("bottom", 0));
		}

		stylebox = sb;

	} else if (sb_type == "texture") {
		const String texture_path = _arg_string(p_args, "texture_path", "");
		if (texture_path.is_empty()) {
			return _make_error("texture_path is required for texture stylebox");
		}
		Ref<Texture2D> tex = ResourceLoader::load(texture_path);
		if (tex.is_null()) {
			return _make_error("Failed to load texture: " + texture_path);
		}

		Ref<StyleBoxTexture> sb;
		sb.instantiate();
		sb->set_texture(tex);
		sb->set_region_rect(Rect2(_arg_int(p_args, "region_x", 0), _arg_int(p_args, "region_y", 0),
				_arg_int(p_args, "region_w", 0), _arg_int(p_args, "region_h", 0)));
		sb->set_modulate(bg_color);

		const Variant cm2 = p_args.get("content_margin", Variant());
		if (cm2.get_type() == Variant::INT || cm2.get_type() == Variant::FLOAT) {
			const int m = int(cm2);
			sb->set_content_margin(SIDE_LEFT, m);
			sb->set_content_margin(SIDE_RIGHT, m);
			sb->set_content_margin(SIDE_TOP, m);
			sb->set_content_margin(SIDE_BOTTOM, m);
		} else if (cm2.get_type() == Variant::DICTIONARY) {
			const Dictionary cmd = cm2;
			sb->set_content_margin(SIDE_LEFT, cmd.get("left", 0));
			sb->set_content_margin(SIDE_RIGHT, cmd.get("right", 0));
			sb->set_content_margin(SIDE_TOP, cmd.get("top", 0));
			sb->set_content_margin(SIDE_BOTTOM, cmd.get("bottom", 0));
		}

		stylebox = sb;

	} else if (sb_type == "line") {
		Ref<StyleBoxLine> sb;
		sb.instantiate();
		sb->set_color(bg_color);
		sb->set_grow_end(_arg_int(p_args, "grow_end", 0));
		sb->set_grow_begin(_arg_int(p_args, "grow_begin", 0));
		sb->set_thickness(_arg_int(p_args, "thickness", 1));
		sb->set_vertical(_arg_bool(p_args, "vertical", false));

		stylebox = sb;

	} else {
		return _make_error("Unknown stylebox_type. Use: flat, texture, line");
	}

	const Error save_err = ResourceSaver::save(stylebox, save_path);
	if (save_err != OK) {
		return _make_error(vformat("Failed to save stylebox resource: error %d", save_err));
	}

	Dictionary extra;
	extra["save_path"] = save_path;
	extra["stylebox_type"] = sb_type;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_create_theme(const Dictionary &p_args) const {
	const String save_path = _arg_string(p_args, "save_path", "");
	if (!save_path.begins_with("res://")) {
		return _make_error("save_path must start with res://");
	}

	Ref<Theme> theme;
	theme.instantiate();

	const String default_font_path = _arg_string(p_args, "default_font", "");
	if (!default_font_path.is_empty()) {
		Ref<Font> font = ResourceLoader::load(default_font_path);
		if (font.is_valid()) {
			theme->set_default_font(font);
		}
	}

	theme->set_default_font_size(_arg_int(p_args, "default_font_size", 16));

	Array overrides = _arg_array(p_args, "overrides");
	for (int i = 0; i < overrides.size(); i++) {
		Dictionary od = overrides[i];
		const String type_name = od.get("type_name", "");
		const String item_type = String(od.get("item_type", "")).to_lower();
		const String name = od.get("name", "");
		if (type_name.is_empty() || item_type.is_empty() || name.is_empty()) {
			continue;
		}

		const Variant value = od.get("value", Variant());

		if (item_type == "color") {
			Color c;
			if (value.get_type() == Variant::DICTIONARY) {
				c = _arg_color(Dictionary(value), "", Color());
			} else if (value.get_type() == Variant::COLOR) {
				c = value;
			}
			theme->set_color(StringName(name), StringName(type_name), c);
		} else if (item_type == "font") {
			const String font_path = String(value);
			if (font_path.begins_with("res://")) {
				Ref<Font> f = ResourceLoader::load(font_path);
				if (f.is_valid()) {
					theme->set_font(StringName(name), StringName(type_name), f);
				}
			}
		} else if (item_type == "font_size") {
			theme->set_font_size(StringName(name), StringName(type_name), int(value));
		} else if (item_type == "icon") {
			const String icon_path = String(value);
			if (icon_path.begins_with("res://")) {
				Ref<Texture2D> tex = ResourceLoader::load(icon_path);
				if (tex.is_valid()) {
					theme->set_icon(StringName(name), StringName(type_name), tex);
				}
			}
		} else if (item_type == "stylebox") {
			const String sb_path = String(value);
			if (sb_path.begins_with("res://")) {
				Ref<StyleBox> sb = ResourceLoader::load(sb_path);
				if (sb.is_valid()) {
					theme->set_stylebox(StringName(name), StringName(type_name), sb);
				}
			}
		}
	}

	const Error save_err = ResourceSaver::save(theme, save_path);
	if (save_err != OK) {
		return _make_error(vformat("Failed to save theme resource: error %d", save_err));
	}

	Dictionary extra;
	extra["save_path"] = save_path;
	extra["override_count"] = overrides.size();
	return _make_ok(extra);
}
Dictionary YeetAIDock::_tool_create_font(const Dictionary &p_args) const {
	const String save_path = _arg_string(p_args, "save_path", "");
	if (!save_path.begins_with("res://")) {
		return _make_error("save_path must start with res://");
	}

	const String variation_type = _arg_string(p_args, "variation_type", "variation").to_lower();

	if (variation_type == "variation") {
		const String base_font_path = _arg_string(p_args, "base_font_path", "");
		if (base_font_path.is_empty()) {
			return _make_error("base_font_path is required for font variation");
		}

		Ref<Font> base_font = ResourceLoader::load(base_font_path);
		if (base_font.is_null()) {
			return _make_error("Failed to load base font: " + base_font_path);
		}

		Ref<FontVariation> fv;
		fv.instantiate();
		fv->set_base_font(base_font);
		fv->set_variation_embolden(_arg_float(p_args, "embolden", 0.0));
		fv->set_variation_face_index(_arg_int(p_args, "face_index", 0));

		const Dictionary var_coords = _arg_dict(p_args, "variation_coordinates");
		if (!var_coords.is_empty()) {
			Dictionary coords;
			Array keys = var_coords.keys();
			for (int i = 0; i < keys.size(); i++) {
				const String key = keys[i];
				coords[key] = var_coords[key];
			}
			fv->set_variation_opentype(coords);
		}

		const Dictionary ot_features = _arg_dict(p_args, "opentype_features");
		if (!ot_features.is_empty()) {
			Dictionary features;
			Array fkeys = ot_features.keys();
			for (int i = 0; i < fkeys.size(); i++) {
				const String key = fkeys[i];
				features[key] = ot_features[key];
			}
			fv->set_opentype_features(features);
		}

		const Dictionary transform_dict = _arg_dict(p_args, "transform");
		if (!transform_dict.is_empty()) {
			Transform2D xform;
			xform.set_rotation(_arg_float(transform_dict, "rotation", 0.0));
			fv->set_variation_transform(xform);
		}

		const Error save_err = ResourceSaver::save(fv, save_path);
		if (save_err != OK) {
			return _make_error(vformat("Failed to save font variation: error %d", save_err));
		}

		Dictionary extra;
		extra["save_path"] = save_path;
		extra["variation_type"] = "variation";
		return _make_ok(extra);

	} else if (variation_type == "system") {
		Ref<SystemFont> sf;
		sf.instantiate();
		sf->set_font_weight(_arg_int(p_args, "weight", 400));
		sf->set_font_stretch(_arg_int(p_args, "stretch", 100));
		sf->set_font_italic(_arg_bool(p_args, "italic", false));

		const String sys_name = _arg_string(p_args, "font_name", "");
		if (!sys_name.is_empty()) {
			PackedStringArray names;
			names.push_back(sys_name);
			sf->set_font_names(names);
		}

		const Error save_err = ResourceSaver::save(sf, save_path);
		if (save_err != OK) {
			return _make_error(vformat("Failed to save system font: error %d", save_err));
		}

		Dictionary extra;
		extra["save_path"] = save_path;
		extra["variation_type"] = "system";
		return _make_ok(extra);

	} else if (variation_type == "file") {
		const String base_font_path = _arg_string(p_args, "base_font_path", "");
		if (base_font_path.is_empty()) {
			return _make_error("base_font_path is required for file font");
		}

		Ref<FontFile> ff = ResourceLoader::load(base_font_path);
		if (ff.is_null()) {
			return _make_error("Failed to load font file: " + base_font_path);
		}

		const Error save_err = ResourceSaver::save(ff, save_path);
		if (save_err != OK) {
			return _make_error(vformat("Failed to save font file: error %d", save_err));
		}

		Dictionary extra;
		extra["save_path"] = save_path;
		extra["variation_type"] = "file";
		return _make_ok(extra);
	}

	return _make_error("Unknown variation_type. Use: variation, system, file");
}

Dictionary YeetAIDock::_tool_create_texture_2d(const Dictionary &p_args) const {
	const String save_path = _arg_string(p_args, "save_path", "");
	if (!save_path.begins_with("res://")) {
		return _make_error("save_path must start with res://");
	}

	const String texture_type = _arg_string(p_args, "texture_type", "placeholder").to_lower();

	if (texture_type == "placeholder") {
		Ref<PlaceholderTexture2D> tex;
		tex.instantiate();
		tex->set_size(Size2(_arg_int(p_args, "size_x", 16), _arg_int(p_args, "size_y", 16)));

		const Error save_err = ResourceSaver::save(tex, save_path);
		if (save_err != OK) {
			return _make_error(vformat("Failed to save placeholder texture: error %d", save_err));
		}

		Dictionary extra;
		extra["save_path"] = save_path;
		extra["texture_type"] = "placeholder";
		return _make_ok(extra);

	} else if (texture_type == "image") {
		const String image_path = _arg_string(p_args, "image_path", "");
		if (image_path.is_empty()) {
			return _make_error("image_path is required for image texture");
		}
		Ref<Texture2D> tex = ResourceLoader::load(image_path);
		if (tex.is_null()) {
			return _make_error("Failed to load image texture: " + image_path);
		}

		const Error save_err = ResourceSaver::save(tex, save_path);
		if (save_err != OK) {
			return _make_error(vformat("Failed to save image texture: error %d", save_err));
		}

		Dictionary extra;
		extra["save_path"] = save_path;
		extra["texture_type"] = "image";
		return _make_ok(extra);
	}

	return _make_error("Unknown texture_type. Use: placeholder, image");
}

Dictionary YeetAIDock::_tool_create_noise_texture(const Dictionary &p_args) const {
	const String save_path = _arg_string(p_args, "save_path", "");
	if (!save_path.begins_with("res://")) {
		return _make_error("save_path must start with res://");
	}

	const bool as_2d = _arg_bool(p_args, "as_2d", true);

	FastNoiseLite::NoiseType noise_type = FastNoiseLite::TYPE_SIMPLEX_SMOOTH;
	const String nt = _arg_string(p_args, "noise_type", "simplex_smooth").to_lower();
	if (nt == "simplex") {
		noise_type = FastNoiseLite::TYPE_SIMPLEX;
	} else if (nt == "cellular") {
		noise_type = FastNoiseLite::TYPE_CELLULAR;
	} else if (nt == "perlin") {
		noise_type = FastNoiseLite::TYPE_PERLIN;
	} else if (nt == "value") {
		noise_type = FastNoiseLite::TYPE_VALUE;
	} else if (nt == "value_cubic") {
		noise_type = FastNoiseLite::TYPE_VALUE_CUBIC;
	}

	FastNoiseLite::FractalType fractal_type = FastNoiseLite::FRACTAL_NONE;
	const String ft = _arg_string(p_args, "fractal_type", "none").to_lower();
	if (ft == "fbm") {
		fractal_type = FastNoiseLite::FRACTAL_FBM;
	} else if (ft == "ridged") {
		fractal_type = FastNoiseLite::FRACTAL_RIDGED;
	} else if (ft == "pingpong") {
		fractal_type = FastNoiseLite::FRACTAL_PING_PONG;
	}

	Ref<FastNoiseLite> noise;
	noise.instantiate();
	noise->set_noise_type(noise_type);
	noise->set_seed(_arg_int(p_args, "seed", 0));
	noise->set_frequency(_arg_float(p_args, "frequency", 0.05));
	noise->set_fractal_type(fractal_type);
	noise->set_fractal_octaves(_arg_int(p_args, "fractal_octaves", 5));
	noise->set_fractal_lacunarity(_arg_float(p_args, "fractal_lacunarity", 2.0));
	noise->set_fractal_gain(_arg_float(p_args, "fractal_gain", 0.5));

	Ref<Resource> resource_to_save;

	if (as_2d) {
		Ref<NoiseTexture2D> tex;
		tex.instantiate();
		tex->set_noise(noise);
		tex->set_seamless(_arg_bool(p_args, "seamless", false));
		tex->set_width(_arg_int(p_args, "width", 512));
		tex->set_height(_arg_int(p_args, "height", 512));
		resource_to_save = tex;
	} else {
		resource_to_save = noise;
	}

	const Error save_err = ResourceSaver::save(resource_to_save, save_path);
	if (save_err != OK) {
		return _make_error(vformat("Failed to save noise resource: error %d", save_err));
	}

	Dictionary extra;
	extra["save_path"] = save_path;
	extra["noise_type"] = nt;
	extra["as_2d"] = as_2d;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_create_atlas_texture(const Dictionary &p_args) const {
	const String save_path = _arg_string(p_args, "save_path", "");
	if (!save_path.begins_with("res://")) {
		return _make_error("save_path must start with res://");
	}

	const String atlas_path = _arg_string(p_args, "atlas_path", "");
	if (atlas_path.is_empty()) {
		return _make_error("atlas_path is required");
	}

	Ref<Texture2D> atlas = ResourceLoader::load(atlas_path);
	if (atlas.is_null()) {
		return _make_error("Failed to load atlas texture: " + atlas_path);
	}

	Ref<AtlasTexture> at;
	at.instantiate();
	at->set_atlas(atlas);
	at->set_region(Rect2(_arg_int(p_args, "region_x", 0), _arg_int(p_args, "region_y", 0),
			_arg_int(p_args, "region_w", 0), _arg_int(p_args, "region_h", 0)));
	const int ml = _arg_int(p_args, "margin_left", 0);
	const int mt = _arg_int(p_args, "margin_top", 0);
	const int mr = _arg_int(p_args, "margin_right", 0);
	const int mb = _arg_int(p_args, "margin_bottom", 0);
	at->set_margin(Rect2(Vector2(ml, mt), Vector2(mr, mb)));
	at->set_filter_clip(_arg_bool(p_args, "filter_clip", false));

	const Error save_err = ResourceSaver::save(at, save_path);
	if (save_err != OK) {
		return _make_error(vformat("Failed to save atlas texture: error %d", save_err));
	}

	Dictionary extra;
	extra["save_path"] = save_path;
	extra["atlas_path"] = atlas_path;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_import_asset(const Dictionary &p_args) const {
	const String source_path = _arg_string(p_args, "source_path", "");
	if (source_path.is_empty()) {
		return _make_error("source_path is required");
	}

	const String dest_path = _arg_string(p_args, "dest_path", "");
	if (!dest_path.begins_with("res://")) {
		return _make_error("dest_path must start with res://");
	}

	const bool overwrite = _arg_bool(p_args, "overwrite", false);

	if (!overwrite && FileAccess::exists(dest_path)) {
		return _make_error("Destination file already exists. Set overwrite=true to replace it.");
	}

	if (!FileAccess::exists(source_path)) {
		return _make_error("Source file does not exist: " + source_path);
	}

	const String dest_dir = dest_path.get_base_dir();
	{
		Error dir_err = OK;
		Ref<DirAccess> da = DirAccess::open(dest_dir, &dir_err);
		if (da.is_null() || dir_err != OK) {
			dir_err = DirAccess::make_dir_recursive_absolute(dest_dir);
			if (dir_err != OK) {
				return _make_error(vformat("Failed to create destination directory: error %d", dir_err));
			}
		}
	}

	const Error copy_err = DirAccess::copy_absolute(source_path, dest_path);
	if (copy_err != OK) {
		return _make_error(vformat("Failed to copy file: error %d", copy_err));
	}

	EditorFileSystem::get_singleton()->scan();

	Dictionary extra;
	extra["source_path"] = source_path;
	extra["dest_path"] = dest_path;
	extra["overwrite"] = overwrite;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_set_import_setting(const Dictionary &p_args) const {
	return _make_error("Import setting changes require restarting the editor. Use the Import dock manually or modify the .import file.");
}
// ═══════════════════════════════════════════════════════════════════════════
// K. Navigation & AI
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_bake_navigation_mesh(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	NavigationRegion3D *nav_region = Object::cast_to<NavigationRegion3D>(node);
	if (nav_region == nullptr) {
		return _make_error("Target node is not a NavigationRegion3D");
	}

	nav_region->bake_navigation_mesh(false);

	_mark_unsaved();
	Dictionary extra;
	extra["node_path"] = String(node->get_path());
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_create_navigation_link(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String link_type = _arg_string(p_args, "link_type", "3d").to_lower();
	const String node_name = _arg_string(p_args, "name", "NavigationLink");
	const bool bidirectional = _arg_bool(p_args, "bidirectional", true);

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}

	if (link_type == "2d") {
		NavigationLink2D *link = memnew(NavigationLink2D);
		link->set_name(node_name);
		link->set_start_position(Vector2(_arg_float(p_args, "start_x", 0.0), _arg_float(p_args, "start_y", 0.0)));
		link->set_end_position(Vector2(_arg_float(p_args, "end_x", 0.0), _arg_float(p_args, "end_y", 0.0)));
		link->set_bidirectional(bidirectional);

		_add_to_scene(parent, link, scene_root);

		Dictionary extra;
		extra["node_path"] = String(link->get_path());
		extra["link_type"] = "2d";
		return _make_ok(extra);

	} else {
		NavigationLink3D *link = memnew(NavigationLink3D);
		link->set_name(node_name);
		link->set_start_position(Vector3(_arg_float(p_args, "start_x", 0.0), _arg_float(p_args, "start_y", 0.0), _arg_float(p_args, "start_z", 0.0)));
		link->set_end_position(Vector3(_arg_float(p_args, "end_x", 0.0), _arg_float(p_args, "end_y", 0.0), _arg_float(p_args, "end_z", 0.0)));
		link->set_bidirectional(bidirectional);

		Node3D *parent_3d = Object::cast_to<Node3D>(parent);
		if (parent_3d == nullptr) {
			parent_3d = Object::cast_to<Node3D>(scene_root);
		}
		_add_to_scene(parent_3d, link, scene_root);

		Dictionary extra;
		extra["node_path"] = String(link->get_path());
		extra["link_type"] = "3d";
		return _make_ok(extra);
	}
}

Dictionary YeetAIDock::_tool_create_navigation_obstacle(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	if (!_resolve_scene(p_args, &scene_root, err)) {
		return _make_error(err);
	}

	const String obstacle_type = _arg_string(p_args, "obstacle_type", "3d").to_lower();
	const String node_name = _arg_string(p_args, "name", "NavigationObstacle");

	Node *parent = _resolve_node_target(scene_root, _arg_string(p_args, "parent_path", ""), err);
	if (parent == nullptr) {
		parent = scene_root;
	}

	if (obstacle_type == "2d") {
		NavigationObstacle2D *obstacle = memnew(NavigationObstacle2D);
		obstacle->set_name(node_name);

		Array vertices = _arg_array(p_args, "vertices");
		if (!vertices.is_empty()) {
			Vector<Vector2> verts;
			for (int i = 0; i < vertices.size(); i++) {
				Dictionary vd = vertices[i];
				verts.push_back(Vector2(vd.get("x", 0.0), vd.get("y", 0.0)));
			}
			obstacle->set_vertices(verts);
		}

		_add_to_scene(parent, obstacle, scene_root);

		Dictionary extra;
		extra["node_path"] = String(obstacle->get_path());
		extra["obstacle_type"] = "2d";
		return _make_ok(extra);

	} else {
		NavigationObstacle3D *obstacle = memnew(NavigationObstacle3D);
		obstacle->set_name(node_name);
		obstacle->set_radius(_arg_float(p_args, "radius", 1.0));

		Array vertices = _arg_array(p_args, "vertices");
		if (!vertices.is_empty()) {
			Vector<Vector3> verts;
			for (int i = 0; i < vertices.size(); i++) {
				Dictionary vd = vertices[i];
				verts.push_back(Vector3(vd.get("x", 0.0), vd.get("y", 0.0), vd.get("z", 0.0)));
			}
			obstacle->set_vertices(verts);
		}

		Node3D *parent_3d = Object::cast_to<Node3D>(parent);
		if (parent_3d == nullptr) {
			parent_3d = Object::cast_to<Node3D>(scene_root);
		}
		_add_to_scene(parent_3d, obstacle, scene_root);

		Dictionary extra;
		extra["node_path"] = String(obstacle->get_path());
		extra["obstacle_type"] = "3d";
		return _make_ok(extra);
	}
}

Dictionary YeetAIDock::_tool_get_navigation_path(const Dictionary &p_args) const {
	return _make_error("Navigation path queries require a running game. Use play_current_scene + run_gdscript_expression to query NavigationServer3D at runtime.");
}

Dictionary YeetAIDock::_tool_set_navigation_agent_params(const Dictionary &p_args) const {
	String err;
	Node *scene_root;
	Node *node;
	if (!_resolve_scene_and_node(p_args, "node_path", &scene_root, &node, err)) {
		return _make_error(err);
	}

	if (p_args.has("path_offset")) {
		const StringName property = SNAME("path_offset");
		_commit_ai_property_change(node, property, node->get(property), _arg_float(p_args, "path_offset", 0.0), "Set Navigation Agent Parameter");
	}
	if (p_args.has("avoidance_enabled")) {
		const StringName property = SNAME("avoidance_enabled");
		_commit_ai_property_change(node, property, node->get(property), _arg_bool(p_args, "avoidance_enabled", false), "Set Navigation Agent Parameter");
	}
	if (p_args.has("path_desired_distance")) {
		const StringName property = SNAME("path_desired_distance");
		_commit_ai_property_change(node, property, node->get(property), _arg_float(p_args, "path_desired_distance", 1.0), "Set Navigation Agent Parameter");
	}
	if (p_args.has("target_desired_distance")) {
		const StringName property = SNAME("target_desired_distance");
		_commit_ai_property_change(node, property, node->get(property), _arg_float(p_args, "target_desired_distance", 1.0), "Set Navigation Agent Parameter");
	}
	if (p_args.has("path_max_distance")) {
		const StringName property = SNAME("path_max_distance");
		_commit_ai_property_change(node, property, node->get(property), _arg_float(p_args, "path_max_distance", 10.0), "Set Navigation Agent Parameter");
	}
	if (p_args.has("navigation_layers")) {
		const StringName property = SNAME("navigation_layers");
		_commit_ai_property_change(node, property, node->get(property), _arg_int(p_args, "navigation_layers", 1), "Set Navigation Agent Parameter");
	}

	Dictionary extra;
	extra["node_path"] = String(node->get_path());
	return _make_ok(extra);
}
// ═══════════════════════════════════════════════════════════════════════════
// M. Debugging & Runtime Inspection
// ═══════════════════════════════════════════════════════════════════════════

Dictionary YeetAIDock::_tool_inspect_runtime_variable(const Dictionary &p_args) const {
	return _make_error("Runtime variable inspection is asynchronous. Use run_gdscript_expression with the expression 'variable_name' to inspect variables at runtime.");
}

Dictionary YeetAIDock::_tool_set_breakpoint(const Dictionary &p_args) const {
	const String script_path = _arg_string(p_args, "script_path", "");
	if (script_path.is_empty()) {
		return _make_error("script_path is required");
	}

	const int line = _arg_int(p_args, "line", -1);
	if (line < 0) {
		return _make_error("line must be a non-negative integer");
	}

	const bool remove = _arg_bool(p_args, "remove", false);

	EditorDebuggerNode *dbg = EditorDebuggerNode::get_singleton();
	if (dbg == nullptr) {
		return _make_error("EditorDebuggerNode is not available");
	}

	dbg->set_breakpoint(script_path, line, !remove);

	Dictionary extra;
	extra["script_path"] = script_path;
	extra["line"] = line;
	extra["remove"] = remove;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_debugger_continue(const Dictionary &p_args) const {
	const String action = _arg_string(p_args, "action", "continue").to_lower();

	EditorDebuggerNode *dbg = EditorDebuggerNode::get_singleton();
	if (dbg == nullptr) {
		return _make_error("EditorDebuggerNode is not available");
	}

	if (action == "continue") {
		dbg->debug_continue();
	} else if (action == "step_over") {
		dbg->debug_next();
	} else if (action == "step_into") {
		dbg->debug_step();
	} else if (action == "break") {
		dbg->debug_break();
	} else {
		return _make_error("Unknown action. Use: continue, step_over, step_into, break");
	}

	Dictionary extra;
	extra["action"] = action;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_get_console_output(const Dictionary &p_args) const {
	return _make_error("Console output is captured by the editor log. Use get_editor_log to read runtime output.");
}

Dictionary YeetAIDock::_tool_profile_frame(const Dictionary &p_args) const {
	return _make_error("Profiling requires the profiler to be active. Open the Profiler dock, start profiling, then use get_runtime_debugger_state to read results.");
}

Dictionary YeetAIDock::_tool_monitor_runtime_performance(const Dictionary &p_args) const {
	Performance *perf = Performance::get_singleton();
	if (perf == nullptr) {
		return _make_error("Performance singleton is not available");
	}

	Dictionary result;
	Array requested = _arg_array(p_args, "monitors");

	const int monitor_count = (int)Performance::MONITOR_MAX;

	if (requested.is_empty()) {
		for (int i = 0; i < monitor_count; i++) {
			const StringName name = perf->get_monitor_name(Performance::Monitor(i));
			result[name] = perf->get_monitor(Performance::Monitor(i));
		}
	} else {
		for (int i = 0; i < requested.size(); i++) {
			const String monitor_name = String(requested[i]).to_lower();
			bool found = false;
			for (int j = 0; j < monitor_count; j++) {
				const StringName name = perf->get_monitor_name(Performance::Monitor(j));
				if (String(name).to_lower() == monitor_name) {
					result[name] = perf->get_monitor(Performance::Monitor(j));
					found = true;
					break;
				}
			}
			if (!found) {
				result[monitor_name] = Variant();
			}
		}
	}

	Dictionary extra;
	extra["monitors"] = result;
	return _make_ok(extra);
}

Dictionary YeetAIDock::_tool_inspect_runtime_node(const Dictionary &p_args) const {
	return _make_error("Runtime node inspection requires the game to be running. Use get_remote_scene_tree + get_node_details pattern with run_gdscript_expression.");
}
