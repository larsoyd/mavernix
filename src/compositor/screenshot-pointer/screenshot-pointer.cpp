// lets a screenshot program take the pointer out of the picture for a short while
// a lease that runs out puts it back, so a program that dies mid capture can't leave it hidden

#include <algorithm>

#include <wayfire/core.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/util.hpp>

namespace screenshot_pointer
{
const std::string HIDE_METHOD = "screenshot-pointer/hide";
const std::string SHOW_METHOD = "screenshot-pointer/show";
const int LONGEST_MS = 5000;

class plugin_t : public wf::plugin_interface_t
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> repo;
    wf::wl_timer<false> lease;
    bool hidden = false;

    void show()
    {
        lease.disconnect();
        if (hidden)
        {
            wf::get_core().unhide_cursor();
            hidden = false;
        }
    }

    wf::ipc::method_callback on_hide = [this](const wf::json_t& data) {
        if (!data.has_member("ms") || !data["ms"].is_int())
        {
            return wf::ipc::json_error("ms must be a whole number");
        }
        const int ms = std::clamp(int(data["ms"]), 1, LONGEST_MS);
        if (!hidden)
        {
            wf::get_core().hide_cursor();
            hidden = true;
        }
        lease.set_timeout(ms, [this] { show(); });
        wf::json_t answer = wf::ipc::json_ok();
        answer["ms"] = ms;
        return answer;
    };

    wf::ipc::method_callback on_show = [this](const wf::json_t&) {
        show();
        return wf::ipc::json_ok();
    };

  public:
    void init() override
    {
        repo->register_method(HIDE_METHOD, on_hide);
        repo->register_method(SHOW_METHOD, on_show);
    }

    void fini() override
    {
        repo->unregister_method(HIDE_METHOD);
        repo->unregister_method(SHOW_METHOD);
        show();
    }
};
} // namespace screenshot_pointer

DECLARE_WAYFIRE_PLUGIN(screenshot_pointer::plugin_t);
