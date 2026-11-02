#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <wayfire/core.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/util/log.hpp>

#include "../decoration/hide-maximized.hpp"

// GTK draws its stock title bar itself, so the desktop's GTK module reads this
static std::filesystem::path gtk_marker()
{
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    return runtime ? std::filesystem::path{runtime} / "mavernix/hide-maximized-titlebars" : "";
}

class borderless_maximized : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        std::error_code error;
        std::filesystem::create_directories(gtk_marker().parent_path(), error);
        if (error || !std::ofstream{gtk_marker()})
        {
            LOGE("[borderless-maximized] gtk marker not written path=", gtk_marker().string(),
                " error=", error.message());
        }

        wf::get_core().get_data_safe<wf::hide_maximized_frames_t>();
        wf::hide_maximized_changed_signal changed;
        wf::get_core().emit(&changed);
    }

    void fini() override
    {
        std::error_code error;
        std::filesystem::remove(gtk_marker(), error);
        wf::get_core().erase_data<wf::hide_maximized_frames_t>();
        wf::hide_maximized_changed_signal changed;
        wf::get_core().emit(&changed);
    }
};

DECLARE_WAYFIRE_PLUGIN(borderless_maximized);
