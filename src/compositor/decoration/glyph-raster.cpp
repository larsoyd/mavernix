#include "glyph-raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include <cairo.h>
#include <hb.h>

namespace themed
{
namespace
{
// em fractions each edge moves out by, and the most it may move in pixels
constexpr double DILATE_X   = 0.0121;
constexpr double DILATE_Y   = 0.015125;
constexpr double DILATE_CAP = 0.3;
constexpr int SAMPLES = 4;
constexpr int TAPS    = 19;
// taps centred 7 samples left of a pixel's first sample, each kernel sums to 1023 of 1024
constexpr std::array<int, TAPS> RED_KERNEL = {1, 0, -13, -30, -21, 41, 148, 257, 297, 238, 129, 30, -21, -24, -9, 0, 0, 0, 0};
constexpr std::array<int, TAPS> GREEN_KERNEL = {0, 0, 1, -11, -30, -27, 33, 143, 258, 312, 251, 133, 26, -30, -29, -9, 2, 0, 0};
constexpr std::array<int, TAPS> BLUE_KERNEL = {0, 0, -1, -2, -10, -21, -14, 40, 137, 238, 290, 247, 141, 38, -21, -29, -12, 0, 1};

struct point_t
{
    double x, y;
};

using contour_t = std::vector<point_t>;

// outlines in pixels with y up from the baseline
struct outline_sink_t
{
    cairo_t *cr;
    double scale, pen_x, pen_y;
};

void move_to(hb_draw_funcs_t*, void *data, hb_draw_state_t*, float x, float y, void*)
{
    auto sink = static_cast<outline_sink_t*>(data);
    cairo_move_to(sink->cr, sink->pen_x + x * sink->scale, sink->pen_y + y * sink->scale);
}

void line_to(hb_draw_funcs_t*, void *data, hb_draw_state_t*, float x, float y, void*)
{
    auto sink = static_cast<outline_sink_t*>(data);
    cairo_line_to(sink->cr, sink->pen_x + x * sink->scale, sink->pen_y + y * sink->scale);
}

void quadratic_to(hb_draw_funcs_t*, void *data, hb_draw_state_t *state, float cx, float cy,
    float x, float y, void*)
{
    auto sink = static_cast<outline_sink_t*>(data);
    const double s = sink->scale;
    const double x0 = state->current_x, y0 = state->current_y;
    cairo_curve_to(sink->cr,
        sink->pen_x + (x0 + 2 * (cx - x0) / 3) * s, sink->pen_y + (y0 + 2 * (cy - y0) / 3) * s,
        sink->pen_x + (x + 2 * (cx - x) / 3) * s, sink->pen_y + (y + 2 * (cy - y) / 3) * s,
        sink->pen_x + x * s, sink->pen_y + y * s);
}

void cubic_to(hb_draw_funcs_t*, void *data, hb_draw_state_t*, float c1x, float c1y, float c2x,
    float c2y, float x, float y, void*)
{
    auto sink = static_cast<outline_sink_t*>(data);
    const double s = sink->scale;
    cairo_curve_to(sink->cr, sink->pen_x + c1x * s, sink->pen_y + c1y * s, sink->pen_x + c2x * s,
        sink->pen_y + c2y * s, sink->pen_x + x * s, sink->pen_y + y * s);
}

void close_path(hb_draw_funcs_t*, void *data, hb_draw_state_t*, void*)
{
    cairo_close_path(static_cast<outline_sink_t*>(data)->cr);
}

hb_draw_funcs_t *outline_funcs()
{
    static hb_draw_funcs_t *funcs = [] {
        hb_draw_funcs_t *made = hb_draw_funcs_create();
        hb_draw_funcs_set_move_to_func(made, move_to, nullptr, nullptr);
        hb_draw_funcs_set_line_to_func(made, line_to, nullptr, nullptr);
        hb_draw_funcs_set_quadratic_to_func(made, quadratic_to, nullptr, nullptr);
        hb_draw_funcs_set_cubic_to_func(made, cubic_to, nullptr, nullptr);
        hb_draw_funcs_set_close_path_func(made, close_path, nullptr, nullptr);
        hb_draw_funcs_make_immutable(made);
        return made;
    }();
    return funcs;
}

double signed_area(const contour_t& contour)
{
    double area = 0;
    for (size_t i = 0; i < contour.size(); i++)
    {
        const point_t& a = contour[i];
        const point_t& b = contour[(i + 1) % contour.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area / 2;
}

// flattened contours of one glyph, outer ones turning left so that dilation grows them
std::vector<contour_t> glyph_contours(hb_font_t *font, hb_codepoint_t glyph, double scale,
    double pen_x)
{
    cairo_surface_t *scratch = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t *cr = cairo_create(scratch);
    cairo_set_tolerance(cr, 0.01);
    outline_sink_t sink{cr, scale, pen_x, 0};
    hb_font_draw_glyph(font, glyph, outline_funcs(), &sink);

    std::vector<contour_t> contours;
    cairo_path_t *path = cairo_copy_path_flat(cr);
    for (int i = 0; i < path->num_data; i += path->data[i].header.length)
    {
        const cairo_path_data_t& part = path->data[i];
        if (part.header.type == CAIRO_PATH_MOVE_TO)
        {
            contours.emplace_back();
        }
        if ((part.header.type == CAIRO_PATH_MOVE_TO) || (part.header.type == CAIRO_PATH_LINE_TO))
        {
            contours.back().push_back({path->data[i + 1].point.x, path->data[i + 1].point.y});
        }
    }
    cairo_path_destroy(path);
    cairo_destroy(cr);
    cairo_surface_destroy(scratch);

    contours.erase(std::remove_if(contours.begin(), contours.end(),
        [] (const contour_t& contour) { return contour.size() < 3; }), contours.end());
    double area = 0;
    for (const contour_t& contour : contours)
    {
        area += signed_area(contour);
    }
    if (area < 0)
    {
        for (contour_t& contour : contours)
        {
            std::reverse(contour.begin(), contour.end());
        }
    }
    return contours;
}

// each edge moves out along its run, then everything rises, so stems widen and letters grow up
contour_t dilate(const contour_t& contour, double dx, double dy)
{
    auto sign = [] (double value) { return double((value > 0) - (value < 0)); };
    contour_t out;
    for (size_t i = 0; i < contour.size(); i++)
    {
        const point_t& a = contour[i];
        const point_t& b = contour[(i + 1) % contour.size()];
        const double sx = dx * sign(b.y - a.y);
        const double sy = dy - dy * sign(b.x - a.x);
        out.push_back({a.x + sx, a.y + sy});
        out.push_back({b.x + sx, b.y + sy});
    }
    return out;
}

struct knots_t
{
    std::array<double, 6> from, to;
};

struct fit_t
{
    knots_t knots;
    double bend;
};

fit_t stem_preserving_fit(double o, double cap, double top, double stem, double d, double o_to,
    double cap_to)
{
    const double bar = std::min(std::min(stem, 0.333 * o), (cap - o) * 0.5);
    knots_t k{{-d, bar + d, o + d, cap - bar - d, cap + d, top + d},
        {-d, bar + d, o_to - d, cap_to - bar - 3 * d, cap_to - d, top + d}};
    const double low = (k.to[2] - k.to[1]) / (k.from[2] - k.from[1]);
    double middle    = (k.to[3] - k.to[2]) / (k.from[3] - k.from[2]);
    if ((middle < 0.75) || (middle - low > 0.3))
    {
        k.to[4] = cap + d;
        k.to[3] = cap - bar - d;
        middle  = (k.to[3] - k.to[2]) / (k.from[3] - k.from[2]);
    }
    return {k, std::abs(low - 1) + std::abs(middle - 1)};
}

std::optional<double> glyph_top(hb_font_t *font, char letter, double scale)
{
    hb_codepoint_t glyph;
    hb_glyph_extents_t extents;
    if (!hb_font_get_nominal_glyph(font, letter, &glyph) || !hb_font_get_glyph_extents(font, glyph, &extents))
    {
        return {};
    }
    return extents.y_bearing * scale;
}

// x height and cap height land on whole pixels, from 7 to 18 pixels only
std::optional<knots_t> hint_knots(hb_font_t *font, double scale, double pixels, double d)
{
    if ((pixels != std::floor(pixels)) || (pixels < 7) || (pixels > 18))
    {
        return {};
    }
    const auto o = glyph_top(font, 'o', scale), cap = glyph_top(font, 'X', scale);
    hb_codepoint_t l;
    hb_glyph_extents_t stem;
    if (!o || !cap || (*cap <= 0) || (*o / *cap > 0.85) || !hb_font_get_nominal_glyph(font, 'l', &l) ||
        !hb_font_get_glyph_extents(font, l, &stem))
    {
        return {};
    }
    const double stem_width = stem.width * scale, top = *cap * 1.4;
    const fit_t grown = stem_preserving_fit(*o, *cap, top, stem_width, d, std::floor(2 * d + *o + 0.5),
        std::floor(2 * d + *cap + 0.5));
    const fit_t plain = stem_preserving_fit(*o, *cap, top, stem_width, d, std::floor(*o + 0.5),
        std::floor(*cap + 0.5));
    return (grown.bend <= plain.bend) ? grown.knots : plain.knots;
}

// past the end knots a height moves by the shift of the nearest one, so descenders keep their depth
double hint(double y, const knots_t& k)
{
    if (y <= k.from.front())
    {
        return y + k.to.front() - k.from.front();
    }
    if (y >= k.from.back())
    {
        return y + k.to.back() - k.from.back();
    }
    size_t i = 1;
    while (y > k.from[i])
    {
        i++;
    }
    const double t = (y - k.from[i - 1]) / (k.from[i] - k.from[i - 1]);
    return k.to[i - 1] + t * (k.to[i] - k.to[i - 1]);
}

// four columns per pixel, each the mean of four rows, as whole 4096ths
std::vector<int> coverage(const std::vector<contour_t>& contours, int width, int height, int baseline)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_A8, width * SAMPLES, height * SAMPLES);
    cairo_t *cr = cairo_create(surface);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    cairo_scale(cr, SAMPLES, SAMPLES);
    for (const contour_t& contour : contours)
    {
        cairo_move_to(cr, contour[0].x, baseline - contour[0].y);
        for (size_t i = 1; i < contour.size(); i++)
        {
            cairo_line_to(cr, contour[i].x, baseline - contour[i].y);
        }
        cairo_close_path(cr);
    }
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_flush(surface);

    const unsigned char *data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    std::vector<int> samples(size_t(width) * SAMPLES * height);
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width * SAMPLES; x++)
        {
            int sum = 0;
            for (int sub = 0; sub < SAMPLES; sub++)
            {
                sum += data[(y * SAMPLES + sub) * stride + x];
            }
            samples[size_t(y) * width * SAMPLES + x] = std::lround(sum * 4096.0 / (255.0 * SAMPLES));
        }
    }
    cairo_surface_destroy(surface);
    return samples;
}

// 8 bit coverage to the 10 bit weight a blend uses
float weight_of(int level)
{
    return std::lround(level * 1024.0 / 255.0) / 1024.0f;
}

void filter_lcd(const std::vector<int>& samples, glyph_mask_t& mask, mask_kind_t kind)
{
    const std::array<const std::array<int, TAPS>*, 3> kernels = (kind == mask_kind_t::BGR) ?
        std::array<const std::array<int, TAPS>*, 3>{&BLUE_KERNEL, &GREEN_KERNEL, &RED_KERNEL} :
        std::array<const std::array<int, TAPS>*, 3>{&RED_KERNEL, &GREEN_KERNEL, &BLUE_KERNEL};
    const int columns = mask.width * SAMPLES;
    for (int y = 0; y < mask.height; y++)
    {
        const int *row = &samples[size_t(y) * columns];
        for (int x = 0; x < mask.width; x++)
        {
            std::array<int, 3> level;
            for (int channel = 0; channel < 3; channel++)
            {
                long sum = 0;
                for (int tap = 0; tap < TAPS; tap++)
                {
                    const int at = x * SAMPLES + tap - 7;
                    if ((at >= 0) && (at < columns))
                    {
                        sum += long((*kernels[channel])[tap]) * row[at];
                    }
                }
                level[channel] = int(std::floor((sum + 0x1c00) / 16384.0));
            }
            for (int channel = 0; channel < 3; channel++)
            {
                const int five = (level[channel] > 255) ? 31 : (std::max(level[channel], 0) >> 3);
                mask.weight[(size_t(y) * mask.width + x) * 3 + channel] = weight_of((five << 3) | (five >> 2));
            }
        }
    }
}

void filter_grey(const std::vector<int>& samples, glyph_mask_t& mask)
{
    const int columns = mask.width * SAMPLES;
    for (int y = 0; y < mask.height; y++)
    {
        for (int x = 0; x < mask.width; x++)
        {
            int sum = 0;
            for (int sub = 0; sub < SAMPLES; sub++)
            {
                sum += samples[size_t(y) * columns + x * SAMPLES + sub];
            }
            const float weight = weight_of(std::lround(sum * 255.0 / (4096.0 * SAMPLES)));
            std::fill_n(&mask.weight[(size_t(y) * mask.width + x) * 3], 3, weight);
        }
    }
}
} // anonymous namespace

glyph_mask_t rasterize_line(PangoLayout *layout, double x, int baseline, int width, int height,
    mask_kind_t kind)
{
    glyph_mask_t mask;
    mask.width  = width;
    mask.height = height;
    mask.weight.assign(size_t(width) * height * 3, 0.0f);
    PangoLayoutLine *line = pango_layout_get_line_readonly(layout, 0);
    if (!line || (width <= 0) || (height <= 0))
    {
        return mask;
    }

    std::vector<contour_t> contours;
    double top = INFINITY, bottom = -INFINITY;
    double pen = x;
    for (GSList *item = line->runs; item; item = item->next)
    {
        auto run = static_cast<PangoGlyphItem*>(item->data);
        PangoFontDescription *description = pango_font_describe_with_absolute_size(run->item->analysis.font);
        const double pixels = double(pango_font_description_get_size(description)) / PANGO_SCALE;
        pango_font_description_free(description);
        // a font of its own, so no synthetic bold or slant from the session's font rules
        hb_face_t *face = hb_font_get_face(pango_font_get_hb_font(run->item->analysis.font));
        hb_font_t *font = hb_font_create(face);
        const double scale = pixels / hb_face_get_upem(face);
        const double dx = std::min(DILATE_X * pixels, DILATE_CAP);
        const double dy = std::min(DILATE_Y * pixels, DILATE_CAP);
        const std::optional<knots_t> knots = hint_knots(font, scale, pixels, dy);

        for (int i = 0; i < run->glyphs->num_glyphs; i++)
        {
            const PangoGlyphInfo& info = run->glyphs->glyphs[i];
            const double glyph_x = pen + double(info.geometry.x_offset) / PANGO_SCALE;
            pen += double(info.geometry.width) / PANGO_SCALE;
            if ((info.glyph == PANGO_GLYPH_EMPTY) || (info.glyph & PANGO_GLYPH_UNKNOWN_FLAG))
            {
                continue;
            }
            hb_glyph_extents_t extents;
            if (!hb_font_get_glyph_extents(font, info.glyph, &extents) || (extents.width == 0))
            {
                continue;
            }
            top    = std::min(top, baseline - extents.y_bearing * scale);
            bottom = std::max(bottom, baseline - (extents.y_bearing + extents.height) * scale);
            std::vector<contour_t> shapes = glyph_contours(font, info.glyph, scale, glyph_x);
            mask.missing_outline = mask.missing_outline || shapes.empty();
            for (const contour_t& shape : shapes)
            {
                contour_t grown = dilate(shape, dx, dy);
                if (knots)
                {
                    for (point_t& p : grown)
                    {
                        p.y = hint(p.y, *knots);
                    }
                }
                contours.push_back(std::move(grown));
            }
        }
        hb_font_destroy(font);
    }

    if (contours.empty())
    {
        return mask;
    }
    mask.bounds_top    = int(std::floor(top));
    mask.bounds_bottom = int(std::ceil(bottom));
    const std::vector<int> samples = coverage(contours, width, height, baseline);
    if (kind == mask_kind_t::GREY)
    {
        filter_grey(samples, mask);
    } else
    {
        filter_lcd(samples, mask, kind);
    }
    return mask;
}
}
