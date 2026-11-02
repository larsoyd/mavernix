// how the menu bar's clock shows the time

#pragma once

#include <QWidget>

class Choices;

class DateTimePane : public QWidget
{
    Q_OBJECT

  public:
    DateTimePane(Choices& choices, QWidget* parent = nullptr);

  private:
    Choices& choices;
};
