#include <wayfire/core.hpp>
#include <wayfire/plugin.hpp>

#include "../decoration/rounded-frameless.hpp"

class rounded_corners : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        wf::get_core().get_data_safe<wf::round_frameless_windows_t>();
        wf::round_frameless_changed_signal changed;
        wf::get_core().emit(&changed);
    }

    void fini() override
    {
        wf::get_core().erase_data<wf::round_frameless_windows_t>();
        wf::round_frameless_changed_signal changed;
        wf::get_core().emit(&changed);
    }
};

DECLARE_WAYFIRE_PLUGIN(rounded_corners);
