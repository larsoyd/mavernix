// A Wayfire transformer that draws a minimising window as a triangle strip
// between the rows warp-geometry gives.
//
// Adapted from the "squeezimize" effect of Wayfire's animate plugin (see
// UPSTREAM.md), which bends the picture in a shader instead.

#pragma once

#include <wayfire/opengl.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/view.hpp>

#include "warp-geometry.hpp"

namespace themed_minimize
{
enum class effect_t
{
    GENIE,
    SCALE,
};

class warp_transformer_t : public wf::scene::view_2d_transformer_t
{
  public:
    // the icon in output pixels, empty for the middle of the dock's edge, toward the side the dock is on
    // the duration's curve should be linear as the shapes ease themselves
    warp_transformer_t(wayfire_view view, effect_t effect,
        wf::animation_description_t duration, wf::geometry_t icon, warp::toward_t toward = warp::toward_t::below);
    ~warp_transformer_t() override;

    void start(bool hiding);
    void reverse();
    bool is_running();

    /** The window's rectangle, frame included, without its shadow. */
    wf::geometry_t window_frame(wf::geometry_t picture_area);

    warp::shape_t current_shape(wf::geometry_t window);

    wf::geometry_t get_bounding_box() override;
    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on) override;

    bool has_held_picture();
    // draws the strip moved by offset, which is zero on the window's own screen
    void draw(const wf::scene::render_instruction_t& data, wf::gles_texture_t picture, wf::pointf_t offset);
    // the other screens must repaint with each step
    void damage_carrier();

    OpenGL::program_t program;
    // The window and the icon.
    wf::geometry_t affected_area;
    // a hiding window may blank itself once told, long before the effect ends
    wf::auxilliary_buffer_t held_picture;

  private:
    void hold_picture();

    std::weak_ptr<wf::view_interface_t> _view;
    // set when the icon is on another screen than the window
    wf::scene::node_ptr carrier;
    effect_t effect;
    wf::geometry_t icon;
    warp::toward_t toward;
    // Runs from 0 (the window as it is) to 1 (inside the icon).
    wf::animation::simple_animation_t progress;
};
}
