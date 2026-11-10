#include <QtTest>

#include <QGuiApplication>
#include <QMimeData>

#include <memory>
#include <vector>
#include <xkbcommon/xkbcommon.h>

#include "typed-text.hpp"

namespace
{
struct Keymap
{
    explicit Keymap(const QByteArray& text)
    {
        context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        keymap = xkb_keymap_new_from_string(context, text.constData(), XKB_KEYMAP_FORMAT_TEXT_V1,
                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
        state = keymap != nullptr ? xkb_state_new(keymap) : nullptr;
    }
    ~Keymap()
    {
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
    }
    xkb_context* context;
    xkb_keymap* keymap;
    xkb_state* state;
};

// what a client reading the keymap sees for the planned presses
QString readBack(const TypedText::Plan& plan)
{
    const Keymap map(plan.keymap);
    QString out;
    if (map.state == nullptr)
        return out;
    for (const TypedText::Step& step : plan.steps)
    {
        if (!step.down)
            continue;
        const char32_t point = xkb_state_key_get_utf32(map.state, step.key + 8);
        out += QString::fromUcs4(&point, 1);
    }
    return out;
}

QList<quint32> pressed(const TypedText::Plan& plan)
{
    QList<quint32> out;
    for (const TypedText::Step& step : plan.steps)
    {
        if (step.down)
            out << step.key;
    }
    return out;
}

QMimeData* textData(const QString& text)
{
    auto* data = new QMimeData;
    data->setData("text/plain", text.toUtf8());
    data->setData("UTF8_STRING", text.toUtf8());
    return data;
}

// a clipboard and a keyboard that only write down what they are asked
struct Desk
{
    // declared before the text it serves, which may still put things back as it goes
    void serve(TypedText& typed)
    {
        typed.keyboardFree = [this] { return free; };
        typed.press = [this](const TypedText::Plan& plan) {
            presses << plan;
            return pressFailure;
        };
        typed.offer = [this](QMimeData* data, QClipboard::Mode mode) {
            offers << (mode == QClipboard::Clipboard ? "clipboard:" : "selection:")
                          + (data ? QString::fromUtf8(data->data("text/plain")) : QString("cleared"));
            std::unique_ptr<QMimeData>& slot = mode == QClipboard::Clipboard ? clipboard : selection;
            if (keepsReplaced)
                replaced.push_back(std::move(slot));
            slot.reset(data);
        };
        typed.held = [this](QClipboard::Mode mode) -> const QMimeData* {
            return (mode == QClipboard::Clipboard ? clipboard : selection).get();
        };
        typed.deadlineMs = 60;
        typed.putBackAfterMs = 20;
        typed.putBackDeadlineMs = 150;
    }
    void keyboardLeaves()
    {
        free = true;
        emit qGuiApp->focusWindowChanged(nullptr);
    }
    bool free = false;
    // a clipboard may free what it held some time after it was replaced
    bool keepsReplaced = false;
    std::vector<std::unique_ptr<QMimeData>> replaced;
    QString pressFailure;
    QList<TypedText::Plan> presses;
    QStringList offers;
    std::unique_ptr<QMimeData> clipboard;
    std::unique_ptr<QMimeData> selection;
};
} // namespace

class TypedTextTest : public QObject
{
    Q_OBJECT

  private slots:
    void aClientReadsTheTypedKeysAsTheText_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("one emoji") << QString("😀");
        QTest::newRow("family joined by zero width joiners") << QString("👨‍👩‍👧‍👦");
        QTest::newRow("keycap on a plain sign") << QString("#️⃣");
        QTest::newRow("flag of tag letters") << QString("🏴󠁧󠁢󠁥󠁮󠁧󠁿");
        QTest::newRow("skin tone") << QString("👋🏿");
        QTest::newRow("plain letters and a space") << QString("a Z9");
        QTest::newRow("the same one twice") << QString("😀😀");
    }

    void aClientReadsTheTypedKeysAsTheText()
    {
        QFETCH(QString, text);
        const TypedText::Plan plan = TypedText::typingPlan(text);
        QCOMPARE(plan.steps.size(), 2 * text.toUcs4().size());
        QCOMPARE(readBack(plan), text);
    }

    void everyTypedKeyIsLetGoRightAfterItsPress()
    {
        const TypedText::Plan plan = TypedText::typingPlan("😀a");
        QCOMPARE(plan.steps.size(), 4);
        for (int step = 0; step < 4; step += 2)
        {
            QVERIFY(plan.steps[step].down);
            QVERIFY(!plan.steps[step + 1].down);
            QCOMPARE(plan.steps[step].key, plan.steps[step + 1].key);
            QCOMPARE(plan.steps[step].heldModifiers, 0u);
        }
    }

    void aRepeatedCodePointSharesItsKey()
    {
        const QList<quint32> keys = pressed(TypedText::typingPlan("😀a😀"));
        QCOMPARE(keys.size(), 3);
        QCOMPARE(keys[0], keys[2]);
        QVERIFY(keys[0] != keys[1]);
    }

    void typedKeysStayWithinWhatAnXServerCanHold()
    {
        QString many;
        for (char32_t point = 0x4E00; point < 0x4E00 + 247; ++point)
            many += QString::fromUcs4(&point, 1);
        const TypedText::Plan fits = TypedText::typingPlan(many);
        const QList<quint32> keys = pressed(fits);
        QCOMPARE(keys.size(), 247);
        QCOMPARE(*std::max_element(keys.begin(), keys.end()), 247u);
        QCOMPARE(readBack(fits), many);

        const char32_t extra = 0x5000;
        const TypedText::Plan tooMany = TypedText::typingPlan(many + QString::fromUcs4(&extra, 1));
        QVERIFY(tooMany.steps.isEmpty());
        QVERIFY(tooMany.keymap.isEmpty());
    }

    void nothingAndControlCharactersAreNotTyped()
    {
        QVERIFY(TypedText::typingPlan("").steps.isEmpty());
        QVERIFY(TypedText::typingPlan("a\nb").steps.isEmpty());
        QVERIFY(TypedText::typingPlan(QString(QChar(0x1b))).steps.isEmpty());
    }

    void pastingIsShiftAndInsertOnTheirUsualCodes()
    {
        const TypedText::Plan plan = TypedText::pastePlan();
        const Keymap map(plan.keymap);
        QVERIFY(map.state != nullptr);
        // the codes of every ordinary layout, where a program with an older keymap looks them up
        QCOMPARE(xkb_state_key_get_one_sym(map.state, 42 + 8), xkb_keysym_t(XKB_KEY_Shift_L));
        QCOMPARE(xkb_state_key_get_one_sym(map.state, 110 + 8), xkb_keysym_t(XKB_KEY_Insert));

        QStringList seen;
        for (const TypedText::Step& step : plan.steps)
        {
            xkb_state_update_key(map.state, step.key + 8, step.down ? XKB_KEY_DOWN : XKB_KEY_UP);
            const bool shift = xkb_state_mod_name_is_active(map.state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) == 1;
            // what is announced must be what the keymap itself makes of the keys
            QCOMPARE(step.heldModifiers, shift ? 1u : 0u);
            seen << QString("%1%2%3").arg(step.key).arg(step.down ? "down" : "up").arg(shift ? "+shift" : "");
        }
        QCOMPARE(seen, QStringList({"42down+shift", "110down+shift", "110up+shift", "42up"}));
    }

    void onlyPlainTextIsKeptForPuttingBack()
    {
        const std::unique_ptr<QMimeData> text(textData("old words"));
        const std::unique_ptr<QMimeData> copy(TypedText::keptCopy(text.get()));
        QVERIFY(copy != nullptr);
        QCOMPARE(copy->formats(), text->formats());
        QCOMPARE(copy->data("UTF8_STRING"), QByteArray("old words"));

        QMimeData picture;
        picture.setData("text/plain", "a picture");
        picture.setData("image/png", "bytes");
        QVERIFY(TypedText::keptCopy(&picture) == nullptr);

        QMimeData secret;
        secret.setData("text/plain", "hunter2");
        secret.setData("x-kde-passwordManagerHint", "secret");
        QVERIFY(TypedText::keptCopy(&secret) == nullptr);

        const QMimeData nothing;
        QVERIFY(TypedText::keptCopy(&nothing) == nullptr);
        QVERIFY(TypedText::keptCopy(nullptr) == nullptr);

        QMimeData book;
        book.setData("text/plain", QByteArray((1 << 20) + 1, 'a'));
        QVERIFY(TypedText::keptCopy(&book) == nullptr);
        QMimeData page;
        page.setData("text/plain", QByteArray(1 << 20, 'a'));
        const std::unique_ptr<QMimeData> kept(TypedText::keptCopy(&page));
        QVERIFY(kept != nullptr);
    }

    void pastingLendsTheTextThenPressesOnceTheKeyboardLeft()
    {
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        desk.clipboard.reset(textData("old words"));
        QSignalSpy inserted(&typed, &TypedText::inserted);

        typed.insert("😀");
        QCOMPARE(desk.offers, QStringList({"clipboard:😀", "selection:😀"}));
        QCOMPARE(desk.clipboard->data("text/plain;charset=utf-8"), QByteArray("😀"));
        QCOMPARE(desk.clipboard->data("UTF8_STRING"), QByteArray("😀"));
        QVERIFY(desk.presses.isEmpty());

        desk.keyboardLeaves();
        QCOMPARE(desk.presses.size(), 1);
        QCOMPARE(pressed(desk.presses[0]), QList<quint32>({42, 110}));
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(desk.offers.size(), 2);

        // a later change of windows presses nothing more
        desk.keyboardLeaves();
        QCOMPARE(desk.presses.size(), 1);
        QCOMPARE(inserted.count(), 1);
    }

    void whatWasThereGoesBackOnceAProgramReadTheText()
    {
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        desk.clipboard.reset(textData("old words"));
        typed.insert("😀");
        desk.keyboardLeaves();

        desk.clipboard->data("text/plain");
        desk.clipboard->data("UTF8_STRING");
        QCOMPARE(desk.offers.size(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(desk.offers.size(), 4, 100);
        QCOMPARE(desk.offers.mid(2), QStringList({"clipboard:old words", "selection:cleared"}));
        QCOMPARE(desk.clipboard->data("UTF8_STRING"), QByteArray("old words"));
        QTest::qWait(200);
        QCOMPARE(desk.offers.size(), 4);
    }

    void theSelectionAloneBeingReadPutsBothBack()
    {
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        desk.selection.reset(textData("marked words"));
        typed.insert("😀");
        desk.keyboardLeaves();
        desk.selection->data("text/plain");
        QTRY_COMPARE_WITH_TIMEOUT(desk.offers.size(), 4, 100);
        QCOMPARE(desk.offers.mid(2), QStringList({"clipboard:cleared", "selection:marked words"}));
    }

    void aReadBeforeThePressPutsNothingBack()
    {
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        typed.deadlineMs = 400;
        typed.insert("😀");
        desk.clipboard->data("text/plain");
        QTest::qWait(60);
        QCOMPARE(desk.offers.size(), 2);
        desk.keyboardLeaves();
        QCOMPARE(desk.presses.size(), 1);
        QTest::qWait(60);
        QCOMPARE(desk.offers.size(), 2);
    }

    void unreadTextGoesBackAtTheDeadline()
    {
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        typed.insert("😀");
        desk.keyboardLeaves();
        QTest::qWait(80);
        QCOMPARE(desk.offers.size(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(desk.offers.size(), 4, 200);
    }

    void aKeyboardThatNeverLeavesIsRefusedAndTheLoanEnds()
    {
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        desk.clipboard.reset(textData("old words"));
        QSignalSpy refused(&typed, &TypedText::refused);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("\\[typing\\] refused reason=keyboard_still_here.*"));
        typed.insert("😀");
        QTRY_COMPARE_WITH_TIMEOUT(refused.count(), 1, 300);
        QCOMPARE(refused[0][1].toString(), QString("keyboard_still_here"));
        QVERIFY(desk.presses.isEmpty());
        QCOMPARE(desk.offers.mid(2), QStringList({"clipboard:old words", "selection:cleared"}));
        desk.keyboardLeaves();
        QVERIFY(desk.presses.isEmpty());
    }

    void aFailedPressIsRefusedAndTheLoanEnds()
    {
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        desk.pressFailure = "no_virtual_keyboard";
        desk.free = true;
        QSignalSpy refused(&typed, &TypedText::refused);
        QSignalSpy inserted(&typed, &TypedText::inserted);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("\\[typing\\] refused reason=no_virtual_keyboard.*"));
        typed.insert("😀");
        QCOMPARE(refused.count(), 1);
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(desk.offers.mid(2), QStringList({"clipboard:cleared", "selection:cleared"}));
    }

    void aSecondTextKeepsWhatTheFirstReplaced()
    {
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        desk.clipboard.reset(textData("old words"));
        desk.free = true;
        typed.insert("😀");
        // the first was read, and its put back must not land on the second
        desk.clipboard->data("text/plain");
        typed.insert("🙂");
        QCOMPARE(desk.offers, QStringList({"clipboard:😀", "selection:😀", "clipboard:🙂", "selection:🙂"}));
        QTest::qWait(60);
        QCOMPARE(desk.offers.size(), 4);
        desk.clipboard->data("text/plain");
        QTRY_COMPARE_WITH_TIMEOUT(desk.offers.size(), 6, 100);
        QCOMPARE(desk.offers.mid(4), QStringList({"clipboard:old words", "selection:cleared"}));
    }

    void aSecondTextBeforeTheKeyboardLeftReplacesTheFirst()
    {
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        desk.clipboard.reset(textData("old words"));
        typed.deadlineMs = 400;
        QSignalSpy refused(&typed, &TypedText::refused);
        QSignalSpy inserted(&typed, &TypedText::inserted);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("\\[typing\\] refused reason=replaced.*"));
        typed.insert("😀");
        typed.insert("🙂");
        QCOMPARE(refused.count(), 1);
        QCOMPARE(refused[0][0].toString(), QString("😀"));
        QCOMPARE(desk.offers, QStringList({"clipboard:😀", "selection:😀", "clipboard:🙂", "selection:🙂"}));
        desk.keyboardLeaves();
        QCOMPARE(desk.presses.size(), 1);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(inserted[0][0].toString(), QString("🙂"));
        desk.clipboard->data("text/plain");
        QTRY_COMPARE_WITH_TIMEOUT(desk.offers.size(), 6, 100);
        QCOMPARE(desk.offers.mid(4), QStringList({"clipboard:old words", "selection:cleared"}));
    }

    void theFirstLoanEndingMustNotTakeTheSecondAway_data()
    {
        QTest::addColumn<bool>("firstWasRead");
        QTest::addColumn<bool>("readWasNoticed");
        QTest::addColumn<bool>("keepsReplaced");
        QTest::newRow("never read, its deadline still running") << false << false << false;
        QTest::newRow("read, the notice still on its way") << true << false << false;
        QTest::newRow("read, the notice on its way from text the clipboard still holds") << true << false << true;
        QTest::newRow("read and noticed, the put back counting down") << true << true << false;
    }

    void theFirstLoanEndingMustNotTakeTheSecondAway()
    {
        QFETCH(bool, firstWasRead);
        QFETCH(bool, readWasNoticed);
        QFETCH(bool, keepsReplaced);
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        desk.keepsReplaced = keepsReplaced;
        typed.deadlineMs = 600;
        desk.free = true;
        typed.insert("😀");
        if (firstWasRead)
            desk.clipboard->data("text/plain");
        if (readWasNoticed)
            QCoreApplication::processEvents();
        desk.free = false;
        typed.insert("🙂");
        QTest::qWait(250);
        QCOMPARE(desk.offers, QStringList({"clipboard:😀", "selection:😀", "clipboard:🙂", "selection:🙂"}));
        QCOMPARE(desk.presses.size(), 1);
        desk.keyboardLeaves();
        QCOMPARE(desk.presses.size(), 2);
    }

    void typingAfterAPasteEndsItsLoan()
    {
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        desk.clipboard.reset(textData("old words"));
        desk.free = true;
        typed.insert("😀");
        typed.setWay("type");
        typed.insert("🙂");
        QCOMPARE(desk.offers.mid(2), QStringList({"clipboard:old words", "selection:cleared"}));
        QCOMPARE(desk.presses.size(), 2);
    }

    void typingLeavesTheClipboardAlone()
    {
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        typed.setWay("type");
        QCOMPARE(typed.way(), QString("type"));
        desk.free = true;
        typed.insert("🙂");
        QVERIFY(desk.offers.isEmpty());
        QCOMPARE(desk.presses.size(), 1);
        QCOMPARE(readBack(desk.presses[0]), QString("🙂"));
        typed.setWay("anything else");
        QCOMPARE(typed.way(), QString("paste"));
    }

    void textThatCannotBeTypedIsRefused()
    {
        Desk desk;
        TypedText typed;
        desk.serve(typed);
        typed.setWay("type");
        desk.free = true;
        QSignalSpy refused(&typed, &TypedText::refused);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("\\[typing\\] refused reason=cannot_be_typed.*"));
        typed.insert("a\nb");
        QCOMPARE(refused.count(), 1);
        QVERIFY(desk.presses.isEmpty());
    }

    void whatWentBackIsNotPutBackAgain()
    {
        Desk desk;
        {
            TypedText typed;
            desk.serve(typed);
            desk.free = true;
            typed.insert("😀");
            desk.clipboard->data("text/plain");
            QTRY_COMPARE_WITH_TIMEOUT(desk.offers.size(), 4, 100);
        }
        QCOMPARE(desk.offers.size(), 4);
    }

    void goingAwayEndsTheLoan()
    {
        QStringList offers;
        {
            Desk desk;
            TypedText typed;
            desk.serve(typed);
            desk.clipboard.reset(textData("old words"));
            typed.insert("😀");
            desk.keyboardLeaves();
            typed.offer = [&offers](QMimeData* data, QClipboard::Mode) {
                offers << (data ? QString::fromUtf8(data->data("text/plain")) : QString("cleared"));
                delete data;
            };
        }
        QCOMPARE(offers, QStringList({"old words", "cleared"}));
    }
};

QTEST_MAIN(TypedTextTest)
#include "test-typed-text.moc"
