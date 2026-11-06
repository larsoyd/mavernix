#include "save-panel.h"

#include <QApplication>
#include <QSocketNotifier>

#include <cstdio>
#include <unistd.h>

// reads the plugin's lines on stdin and answers on stdout, and leaves when the plugin closes its end
int main(int argc, char** argv)
{
    // the look of the desktop's own file dialogs, whatever reaper was started with
    qputenv("QT_QPA_PLATFORM", "wayland");
    qputenv("QT_QPA_PLATFORMTHEME", "kde");
    qunsetenv("QT_STYLE_OVERRIDE");
    QApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("cockos-reaper"));
    QApplication::setQuitOnLastWindowClosed(false);

    SavePanel panel;
    QObject::connect(&panel, &SavePanel::send, [](const QString& line) {
        const QByteArray out = line.toUtf8() + '\n';
        if (write(STDOUT_FILENO, out.constData(), size_t(out.size())) != out.size())
            QCoreApplication::exit(3);
    });
    QByteArray buffer;
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, [&] {
        char chunk[4096];
        const ssize_t got = read(STDIN_FILENO, chunk, sizeof(chunk));
        if (got <= 0)
        {
            input.setEnabled(false);
            QCoreApplication::exit(0);
            return;
        }
        buffer.append(chunk, got);
        for (qsizetype end; (end = buffer.indexOf('\n')) >= 0;)
        {
            const QString line = QString::fromUtf8(buffer.left(end));
            buffer.remove(0, end + 1);
            if (!panel.feed(line))
            {
                fprintf(stderr, "[save-panel] refused: line=%s\n", qPrintable(SavePanel::escaped(line)));
                input.setEnabled(false);
                QCoreApplication::exit(2);
                return;
            }
        }
    });
    return app.exec();
}
