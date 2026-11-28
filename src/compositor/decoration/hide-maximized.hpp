#pragma once

#include <wayfire/object.hpp>

namespace wf
{
// stored on core while maximized windows go without a frame
struct hide_maximized_frames_t : public wf::custom_data_t
{
    // also when the window keeps a gap around it
    bool with_gaps = false;
};

// emitted on core when that changes
struct hide_maximized_changed_signal
{};
}
