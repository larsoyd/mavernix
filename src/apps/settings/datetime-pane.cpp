#include "datetime-pane.hpp"

#include <QCheckBox>
#include <QComboBox>
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
    auto* day_hours = new QCheckBox(word("menu_bar_24_hours", "Use a 24-hour clock"));
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
    auto* seconds = new QCheckBox(word("menu_bar_seconds", "Display the time with seconds"));
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

    auto* position = new QComboBox;
    position->addItem(word("datetime_position_left", "Left"), QStringLiteral("left"));
    position->addItem(word("datetime_position_middle", "Center"), QStringLiteral("middle"));
    position->addItem(word("datetime_position_right", "Right"), QStringLiteral("right"));
    const int placed = position->findData(panel_value("clock_position").toString());
    position->setCurrentIndex(placed >= 0 ? placed : position->findData(QStringLiteral("right")));
    connect(position, &QComboBox::activated, this, [this, position](int index) {
        set_panel("clock_position", position->itemData(index));
    });

    auto* show_time = check(word("datetime_show_time", "Show the time"), "clock_shows_time", true);
    auto* region_time = check(word("datetime_region_time", "Use the region's time format"), "clock_region_time");

    // a day of the week leads the format, and a date too when one is shown
    const QRegularExpression weekday_part(QStringLiteral("^dddd?[ ,]*"));
    auto* weekday = new QCheckBox(word("datetime_weekday", "Show the day of the week"));
    weekday->setChecked(format.contains(weekday_part));
    connect(weekday, &QCheckBox::toggled, this, [this, weekday_part](bool on) {
        QString changed = this->choices.value(QStringLiteral("panel"), QStringLiteral("clock_format")).toString();
        changed.remove(weekday_part);
        this->choices.set(QStringLiteral("panel"), QStringLiteral("clock_format"),
            on ? QStringLiteral("ddd ") + changed : changed);
    });

    auto* show_date = check(word("datetime_show_date", "Show date"), "clock_shows_date");
    auto* region_date = check(word("datetime_region_date", "Use the region's date format"), "clock_region_date");
    auto* date_style = new QComboBox;
    date_style->addItem(word("datetime_style_long", "Long"), QStringLiteral("long"));
    date_style->addItem(word("datetime_style_medium", "Medium"), QStringLiteral("medium"));
    date_style->addItem(word("datetime_style_short", "Short"), QStringLiteral("short"));
    date_style->setCurrentIndex(qMax(0, date_style->findData(panel_value("clock_date_style").toString())));
    connect(date_style, &QComboBox::activated, this, [this, date_style](int index) {
        set_panel("clock_date_style", date_style->itemData(index));
    });

    // one of time and date always shows, the region's format decides the hours itself
    const auto follow = [=]() {
        show_time->setEnabled(show_date->isChecked());
        show_date->setEnabled(show_time->isChecked());
        seconds->setEnabled(show_time->isChecked());
        region_time->setEnabled(show_time->isChecked());
        day_hours->setEnabled(show_time->isChecked() && !region_time->isChecked());
        region_date->setEnabled(show_date->isChecked() || weekday->isChecked());
        date_style->setEnabled(show_date->isChecked());
    };
    for (QCheckBox* box : {show_time, show_date, region_time, weekday})
    {
        connect(box, &QCheckBox::toggled, this, follow);
    }
    follow();

    form->addRow(word("datetime_position", "Position in menu bar:"), position);
    form->addRow(word("datetime_time_options", "Time options:"), column({show_time, seconds, day_hours, region_time}));
    form->addRow(word("datetime_date_options", "Date options:"), column({weekday, show_date, region_date}));
    form->addRow(word("datetime_date_style", "Date style:"), date_style);
}

QVariant DateTimePane::panel_value(const char* key) const
{
    return choices.value(QStringLiteral("panel"), QLatin1String(key));
}

void DateTimePane::set_panel(const char* key, const QVariant& value)
{
    choices.set(QStringLiteral("panel"), QLatin1String(key), value);
}

QCheckBox* DateTimePane::check(const QString& words, const char* key, bool unset)
{
    auto* box = new QCheckBox(words);
    const QVariant stored = panel_value(key);
    box->setChecked(stored.isValid() ? stored.toBool() : unset);
    connect(box, &QCheckBox::toggled, this, [this, key](bool on) { set_panel(key, on); });
    return box;
}

QWidget* DateTimePane::column(std::initializer_list<QCheckBox*> boxes)
{
    auto* options = new QWidget;
    auto* rows = new QVBoxLayout(options);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    for (QCheckBox* box : boxes)
    {
        keep_to_pitch(box);
        rows->addWidget(box);
    }
    return options;
}
