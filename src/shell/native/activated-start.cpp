#include "activated-start.hpp"

#include <KWaylandExtras>
#include <QDebug>
#include <QFile>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <memory>

namespace
{
const QString TOKEN = QStringLiteral("XDG_ACTIVATION_TOKEN");
// the compositor answers within a frame or two, a silent one must not hold up the open
constexpr int TOKEN_WAIT_MS = 400;

QFuture<QString> askCompositor(QWindow* window)
{
    if (QGuiApplication::platformName() != QLatin1String("wayland")) {
        return QtFuture::makeReadyValueFuture(QString());
    }
    return KWaylandExtras::xdgActivationToken(window, QString());
}

// quickshell drops the qt style for itself once running, programs get the one it was started with
QProcessEnvironment startedWith()
{
    QFile file(QStringLiteral("/proc/self/environ"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "[activated-start] environ=unreadable fallback=current";
        return QProcessEnvironment(QProcessEnvironment::InheritFromParent);
    }
    QProcessEnvironment environment;
    for (const QByteArray& entry : file.readAll().split('\0')) {
        const qsizetype split = entry.indexOf('=');
        if (split > 0) {
            environment.insert(QString::fromLocal8Bit(entry.left(split)), QString::fromLocal8Bit(entry.mid(split + 1)));
        }
    }
    return environment;
}
}

std::function<QFuture<QString>(QWindow*)> ActivatedStart::tokenSource = askCompositor;

ActivatedStart::ActivatedStart(QObject* parent) : QObject(parent)
{
}

void ActivatedStart::run(const QStringList& command, QQuickItem* from)
{
    QWindow* window = from ? from->window() : nullptr;
    if (!window) {
        start(command, QString());
        return;
    }
    auto* watcher = new QFutureWatcher<QString>(this);
    auto* late = new QTimer(watcher);
    late->setSingleShot(true);
    // the answer and the deadline can land in the same pass of the event loop
    auto started = std::make_shared<bool>(false);
    auto finish = [watcher, started, command](const QString& token, const char* reason) {
        if (*started) {
            return;
        }
        *started = true;
        watcher->deleteLater();
        if (!start(command, token)) {
            qWarning().noquote().nospace() << "[activated-start] refused reason=no-start program=" << command.value(0);
        } else if (token.isEmpty()) {
            qInfo().noquote().nospace() << "[activated-start] token=none reason=" << reason << " program=" << command.value(0);
        }
    };
    connect(watcher, &QFutureWatcherBase::finished, watcher, [watcher, finish] {
        const bool given = watcher->future().resultCount() > 0;
        finish(given ? watcher->result() : QString(), "none-given");
    });
    connect(late, &QTimer::timeout, watcher, [finish] { finish(QString(), "late"); });
    watcher->setFuture(tokenSource(window));
    late->start(TOKEN_WAIT_MS);
}

bool ActivatedStart::start(const QStringList& command, const QString& token)
{
    if (command.isEmpty()) {
        return false;
    }
    QProcessEnvironment environment = startedWith();
    environment.remove(TOKEN);
    if (!token.isEmpty()) {
        environment.insert(TOKEN, token);
    }
    QProcess process;
    process.setProgram(command.first());
    process.setArguments(command.mid(1));
    process.setProcessEnvironment(environment);
    return process.startDetached();
}
