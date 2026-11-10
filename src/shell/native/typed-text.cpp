#include "typed-text.hpp"

#include <QGuiApplication>
#include <QHash>
#include <QMimeData>
#include <QPointer>

#include <KSystemClipboard>

#include <atomic>
#include <cstring>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"

namespace
{
// an X server holds key codes up to 255, and the first eight are taken
constexpr int mostKeys = 247;
constexpr int keptBytes = 1 << 20;
constexpr quint32 shiftKey = 42;
constexpr quint32 insertKey = 110;
constexpr quint32 shiftHeld = 1;

// text on loan to the clipboard, which tells its lender when a program reads it
class LentText : public QMimeData
{
  public:
    LentText(const QString& text, TypedText* lender, std::shared_ptr<std::atomic<bool>> pressed)
        : lender(lender), pressed(std::move(pressed))
    {
        const QByteArray bytes = text.toUtf8();
        for (const char* format : {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING"})
            setData(QString::fromLatin1(format), bytes);
    }

  protected:
    QVariant retrieveData(const QString& format, QMetaType type) const override
    {
        // the clipboard reads from a thread of its own, and a read before the keys is no paste
        if (pressed->load())
        {
            QMetaObject::invokeMethod(const_cast<LentText*>(this), [lender = lender] {
                if (lender)
                    lender->fetched();
            }, Qt::QueuedConnection);
        }
        return QMimeData::retrieveData(format, type);
    }

  private:
    QPointer<TypedText> lender;
    std::shared_ptr<std::atomic<bool>> pressed;
};

bool plainText(const QString& format)
{
    static const QStringList named = {QStringLiteral("UTF8_STRING"), QStringLiteral("STRING"), QStringLiteral("TEXT"),
                                      QStringLiteral("COMPOUND_TEXT")};
    return format.startsWith(QLatin1String("text/")) || named.contains(format);
}

void bindManager(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t)
{
    if (std::strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0)
    {
        *static_cast<zwp_virtual_keyboard_manager_v1**>(data) = static_cast<zwp_virtual_keyboard_manager_v1*>(
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
    }
}

void globalGone(void*, wl_registry*, uint32_t) {}

const wl_registry_listener registryListener = {bindManager, globalGone};
} // namespace

TypedText::Plan TypedText::typingPlan(const QString& text)
{
    QHash<char32_t, quint32> keyOf;
    Plan plan;
    QByteArray codes;
    QByteArray symbols;
    for (char32_t point : text.toUcs4())
    {
        if (point < 0x20 || (point >= 0x7f && point < 0xa0))
            return {};
        if (!keyOf.contains(point))
        {
            const quint32 key = quint32(keyOf.size()) + 1;
            if (key > mostKeys)
                return {};
            keyOf.insert(point, key);
            codes += QByteArray("<K") + QByteArray::number(key) + "> = " + QByteArray::number(key + 8) + ";\n";
            symbols += QByteArray("key <K") + QByteArray::number(key) + "> {[U" + QByteArray::number(uint(point), 16)
                       + "]};\n";
        }
        plan.steps.append({keyOf.value(point), true, 0});
        plan.steps.append({keyOf.value(point), false, 0});
    }
    plan.keymap = "xkb_keymap {\nxkb_keycodes \"typed\" {\nminimum = 8;\nmaximum = 255;\n" + codes
                  + "};\nxkb_types \"typed\" { include \"complete\" };\n"
                    "xkb_compatibility \"typed\" { include \"complete\" };\n"
                    "xkb_symbols \"typed\" {\n"
                  + symbols + "};\n};\n";
    return plan;
}

// the usual codes for both keys, so a program still reading an older keymap sees the same two
TypedText::Plan TypedText::pastePlan()
{
    return {"xkb_keymap {\nxkb_keycodes \"paste\" {\nminimum = 8;\nmaximum = 255;\n<LFSH> = 50;\n<INS> = 118;\n};\n"
            "xkb_types \"paste\" { include \"complete\" };\n"
            "xkb_compatibility \"paste\" { include \"complete\" };\n"
            "xkb_symbols \"paste\" {\nkey <LFSH> {[Shift_L]};\nkey <INS> {[Insert]};\n};\n};\n",
            {{shiftKey, true, shiftHeld}, {insertKey, true, shiftHeld}, {insertKey, false, shiftHeld}, {shiftKey, false, 0}}};
}

QMimeData* TypedText::keptCopy(const QMimeData* held)
{
    const QStringList formats = held ? held->formats() : QStringList();
    if (formats.isEmpty() || !std::all_of(formats.begin(), formats.end(), plainText))
        return nullptr;
    auto* copy = new QMimeData;
    qsizetype bytes = 0;
    for (const QString& format : formats)
    {
        const QByteArray data = held->data(format);
        bytes += data.size();
        if (bytes > keptBytes)
        {
            delete copy;
            return nullptr;
        }
        copy->setData(format, data);
    }
    return copy;
}

TypedText::TypedText(QObject* parent) : QObject(parent)
{
    keyboardFree = [] { return QGuiApplication::focusWindow() == nullptr; };
    press = [this](const Plan& plan) { return send(plan); };
    offer = [](QMimeData* data, QClipboard::Mode mode) {
        KSystemClipboard* board = KSystemClipboard::instance();
        if (board == nullptr)
            delete data;
        else if (data != nullptr)
            board->setMimeData(data, mode);
        else
            board->clear(mode);
    };
    held = [](QClipboard::Mode mode) -> const QMimeData* {
        KSystemClipboard* board = KSystemClipboard::instance();
        return board != nullptr ? board->mimeData(mode) : nullptr;
    };
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, this, [this] { refuse(QStringLiteral("keyboard_still_here")); });
    afterFetch.setSingleShot(true);
    connect(&afterFetch, &QTimer::timeout, this, [this] { putBack("read"); });
    putBackDeadline.setSingleShot(true);
    connect(&putBackDeadline, &QTimer::timeout, this, [this] { putBack("deadline"); });
    connect(qGuiApp, &QGuiApplication::focusWindowChanged, this, &TypedText::sendWhenFree);
}

TypedText::~TypedText()
{
    // the clipboard must not keep what was only lent to it
    if (lent)
        putBack("going_away");
    if (keyboard != nullptr)
        zwp_virtual_keyboard_v1_destroy(keyboard);
    if (manager != nullptr)
        zwp_virtual_keyboard_manager_v1_destroy(manager);
}

void TypedText::setWay(const QString& way)
{
    const bool wanted = way == QLatin1String("type");
    if (wanted == typing)
        return;
    typing = wanted;
    emit wayChanged();
}

void TypedText::insert(const QString& text)
{
    if (!waiting.isEmpty())
    {
        qWarning("[typing] refused reason=replaced chars=%d waited_ms=%lld", int(waiting.toUcs4().size()), waited.elapsed());
        emit refused(waiting, QStringLiteral("replaced"));
    }
    waiting = text;
    waited.start();
    if (!typing)
    {
        // a loan still out holds this program's own text, what goes back is what was kept then
        if (!lent)
        {
            keptClipboard = keptCopy(held(QClipboard::Clipboard));
            keptSelection = keptCopy(held(QClipboard::Selection));
        }
        afterFetch.stop();
        putBackDeadline.stop();
        lent = true;
        pressed = std::make_shared<std::atomic<bool>>(false);
        offer(new LentText(text, this, pressed), QClipboard::Clipboard);
        offer(new LentText(text, this, pressed), QClipboard::Selection);
    }
    else if (lent)
    {
        putBack("replaced");
    }
    deadline.start(deadlineMs);
    sendWhenFree();
}

void TypedText::refuse(const QString& reason)
{
    const QString text = waiting;
    waiting.clear();
    deadline.stop();
    qWarning("[typing] refused reason=%s chars=%d waited_ms=%lld", qPrintable(reason), int(text.toUcs4().size()),
             waited.elapsed());
    if (lent)
        putBack("refused");
    emit refused(text, reason);
}

void TypedText::sendWhenFree()
{
    if (waiting.isEmpty() || !keyboardFree())
        return;
    const Plan plan = typing ? typingPlan(waiting) : pastePlan();
    const QString failure = plan.steps.isEmpty() ? QStringLiteral("cannot_be_typed") : press(plan);
    if (!failure.isEmpty())
    {
        refuse(failure);
        return;
    }
    const QString text = waiting;
    waiting.clear();
    deadline.stop();
    if (lent)
    {
        pressed->store(true);
        putBackDeadline.start(putBackDeadlineMs);
    }
    qInfo("[typing] sent way=%s chars=%d waited_ms=%lld", qPrintable(way()), int(text.toUcs4().size()), waited.elapsed());
    emit inserted(text);
}

// a program reads the lent text when it pastes, some more than once
void TypedText::fetched()
{
    if (lent && pressed->load() && !afterFetch.isActive())
        afterFetch.start(putBackAfterMs);
}

void TypedText::putBack(const char* reason)
{
    afterFetch.stop();
    putBackDeadline.stop();
    lent = false;
    qInfo("[typing] put_back reason=%s clipboard=%s selection=%s waited_ms=%lld", reason, keptClipboard ? "text" : "cleared",
          keptSelection ? "text" : "cleared", waited.elapsed());
    offer(std::exchange(keptClipboard, nullptr), QClipboard::Clipboard);
    offer(std::exchange(keptSelection, nullptr), QClipboard::Selection);
}

QString TypedText::connectKeyboard()
{
    if (keyboard != nullptr)
        return {};
    auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (wayland == nullptr || wayland->seat() == nullptr)
        return QStringLiteral("no_wayland_seat");
    display = wayland->display();

    // a queue of its own, so the answer is read without running the toolkit's handlers
    wl_event_queue* queue = wl_display_create_queue(display);
    auto* wrapped = static_cast<wl_display*>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapped), queue);
    wl_registry* registry = wl_display_get_registry(wrapped);
    wl_proxy_wrapper_destroy(wrapped);
    wl_registry_add_listener(registry, &registryListener, &manager);
    const bool answered = wl_display_roundtrip_queue(display, queue) >= 0;
    if (manager != nullptr)
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(manager), nullptr);
    wl_registry_destroy(registry);
    wl_event_queue_destroy(queue);
    if (!answered)
        return QStringLiteral("no_answer");
    if (manager == nullptr)
        return QStringLiteral("no_virtual_keyboard");
    keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(manager, wayland->seat());
    return {};
}

QString TypedText::send(const Plan& plan)
{
    const QString failure = connectKeyboard();
    if (!failure.isEmpty())
        return failure;
    const int file = memfd_create("typed-keymap", MFD_CLOEXEC);
    const qsizetype size = plan.keymap.size() + 1;
    if (file < 0 || write(file, plan.keymap.constData(), size) != size)
    {
        if (file >= 0)
            close(file);
        return QStringLiteral("keymap_file");
    }
    zwp_virtual_keyboard_v1_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, file, uint32_t(size));
    close(file);
    uint32_t time = 0;
    quint32 heldModifiers = 0;
    for (const Step& step : plan.steps)
    {
        zwp_virtual_keyboard_v1_key(keyboard, time++, step.key,
                                    step.down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
        if (step.heldModifiers != heldModifiers)
            zwp_virtual_keyboard_v1_modifiers(keyboard, step.heldModifiers, 0, 0, 0);
        heldModifiers = step.heldModifiers;
    }
    wl_display_flush(display);
    return {};
}
