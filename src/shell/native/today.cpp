#include "today.hpp"

namespace
{
// a clock that was set or a machine that slept is found within this long
const int longest_wait_ms = 60000;
}

Today::Today(QObject* parent, Clock clock) : QObject(parent), clock(std::move(clock))
{
    held = this->clock().date();
    next.setSingleShot(true);
    next.setTimerType(Qt::PreciseTimer);
    connect(&next, &QTimer::timeout, this, &Today::look);
    look();
}

Today* Today::shared()
{
    static Today one;
    return &one;
}

QDate Today::date() const
{
    return held;
}

QString Today::stamp() const
{
    return held.toString(Qt::ISODate);
}

int Today::look()
{
    const QDateTime now = clock();
    if (now.date() != held)
    {
        held = now.date();
        emit changed();
    }
    const qint64 to_midnight = now.msecsTo(now.date().addDays(1).startOfDay()) + 1;
    const int wait = int(qBound(qint64(1), to_midnight, qint64(longest_wait_ms)));
    next.start(wait);
    return wait;
}

bool Today::shown() const
{
    return showing;
}

void Today::setShown(bool shown)
{
    if (shown != showing)
    {
        showing = shown;
        emit shownChanged();
    }
}
