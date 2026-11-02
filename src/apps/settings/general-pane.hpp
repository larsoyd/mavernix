// how the desktop looks: the theme in use, whether GTK and Qt programs take
// it, and the menu bar items that belong to no other pane

#pragma once

#include <QWidget>

class Choices;

class GeneralPane : public QWidget
{
    Q_OBJECT

  public:
    GeneralPane(Choices& choices, QWidget* parent = nullptr);
};
