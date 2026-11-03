// the clipboard history: how much it keeps, for how long, and where it shows

#pragma once

#include <QWidget>

class Choices;
class QCheckBox;

class ClipboardPane : public QWidget
{
    Q_OBJECT

  public:
    ClipboardPane(Choices& choices, QWidget* parent = nullptr);

  private:
    QVariant chosen(const QString& key) const;
    bool dockHolds() const;
    void showInDock(bool on);

    Choices& choices;
};
