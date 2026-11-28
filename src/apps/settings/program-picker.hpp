// a searchable picker of installed programs, and a list of picked ones kept in the user's choices

#pragma once

#include <QString>
#include <QWidget>

#include <functional>

class Choices;
class QListWidget;
class QPushButton;

// the desktop entry name of the program picked, empty when none was
QString choose_program(QWidget* parent, const QString& heading, const QString& search_word);

class ProgramList : public QWidget
{
    Q_OBJECT

  public:
    using picker_t = std::function<QString(QWidget*)>;

    // keeps desktop entry names under key in the choices table
    ProgramList(Choices& choices, const QString& table, const QString& key, picker_t pick, QWidget* parent = nullptr);

  private:
    void fill();
    QStringList kept() const;
    void keep(const QStringList& ids);

    Choices& choices;
    QString table;
    QString key;
    picker_t pick;
    QListWidget* list;
    QPushButton* remove;
};
