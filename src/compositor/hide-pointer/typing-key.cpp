#include "typing-key.hpp"

#include <initializer_list>

namespace hide_pointer
{
bool is_typing(xkb_state* state, xkb_keycode_t key)
{
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(state, key);
    if (sym >= XKB_KEY_KP_Space && sym <= XKB_KEY_KP_Equal)
    {
        return false;
    }
    // return, tab, escape and the like have a code, but below the space
    const uint32_t typed = xkb_keysym_to_utf32(sym);
    if (typed < 0x20 || typed == 0x7f)
    {
        return false;
    }
    for (const char* held : {XKB_MOD_NAME_CTRL, XKB_MOD_NAME_ALT, XKB_MOD_NAME_LOGO})
    {
        if (xkb_state_mod_name_is_active(state, held, XKB_STATE_MODS_EFFECTIVE) > 0)
        {
            return false;
        }
    }
    return true;
}
} // namespace hide_pointer
