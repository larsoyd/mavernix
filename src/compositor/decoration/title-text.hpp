#pragma once

#include <string>

#include <cairo.h>

#include "theme-file.hpp"

namespace themed
{
// a window's title as a picture, sizes and the wanted middle in device pixels
cairo_surface_t *render_title(const window_theme_t& theme, const std::string& text, int width,
    int height, double scale, bool active, double preferred_center);
}
