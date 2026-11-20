#include "title-text.hpp"

#include <algorithm>
#include <cmath>

#include <pango/pangocairo.h>

#include "fill-painter.hpp"
#include "glyph-raster.hpp"

namespace themed
{
namespace
{
struct placed_t
{
    double left, top, baseline;
};

PangoLayout *title_layout(cairo_t *cr, const window_theme_t& theme, const std::string& text,
    int width, double scale, bool fractional)
{
    PangoContext *context = pango_cairo_create_context(cr);
    if (fractional)
    {
        cairo_font_options_t *options = cairo_font_options_create();
        cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_NONE);
        cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_OFF);
        pango_cairo_context_set_font_options(context, options);
        cairo_font_options_destroy(options);
        pango_context_set_round_glyph_positions(context, FALSE);
    }

    // the theme gives the font size in logical pixels, not points
    PangoFontDescription *font = pango_font_description_from_string(theme.title_font_family.c_str());
    pango_font_description_set_absolute_size(font, theme.title_font_size * scale * PANGO_SCALE);

    PangoLayout *layout = pango_layout_new(context);
    g_object_unref(context);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_text(layout, text.c_str(), text.size());
    pango_layout_set_width(layout, width * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    return layout;
}

placed_t place(PangoLayout *layout, const window_theme_t& theme, int width, int height,
    double preferred_center)
{
    PangoRectangle logical;
    pango_layout_get_extents(layout, nullptr, &logical);
    const double text_width  = double(logical.width) / PANGO_SCALE;
    const double text_height = double(logical.height) / PANGO_SCALE;

    double x = 0;
    switch (theme.title_align)
    {
      case title_align_t::LEFT:
        x = 0;
        break;

      case title_align_t::RIGHT:
        x = width - text_width;
        break;

      case title_align_t::CENTER:
        // centred on the window, kept inside the space given
        x = preferred_center - text_width / 2.0;
        x = std::clamp(x, 0.0, std::max(0.0, width - text_width));
        break;
    }

    // whole pixels, so the text is not blurred
    const double y = (height - text_height) / 2.0;
    return {std::round(x), std::round(y),
        std::round(y + double(pango_layout_get_baseline(layout)) / PANGO_SCALE)};
}

void draw_plain(cairo_t *cr, PangoLayout *layout, const window_theme_t& theme, const placed_t& at,
    double scale, bool active)
{
    set_source_color(cr, active ? theme.title_shadow_color : theme.title_shadow_color_inactive);
    cairo_move_to(cr, at.left, at.top + std::round(theme.title_shadow_drop * scale));
    pango_cairo_show_layout(cr, layout);

    const auto& gradient = active ? theme.title_gradient : theme.title_gradient_inactive;
    if (gradient.empty())
    {
        set_source_color(cr, active ? theme.title_color : theme.title_color_inactive);
    } else
    {
        PangoRectangle ink;
        pango_layout_get_extents(layout, &ink, nullptr);
        const double ink_top = at.top + (double)ink.y / PANGO_SCALE;
        cairo_pattern_t *paint = cairo_pattern_create_linear(0, ink_top,
            0, ink_top + (double)ink.height / PANGO_SCALE);
        for (size_t stop = 0; stop < gradient.size(); stop++)
        {
            const rgba_t& color = gradient[stop];
            cairo_pattern_add_color_stop_rgba(paint, stop, color.red, color.green, color.blue, color.alpha);
        }

        cairo_set_source(cr, paint);
        cairo_pattern_destroy(paint);
    }

    cairo_move_to(cr, at.left, at.top);
    pango_cairo_show_layout(cr, layout);
}

// the gradient lies over the glyphs' boxes, made in 8 bit steps as a bitmap pattern would be
rgba_t row_color(const std::vector<rgba_t>& gradient, const rgba_t& flat, const glyph_mask_t& mask, int y)
{
    if (gradient.empty() || (mask.bounds_bottom <= mask.bounds_top))
    {
        return flat;
    }
    const double t = std::clamp((y + 0.5 - mask.bounds_top) / (mask.bounds_bottom - mask.bounds_top), 0.0, 1.0);
    auto step = [t] (double a, double b) { return std::round((a + (b - a) * t) * 255) / 255; };
    const rgba_t& a = gradient[0];
    const rgba_t& b = gradient[1];
    return {step(a.red, b.red), step(a.green, b.green), step(a.blue, b.blue), step(a.alpha, b.alpha)};
}

// mixes in gamma 2 under the letters' coverage, then by the colour's own alpha in a straight line
void blend(unsigned char *pixel, const glyph_mask_t& mask, int mx, int my, const rgba_t& color)
{
    const double coverage_red = mask.at(mx, my, 0), coverage_green = mask.at(mx, my, 1),
        coverage_blue = mask.at(mx, my, 2);
    if ((coverage_red == 0) && (coverage_green == 0) && (coverage_blue == 0))
    {
        return;
    }

    uint32_t& value = *reinterpret_cast<uint32_t*>(pixel);
    const double alpha = (value >> 24) / 255.0;
    const double source[3] = {color.red, color.green, color.blue};
    const double coverage[3] = {coverage_red, coverage_green, coverage_blue};
    const int shifts[3] = {16, 8, 0};
    const double reach = color.alpha * (coverage_red + coverage_green + coverage_blue) / 3;
    const double alpha_out = alpha + (1 - alpha) * reach;
    uint32_t out = uint32_t(std::lround(alpha_out * 255)) << 24;
    for (int channel = 0; channel < 3; channel++)
    {
        const double d = (alpha > 0) ? ((value >> shifts[channel]) & 255) / 255.0 / alpha : 0.0;
        const double w = coverage[channel], s = source[channel];
        const double mixed = d + (std::sqrt(w * s * s + (1 - w) * d * d) - d) * color.alpha;
        out |= uint32_t(std::lround(std::clamp(mixed * alpha_out, 0.0, 1.0) * 255)) << shifts[channel];
    }
    value = out;
}

void draw_smoothed(cairo_surface_t *bar, const cairo_rectangle_int_t& box, const glyph_mask_t& mask,
    const window_theme_t& theme, double scale, bool active)
{
    cairo_surface_flush(bar);
    unsigned char *data = cairo_image_surface_get_data(bar);
    const int stride = cairo_image_surface_get_stride(bar);
    const int bar_width = cairo_image_surface_get_width(bar), bar_height = cairo_image_surface_get_height(bar);
    auto each_pixel = [&] (int drop, auto&& paint)
    {
        for (int y = std::max(0, -box.y); y < std::min(box.height, bar_height - box.y); y++)
        {
            if ((y - drop < 0) || (y - drop >= mask.height))
            {
                continue;
            }
            for (int x = std::max(0, -box.x); x < std::min(box.width, bar_width - box.x); x++)
            {
                paint(data + (box.y + y) * stride + (box.x + x) * 4, x, y - drop, y);
            }
        }
    };

    const rgba_t& shadow = active ? theme.title_shadow_color : theme.title_shadow_color_inactive;
    if (shadow.alpha > 0)
    {
        each_pixel(std::round(theme.title_shadow_drop * scale), [&] (unsigned char *pixel, int x, int y, int)
        {
            blend(pixel, mask, x, y, shadow);
        });
    }

    const auto& gradient = active ? theme.title_gradient : theme.title_gradient_inactive;
    const rgba_t& flat   = active ? theme.title_color : theme.title_color_inactive;
    each_pixel(0, [&] (unsigned char *pixel, int x, int y, int row)
    {
        blend(pixel, mask, x, y, row_color(gradient, flat, mask, row));
    });
    cairo_surface_mark_dirty_rectangle(bar, box.x, box.y, box.width, box.height);
}

mask_kind_t mask_kind(const window_theme_t& theme, double scale, subpixel_t order)
{
    // a picture that will be resampled or a screen without side by side subpixels gets grey letters
    if ((theme.title_smoothing == title_smoothing_t::GRAYSCALE) || (order == subpixel_t::NONE) ||
        (scale != std::floor(scale)))
    {
        return mask_kind_t::GREY;
    }
    return (order == subpixel_t::BGR) ? mask_kind_t::BGR : mask_kind_t::RGB;
}
} // anonymous namespace

subpixel_t subpixel_order(wl_output_subpixel subpixel, wl_output_transform transform)
{
    // a quarter turn stands the subpixels on top of each other
    if (transform & WL_OUTPUT_TRANSFORM_90)
    {
        return subpixel_t::NONE;
    }

    const bool reversed = (transform == WL_OUTPUT_TRANSFORM_180) || (transform == WL_OUTPUT_TRANSFORM_FLIPPED);
    switch (subpixel)
    {
      case WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR:
        return reversed ? subpixel_t::RGB : subpixel_t::BGR;

      case WL_OUTPUT_SUBPIXEL_VERTICAL_RGB:
      case WL_OUTPUT_SUBPIXEL_VERTICAL_BGR:
      case WL_OUTPUT_SUBPIXEL_NONE:
        return subpixel_t::NONE;

      default:
        // most screens are red to blue, and os x assumed it for any screen it could not ask
        return reversed ? subpixel_t::BGR : subpixel_t::RGB;
    }
}

void draw_title(cairo_surface_t *bar, const window_theme_t& theme, const std::string& text,
    const cairo_rectangle_int_t& box, double scale, bool active, double preferred_center,
    subpixel_t order)
{
    const int width = box.width, height = box.height;
    if ((width <= 0) || (height <= 0))
    {
        return;
    }

    auto cr = cairo_create(bar);
    cairo_rectangle(cr, box.x, box.y, width, height);
    cairo_clip(cr);
    cairo_translate(cr, box.x, box.y);

    bool drawn = false;
    if (theme.title_smoothing != title_smoothing_t::PLAIN)
    {
        PangoLayout *layout = title_layout(cr, theme, text, width, scale, true);
        const placed_t at   = place(layout, theme, width, height, preferred_center);
        const glyph_mask_t mask = rasterize_line(layout, at.left, at.baseline, width, height,
            mask_kind(theme, scale, order));
        g_object_unref(layout);
        if (!mask.missing_outline)
        {
            draw_smoothed(bar, box, mask, theme, scale, active);
            drawn = true;
        }
    }

    if (!drawn)
    {
        PangoLayout *layout = title_layout(cr, theme, text, width, scale, false);
        draw_plain(cr, layout, theme, place(layout, theme, width, height, preferred_center), scale, active);
        g_object_unref(layout);
    }

    cairo_destroy(cr);
}
}
