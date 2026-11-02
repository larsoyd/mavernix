#include "datetime-pane.hpp"

#include <QCheckBox>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "choices.hpp"
#include "menu-bar-box.hpp"
#include "pane-parts.hpp"

DateTimePane::DateTimePane(Choices& choices, QWidget* parent) : QWidget(parent), choices(choices)
{
    const QVariantMap words = choices.configTable(QStringLiteral("settings")).value(QStringLiteral("words")).toMap();
    const auto word = [&words](const char* key, const char* fallback) {
        return words.value(QLatin1String(key), QString::fromUtf8(fallback)).toString();
    };

    auto* form = new PaneForm(this);
    if (QCheckBox* clock = menu_bar_box(choices, QStringLiteral("clock"),
            word("datetime_in_menu_bar", "Show date and time in menu bar")))
    {
        form->addCheck(clock);
        form->addDivider();
    }

    // hours as 13:00 or 1:00 PM, the rest of the theme's format stays
    const QString format = choices.value(QStringLiteral("panel"), QStringLiteral("clock_format")).toString();
    auto* day_hours = new QCheckBox(word("menu_bar_24_hours", "Show the time with 24 hours"));
    day_hours->setChecked(!format.contains(QLatin1String("AP"), Qt::CaseInsensitive));
    connect(day_hours, &QCheckBox::toggled, this, [this](bool on) {
        QString changed = this->choices.value(QStringLiteral("panel"), QStringLiteral("clock_format")).toString();
        if (on)
        {
            changed.replace(QRegularExpression(QStringLiteral(" ?[Aa][Pp]")), QString());
            changed.replace(QRegularExpression(QStringLiteral("\\bh{1,2}\\b")), QStringLiteral("HH"));
        }
        else
        {
            changed.replace(QRegularExpression(QStringLiteral("\\bH{1,2}\\b")), QStringLiteral("h"));
            changed += QStringLiteral(" AP");
        }
        this->choices.set(QStringLiteral("panel"), QStringLiteral("clock_format"), changed);
    });

    // seconds after the minutes, the clock then ticks every second
    const QRegularExpression seconds_part(QStringLiteral(":ss?\\b"));
    auto* seconds = new QCheckBox(word("menu_bar_seconds", "Show seconds"));
    seconds->setChecked(format.contains(seconds_part));
    connect(seconds, &QCheckBox::toggled, this, [this, seconds_part](bool on) {
        QString changed = this->choices.value(QStringLiteral("panel"), QStringLiteral("clock_format")).toString();
        changed.remove(seconds_part);
        if (on)
        {
            changed.replace(QRegularExpression(QStringLiteral("\\bmm\\b")), QStringLiteral("mm:ss"));
        }
        this->choices.set(QStringLiteral("panel"), QStringLiteral("clock_format"), changed);
    });

    keep_to_pitch(seconds);
    keep_to_pitch(day_hours);
    auto* options = new QWidget;
    auto* option_rows = new QVBoxLayout(options);
    option_rows->setContentsMargins(0, 0, 0, 0);
    option_rows->setSpacing(0);
    option_rows->addWidget(seconds);
    option_rows->addWidget(day_hours);
    form->addRow(word("datetime_time_options", "Time options:"), options);
}
