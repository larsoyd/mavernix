// The plugin "window-pictures": writes a small picture of one window (frame
// included, shadow left out) for the dock's miniatures. Clients can only
// capture whole outputs, and a minimised window is on none.
//
// IPC method "window-pictures/write": id, path (".png", or ".rgba" for plain
// pixels), largest_side; answers with the picture's "width" and "height".
// A ".rgba" file is the line "WINDOWPICTURE width height" followed by RGBA
// rows; it takes milliseconds where a PNG of a large window stalls the
// compositor for about 100 ms.
//
// (wayfire-plugins-extra's "view-shot" writes full size and is not packaged
// on Arch.)
//
// some clients drop their contents once told they are minimised, steam goes
// white within 30 ms, so a window is captured at the request

#include <fstream>
#include <optional>

#include <drm_fourcc.h>

#include <wayfire/img.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/render.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view.hpp>

#include "shrink.hpp"

namespace window_pictures
{
const std::string WRITE_METHOD = "window-pictures/write";
const std::string PNG_FILE_ENDING = ".png";
const std::string PLAIN_FILE_ENDING = ".rgba";
const std::string PLAIN_FILE_WORD = "WINDOWPICTURE";

static bool ends_with(const std::string& text, const std::string& ending)
{
    return (text.size() > ending.size()) && (text.compare(text.size() - ending.size(), std::string::npos, ending) == 0);
}

/** False if the file could not be written. */
static bool write_plain(const std::string& path, const picture_t& picture)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << PLAIN_FILE_WORD << " " << picture.width << " " << picture.height << "\n";
    file.write((const char*)picture.pixels.data(), (std::streamsize)picture.pixels.size());
    return file.good();
}

// a window at one moment with its rectangles, a minimised window can still change size
struct capture_t : public wf::custom_data_t
{
    wf::auxilliary_buffer_t snapshot;
    wf::geometry_t everything{};
    wf::geometry_t window{};

    explicit capture_t(wayfire_view view)
    {
        // Wayfire's take_snapshot() needs the window's output, which a window
        // being taken away, or one on no screen, does not have.
        if (!view->is_mapped() || (view->get_output() == nullptr))
        {
            return;
        }
        view->take_snapshot(snapshot);
        everything = view->get_surface_root_node()->get_bounding_box();
        auto toplevel = wf::toplevel_cast(view);
        window = toplevel ? toplevel->get_geometry() : everything;
    }
};

// the capture's pixels at its output's density, empty if it has none
static picture_t contents_of(capture_t& capture)
{
    picture_t contents;
    const auto size = capture.snapshot.get_size();
    if ((size.width <= 0) || (size.height <= 0))
    {
        return contents;
    }

    contents.width = size.width;
    contents.height = size.height;
    contents.pixels.resize(size_t(size.width) * size.height * 4);

    // Bytes R, G, B, A: DRM names formats from the least significant byte.
    wlr_texture_read_pixels_options options{};
    options.data = contents.pixels.data();
    options.format = DRM_FORMAT_ABGR8888;
    options.stride = uint32_t(size.width) * 4;
    if (!wlr_texture_read_pixels(capture.snapshot.get_texture(), &options))
    {
        return picture_t{};
    }

    return contents;
}

class plugin_t : public wf::plugin_interface_t, public wf::per_output_tracker_mixin_t<>
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> methods;

    wf::signal::connection_t<wf::view_minimize_request_signal> on_minimize_request =
        [](wf::view_minimize_request_signal* request) {
        if (request->state)
        {
            request->view->store_data(std::make_unique<capture_t>(request->view));
        }
        else
        {
            request->view->erase_data<capture_t>();
        }
    };

    wf::ipc::method_callback write_picture = [](const wf::json_t& data) -> wf::json_t {
        auto view = wf::ipc::json_find_view_or_throw(data);
        const std::string path = wf::ipc::json_get_string(data, "path");
        const int largest_side = int(wf::ipc::json_get_int64(data, "largest_side"));
        // optional, the answer says if this build honoured it
        const bool with_shadow = wf::ipc::json_get_optional_bool(data, "shadow").value_or(false);

        const bool as_plain_pixels = ends_with(path, PLAIN_FILE_ENDING);
        if ((!as_plain_pixels && !ends_with(path, PNG_FILE_ENDING)) || (largest_side <= 0))
        {
            return wf::ipc::json_error(
                "window-pictures/write needs a path ending in .png or .rgba and a largest_side above 0");
        }

        std::optional<capture_t> now;
        capture_t* capture = view->get_data<capture_t>().get();
        if (!capture)
        {
            capture = &now.emplace(view);
        }

        const picture_t contents = contents_of(*capture);
        if (contents.pixels.empty())
        {
            return wf::ipc::json_error("the window has no contents to take a picture of");
        }

        // Cut away the shadow. The rectangles are in points, the contents in
        // pixels.
        const wf::geometry_t everything = capture->everything;
        const wf::geometry_t window = with_shadow ? everything : capture->window;

        const double pixels_per_point = contents.width / std::max(1.0, double(everything.width));
        const int left = std::clamp(int((window.x - everything.x) * pixels_per_point), 0, contents.width - 1);
        const int top = std::clamp(int((window.y - everything.y) * pixels_per_point), 0, contents.height - 1);
        const int width = std::min(int(window.width * pixels_per_point), contents.width - left);
        const int height = std::min(int(window.height * pixels_per_point), contents.height - top);

        picture_t small = shrink_part(contents, left, top, width, height, largest_side);
        if (small.pixels.empty())
        {
            return wf::ipc::json_error("the window is empty");
        }

        if (as_plain_pixels)
        {
            if (!write_plain(path, small))
            {
                return wf::ipc::json_error("the picture could not be written to " + path);
            }
        }
        else
        {
            image_io::write_to_file(path, small.pixels.data(), small.width, small.height, "png");
        }

        auto answer = wf::ipc::json_ok();
        answer["width"] = small.width;
        answer["height"] = small.height;
        answer["shadow"] = with_shadow;
        return answer;
    };

  public:
    void init() override
    {
        methods->register_method(WRITE_METHOD, write_picture);
        init_output_tracking();
    }

    void fini() override
    {
        fini_output_tracking();
        methods->unregister_method(WRITE_METHOD);
    }

    void handle_new_output(wf::output_t* output) override { output->connect(&on_minimize_request); }
};
} // namespace window_pictures

DECLARE_WAYFIRE_PLUGIN(window_pictures::plugin_t);
