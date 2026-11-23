#pragma once

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <functional>

// the day it is, renewed at midnight and when the clock is found on another day,
// and whether pictures show it
class Today : public QObject
{
    Q_OBJECT

  public:
    using Clock = std::function<QDateTime()>;

    explicit Today(QObject* parent = nullptr, Clock clock = [] { return QDateTime::currentDateTime(); });

    // one for the whole shell, so every picture turns on the same tick
    static Today* shared();

    QDate date() const;
    // as 2026-11-23
    QString stamp() const;

    bool shown() const;
    void setShown(bool shown);

    // reads the clock and returns how many ms it waits before reading it again
    int look();

  signals:
    void changed();
    void shownChanged();

  private:
    Clock clock;
    QDate held;
    bool showing = true;
    QTimer next;
};
