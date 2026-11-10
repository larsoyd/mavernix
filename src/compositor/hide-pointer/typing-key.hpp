#pragma once

#include <xkbcommon/xkbcommon.h>

namespace hide_pointer
{
// true when this key types a character with the keys held now
// shortcuts and the number pad never do
bool is_typing(xkb_state* state, xkb_keycode_t key);
} // namespace hide_pointer
