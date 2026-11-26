#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include "choices.hpp"
#include "login-pane.hpp"
#include "pane-parts.hpp"
#include "pane-resets.hpp"

class TestLoginPane : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir folder;

    void start(const QJsonObject& greeter = {}, const QJsonObject& lock = {})
    {
        QFile::remove(folder.filePath(QStringLiteral("choices.json")));
        QJsonObject config;
        if (!greeter.isEmpty())
            config[QStringLiteral("greeter")] = greeter;
        if (!lock.isEmpty())
            config[QStringLiteral("lock")] = lock;
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

    QComboBox* comboWithData(QWidget& pane, const QVariant& needle)
    {
        for (QComboBox* box : pane.findChildren<QComboBox*>())
        {
            if (box->findData(needle) >= 0)
                return box;
        }
        return nullptr;
    }

    QCheckBox* checkWithText(QWidget& pane, const QString& text)
    {
        for (QCheckBox* box : pane.findChildren<QCheckBox*>())
        {
            if (box->text() == text)
                return box;
        }
        return nullptr;
    }

  private slots:
    void changingAppearanceAndHandoffWritesChoices()
    {
        start();
        Choices choices;
        LoginPane pane(choices);

        QComboBox* bg = comboWithData(pane, QStringLiteral("desktop_blurred"));
        QVERIFY(bg != nullptr);
        const int blurred = bg->findData(QStringLiteral("desktop_blurred"));
        bg->setCurrentIndex(blurred);
        emit bg->activated(blurred);
        QCOMPARE(choices.value(QStringLiteral("greeter"), QStringLiteral("background_mode")),
            QVariant(QStringLiteral("desktop_blurred")));

        QComboBox* handoff = comboWithData(pane, QStringLiteral("instant"));
        QVERIFY(handoff != nullptr);
        const int inst = handoff->findData(QStringLiteral("instant"));
        handoff->setCurrentIndex(inst);
        emit handoff->activated(inst);
        QCOMPARE(choices.value(QStringLiteral("greeter"), QStringLiteral("handoff_style")),
            QVariant(QStringLiteral("instant")));

        QComboBox* lockStyle = comboWithData(pane, QStringLiteral("card"));
        QVERIFY(lockStyle != nullptr);
        const int card = lockStyle->findData(QStringLiteral("card"));
        lockStyle->setCurrentIndex(card);
        emit lockStyle->activated(card);
        QCOMPARE(choices.value(QStringLiteral("lock"), QStringLiteral("style")), QVariant(QStringLiteral("card")));

        QCheckBox* sched = checkWithText(pane, QStringLiteral("Follow day/night theme schedule"));
        QVERIFY(sched != nullptr);
        sched->setChecked(false);
        QCOMPARE(choices.value(QStringLiteral("greeter"), QStringLiteral("follow_schedule")), QVariant(false));
    }

    void resettingThePaneCoversGreeterAndLockStyle()
    {
        QStringList reset;
        for (const Choices::Key& key : pane_choices(QStringLiteral("login")))
        {
            reset << key.table + QLatin1Char('.') + key.key;
        }
        for (const char* key : {"greeter.background_mode", "greeter.background_blur", "greeter.handoff_style",
                 "greeter.follow_schedule", "greeter.show_clock", "greeter.screen_off_after_seconds", "lock.style"})
        {
            QVERIFY2(reset.contains(QLatin1String(key)), qPrintable(reset.join(' ')));
        }
    }

    void labelsAlignWithControlsAndHaveNoBrokenMnemonics()
    {
        start();
        Choices choices;
        QWidget window;
        auto* rows = new QVBoxLayout(&window);
        auto* pane = new LoginPane(choices);
        rows->addWidget(pane, 0, Qt::AlignTop);
        rows->addStretch();
        window.resize(628, 700);
        window.show();
        QCoreApplication::processEvents();

        const QRegularExpression single_ampersand(QStringLiteral("(?<!&)&(?!&)"));
        for (const QComboBox* box : pane->findChildren<QComboBox*>())
        {
            for (int i = 0; i < box->count(); i++)
            {
                const QString text = box->itemText(i);
                QVERIFY2(!text.contains(single_ampersand), qPrintable(text));
                QVERIFY2(!text.contains(QLatin1String("  ")), qPrintable(text));
            }
        }

        auto* form = dynamic_cast<PaneForm*>(pane->layout());
        QVERIFY(form != nullptr);
        for (const auto& pair : form->labeledRows())
        {
            auto* label = qobject_cast<QLabel*>(pair.first);
            QWidget* control = pair.second;
            if (label == nullptr || control == nullptr || !label->isVisible() || !control->isVisible() ||
                label->text().isEmpty())
            {
                continue;
            }
            const int label_cy = label->mapTo(pane, label->contentsRect().center()).y();
            if (auto* hbox = qobject_cast<QHBoxLayout*>(control->layout()); hbox != nullptr)
            {
                for (int i = 0; i < hbox->count(); i++)
                {
                    if (QWidget* child = hbox->itemAt(i)->widget(); child != nullptr && child->isVisible())
                    {
                        const int child_cy = child->mapTo(pane, child->contentsRect().center()).y();
                        QVERIFY2(qAbs(child_cy - label_cy) <= 2,
                            qPrintable(QStringLiteral("%1 delta=%2").arg(label->text()).arg(child_cy - label_cy)));
                    }
                }
            }
            else
            {
                const int ctrl_cy = control->mapTo(pane, control->contentsRect().center()).y();
                QVERIFY2(qAbs(ctrl_cy - label_cy) <= 2,
                    qPrintable(QStringLiteral("%1 delta=%2").arg(label->text()).arg(ctrl_cy - label_cy)));
            }
        }
    }
};

QTEST_MAIN(TestLoginPane)
#include "test-login-pane.moc"
