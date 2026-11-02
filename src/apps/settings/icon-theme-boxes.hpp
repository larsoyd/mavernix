// the icon theme boxes, for programs and for the dock and menu bar

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class Choices;
class QComboBox;

// what the boxes say, from the config's words
struct IconThemeWords
{
    QString own;
    QString used_by_plasma;
    QString used_by_gnome;
};

// a box for each [icons] key, programs first, then the dock and menu bar
QList<QComboBox*> icon_theme_boxes(Choices& choices, const IconThemeWords& words);

// Rewrites everything that shows icons for the icon themes the user's file
// holds now, programs' and the Dock's and menu bar's, as a switch to the
// running desktop does. False outside a session: then they take effect at
// the next login.
bool apply_icon_theme(const Choices& choices);

// The icon themes the user picked as their file holds them now, programs'
// and the Dock's and menu bar's, each empty for the theme's own: to tell
// whether a reset changed them.
QStringList icon_theme_choices(const Choices& choices);
