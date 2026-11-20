#pragma once

#include <vector>

#include <pango/pango.h>

namespace themed
{
enum class mask_kind_t
{
    RGB,
    BGR,
    GREY,
};

// how much of each pixel's red, green and blue the letters cover, 0 to 1
struct glyph_mask_t
{
    int width  = 0;
    int height = 0;
    std::vector<float> weight;
    // rows of the glyphs' own boxes, outward to whole pixels
    int bounds_top    = 0;
    int bounds_bottom = 0;
    // a glyph had no outline to draw, a colour emoji for one
    bool missing_outline = false;

    float at(int x, int y, int channel) const
    {
        return weight[(size_t(y) * width + x) * 3 + channel];
    }
};

// the first line of layout with its pen starting at x and its baseline on row baseline,
// dilated, hinted and filtered the way os x 10.9 draws window text
glyph_mask_t rasterize_line(PangoLayout *layout, double x, int baseline, int width, int height,
    mask_kind_t kind);
}
