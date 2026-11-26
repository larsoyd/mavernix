#include <algorithm>
#include <map>
#include <memory>
#include <set>

#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/common/util.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/util.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/workspace-set.hpp>

namespace across_screens
{
// wayfire draws a window only on its own screen so the part hanging over is drawn again here
class overhang_node_t : public wf::scene::node_t
{
  public:
    std::shared_ptr<wf::toplevel_view_interface_t> view;
    wf::output_t* shown;

    overhang_node_t(wayfire_toplevel_view view, wf::output_t* shown) :
        node_t(false), view(std::dynamic_pointer_cast<wf::toplevel_view_interface_t>(view->shared_from_this())),
        shown(shown)
    {}

    // from the window's own screen to this one in output local terms
    wf::pointf_t offset() const
    {
        if (!view->get_output())
        {
            return {0, 0};
        }

        return wf::origin(view->get_output()->get_layout_geometry()) - wf::origin(shown->get_layout_geometry());
    }

    std::string stringify() const override
    {
        return "across-screens " + stringify_flags();
    }

    wf::geometry_t get_bounding_box() override
    {
        return view->get_root_node()->get_bounding_box() + offset();
    }

    std::optional<wf::scene::input_node_t> find_node_at(const wf::pointf_t& at) override
    {
        auto by = offset();
        return view->get_root_node()->find_node_at({at.x - by.x, at.y - by.y});
    }

    // a click makes the screen under it active after focusing the window and refocus there must keep it
    wf::keyboard_focus_node_t keyboard_refocus(wf::output_t* output) override
    {
        // wayfire refocuses at once on minimize and close, before this overhang is dropped
        if ((output != shown) || view->minimized || !view->get_output() ||
            !view->get_keyboard_focus_surface())
        {
            return {};
        }

        auto box = view->get_pending_geometry() + wf::origin(view->get_output()->get_layout_geometry());
        auto there = wf::geometry_intersection(box, shown->get_layout_geometry());
        double part = (there.width * there.height) / std::max(1.0, (double)box.width * box.height);
        auto importance = (part >= 0.1) ? wf::focus_importance::REGULAR : wf::focus_importance::LOW;
        return {view->get_surface_root_node().get(), importance};
    }

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t* shown_on) override;
};

class overhang_render_instance_t : public wf::scene::render_instance_t
{
    std::shared_ptr<overhang_node_t> self;
    std::vector<wf::scene::render_instance_uptr> children;

  public:
    overhang_render_instance_t(std::shared_ptr<overhang_node_t> self, wf::scene::damage_callback push_damage,
        wf::output_t* shown_on) : self(self)
    {
        auto moved = [self, push_damage](const wf::regionf_t& damage)
        {
            push_damage(damage + self->offset());
        };
        self->view->get_root_node()->gen_render_instances(children, moved, shown_on);
    }

    void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::regionf_t& damage) override
    {
        auto by = self->offset();
        damage += -by;
        auto moved_target = target.translated(-by);
        for (auto& child : children)
        {
            child->schedule_instructions(instructions, moved_target, damage);
        }

        damage += by;
    }

    void presentation_feedback(wf::output_t* output) override
    {
        for (auto& child : children)
        {
            child->presentation_feedback(output);
        }
    }

    // a full screen window below must not be scanned out over this
    wf::scene::direct_scanout try_scanout(wf::output_t* output) override
    {
        auto local = output->get_relative_geometry();
        auto shared = wf::geometry_intersection(self->get_bounding_box(), local);
        return (shared.width > 0 && shared.height > 0) ? wf::scene::direct_scanout::OCCLUSION :
               wf::scene::direct_scanout::SKIP;
    }
};

void overhang_node_t::gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
    wf::scene::damage_callback push_damage, wf::output_t* shown_on)
{
    instances.push_back(std::make_unique<overhang_render_instance_t>(
        std::dynamic_pointer_cast<overhang_node_t>(shared_from_this()), push_damage, shown_on));
}

// screens besides its own that the window or its dialogs reach while it shows on its own
static std::set<wf::output_t*> reached(wayfire_toplevel_view view)
{
    auto home = view->get_output();
    if (!home || !view->is_mapped() || view->minimized ||
        !view->get_transformed_node()->is_enabled())
    {
        return {};
    }

    auto own = home->get_relative_geometry();
    std::set<wf::output_t*> found;
    for (auto& member : view->enumerate_views())
    {
        auto box = member->get_geometry();
        auto inside = wf::geometry_intersection(box, own);
        if ((inside.width <= 0) || (inside.height <= 0))
        {
            continue;
        }

        box = box + wf::origin(home->get_layout_geometry());
        for (auto output : wf::get_core().output_layout->get_outputs())
        {
            auto there = wf::geometry_intersection(box, output->get_layout_geometry());
            if ((output != home) && (there.width > 0) && (there.height > 0))
            {
                found.insert(output);
            }
        }
    }

    return found;
}

class plugin_t : public wf::plugin_interface_t
{
    using key_t = std::pair<wf::view_interface_t*, wf::output_t*>;
    std::map<key_t, std::shared_ptr<overhang_node_t>> overhangs;
    wf::wl_idle_call idle;

    void drop(const std::shared_ptr<overhang_node_t>& node)
    {
        wf::scene::damage_node(node, node->get_bounding_box());
        wf::scene::remove_child(node);
    }

    // above the windows of that screen used less recently and below the rest
    void restack(const std::shared_ptr<overhang_node_t>& node)
    {
        auto parent = node->shown->wset()->get_node();
        auto stamp  = wf::get_focus_timestamp(node->view.get());
        auto list   = parent->get_children();
        list.erase(std::remove(list.begin(), list.end(), node), list.end());
        auto older = std::find_if(list.begin(), list.end(), [&](const wf::scene::node_ptr& child)
        {
            auto other = std::dynamic_pointer_cast<overhang_node_t>(child);
            auto view  = other ? wayfire_view{other->view.get()} : wf::node_to_view(child);
            return view && (wf::get_focus_timestamp(view) < stamp);
        });
        list.insert(older, node);
        if (list != parent->get_children())
        {
            if (node->parent() && (node->parent() != parent.get()))
            {
                wf::scene::remove_child(node);
            }

            parent->set_children_list(list);
            wf::scene::update(parent, wf::scene::update_flag::CHILDREN_LIST);
        }
    }

    void refresh()
    {
        std::map<key_t, wayfire_toplevel_view> wanted;
        for (auto output : wf::get_core().output_layout->get_outputs())
        {
            for (auto& view : output->wset()->get_views(wf::WSET_MAPPED_ONLY))
            {
                for (auto other : reached(view))
                {
                    wanted[{view.get(), other}] = view;
                }
            }
        }

        for (auto it = overhangs.begin(); it != overhangs.end();)
        {
            if (wanted.count(it->first))
            {
                ++it;
            } else
            {
                drop(it->second);
                it = overhangs.erase(it);
            }
        }

        for (auto& [key, view] : wanted)
        {
            if (!overhangs.count(key))
            {
                auto node = std::make_shared<overhang_node_t>(view, key.second);
                overhangs[key] = node;
                wf::scene::damage_node(node, node->get_bounding_box());
            }
        }

        for (auto& [key, node] : overhangs)
        {
            restack(node);
        }
    }

    // signals come from inside half done changes so the work waits for them to finish
    void later()
    {
        if (!idle.is_connected())
        {
            idle.run_once([=] { refresh(); });
        }
    }

    // a screen leaving takes every overhang shown on it or drawn from it
    void forget(wf::output_t* output)
    {
        for (auto it = overhangs.begin(); it != overhangs.end();)
        {
            if ((it->first.second == output) || (it->second->view->get_output() == output))
            {
                drop(it->second);
                it = overhangs.erase(it);
            } else
            {
                ++it;
            }
        }
    }

    wf::signal::connection_t<wf::view_geometry_changed_signal> on_geometry = [=](auto) { later(); };
    wf::signal::connection_t<wf::view_mapped_signal> on_mapped = [=](auto) { later(); };
    wf::signal::connection_t<wf::view_unmapped_signal> on_unmapped = [=](auto) { later(); };
    wf::signal::connection_t<wf::view_set_output_signal> on_set_output = [=](auto) { later(); };
    wf::signal::connection_t<wf::view_minimized_signal> on_minimized = [=](auto) { later(); };
    wf::signal::connection_t<wf::output_plugin_activated_changed_signal> on_plugin = [=](auto) { later(); };
    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_focus = [=](auto) { later(); };
    wf::signal::connection_t<wf::output_layout_configuration_changed_signal> on_layout = [=](auto) { later(); };
    wf::signal::connection_t<wf::output_added_signal> on_added = [=](wf::output_added_signal* event)
    {
        event->output->connect(&on_minimized);
        event->output->connect(&on_plugin);
        later();
    };
    wf::signal::connection_t<wf::output_pre_remove_signal> on_removing = [=](wf::output_pre_remove_signal* event)
    {
        forget(event->output);
    };

  public:
    void init() override
    {
        auto& core = wf::get_core();
        core.connect(&on_geometry);
        core.connect(&on_mapped);
        core.connect(&on_unmapped);
        core.connect(&on_set_output);
        core.connect(&on_focus);
        core.output_layout->connect(&on_layout);
        core.output_layout->connect(&on_added);
        core.output_layout->connect(&on_removing);
        for (auto output : core.output_layout->get_outputs())
        {
            output->connect(&on_minimized);
            output->connect(&on_plugin);
        }

        refresh();
    }

    void fini() override
    {
        idle.disconnect();
        for (auto& [key, node] : overhangs)
        {
            drop(node);
        }

        overhangs.clear();
    }
};
} // namespace across_screens

DECLARE_WAYFIRE_PLUGIN(across_screens::plugin_t);
