#include <wayfire/core.hpp>
#include <wayfire/plugin.hpp>

#include "../decoration/hide-maximized.hpp"

class borderless_maximized : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        wf::get_core().get_data_safe<wf::hide_maximized_frames_t>();
        wf::hide_maximized_changed_signal changed;
        wf::get_core().emit(&changed);
    }

    void fini() override
    {
        wf::get_core().erase_data<wf::hide_maximized_frames_t>();
        wf::hide_maximized_changed_signal changed;
        wf::get_core().emit(&changed);
    }
};

DECLARE_WAYFIRE_PLUGIN(borderless_maximized);
