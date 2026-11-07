#pragma once

#include <wayfire/object.hpp>

namespace wf
{
// stored on core while windows that draw their own frame get the theme's corners and shadow
struct round_frameless_windows_t : public wf::custom_data_t
{};

// emitted on core when that changes
struct round_frameless_changed_signal
{};
}
