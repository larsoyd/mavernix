// a tick box for one menu bar item, shown in the pane about that thing

#pragma once

#include <QString>
#include <QStringList>

class Choices;
class QCheckBox;

// null when the desktop's menu bar has no such item
QCheckBox* menu_bar_box(Choices& choices, const QString& item, const QString& words);

// the items with a pane of their own to live in are left out
QStringList items_without_a_pane(Choices& choices);
