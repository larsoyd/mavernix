// hides the pointer from compositor start until the entry curtain lifts
// a failsafe timer puts it back if the shell never asks

#include <algorithm>
#include <chrono>

#include <wayfire/core.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/util.hpp>
#include <wayfire/util/log.hpp>

namespace entry_cursor
{
const std::string SHOW_METHOD = "entry-cursor/show";
const std::string HIDE_METHOD = "entry-cursor/hide";
const int DEFAULT_HIDE_MS = 3000;
const int MAX_HIDE_MS = 30000;

class plugin_t : public wf::plugin_interface_t
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> repo;
    wf::option_wrapper_t<bool> hide_at_start{"entry-cursor/hide_at_start"};
    wf::option_wrapper_t<int> timeout_ms{"entry-cursor/timeout_ms"};
    wf::wl_timer<false> failsafe;
    std::chrono::steady_clock::time_point hidden_at{};
    bool hidden = false;

    void hide(int ms, const char* reason)
    {
        const int clamped = std::clamp(ms, 1, MAX_HIDE_MS);
        if (!hidden)
        {
            wf::get_core().hide_cursor();
            hidden = true;
            hidden_at = std::chrono::steady_clock::now();
        }
        failsafe.set_timeout(clamped, [this] { show("timeout"); });
        LOGI("[entry-cursor] hide reason=", reason, " timeout_ms=", clamped);
    }

    void show(const char* reason)
    {
        failsafe.disconnect();
        if (!hidden)
        {
            return;
        }
        const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - hidden_at).count();
        wf::get_core().unhide_cursor();
        hidden = false;
        LOGI("[entry-cursor] show reason=", reason, " waited_ms=", waited);
    }

    wf::ipc::method_callback on_show = [this](const wf::json_t&) {
        show("ipc");
        return wf::ipc::json_ok();
    };

    wf::ipc::method_callback on_hide = [this](const wf::json_t& data) {
        int ms = DEFAULT_HIDE_MS;
        if (data.has_member("ms"))
        {
            if (!data["ms"].is_int())
            {
                return wf::ipc::json_error("ms must be a whole number");
            }
            ms = int(data["ms"]);
        }
        hide(ms, "ipc");
        wf::json_t answer = wf::ipc::json_ok();
        answer["ms"] = std::clamp(ms, 1, MAX_HIDE_MS);
        return answer;
    };

  public:
    void init() override
    {
        repo->register_method(SHOW_METHOD, on_show);
        repo->register_method(HIDE_METHOD, on_hide);
        if (hide_at_start)
        {
            hide(std::clamp(int(timeout_ms), 500, MAX_HIDE_MS), "start");
        }
    }

    void fini() override
    {
        repo->unregister_method(SHOW_METHOD);
        repo->unregister_method(HIDE_METHOD);
        show("fini");
    }
};
} // namespace entry_cursor

DECLARE_WAYFIRE_PLUGIN(entry_cursor::plugin_t);
