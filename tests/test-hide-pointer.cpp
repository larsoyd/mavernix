// which key presses count as typing and so hide the pointer

#include <QtTest>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

#include "typing-key.hpp"

using namespace hide_pointer;

namespace
{
// one layout, keys stay held in the order given
struct keyboard_t
{
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap* keymap = nullptr;
    xkb_state* state = nullptr;

    explicit keyboard_t(const char* layout = "us", const char* variant = "")
    {
        const xkb_rule_names names = {nullptr, "pc105", layout, variant, nullptr};
        keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        state = keymap ? xkb_state_new(keymap) : nullptr;
    }

    ~keyboard_t()
    {
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
    }

    void hold(uint32_t key) { xkb_state_update_key(state, key + 8, XKB_KEY_DOWN); }

    void tap(uint32_t key)
    {
        xkb_state_update_key(state, key + 8, XKB_KEY_DOWN);
        xkb_state_update_key(state, key + 8, XKB_KEY_UP);
    }

    bool types(uint32_t key) const { return is_typing(state, key + 8); }
};
} // namespace

class TestHidePointer : public QObject
{
    Q_OBJECT

  private slots:
    void letters_digits_and_space_are_typing()
    {
        keyboard_t keyboard;
        QVERIFY(keyboard.state);
        QVERIFY(keyboard.types(KEY_A));
        QVERIFY(keyboard.types(KEY_7));
        QVERIFY(keyboard.types(KEY_COMMA));
        QVERIFY(keyboard.types(KEY_SPACE));
    }

    void shift_and_caps_lock_still_type()
    {
        keyboard_t shifted;
        shifted.hold(KEY_LEFTSHIFT);
        QVERIFY(shifted.types(KEY_A));
        QVERIFY(shifted.types(KEY_1));

        keyboard_t locked;
        locked.tap(KEY_CAPSLOCK);
        QVERIFY(locked.types(KEY_A));
    }

    void shortcuts_are_not_typing_data()
    {
        QTest::addColumn<uint32_t>("modifier");
        QTest::newRow("ctrl") << uint32_t(KEY_LEFTCTRL);
        QTest::newRow("right ctrl") << uint32_t(KEY_RIGHTCTRL);
        QTest::newRow("alt") << uint32_t(KEY_LEFTALT);
        QTest::newRow("super") << uint32_t(KEY_LEFTMETA);
    }

    void shortcuts_are_not_typing()
    {
        QFETCH(uint32_t, modifier);
        keyboard_t keyboard;
        keyboard.hold(modifier);
        QVERIFY(!keyboard.types(KEY_A));
        QVERIFY(!keyboard.types(KEY_SPACE));

        keyboard_t with_shift;
        with_shift.hold(KEY_LEFTSHIFT);
        with_shift.hold(modifier);
        QVERIFY(!with_shift.types(KEY_A));
    }

    void keys_that_type_nothing_are_not_typing_data()
    {
        QTest::addColumn<uint32_t>("key");
        const QList<QPair<const char*, uint32_t>> keys = {
            {"return", KEY_ENTER}, {"backspace", KEY_BACKSPACE}, {"tab", KEY_TAB}, {"escape", KEY_ESC},
            {"delete", KEY_DELETE}, {"left", KEY_LEFT}, {"page down", KEY_PAGEDOWN}, {"home", KEY_HOME},
            {"f5", KEY_F5}, {"shift", KEY_LEFTSHIFT}, {"ctrl", KEY_LEFTCTRL}, {"alt gr", KEY_RIGHTALT},
            {"caps lock", KEY_CAPSLOCK}, {"volume up", KEY_VOLUMEUP}, {"print", KEY_SYSRQ}, {"menu", KEY_COMPOSE},
        };
        for (const auto& [name, key] : keys)
        {
            QTest::newRow(name) << key;
        }
    }

    void keys_that_type_nothing_are_not_typing()
    {
        QFETCH(uint32_t, key);
        keyboard_t keyboard;
        QVERIFY(!keyboard.types(key));
    }

    void the_number_pad_is_not_typing()
    {
        keyboard_t keyboard;
        QVERIFY(!keyboard.types(KEY_KP5));
        QVERIFY(!keyboard.types(KEY_KPENTER));
        QVERIFY(!keyboard.types(KEY_KPPLUS));
        keyboard.tap(KEY_NUMLOCK);
        QVERIFY(!keyboard.types(KEY_KP5));
        QVERIFY(!keyboard.types(KEY_KPDOT));
    }

    void alt_gr_characters_are_typing()
    {
        // at sign on a Norwegian keyboard
        keyboard_t keyboard("no");
        QVERIFY(keyboard.state);
        keyboard.hold(KEY_RIGHTALT);
        QVERIFY(keyboard.types(KEY_2));
    }

    void letters_of_other_scripts_are_typing()
    {
        keyboard_t norwegian("no");
        QVERIFY(norwegian.types(KEY_APOSTROPHE));
        keyboard_t russian("ru");
        QVERIFY(russian.state);
        QVERIFY(russian.types(KEY_A));
    }

    void dead_keys_are_not_typing()
    {
        // the diaeresis key waits for a letter
        keyboard_t keyboard("no");
        QVERIFY(!keyboard.types(KEY_RIGHTBRACE));
        QVERIFY(keyboard.types(KEY_E));
    }
};

QTEST_APPLESS_MAIN(TestHidePointer)
#include "test-hide-pointer.moc"
