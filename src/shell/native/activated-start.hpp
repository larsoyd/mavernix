// starts a command with a token made from the click that asked for it, so a
// program that already has the thing open may bring its window back

#pragma once

#include <QFuture>
#include <QObject>
#include <QStringList>
#include <functional>

class QQuickItem;
class QWindow;

class ActivatedStart : public QObject
{
    Q_OBJECT

  public:
    explicit ActivatedStart(QObject* parent = nullptr);

    // starts without a token when the compositor gives none in time
    Q_INVOKABLE void run(const QStringList& command, QQuickItem* from);

    static bool start(const QStringList& command, const QString& token);

    // the compositor by default, tests give their own
    static std::function<QFuture<QString>(QWindow*)> tokenSource;
};
