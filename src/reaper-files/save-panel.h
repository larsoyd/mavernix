#pragma once

#include <KFileFilter>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QStringList>

class KFileCustomDialog;
class QWidget;

// a desktop save dialog with reaper's controls below the list
class SavePanel : public QObject
{
    Q_OBJECT

public:
    explicit SavePanel(QObject* parent = nullptr);
    ~SavePanel() override;

    // false when the line breaks the protocol, then the panel is of no use
    bool feed(const QString& line);
    KFileCustomDialog* dialog() const { return window; }
    QWidget* options() const { return panel; }

    static QString escaped(const QString& raw);
    static QStringList fields(const QString& line);

Q_SIGNALS:
    void send(const QString& line);

private:
    struct Control
    {
        QString kind;
        QRect rect;
        bool enabled = true;
        int value = 0;
        QString text;
        QStringList items;
        QPointer<QWidget> widget;
    };

    void show();
    QWidget* build(const QString& id, Control& control);
    void apply(Control& control);
    void finished(int result);

    QString title;
    QString folder;
    QString name;
    QList<KFileFilter> filters;
    int current = 0;
    QList<QString> order;
    QHash<QString, Control> controls;
    KFileCustomDialog* window = nullptr;
    QWidget* panel = nullptr;
    bool answered = false;
};
