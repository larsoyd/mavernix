// The desktop's settings window.
//
//   mavernix-settings [PANE]
//   mavernix-settings --check-fit   exits 1 if a pane is wider than the window
//
// Shows the overview of all panes, or the pane named ("dock", "desktop").
// The session's settings are found through DESKTOP_SHELL_SETTINGS.

#include <QApplication>
#include <QTimer>
#include <QFont>

#include <cstdio>

#include "choices.hpp"
#include "network-pane.hpp"
#include "settings-window.hpp"

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    // The dock and the panel recognise the windows by their desktop entry.
    QApplication::setDesktopFileName(QStringLiteral("mavernix-settings"));

    Choices choices;
    // The theme's interface face, as in the shell; else the toolkit's.
    const QVariantMap face = choices.themeTable(QStringLiteral("fonts")).value(QStringLiteral("interface")).toMap();
    if (face.contains(QStringLiteral("family")))
    {
        QFont font(face.value(QStringLiteral("family")).toString());
        font.setPixelSize(face.value(QStringLiteral("size"), 13).toInt());
        QApplication::setFont(font);
    }
    SettingsWindow window(choices);
    const QStringList arguments = application.arguments();
    if (arguments.contains(QStringLiteral("--check-fit")))
    {
        window.show();
        const QStringList too_wide = window.tooWide();
        const QStringList issues = window.layoutIssues();
        for (const QString& line : too_wide)
        {
            std::fprintf(stderr, "[settings] too_wide %s\n", qPrintable(line));
        }
        for (const QString& line : issues)
        {
            std::fprintf(stderr, "[settings] layout_issue %s\n", qPrintable(line));
        }
        std::fprintf(stderr, "[settings] check_fit panes_too_wide=%lld layout_issues=%lld\n",
            static_cast<long long>(too_wide.size()), static_cast<long long>(issues.size()));
        return too_wide.isEmpty() && issues.isEmpty() ? 0 : 1;
    }
    if (arguments.size() > 1)
    {
        window.showPane(arguments[1]);
    }
    window.show();
    // "network join-hidden": straight to joining a hidden network.
    if (arguments.size() > 2 && arguments[2] == QLatin1String("join-hidden"))
    {
        if (auto* network = window.findChild<NetworkPane*>())
        {
            QTimer::singleShot(0, network, &NetworkPane::joinHidden);
        }
    }

    return application.exec();
}
