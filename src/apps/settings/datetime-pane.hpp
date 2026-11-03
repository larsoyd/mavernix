// how the menu bar's clock shows the time

#pragma once

#include <QVariant>
#include <QWidget>

#include <initializer_list>

class Choices;
class QCheckBox;

class DateTimePane : public QWidget
{
    Q_OBJECT

  public:
    DateTimePane(Choices& choices, QWidget* parent = nullptr);

  private:
    Choices& choices;

    QVariant panel_value(const char* key) const;
    void set_panel(const char* key, const QVariant& value);
    QCheckBox* check(const QString& words, const char* key, bool unset = false);
    static QWidget* column(std::initializer_list<QCheckBox*> boxes);
};
