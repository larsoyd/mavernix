// how Almanac shows days and weeks, and what the clock and its icon do

#pragma once

#include <QWidget>

class Choices;

class CalendarPane : public QWidget
{
    Q_OBJECT

  public:
    CalendarPane(Choices& choices, QWidget* parent = nullptr);

  private:
    Choices& choices;
};
