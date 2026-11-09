#include "wayfire/geometry.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel.hpp"
#include <memory>
#define GLM_FORCE_RADIANS
#include <glm/gtc/matrix_transform.hpp>

#include <linux/input-event-codes.h>

#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include "deco-subsurface.hpp"
#include "hide-maximized.hpp"
#include "deco-layout.hpp"
#include "deco-theme.hpp"
#include <wayfire/window-manager.hpp>

#include <wayfire/plugins/common/cairo-util.hpp>
#include <wayfire/util.hpp>
#include <wayfire/util/duration.hpp>

#include <cairo.h>

// The corner clip's place among a window's transformers (lowest first).
const int CORNER_CLIP_Z_ORDER = 0;
const char *const CORNER_CLIP_NAME = "themed-decoration-corner-clip";
// apps that draw their own frame round it near 8 px
const double SELF_FRAMED_CORNER_RADIUS = 8.0;
const char *const FRAMELESS_CLIP_NAME = "themed-decoration-frameless-clip";

static bool frame_hidden(const wf::toplevel_state_t& state)
{
    return state.fullscreen || ((state.tiled_edges == wf::TILED_EDGES_ALL) &&
        wf::get_core().has_data<wf::hide_maximized_frames_t>());
}

class simple_decoration_node_t : public wf::scene::node_t, public wf::pointer_interaction_t,
    public wf::touch_interaction_t
{
    std::weak_ptr<wf::toplevel_view_interface_t> _view;
    wf::signal::connection_t<wf::view_title_changed_signal> title_set =
        [=] (wf::view_title_changed_signal*)
    {
        if (auto view = _view.lock())
        {
            view->damage();
        }
    };

    /** Redraw the title text if anything it depends on changed. Logical pixels. */
    void update_title(int width, int height, double scale, bool active,
        double preferred_center)
    {
        if (auto view = _view.lock())
        {
            wf::dimensions_t target_size = {
                static_cast<int32_t>(width * scale),
                static_cast<int32_t>(height * scale)
            };

            const bool unchanged =
                (title_texture.tex.get_size() == target_size) &&
                (title_texture.current_text == view->get_title()) &&
                (title_texture.drawn_active == active);
            if (unchanged)
            {
                return;
            }

            auto surface = theme.render_text(view->get_title(), target_size.width,
                target_size.height, scale, active, preferred_center * scale);
            title_texture.tex = wf::owned_texture_t{surface};
            cairo_surface_destroy(surface);
            title_texture.current_text = view->get_title();
            title_texture.drawn_active = active;
        }
    }

    struct
    {
        wf::owned_texture_t tex;
        std::string current_text = "";
        bool drawn_active = false;
    } title_texture;

    void update_titlebar(int width, int height, double scale, bool active)
    {
        wf::dimensions_t target_size = {
            static_cast<int32_t>(width * scale),
            static_cast<int32_t>(height * scale)
        };

        const bool unchanged =
            (titlebar_texture.tex.get_size() == target_size) &&
            (titlebar_texture.drawn_active == active);
        if (unchanged)
        {
            return;
        }

        auto surface = theme.render_titlebar(target_size.width, target_size.height, scale, active);
        titlebar_texture.tex = wf::owned_texture_t{surface};
        cairo_surface_destroy(surface);
        titlebar_texture.drawn_active = active;
    }

    struct
    {
        wf::owned_texture_t tex;
        bool drawn_active = false;
    } titlebar_texture;

    struct shadow_piece_t
    {
        wf::owned_texture_t tex;
        // Where it goes, measured from the frame's top left corner.
        wf::geometry_t box;
    };

    struct shadow_pictures_t
    {
        std::vector<shadow_piece_t> pieces;
        wf::dimensions_t laid_out_size = {-1, -1};
        // Whether the pictures were drawn to stretch to any size (see update_shadow).
        bool drawn_stretching = false;
    };

    // one set per state, indexed by active, a fade draws both
    shadow_pictures_t shadow_textures[2];

    static double fast_then_settling(double progress)
    {
        const double left = 1.0 - progress;
        return 1.0 - left * left * left;
    }

    wf::wl_idle_call shadow_fade_frame;

    struct shadow_cut_t
    {
        // Where the piece goes, measured from the frame's top left corner.
        wf::geometry_t box;
        // The part of the shadow its picture shows, on the frame it is drawn for.
        wf::geometry_t drawn_box;
    };

    /**
     * The shadow as four pieces around the frame, drawn for its exact size;
     * used for small frames. Pieces rather than one picture, so that nothing
     * is drawn behind a see-through window's contents.
     */
    std::vector<shadow_cut_t> shadow_cuts_for_one_size(double width, double height,
        double reach, double corner, double bottom_corner) const
    {
        const wf::geometry_t boxes[] = {
            {-reach, -reach, width + 2 * reach, reach + corner},   // above
            {-reach, height, width + 2 * reach, reach},            // below
            {-reach, corner, reach, height - corner},              // left
            {width, corner, reach, height - corner},               // right
        };

        std::vector<shadow_cut_t> cuts;
        for (const wf::geometry_t& box : boxes)
        {
            cuts.push_back({box, box});
        }

        // Behind the two bottom corners, if they are cut round (corner-clip.hpp).
        if (bottom_corner > 0)
        {
            const wf::geometry_t left_corner{0, height - bottom_corner, bottom_corner, bottom_corner};
            const wf::geometry_t right_corner{width - bottom_corner, height - bottom_corner,
                bottom_corner, bottom_corner};
            cuts.push_back({left_corner, left_corner});
            cuts.push_back({right_corner, right_corner});
        }

        return cuts;
    }

    /**
     * The same four bands, each cut into two ends and a one-pixel middle that
     * is stretched. They are drawn for a `stand_in` square frame: a blurred
     * edge has run out `reach` away from a corner, so on any larger frame
     * the ends look the same.
     */
    std::vector<shadow_cut_t> shadow_cuts_for_any_size(double width, double height,
        double reach, double corner, double bottom_corner, double stand_in) const
    {
        const double moved_right = width - stand_in;
        const double moved_down  = height - stand_in;

        std::vector<shadow_cut_t> cuts;
        // Above and below: the bands run across.
        const struct
        {
            double y, band_height, drawn_y;
        } bands_across[] = {
            {-reach, reach + corner, -reach},
            {height, reach, stand_in},
        };
        for (const auto& band : bands_across)
        {
            cuts.push_back({{-reach, band.y, 2 * reach, band.band_height},
                {-reach, band.drawn_y, 2 * reach, band.band_height}});
            cuts.push_back({{reach, band.y, width - 2 * reach, band.band_height},
                {reach, band.drawn_y, 1, band.band_height}});
            cuts.push_back({{width - reach, band.y, 2 * reach, band.band_height},
                {width - reach - moved_right, band.drawn_y, 2 * reach, band.band_height}});
        }

        // Left and right: the bands run down, from under the top corners.
        const struct
        {
            double x, drawn_x;
        } bands_down[] = {
            {-reach, -reach},
            {width, stand_in},
        };
        for (const auto& band : bands_down)
        {
            cuts.push_back({{band.x, corner, reach, reach - corner},
                {band.drawn_x, corner, reach, reach - corner}});
            cuts.push_back({{band.x, reach, reach, height - 2 * reach},
                {band.drawn_x, reach, reach, 1}});
            cuts.push_back({{band.x, height - reach, reach, reach},
                {band.drawn_x, height - reach - moved_down, reach, reach}});
        }

        // Behind the two bottom corners, as in shadow_cuts_for_one_size.
        if (bottom_corner > 0)
        {
            cuts.push_back({{0, height - bottom_corner, bottom_corner, bottom_corner},
                {0, stand_in - bottom_corner, bottom_corner, bottom_corner}});
            cuts.push_back({{width - bottom_corner, height - bottom_corner, bottom_corner, bottom_corner},
                {stand_in - bottom_corner, stand_in - bottom_corner, bottom_corner, bottom_corner}});
        }

        return cuts;
    }

    // drawing takes 5 to 9 ms per window, too slow for a resize or a fade frame
    // so pictures are made once per state and only moved and stretched after that
    void update_shadow(shadow_pictures_t& pictures, bool active)
    {
        const double reach    = theme.get_shadow_reach();
        const double corner   = theme.get_rim_corner_height();
        const double bottom_corner = theme.get_rim_bottom_corner_height();
        const double stand_in = 2 * reach + 1;
        const bool stretching =
            (size.width >= stand_in) && (size.height >= stand_in) && (corner < reach);

        const bool laid_out = (pictures.laid_out_size == size);
        const bool pictures_fit = !pictures.pieces.empty() &&
            (pictures.drawn_stretching == stretching) &&
            (stretching || laid_out);
        if (pictures_fit && laid_out)
        {
            return;
        }

        const std::vector<shadow_cut_t> cuts = stretching ?
            shadow_cuts_for_any_size(size.width, size.height, reach, corner, bottom_corner, stand_in) :
            shadow_cuts_for_one_size(size.width, size.height, reach, corner, bottom_corner);

        if (pictures_fit)
        {
            for (size_t piece = 0; piece < cuts.size(); piece++)
            {
                pictures.pieces[piece].box = cuts[piece].box;
            }
        } else
        {
            const double drawn_for_width  = stretching ? stand_in : size.width;
            const double drawn_for_height = stretching ? stand_in : size.height;
            pictures.pieces.clear();
            for (const shadow_cut_t& cut : cuts)
            {
                auto surface = theme.render_shadow_piece(cut.drawn_box, drawn_for_width,
                    drawn_for_height, active);
                pictures.pieces.push_back({wf::owned_texture_t{surface}, cut.box});
                cairo_surface_destroy(surface);
            }
        }

        pictures.laid_out_size    = size;
        pictures.drawn_stretching = stretching;
    }

    void render_shadow(const wf::scene::render_instruction_t& data, wf::pointf_t origin)
    {
        const double active_share = shadow_fade;
        for (const bool active : {false, true})
        {
            const double share = active ? active_share : 1.0 - active_share;
            if (share <= 0.0)
            {
                continue;
            }

            shadow_pictures_t& pictures = shadow_textures[active];
            update_shadow(pictures, active);
            for (const shadow_piece_t& piece : pictures.pieces)
            {
                data.pass->add_texture(piece.tex.get_texture(), data.target, piece.box + origin,
                    data.damage, share);
            }
        }

        if (shadow_fade.running())
        {
            shadow_fade_frame.run_once([this] ()
            {
                wf::scene::damage_node(shared_from_this(), get_bounding_box());
            });
        }
    }

    /** The rim's top or bottom picture, with its rounded corners. */
    struct rim_picture_t
    {
        wf::owned_texture_t tex;
        bool drawn_active = false;
    };

    rim_picture_t rim_top_texture;
    rim_picture_t rim_bottom_texture;

    enum class rim_end_t
    {
        TOP,
        BOTTOM,
    };

    void update_rim_picture(rim_end_t end, int width, int height, double scale, bool active)
    {
        rim_picture_t& picture = (end == rim_end_t::TOP) ? rim_top_texture : rim_bottom_texture;
        wf::dimensions_t target_size = {
            static_cast<int32_t>(width * scale),
            static_cast<int32_t>(height * scale)
        };

        const bool unchanged =
            (picture.tex.get_size() == target_size) &&
            (picture.drawn_active == active);
        if (unchanged)
        {
            return;
        }

        auto surface = (end == rim_end_t::TOP) ?
            theme.render_rim_top(target_size.width, target_size.height, scale, active) :
            theme.render_rim_bottom(target_size.width, target_size.height, scale, active);
        picture.tex = wf::owned_texture_t{surface};
        cairo_surface_destroy(surface);
        picture.drawn_active = active;
    }

    /** The rim: pictures for the top and bottom with their corners, plain lines for the sides. */
    void render_rim(const wf::scene::render_instruction_t& data, wf::pointf_t origin, bool activated)
    {
        using rim_part_t = wf::decor::decoration_theme_t::rim_part_t;
        const double rim = RIM_WIDTH;
        const double corner_height = theme.get_rim_corner_height();

        wf::geometry_t top{origin.x - rim, origin.y - rim, size.width + 2 * rim, rim + corner_height};
        update_rim_picture(rim_end_t::TOP, top.width, top.height, data.target.scale, activated);
        data.pass->add_texture(rim_top_texture.tex.get_texture(), data.target, top, data.damage);

        const double bottom_corner_height = theme.get_rim_bottom_corner_height();
        wf::geometry_t bottom{origin.x - rim, origin.y + size.height - bottom_corner_height,
            size.width + 2 * rim, bottom_corner_height + rim};
        update_rim_picture(rim_end_t::BOTTOM, bottom.width, bottom.height, data.target.scale, activated);
        data.pass->add_texture(rim_bottom_texture.tex.get_texture(), data.target, bottom, data.damage);

        const double sides_top    = origin.y + corner_height;
        const double sides_height = size.height - corner_height - bottom_corner_height;
        wf::geometry_t left{origin.x - rim, sides_top, rim, sides_height};
        wf::geometry_t right{origin.x + size.width, sides_top, rim, sides_height};

        const wf::color_t sides_color = theme.get_rim_color(rim_part_t::SIDES, activated);
        data.pass->add_rect(sides_color, data.target, left, data.damage);
        data.pass->add_rect(sides_color, data.target, right, data.damage);
    }

  public:
    static constexpr double RIM_WIDTH = 1.0;

    wf::decor::decoration_theme_t theme;
    wf::decor::decoration_layout_t layout;
    wf::regionf_t cached_region;

    wf::dimensions_t size;

    int current_thickness;
    int current_titlebar;
    // only the shadow and rim, around a window that draws its own frame
    const bool bare;

    // 0 is the inactive shadow, 1 the active one
    wf::animation::simple_animation_t shadow_fade;

    simple_decoration_node_t(wayfire_toplevel_view view, bool bare = false) :
        node_t(false),
        theme{},
        layout{theme, [=] (wf::geometry_t box)
        {
            wf::scene::damage_node(shared_from_this(), box + get_offset());
        }},
        bare(bare),
        shadow_fade{wf::create_option<int>(theme.values().shadow_fade_ms), fast_then_settling}
    {
        if (bare)
        {
            theme.set_least_corner_radius(SELF_FRAMED_CORNER_RADIUS);
        }

        this->_view = view->weak_from_this();
        shadow_fade.set(view->activated, view->activated);
        view->connect(&title_set);
        if (view->parent)
        {
            theme.set_buttons(wf::decor::button_type_t(wf::decor::BUTTON_TOGGLE_MAXIMIZE |
                wf::decor::BUTTON_CLOSE));
        } else
        {
            theme.set_buttons(wf::decor::button_type_t(wf::decor::BUTTON_MINIMIZE |
                wf::decor::BUTTON_TOGGLE_MAXIMIZE | wf::decor::BUTTON_CLOSE));
        }

        // make sure to hide frame if the view is fullscreen
        update_decoration_size();
    }

    wf::pointf_t get_offset()
    {
        auto view = _view.lock();
        if (bare && view)
        {
            // a program that draws its own shadow starts its surface outside the window
            return view->get_surface_root_node()->to_local(wf::origin(view->get_geometry()));
        }

        return {(double)-current_thickness, (double)-current_titlebar};
    }

    void render(const wf::scene::render_instruction_t& data)
    {
        auto origin = get_offset();
        const double scale = data.target.scale;

        bool activated = false;
        if (auto view = _view.lock())
        {
            activated = view->activated;
        }

        /* The shadow first: everything else lies on top of it. */
        if (((current_titlebar > 0) || bare) && theme.has_shadow())
        {
            render_shadow(data, origin);
        }

        if (bare)
        {
            if (theme.has_rim(activated))
            {
                render_rim(data, origin, activated);
            }

            return;
        }

        if (current_titlebar > 0)
        {
            wf::geometry_t titlebar_geometry{origin.x, origin.y,
                (double)size.width, (double)current_titlebar};
            update_titlebar(size.width, current_titlebar, scale, activated);
            data.pass->add_texture(titlebar_texture.tex.get_texture(), data.target,
                titlebar_geometry, data.damage);
        }

        /* The top part of the frame line belongs to the title bar's picture. */
        if (current_thickness > 0)
        {
            const wf::color_t line_color = theme.get_border_color(activated);
            const double below_titlebar  = origin.y + current_titlebar;
            const double side_height     = size.height - current_titlebar;
            const double thickness = current_thickness;

            wf::geometry_t left_line{origin.x, below_titlebar, thickness, side_height};
            wf::geometry_t right_line{origin.x + size.width - thickness, below_titlebar,
                thickness, side_height};
            wf::geometry_t bottom_line{origin.x, origin.y + size.height - thickness,
                (double)size.width, thickness};

            for (const auto& line : {left_line, right_line, bottom_line})
            {
                data.pass->add_rect(line_color, data.target, line, data.damage);
            }
        }

        if ((current_titlebar > 0) && theme.has_rim(activated))
        {
            render_rim(data, origin, activated);
        }

        /* Draw title & buttons */
        layout.set_window_active(activated);
        auto renderables = layout.get_renderable_areas();
        for (auto item : renderables)
        {
            if (item->get_type() == wf::decor::DECORATION_AREA_TITLE)
            {
                wf::geometry_t title_geometry = item->get_geometry() + origin;
                // The middle of the window, seen from the title area's left edge.
                const double window_center = size.width / 2.0 - item->get_geometry().x;
                update_title(title_geometry.width, title_geometry.height, scale, activated,
                    window_center);
                if (title_texture.tex.get_texture() != NULL)
                {
                    data.pass->add_texture(title_texture.tex.get_texture(), data.target,
                        title_geometry, data.damage);
                }
            } else // button
            {
                item->as_button().render(data, item->get_geometry() + origin);
            }
        }
    }

    std::optional<wf::scene::input_node_t> find_node_at(const wf::pointf_t& at) override
    {
        if (auto view = _view.lock())
        {
            wf::pointf_t local = at - get_offset();
            if (cached_region.contains_pointf(local) && view->is_mapped())
            {
                return wf::scene::input_node_t{
                    .node = this,
                    .local_coords = local,
                };
            }
        }

        return {};
    }

    pointer_interaction_t& pointer_interaction() override
    {
        return *this;
    }

    touch_interaction_t& touch_interaction() override
    {
        return *this;
    }

    class decoration_render_instance_t : public wf::scene::render_instance_t
    {
        std::shared_ptr<simple_decoration_node_t> self;
        wf::scene::damage_callback push_damage;

        wf::signal::connection_t<wf::scene::node_damage_signal> on_surface_damage =
            [=] (wf::scene::node_damage_signal *data)
        {
            push_damage(data->region);
        };

      public:
        decoration_render_instance_t(simple_decoration_node_t *self, wf::scene::damage_callback push_damage)
        {
            this->self = std::dynamic_pointer_cast<simple_decoration_node_t>(self->shared_from_this());
            this->push_damage = push_damage;
            self->connect(&on_surface_damage);
        }

        void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage) override
        {
            auto our_region = self->get_bounding_box();
            wf::regionf_t our_damage = damage & our_region;
            if (!our_damage.empty())
            {
                instructions.push_back(wf::scene::render_instruction_t{
                    .instance = this,
                    .target   = target,
                    .damage   = std::move(our_damage),
                });
            }
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            self->render(data);
        }
    };

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t* = nullptr) override
    {
        instances.push_back(std::make_unique<decoration_render_instance_t>(this, push_damage));
    }

    wf::geometry_t get_bounding_box() override
    {
        // Includes the rim, the shadow and the undrawn resize strip: a press
        // is only looked for inside this box.
        const double drawn_outside = theme.has_shadow()
            ? std::max<double>(RIM_WIDTH, theme.get_shadow_reach()) : RIM_WIDTH;
        const double outside = std::max<double>(drawn_outside, theme.get_resize_grip());
        const wf::pointf_t origin = get_offset();
        return wf::geometry_t{origin.x - outside, origin.y - outside,
            size.width + 2.0 * outside, size.height + 2.0 * outside};
    }

    /* wf::compositor_surface_t implementation */
    void handle_pointer_enter(wf::pointf_t point) override
    {
        point -= get_offset();
        layout.handle_motion(point.x, point.y);
    }

    void handle_pointer_leave() override
    {
        layout.handle_focus_lost();
    }

    void handle_pointer_motion(wf::pointf_t to, uint32_t) override
    {
        to -= get_offset();
        handle_action(layout.handle_motion(to.x, to.y));
    }

    void handle_pointer_button(const wlr_pointer_button_event& ev) override
    {
        handle_action(layout.handle_press_event(ev.state == WL_POINTER_BUTTON_STATE_PRESSED, ev.button));
    }

    void toogle_maximize_all_edges(std::shared_ptr<wf::toplevel_view_interface_t> view,
        uint32_t edges_to_switch)
    {
        uint32_t current_edges = view->pending_tiled_edges();
        if ((current_edges & edges_to_switch) == edges_to_switch)
        {
            return wf::get_core().default_wm->tile_request(view, 0);
        } else
        {
            return wf::get_core().default_wm->tile_request(view, edges_to_switch);
        }
    }

    void toogle_maximize_edges(std::shared_ptr<wf::toplevel_view_interface_t> view, uint32_t edges_to_switch)
    {
        uint32_t current_edges = view->pending_tiled_edges();
        if (current_edges & edges_to_switch)
        {
            return wf::get_core().default_wm->tile_request(view, current_edges & ~edges_to_switch);
        } else
        {
            return wf::get_core().default_wm->tile_request(view, current_edges | edges_to_switch);
        }
    }

    void handle_action(wf::decor::decoration_layout_t::action_response_t action)
    {
        if (auto view = _view.lock())
        {
            switch (action.action)
            {
              case wf::decor::DECORATION_ACTION_MOVE:
                return wf::get_core().default_wm->move_request(view);

              case wf::decor::DECORATION_ACTION_RESIZE:
                return wf::get_core().default_wm->resize_request(view, action.edges);

              case wf::decor::DECORATION_ACTION_CLOSE:
                return view->close();

              case wf::decor::DECORATION_ACTION_TOGGLE_MAXIMIZE:
                return toogle_maximize_all_edges(view, wf::TILED_EDGES_ALL);

              case wf::decor::DECORATION_ACTION_TOGGLE_MAXIMIZE_VERTICAL:
                return toogle_maximize_edges(view, WLR_EDGE_TOP | WLR_EDGE_BOTTOM);

              case wf::decor::DECORATION_ACTION_TOGGLE_MAXIMIZE_HORIZONTAL:
                return toogle_maximize_edges(view, WLR_EDGE_LEFT | WLR_EDGE_RIGHT);

              case wf::decor::DECORATION_ACTION_MINIMIZE:
                return wf::get_core().default_wm->minimize_request(view, true);
                break;

              default:
                break;
            }
        }
    }

    void handle_touch_down(uint32_t time_ms, int finger_id, wf::pointf_t position) override
    {
        handle_touch_motion(time_ms, finger_id, position);
        handle_action(layout.handle_press_event());
    }

    void handle_touch_up(uint32_t, int, wf::pointf_t) override
    {
        handle_action(layout.handle_press_event(false));
        layout.handle_focus_lost();
    }

    void handle_touch_motion(uint32_t, int, wf::pointf_t position) override
    {
        position -= get_offset();
        handle_action(layout.handle_motion(position.x, position.y));
    }

    void resize(wf::dimensions_t dims)
    {
        if (auto view = _view.lock())
        {
            view->damage();
            size = dims;
            layout.set_fixed_size(has_fixed_size(view));
            layout.resize(size.width, size.height);
            if (!bare && !frame_hidden(view->toplevel()->current()))
            {
                this->cached_region = layout.calculate_region();
            }

            view->damage();
        }
    }

    // A program fixes its window's size by giving the same least and largest
    // size (Qt's setFixedSize); 0 means "no limit" for either.
    static bool has_fixed_size(const std::shared_ptr<wf::toplevel_view_interface_t>& view)
    {
        const wf::dimensions_t least   = view->toplevel()->get_min_size();
        const wf::dimensions_t largest = view->toplevel()->get_max_size();
        return (least.width > 0) && (least.height > 0) && (least == largest);
    }

    void update_decoration_size()
    {
        if (bare || frame_hidden(_view.lock()->toplevel()->current()))
        {
            current_thickness = 0;
            current_titlebar  = 0;
            this->cached_region.clear();
        } else
        {
            current_thickness = theme.get_border_size();
            current_titlebar = theme.get_top_margin();
            this->cached_region = layout.calculate_region();
        }
    }
};

wf::simple_decorator_t::simple_decorator_t(wayfire_toplevel_view view, bool tool_bar_joined)
{
    this->view = view;
    view_while_alive = view->weak_from_this();
    deco = std::make_shared<simple_decoration_node_t>(view);
    deco->theme.set_tool_bar_joined(tool_bar_joined);
    deco->resize(wf::dimensions(view->get_pending_geometry()));
    wf::scene::add_back(view->get_surface_root_node(), deco);

    if (deco->theme.get_bottom_corner_radius() > 0)
    {
        corner_clip = std::make_shared<corner_clip_transformer_t>(view, 0.0,
            deco->theme.get_bottom_corner_radius(), deco);
    }

    update_corner_clip();

    view->connect(&on_view_activated);
    view->connect(&on_view_geometry_changed);
    view->connect(&on_view_fullscreen);
    view->connect(&on_view_tiled);

    on_view_activated = [this] (auto)
    {
        deco->shadow_fade.animate(this->view->activated);
        wf::scene::damage_node(deco, deco->get_bounding_box());
    };

    on_view_geometry_changed = [this] (auto)
    {
        deco->resize(wf::dimensions(this->view->get_geometry()));
    };

    on_view_fullscreen = [this] (auto) { update_frame(); };
    on_view_tiled = [this] (auto) { update_frame(); };
}

wf::simple_decorator_t::~simple_decorator_t()
{
    // A window that is already gone took its transformers with it, and
    // reaching it through `view` would crash. Removing a transformer that
    // was never added trips Wayfire's assertion "transformer is missing a
    // parent?", so check it is there.
    auto still_alive = view_while_alive.lock();
    if (corner_clip && still_alive)
    {
        auto transformed = still_alive->get_transformed_node();
        if (transformed->get_transformer<corner_clip_transformer_t>(CORNER_CLIP_NAME) != nullptr)
        {
            transformed->rem_transformer(corner_clip);
        }
    }

    wf::scene::remove_child(deco);
}

void wf::simple_decorator_t::update_frame()
{
    update_corner_clip();
    deco->update_decoration_size();
}

void wf::simple_decorator_t::update_corner_clip()
{
    if (!corner_clip || !corner_clip->can_draw())
    {
        return;
    }

    // a window with no frame can use direct scan-out without a transformer
    auto transformed = view->get_transformed_node();
    const bool wanted = !frame_hidden(view->toplevel()->current());
    const bool present = (transformed->get_transformer<corner_clip_transformer_t>(CORNER_CLIP_NAME) != nullptr);
    if (wanted && !present)
    {
        // Below every other transformer, so that it cuts the window itself
        // and not, say, a minimise effect's bent picture of it.
        transformed->add_transformer(corner_clip, CORNER_CLIP_Z_ORDER, CORNER_CLIP_NAME);
    } else if (!wanted && present)
    {
        transformed->rem_transformer(corner_clip);
    }
}

wf::decoration_margins_t wf::simple_decorator_t::get_margins(const wf::toplevel_state_t& state)
{
    if (frame_hidden(state))
    {
        return {0, 0, 0, 0};
    }

    const int thickness = deco->theme.get_border_size();
    const int titlebar  = deco->theme.get_top_margin();
    return wf::decoration_margins_t{
        .left   = (double)thickness,
        .right  = (double)thickness,
        .bottom = (double)thickness,
        .top    = (double)titlebar,
    };
}

wf::frameless_decorator_t::frameless_decorator_t(wayfire_toplevel_view view) :
    view(view), view_while_alive(view->weak_from_this())
{
    on_view_activated = [this] (auto)
    {
        if (shown)
        {
            deco->shadow_fade.animate(this->view->activated);
            wf::scene::damage_node(deco, deco->get_bounding_box());
        }
    };
    on_view_geometry_changed = [this] (auto)
    {
        if (shown)
        {
            deco->resize(wf::dimensions(this->view->get_geometry()));
        }
    };
    on_view_fullscreen = [this] (auto) { update(); };
    on_view_tiled = [this] (auto) { update(); };
    view->connect(&on_view_activated);
    view->connect(&on_view_geometry_changed);
    view->connect(&on_view_fullscreen);
    view->connect(&on_view_tiled);
}

wf::frameless_decorator_t::~frameless_decorator_t()
{
    if (shown && view_while_alive.lock())
    {
        hide();
    }
}

void wf::frameless_decorator_t::set_allowed(bool allowed)
{
    this->allowed = allowed;
    update();
}

void wf::frameless_decorator_t::update()
{
    const auto& state = view->toplevel()->current();
    const bool wanted = allowed && !state.fullscreen && (state.tiled_edges == 0);
    if (wanted && !shown)
    {
        show();
    } else if (!wanted && shown)
    {
        hide();
    }
}

void wf::frameless_decorator_t::show()
{
    deco = std::make_shared<simple_decoration_node_t>(view, true);
    clip = std::make_shared<corner_clip_transformer_t>(view, deco->theme.get_corner_radius(),
        deco->theme.get_bottom_corner_radius(), deco);
    clip->cut_outside = true;
    if (!clip->can_draw())
    {
        deco.reset();
        clip.reset();
        return;
    }

    deco->resize(wf::dimensions(view->get_geometry()));
    wf::scene::add_back(view->get_surface_root_node(), deco);
    view->get_transformed_node()->add_transformer(clip, CORNER_CLIP_Z_ORDER, FRAMELESS_CLIP_NAME);
    shown = true;
    view->damage();
}

void wf::frameless_decorator_t::hide()
{
    view->damage();
    if (view->get_transformed_node()->get_transformer<corner_clip_transformer_t>(FRAMELESS_CLIP_NAME))
    {
        view->get_transformed_node()->rem_transformer(clip);
    }

    wf::scene::remove_child(deco);
    deco.reset();
    clip.reset();
    shown = false;
}
