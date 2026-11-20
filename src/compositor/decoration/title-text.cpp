#include "title-text.hpp"

#include <algorithm>
#include <cmath>

#include <pango/pangocairo.h>

#include "fill-painter.hpp"

namespace themed
{
cairo_surface_t *render_title(const window_theme_t& theme, const std::string& text, int width,
    int height, double scale, bool active, double preferred_center)
{
    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if ((width <= 0) || (height <= 0))
    {
        return surface;
    }

    auto cr = cairo_create(surface);

    // the theme gives the font size in logical pixels, not points
    const double font_pixels = theme.title_font_size * scale;

    PangoFontDescription *font = pango_font_description_from_string(
        theme.title_font_family.c_str());
    pango_font_description_set_absolute_size(font, font_pixels * PANGO_SCALE);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text.c_str(), text.size());

    pango_layout_set_width(layout, width * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(layout, TRUE);

    int text_width, text_height;
    pango_layout_get_pixel_size(layout, &text_width, &text_height);

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
        x = std::clamp(x, 0.0, std::max(0.0, (double)(width - text_width)));
        break;
    }

    const double y = (height - text_height) / 2.0;

    // whole pixels, so the text is not blurred
    const double left = std::round(x);
    const double top  = std::round(y);

    set_source_color(cr, active ? theme.title_shadow_color : theme.title_shadow_color_inactive);
    cairo_move_to(cr, left, top + std::round(theme.title_shadow_drop * scale));
    pango_cairo_show_layout(cr, layout);

    const auto& gradient = active ? theme.title_gradient : theme.title_gradient_inactive;
    if (gradient.empty())
    {
        set_source_color(cr, active ? theme.title_color : theme.title_color_inactive);
    } else
    {
        PangoRectangle ink;
        pango_layout_get_extents(layout, &ink, nullptr);
        const double ink_top = top + (double)ink.y / PANGO_SCALE;
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

    cairo_move_to(cr, left, top);
    pango_cairo_show_layout(cr, layout);

    pango_font_description_free(font);
    g_object_unref(layout);
    cairo_destroy(cr);

    return surface;
}
}
