#pragma once

#include <QByteArray>
#include <QClipboard>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

class QMimeData;
struct wl_display;
struct zwp_virtual_keyboard_manager_v1;
struct zwp_virtual_keyboard_v1;

// puts text into whatever window has the keyboard, through a keyboard of its own
class TypedText : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString way READ way WRITE setWay NOTIFY wayChanged)

  public:
    struct Step
    {
        quint32 key;
        bool down;
        quint32 heldModifiers;
    };
    struct Plan
    {
        QByteArray keymap;
        QList<Step> steps;
    };
    // one key per code point, which only some programs read right
    static Plan typingPlan(const QString& text);
    // shift and insert, which pastes nearly everywhere and the primary selection in terminals
    static Plan pastePlan();
    // a copy of plain text worth putting back, null for anything else
    static QMimeData* keptCopy(const QMimeData* held);

    explicit TypedText(QObject* parent = nullptr);
    ~TypedText() override;

    // "paste" or "type"
    QString way() const { return typing ? QStringLiteral("type") : QStringLiteral("paste"); }
    void setWay(const QString& way);

    // waits until no window of this program holds the keyboard
    Q_INVOKABLE void insert(const QString& text);

    void fetched();

    std::function<bool()> keyboardFree;
    std::function<QString(const Plan&)> press;
    // null data clears
    std::function<void(QMimeData*, QClipboard::Mode)> offer;
    std::function<const QMimeData*(QClipboard::Mode)> held;
    int deadlineMs = 600;
    int putBackAfterMs = 250;
    int putBackDeadlineMs = 1500;

  signals:
    void wayChanged();
    void inserted(const QString& text);
    void refused(const QString& text, const QString& reason);

  private:
    void sendWhenFree();
    void refuse(const QString& reason);
    void putBack(const char* reason);
    QString send(const Plan& plan);
    QString connectKeyboard();

    bool typing = false;
    QString waiting;
    QElapsedTimer waited;
    QTimer deadline;
    // while the pasted text is on the clipboard: what was there, and when it goes back
    bool lent = false;
    std::shared_ptr<std::atomic<bool>> pressed;
    QMimeData* keptClipboard = nullptr;
    QMimeData* keptSelection = nullptr;
    QTimer afterFetch;
    QTimer putBackDeadline;
    wl_display* display = nullptr;
    zwp_virtual_keyboard_manager_v1* manager = nullptr;
    zwp_virtual_keyboard_v1* keyboard = nullptr;
};
