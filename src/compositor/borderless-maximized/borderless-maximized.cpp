#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <wayfire/config-backend.hpp>
#include <wayfire/core.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util/log.hpp>

#include "../decoration/hide-maximized.hpp"
#include "../window-gaps/window-gaps.hpp"

// GTK draws its stock title bar itself, so the desktop's GTK module reads this
static std::filesystem::path gtk_marker()
{
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    return runtime ? std::filesystem::path{runtime} / "mavernix/hide-maximized-titlebars" : "";
}

class borderless_maximized : public wf::plugin_interface_t
{
    wf::option_wrapper_t<bool> with_gaps;

    // GTK cannot tell a window with a gap from one without, so its bars stay while gaps keep them
    void update_gtk_marker()
    {
        auto gaps = wf::get_core().get_data<wf::window_gaps_t>();
        const bool wanted = with_gaps || !gaps || !gaps->px;
        std::error_code error;
        if (!wanted)
        {
            std::filesystem::remove(gtk_marker(), error);
            return;
        }

        std::filesystem::create_directories(gtk_marker().parent_path(), error);
        if (error || !std::ofstream{gtk_marker()})
        {
            LOGE("[borderless-maximized] gtk marker not written path=", gtk_marker().string(),
                " error=", error.message());
        }
    }

    void tell()
    {
        wf::get_core().get_data_safe<wf::hide_maximized_frames_t>()->with_gaps = with_gaps;
        update_gtk_marker();
        wf::hide_maximized_changed_signal changed;
        wf::get_core().emit(&changed);
    }

    wf::signal::connection_t<wf::window_gaps_changed_signal> on_gaps_changed =
        [=] (wf::window_gaps_changed_signal*) { update_gtk_marker(); };

  public:
    void init() override
    {
        auto& core = wf::get_core();
        // the file reload made untyped options if our xml came after the compositor started
        if (!std::dynamic_pointer_cast<wf::config::option_t<bool>>(
                core.config->get_option("borderless-maximized/with_gaps")))
        {
            core.config_backend->reload_config_metadata(*core.config);
        }

        with_gaps.load_option("borderless-maximized/with_gaps");
        with_gaps.set_callback([=] { tell(); });
        core.connect(&on_gaps_changed);
        tell();
    }

    void fini() override
    {
        on_gaps_changed.disconnect();
        std::error_code error;
        std::filesystem::remove(gtk_marker(), error);
        wf::get_core().erase_data<wf::hide_maximized_frames_t>();
        wf::hide_maximized_changed_signal changed;
        wf::get_core().emit(&changed);
    }
};

DECLARE_WAYFIRE_PLUGIN(borderless_maximized);
