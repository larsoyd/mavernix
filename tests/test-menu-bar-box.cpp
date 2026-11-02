#include <QtTest>

#include <QCheckBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "choices.hpp"
#include "menu-bar-box.hpp"

class TestMenuBarBox : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir folder;
    QString choices_file;

    void start(const QStringList& right, const QStringList& hidden)
    {
        choices_file = folder.filePath(QStringLiteral("choices.json"));
        QFile::remove(choices_file);
        QJsonObject panel;
        panel[QStringLiteral("right")] = QJsonArray::fromStringList(right);
        panel[QStringLiteral("hidden")] = QJsonArray::fromStringList(hidden);
        QJsonObject config;
        config[QStringLiteral("panel")] = panel;
        QJsonObject settings;
        settings[QStringLiteral("config")] = config;
        settings[QStringLiteral("user_choices_file")] = choices_file;
        const QString path = folder.filePath(QStringLiteral("settings.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(settings).toJson());
        file.close();
        qputenv("DESKTOP_SHELL_SETTINGS", path.toUtf8());
    }

    QStringList written_hidden()
    {
        QFile file(choices_file);
        if (!file.open(QIODevice::ReadOnly))
        {
            return {};
        }
        const QJsonObject held = QJsonDocument::fromJson(file.readAll()).object();
        QStringList names;
        for (const QJsonValue& name : held[QStringLiteral("panel")].toObject()[QStringLiteral("hidden")].toArray())
        {
            names << name.toString();
        }
        return names;
    }

  private slots:
    void anItemTheBarDoesNotHaveHasNoBox()
    {
        start({QStringLiteral("clock")}, {});
        Choices choices;
        QVERIFY(menu_bar_box(choices, QStringLiteral("volume"), QStringLiteral("Volume")) == nullptr);
    }

    void aHiddenItemStartsUnticked()
    {
        start({QStringLiteral("clock"), QStringLiteral("volume")}, {QStringLiteral("volume")});
        Choices choices;
        QScopedPointer<QCheckBox> shown(menu_bar_box(choices, QStringLiteral("clock"), QStringLiteral("Clock")));
        QScopedPointer<QCheckBox> gone(menu_bar_box(choices, QStringLiteral("volume"), QStringLiteral("Volume")));
        QVERIFY(shown != nullptr && gone != nullptr);
        QVERIFY(shown->isChecked());
        QVERIFY(!gone->isChecked());
        QCOMPARE(shown->text(), QStringLiteral("Clock"));
    }

    void twoBoxesKeepEachOthersChoice()
    {
        start({QStringLiteral("clock"), QStringLiteral("volume")}, {});
        Choices choices;
        QScopedPointer<QCheckBox> clock(menu_bar_box(choices, QStringLiteral("clock"), QStringLiteral("Clock")));
        QScopedPointer<QCheckBox> volume(menu_bar_box(choices, QStringLiteral("volume"), QStringLiteral("Volume")));
        QVERIFY(clock != nullptr && volume != nullptr);
        clock->setChecked(false);
        volume->setChecked(false);
        QCOMPARE(written_hidden(), QStringList({QStringLiteral("clock"), QStringLiteral("volume")}));
        clock->setChecked(true);
        QCOMPARE(written_hidden(), QStringList({QStringLiteral("volume")}));
    }

    void itemsWithAPaneAreNotListedForGeneral()
    {
        start({QStringLiteral("tray"), QStringLiteral("load"), QStringLiteral("bluetooth"), QStringLiteral("network"),
                  QStringLiteral("volume"), QStringLiteral("battery"), QStringLiteral("clock"),
                  QStringLiteral("search"), QStringLiteral("my-own")},
            {});
        Choices choices;
        QCOMPARE(items_without_a_pane(choices),
            QStringList({QStringLiteral("tray"), QStringLiteral("load"), QStringLiteral("search"),
                QStringLiteral("my-own")}));
    }
};

QTEST_MAIN(TestMenuBarBox)
#include "test-menu-bar-box.moc"
