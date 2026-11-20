#pragma once

#include <string>

#include <cairo.h>
#include <wayland-server-protocol.h>

#include "theme-file.hpp"

namespace themed
{
// the order of a screen's subpixels from left to right, none when they are not side by side
enum class subpixel_t
{
    RGB,
    BGR,
    NONE,
};

// what a screen's subpixels allow, a turned or mirrored screen turns them with it
subpixel_t subpixel_order(wl_output_subpixel subpixel, wl_output_transform transform);

// draws a window's title onto its bar inside box, sizes and the wanted middle in device pixels,
// the middle measured from the box's left edge
void draw_title(cairo_surface_t *bar, const window_theme_t& theme, const std::string& text,
    const cairo_rectangle_int_t& box, double scale, bool active, double preferred_center,
    subpixel_t order = subpixel_t::RGB);
}
