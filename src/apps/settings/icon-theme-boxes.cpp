#include "icon-theme-boxes.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QProcess>
#include <QTimer>

#include <algorithm>

#include "choices.hpp"
#include "icon-theme-names.hpp"

static const QString icons_table = QStringLiteral("icons");
// [icons]: the icon theme for programs, and the one for the Dock, the menu
// bar and the rest of the shell. Either left out: the theme's own.
static const QStringList icon_theme_keys = {QStringLiteral("programs"), QStringLiteral("dock_and_menu_bar")};

// Switching to the running desktop rewrites the icon themes for GTK, Qt and
// KDE programs, tells services about them and restarts the Dock and menu bar
// (src/session/bin/mavernix-switch).
static const QString switch_command = QStringLiteral("mavernix-switch");

// How long an icon theme box must rest before its choice is applied.
static const int icon_theme_rest_ms = 700;

bool apply_icon_theme(const Choices& choices)
{
    return QProcess::startDetached(switch_command, {choices.runningDesktop()});
}

QStringList icon_theme_choices(const Choices& choices)
{
    QStringList picked;
    for (const QString& key : icon_theme_keys)
    {
        picked.append(choices.current(icons_table, key).toString());
    }
    return picked;
}

// One entry of an icon theme box: what it says, and the folder name saved
// (empty for the theme's own).
struct IconThemeItem
{
    QString text;
    QString folder;
};

// The theme's own first, then every installed icon theme. Those that Plasma
// or GNOME use say so, for people coming from there who know their theme by
// its look, not its name.
static QList<IconThemeItem> icon_theme_items(const Choices& choices, const IconThemeWords& words)
{
    const QString own = choices.themeIconTheme();
    QList<IconThemeItem> items = {{own.isEmpty() ? words.own : QStringLiteral("%1 (%2)").arg(words.own, own), {}}};
    const QString plasma = plasma_icon_theme();
    const QString gnome = gnome_icon_theme();
    for (const InstalledIconTheme& theme : installed_icon_themes())
    {
        // Plasma copies its icon theme into GNOME's settings for GTK
        // programs, so the same name in both is Plasma's.
        const QString mark = theme.folder == plasma  ? words.used_by_plasma
                             : theme.folder == gnome ? words.used_by_gnome
                                                     : QString();
        items.append({mark.isEmpty() ? theme.name : QStringLiteral("%1 — %2").arg(theme.name, mark), theme.folder});
    }
    return items;
}

// A box for one of the [icons] keys.
static QComboBox* icon_theme_box(Choices& choices, const QList<IconThemeItem>& items, const QString& key)
{
    auto* box = new QComboBox;
    for (const IconThemeItem& item : items)
    {
        box->addItem(item.text, item.folder);
    }
    // The line between the theme's own and the installed ones.
    box->insertSeparator(1);

    // A picked theme that is gone since is still shown, so the box does not
    // claim the theme's own; the session uses a stand-in for it.
    const QString picked = choices.current(icons_table, key).toString();
    if (!picked.isEmpty() && box->findData(picked) < 0)
    {
        box->addItem(picked, picked);
    }
    box->setCurrentIndex(std::max(0, box->findData(picked)));

    // Arrow keys and the mouse wheel step through a closed box, each step a
    // pick: the choice is written at once, but applied, which restarts the
    // Dock and menu bar, only once the box rests. One still waiting when the
    // window closes is applied then.
    auto* rest = new QTimer(box);
    rest->setSingleShot(true);
    rest->setInterval(icon_theme_rest_ms);
    QObject::connect(rest, &QTimer::timeout, box, [&choices]() { apply_icon_theme(choices); });
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, rest, [&choices, rest]() {
        if (rest->isActive())
        {
            rest->stop();
            apply_icon_theme(choices);
        }
    });

    QObject::connect(box, &QComboBox::activated, box, [&choices, box, key, rest](int index) {
        const QString folder = box->itemData(index).toString();
        if (folder == choices.current(icons_table, key).toString())
        {
            return;
        }
        if (folder.isEmpty())
        {
            choices.remove({{icons_table, key}});
        }
        else
        {
            choices.set(icons_table, key, folder);
        }
        rest->start();
    });
    return box;
}

QList<QComboBox*> icon_theme_boxes(Choices& choices, const IconThemeWords& words)
{
    const QList<IconThemeItem> items = icon_theme_items(choices, words);
    QList<QComboBox*> boxes;
    for (const QString& key : icon_theme_keys)
    {
        boxes.append(icon_theme_box(choices, items, key));
    }
    return boxes;
}
