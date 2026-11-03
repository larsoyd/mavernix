#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "choices.hpp"
#include "clipboard-pane.hpp"

class TestClipboardPane : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir folder;
    QCheckBox missing_box;
    QComboBox missing_popup;

    void start(QJsonObject clipboard = {}, QJsonArray groups = {})
    {
        QFile::remove(folder.filePath(QStringLiteral("choices.json")));
        QJsonObject config;
        config[QStringLiteral("status")] = QJsonObject{{QStringLiteral("clipboard"), clipboard}};
        config[QStringLiteral("panel")] =
            QJsonObject{{QStringLiteral("right"), QJsonArray{QStringLiteral("clipboard"), QStringLiteral("clock")}}};
        if (groups.isEmpty())
            groups = QJsonArray{QJsonArray{QStringLiteral("org.kde.dolphin"), QStringLiteral("firefox")}};
        config[QStringLiteral("dock")] = QJsonObject{{QStringLiteral("groups"), groups}};
        QJsonObject settings;
        settings[QStringLiteral("config")] = config;
        settings[QStringLiteral("user_choices_file")] = folder.filePath(QStringLiteral("choices.json"));
        const QString path = folder.filePath(QStringLiteral("settings.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(settings).toJson());
        file.close();
        qputenv("DESKTOP_SHELL_SETTINGS", path.toUtf8());
    }

    QCheckBox* box(QWidget& pane, const QString& words)
    {
        for (QCheckBox* found : pane.findChildren<QCheckBox*>())
            if (found->text() == words)
                return found;
        QTest::qFail(qPrintable(QStringLiteral("no tick box reads \"%1\"").arg(words)), __FILE__, __LINE__);
        return &missing_box;
    }

    QComboBox* popup(QWidget& pane, int value)
    {
        for (QComboBox* found : pane.findChildren<QComboBox*>())
            if (found->findData(value) >= 0)
                return found;
        QTest::qFail(qPrintable(QStringLiteral("no popup offers %1").arg(value)), __FILE__, __LINE__);
        return &missing_popup;
    }

    static void choose(QComboBox* list, int value)
    {
        list->setCurrentIndex(list->findData(value));
        emit list->activated(list->currentIndex());
    }

    static QVariant clipboard(const Choices& choices, const char* key)
    {
        return choices.value(QStringLiteral("clipboard"), QLatin1String(key));
    }

    static QStringList firstGroup(const Choices& choices)
    {
        QStringList names;
        const QVariantList groups = choices.value(QStringLiteral("dock"), QStringLiteral("groups")).toList();
        for (const QVariant& place : groups.value(0).toList())
            names << (place.typeId() == QMetaType::QString ? place.toString()
                                                          : QStringLiteral("clipboard:%1").arg(
                                                                place.toMap().value(QStringLiteral("clipboard")).toBool() ? "true" : "false"));
        return names;
    }

  private slots:
    void the_size_of_the_history_is_chosen()
    {
        start({{"keep_items", 100}});
        Choices choices;
        ClipboardPane pane(choices);
        QComboBox* size = popup(pane, 500);
        QCOMPARE(size->currentData().toInt(), 100);
        choose(size, 25);
        QCOMPARE(clipboard(choices, "keep_items").toInt(), 25);
    }

    void an_unlisted_size_is_offered_as_it_stands()
    {
        start({{"keep_items", 70}});
        Choices choices;
        ClipboardPane pane(choices);
        QCOMPARE(popup(pane, 500)->currentData().toInt(), 70);
    }

    void remembering_and_pictures_are_switched()
    {
        start({{"remember", true}, {"keep_images", true}, {"image_limit_mb", 20}});
        Choices choices;
        ClipboardPane pane(choices);
        QCheckBox* remember = box(pane, QStringLiteral("Keep the history after logging out"));
        QVERIFY(remember->isChecked());
        remember->setChecked(false);
        QCOMPARE(clipboard(choices, "remember"), QVariant(false));
        QCheckBox* pictures = box(pane, QStringLiteral("Keep copied pictures"));
        QComboBox* largest = popup(pane, 5);
        QCOMPARE(largest->currentData().toInt(), 20);
        QVERIFY(largest->isEnabled());
        pictures->setChecked(false);
        QCOMPARE(clipboard(choices, "keep_images"), QVariant(false));
        QVERIFY(!largest->isEnabled());
        choose(largest, 50);
        QCOMPARE(clipboard(choices, "image_limit_mb").toInt(), 50);
    }

    void the_menu_bar_item_is_shown_or_hidden()
    {
        start();
        Choices choices;
        ClipboardPane pane(choices);
        QCheckBox* shown = box(pane, QStringLiteral("Show clipboard history in menu bar"));
        QVERIFY(shown->isChecked());
        shown->setChecked(false);
        QCOMPARE(choices.value(QStringLiteral("panel"), QStringLiteral("hidden")).toStringList(),
                 QStringList({QStringLiteral("clipboard")}));
    }

    void the_dock_tile_goes_right_of_the_first_and_away_again()
    {
        start();
        Choices choices;
        ClipboardPane pane(choices);
        QCheckBox* inDock = box(pane, QStringLiteral("Show clipboard history in the Dock"));
        QVERIFY(!inDock->isChecked());
        inDock->setChecked(true);
        QCOMPARE(firstGroup(choices),
                 QStringList({QStringLiteral("org.kde.dolphin"), QStringLiteral("clipboard:true"), QStringLiteral("firefox")}));
        inDock->setChecked(false);
        QCOMPARE(firstGroup(choices), QStringList({QStringLiteral("org.kde.dolphin"), QStringLiteral("firefox")}));
    }

    void a_kept_dock_tile_shows_ticked()
    {
        start({}, QJsonArray{QJsonArray{QStringLiteral("firefox")},
                             QJsonArray{QJsonObject{{"clipboard", true}, {"name", "Clipboard"}}}});
        Choices choices;
        ClipboardPane pane(choices);
        QVERIFY(box(pane, QStringLiteral("Show clipboard history in the Dock"))->isChecked());
    }
};

QTEST_MAIN(TestClipboardPane)
#include "test-clipboard-pane.moc"
