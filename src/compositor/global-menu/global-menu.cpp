#include <wayfire/plugin.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/view.hpp>

#include <wayfire/nonstd/wlroots-full.hpp>

namespace
{
class GlobalMenu : public wf::plugin_interface_t
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> methods;
    wf::ipc::method_callback address = [](const wf::json_t& request) {
        auto view = wf::ipc::json_find_view_or_throw(request);
        auto result = wf::ipc::json_ok();
        auto property = [&view](const char* name) { return view->get_property<std::string>(name).value_or(""); };
        const auto service = property("kde-appmenu-service-name");
        const auto path = property("kde-appmenu-object-path");
        if (!service.empty() && !path.empty())
        {
            result["protocol"] = "dbusmenu";
            result["service"] = service;
            result["path"] = path;
        }
        else
        {
            const auto gtkService = property("gtk-shell-unique-bus-name");
            auto menuPath = property("gtk-shell-menubar-path");
            if (menuPath.empty())
                menuPath = property("gtk-shell-app-menu-path");
            if (!gtkService.empty() && !menuPath.empty())
            {
                result["protocol"] = "gmenu";
                result["service"] = gtkService;
                result["path"] = menuPath;
                result["appMenu"] = property("gtk-shell-app-menu-path");
                result["appActions"] = property("gtk-shell-application-object-path");
                result["windowActions"] = property("gtk-shell-window-object-path");
            }
        }
#if WF_HAS_XWAYLAND
        if (auto* surface = view->get_wlr_surface())
        {
            if (auto* xwindow = wlr_xwayland_surface_try_from_wlr_surface(surface))
                result["xid"] = uint64_t(xwindow->window_id);
        }
#endif
        return result;
    };

  public:
    void init() override { methods->register_method("global-menu/address", address); }
    void fini() override { methods->unregister_method("global-menu/address"); }
};
} // namespace

DECLARE_WAYFIRE_PLUGIN(GlobalMenu);
