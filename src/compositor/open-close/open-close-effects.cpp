// The plugin "themed-open-close": registers the effect "pop" with Wayfire's
// animate plugin, which runs it as a window opens (and backwards as one
// closes). animate's open_animation and close_animation options pick it.
//
// A popping window grows to its size from a little smaller while it fades
// in, as new windows have opened on OS X since 10.7. Wayfire's own "zoom"
// starts from a third of the size, which reads as a jump more than a pop.

// a window the shell expects from a desktop icon grows out of that icon instead
// and the effect icon-zoom takes it back into the icon as it closes, others close at once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <optional>

#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/animate/animate.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/view.hpp>

namespace themed_open_close
{
const std::string TRANSFORMER_NAME = "themed-open-close-pop";
const std::string ORIGINS_METHOD = "themed-open-close/origins";
const std::string EXPECT_METHOD = "themed-open-close/expect";
const int LONGEST_WAIT_SECONDS = 60;

// Where a window's growth starts, as a share of its size.
const double POP_START_SCALE = 0.85;

using clock = std::chrono::steady_clock;

// each icon's box in layout pixels, by the name the shell gives it
struct icon_places_t : public wf::custom_data_t
{
    std::map<std::string, wf::geometry_t> boxes;
};

// the icon a window came from
struct origin_t : public wf::custom_data_t
{
    std::string icon;
};

icon_places_t& icon_places() { return *wf::get_core().get_data_safe<icon_places_t>(); }

std::optional<wf::geometry_t> icon_box_of(wayfire_view view)
{
    auto origin = view->get_data<origin_t>();
    if (!origin)
    {
        return {};
    }
    const auto& boxes = icon_places().boxes;
    const auto found = boxes.find(origin->icon);
    if (found == boxes.end())
    {
        return {};
    }
    return found->second;
}

wf::option_wrapper_t<int> icon_zoom_ms{"themed-open-close/icon_zoom_ms"};

// moves a window between its icon and its own place, eased in and out as finder does
class icon_zoom_t
{
  public:
    icon_zoom_t(wayfire_view view, bool hiding) :
        view(view),
        clock(wf::create_option<int>(icon_zoom_ms), wf::animation::smoothing::get_cubic_bezier(0.42, 0, 0.58, 1)),
        hiding(hiding)
    {
        clock.start();
    }

    // false once the icon is gone, the window then shows as it is
    bool place(wf::scene::view_2d_transformer_t& transformer)
    {
        const auto icon = icon_box_of(view);
        if (!icon || !view->get_output())
        {
            return false;
        }
        const wf::geometry_t output = view->get_output()->get_layout_geometry();
        // the window before this effect, the transformed node would include it
        const wf::geometry_t local = transformer.get_children_bounding_box();
        const double share = hiding ? 1 - clock.progress() : clock.progress();
        const auto mix = [share](double from, double to) { return from + (to - from) * share; };
        const double middle_x = local.x + local.width / 2.0;
        const double middle_y = local.y + local.height / 2.0;
        transformer.scale_x = float(mix(icon->width, local.width) / std::max(1.0, double(local.width)));
        transformer.scale_y = float(mix(icon->height, local.height) / std::max(1.0, double(local.height)));
        transformer.translation_x = float(mix(icon->x - output.x + icon->width / 2.0, middle_x) - middle_x);
        transformer.translation_y = float(mix(icon->y - output.y + icon->height / 2.0, middle_y) - middle_y);
        transformer.alpha = 1.0f;
        return true;
    }

    bool running() { return clock.running(); }

    void reverse()
    {
        clock.reverse();
    }

  private:
    wayfire_view view;
    wf::animation::duration_t clock;
    bool hiding;
};

/** The effect's clock, with the two things it moves. */
class pop_progress_t : public wf::animation::duration_t
{
  public:
    using duration_t::duration_t;
    wf::animation::timed_transition_t scale{*this};
    wf::animation::timed_transition_t alpha{*this};
};

/** One run of "pop" on one window, in the form the animate plugin wants. */
class pop_animation_t : public wf::animate::animation_base_t
{
  public:
    void init(wayfire_view view, wf::animation_description_t duration, wf::animate::animation_type type) override
    {
        this->view = view;
        hiding = type & WF_ANIMATE_HIDING_ANIMATION;
        // The transitions are made anew for the new clock, not copied with it.
        progress = pop_progress_t(wf::create_option<wf::animation_description_t>(duration));
        progress.scale = wf::animation::timed_transition_t(progress, POP_START_SCALE, 1);
        progress.alpha = wf::animation::timed_transition_t(progress, 0, 1);
        if (hiding)
        {
            progress.scale.flip();
            progress.alpha.flip();
        }
        progress.start();

        // Scaled about the window's middle: the transformer's default.
        view->get_transformed_node()->add_transformer(
            std::make_shared<wf::scene::view_2d_transformer_t>(view), wf::TRANSFORMER_HIGHLEVEL, TRANSFORMER_NAME);
    }

    bool step() override
    {
        auto transformer =
            view->get_transformed_node()->get_transformer<wf::scene::view_2d_transformer_t>(TRANSFORMER_NAME);
        if (!transformer)
        {
            return false;
        }
        // which icon a window came from is known only once it has mapped, after init
        if (first_step && icon_box_of(view))
        {
            zoom.emplace(view, hiding);
        }
        first_step = false;
        if (zoom && zoom->place(*transformer))
        {
            return zoom->running();
        }

        transformer->scale_x = float(progress.scale);
        transformer->scale_y = float(progress.scale);
        transformer->translation_x = 0;
        transformer->translation_y = 0;
        transformer->alpha = float(progress.alpha);
        return progress.running();
    }

    void reverse() override
    {
        progress.reverse();
        if (zoom)
        {
            zoom->reverse();
        }
    }

    ~pop_animation_t() override
    {
        if (view)
        {
            view->get_transformed_node()->rem_transformer(TRANSFORMER_NAME);
        }
    }

  private:
    wayfire_view view;
    pop_progress_t progress;
    bool hiding = false;
    bool first_step = true;
    std::optional<icon_zoom_t> zoom;
};

// a window from an icon goes back into it, any other one is gone at once
class icon_animation_t : public wf::animate::animation_base_t
{
  public:
    void init(wayfire_view view, wf::animation_description_t, wf::animate::animation_type type) override
    {
        this->view = view;
        if (!icon_box_of(view))
        {
            return;
        }
        zoom.emplace(view, type & WF_ANIMATE_HIDING_ANIMATION);
        view->get_transformed_node()->add_transformer(
            std::make_shared<wf::scene::view_2d_transformer_t>(view), wf::TRANSFORMER_HIGHLEVEL, TRANSFORMER_NAME);
    }

    bool step() override
    {
        auto transformer =
            view->get_transformed_node()->get_transformer<wf::scene::view_2d_transformer_t>(TRANSFORMER_NAME);
        return transformer && zoom && zoom->place(*transformer) && zoom->running();
    }

    void reverse() override
    {
        if (zoom)
        {
            zoom->reverse();
        }
    }

    ~icon_animation_t() override
    {
        if (view && zoom)
        {
            view->get_transformed_node()->rem_transformer(TRANSFORMER_NAME);
        }
    }

  private:
    wayfire_view view;
    std::optional<icon_zoom_t> zoom;
};

// a window the shell waits for, known by any of its ids compared without case
struct expected_t
{
    std::vector<std::string> ids;
    std::string icon;
    clock::time_point deadline;
};

bool same_text(const std::string& a, const std::string& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
        [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
}

class plugin_t : public wf::plugin_interface_t
{
    wf::option_wrapper_t<wf::animation_description_t> pop_duration{"themed-open-close/pop_duration"};

    wf::shared_data::ref_ptr_t<wf::animate::animate_effects_registry_t> effects;
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> methods;
    std::vector<expected_t> expected;

    wf::signal::connection_t<wf::view_mapped_signal> on_mapped = [this](wf::view_mapped_signal* event) {
        auto view = wf::toplevel_cast(event->view);
        if (!view || view->parent)
        {
            return;
        }
        const auto now = clock::now();
        expected.erase(std::remove_if(expected.begin(), expected.end(),
                           [now](const expected_t& one) { return one.deadline < now; }),
            expected.end());
        const std::string app_id = view->get_app_id();
        const auto one = std::find_if(expected.begin(), expected.end(), [&](const expected_t& candidate) {
            return std::any_of(candidate.ids.begin(), candidate.ids.end(),
                [&](const std::string& id) { return same_text(id, app_id); });
        });
        if (one == expected.end())
        {
            return;
        }
        auto origin = std::make_unique<origin_t>();
        origin->icon = one->icon;
        view->store_data(std::move(origin));
        LOGI("[themed-open-close] icon=", one->icon, " app_id=", app_id, " view=", view->get_id(), " result=from-icon");
        expected.erase(one);
    };

    // every icon's box at once, in that output's pixels
    wf::ipc::method_callback on_origins = [](const wf::json_t& data) {
        if (!data.has_member("output") || !data["output"].is_string())
        {
            return wf::ipc::json_error("refused: output must be an output's name");
        }
        auto output = wf::get_core().output_layout->find_output(data["output"].as_string());
        if (!output)
        {
            return wf::ipc::json_error("refused: no output named " + data["output"].as_string());
        }
        if (!data.has_member("origins") || !data["origins"].is_object())
        {
            return wf::ipc::json_error("refused: origins must be a table of boxes");
        }
        const wf::geometry_t screen = output->get_layout_geometry();
        std::map<std::string, wf::geometry_t> boxes;
        for (const std::string& name : data["origins"].get_member_names())
        {
            const wf::json_t box = data["origins"][name];
            bool numbers = box.is_array() && box.size() == 4;
            for (size_t at = 0; numbers && at < 4; ++at)
            {
                numbers = box[at].is_int() || box[at].is_double();
            }
            if (!numbers)
            {
                return wf::ipc::json_error("refused: " + name + " must be four numbers");
            }
            const auto part = [&box](size_t at) { return box[at].is_int() ? double(int(box[at])) : box[at].as_double(); };
            boxes[name] = {screen.x + part(0), screen.y + part(1), part(2), part(3)};
        }
        icon_places().boxes = std::move(boxes);
        return wf::ipc::json_ok();
    };

    // {"ids": [...], "origin": name, "seconds": n} for the next window of that program
    wf::ipc::method_callback on_expect = [this](const wf::json_t& data) {
        if (!data.has_member("ids") || !data["ids"].is_array() || data["ids"].size() == 0)
        {
            return wf::ipc::json_error("refused: ids must be a list of names");
        }
        if (!data.has_member("origin") || !data["origin"].is_string())
        {
            return wf::ipc::json_error("refused: origin must be an icon's name");
        }
        if (!data.has_member("seconds") || !data["seconds"].is_int() || int(data["seconds"]) < 1 ||
            int(data["seconds"]) > LONGEST_WAIT_SECONDS)
        {
            return wf::ipc::json_error(
                "refused: seconds must be a whole number from 1 to " + std::to_string(LONGEST_WAIT_SECONDS));
        }
        expected_t one{{}, data["origin"].as_string(), clock::now() + std::chrono::seconds(int(data["seconds"]))};
        for (size_t at = 0; at < data["ids"].size(); ++at)
        {
            if (!data["ids"][at].is_string())
            {
                return wf::ipc::json_error("refused: an id must be text");
            }
            one.ids.push_back(data["ids"][at].as_string());
        }
        LOGI("[themed-open-close] expect icon=", one.icon, " ids=", one.ids.size(), " seconds=", int(data["seconds"]));
        expected.push_back(std::move(one));
        return wf::ipc::json_ok();
    };

  public:
    void init() override
    {
        effects->register_effect("pop", wf::animate::effect_description_t{
                                            .generator = [] { return std::make_unique<pop_animation_t>(); },
                                            .default_duration = [=] { return pop_duration.value(); },
                                        });
        effects->register_effect("icon-zoom", wf::animate::effect_description_t{
                                                  .generator = [] { return std::make_unique<icon_animation_t>(); },
                                                  .default_duration = [=] { return pop_duration.value(); },
                                              });
        methods->register_method(ORIGINS_METHOD, on_origins);
        methods->register_method(EXPECT_METHOD, on_expect);
        wf::get_core().connect(&on_mapped);
    }

    void fini() override
    {
        effects->unregister_effect("pop");
        effects->unregister_effect("icon-zoom");
        methods->unregister_method(ORIGINS_METHOD);
        methods->unregister_method(EXPECT_METHOD);
        wf::get_core().erase_data<icon_places_t>();
    }
};
} // namespace themed_open_close

DECLARE_WAYFIRE_PLUGIN(themed_open_close::plugin_t);
