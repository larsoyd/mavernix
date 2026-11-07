// rounds the corners that are the program's own picture, the bottom two of a framed window or all four
// only the corner squares go through a buffer, the frame first and the window cut round over it

#pragma once

#include <wayfire/opengl.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/toplevel-view.hpp>

namespace wf
{
class corner_clip_transformer_t : public wf::scene::transformer_base_node_t
{
  public:
    enum corner_t { TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT };

    struct square_t
    {
        corner_t corner;
        wf::geometry_t box;
    };

    // radii in logical pixels, 0 keeps those corners square, the frame shows uncut outside the rounding
    corner_clip_transformer_t(wayfire_toplevel_view view, double top_radius, double bottom_radius,
        std::shared_ptr<wf::scene::node_t> frame);
    ~corner_clip_transformer_t() override;

    /** Whether the shader could be prepared: only with the GLES renderer. */
    bool can_draw() const;

    // in the window's coordinates
    std::vector<square_t> corner_squares() const;
    wf::geometry_t window_box() const;
    double radius_of(corner_t corner) const;

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on) override;

    std::string stringify() const override;

    double top_radius;
    double bottom_radius;
    // leave out what the program draws outside its window, its own shadow, and show the frame there
    bool cut_outside = false;
    OpenGL::program_t program;
    // Per corner: the whole window, and the frame alone.
    wf::auxilliary_buffer_t window_buffers[4];
    wf::auxilliary_buffer_t frame_buffers[4];
    std::weak_ptr<wf::scene::node_t> frame;

  private:
    std::weak_ptr<wf::toplevel_view_interface_t> _view;
    bool program_ready = false;
};
}
