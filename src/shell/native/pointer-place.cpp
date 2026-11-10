#include "pointer-place.hpp"

namespace
{
constexpr int deadlineMs = 150;
}

PointerPlace::PointerPlace(QObject* parent) : QObject(parent)
{
    deadline.setSingleShot(true);
    deadline.setInterval(deadlineMs);
    connect(&deadline, &QTimer::timeout, this, [this] { giveUp("no_answer"); });
    connect(&compositor, &CompositorSocket::connected, this, [this] {
        if (deadline.isActive())
            question();
    });
    QTimer::singleShot(0, &compositor, &CompositorSocket::open);
}

void PointerPlace::ask()
{
    ++asked;
    deadline.start();
    if (compositor.isConnected())
        question();
    else if (!compositor.isConnecting())
        compositor.open();
}

void PointerPlace::question()
{
    compositor.request(QStringLiteral("window-rules/get_cursor_position"), {},
                       [this, number = asked](const QJsonObject& answer) {
                           if (number != asked || !deadline.isActive())
                               return;
                           const QJsonObject place = answer.value(QStringLiteral("pos")).toObject();
                           if (!place.contains(QStringLiteral("x")) || !place.contains(QStringLiteral("y")))
                           {
                               giveUp("no_place_in_answer");
                               return;
                           }
                           deadline.stop();
                           emit found(place.value(QStringLiteral("x")).toDouble(), place.value(QStringLiteral("y")).toDouble());
                       });
}

void PointerPlace::giveUp(const char* reason)
{
    deadline.stop();
    qWarning("[pointer-place] unknown reason=%s", reason);
    emit unknown();
}
