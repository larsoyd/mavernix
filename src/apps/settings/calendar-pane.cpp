#include "calendar-pane.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTime>

#include "choices.hpp"
#include "pane-parts.hpp"

namespace
{
const QString table = QStringLiteral("calendar");

// almanac's calendars as id and name, read from its files
QList<std::pair<QString, QString>> almanac_calendars()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/almanac/calendars");
    QList<std::pair<QString, QString>> found;
    for (const QString& name : QDir(folder).entryList({QStringLiteral("*.ics")}, QDir::Files, QDir::Name))
    {
        QFile file(folder + QLatin1Char('/') + name);
        QString title = name.chopped(4);
        if (file.open(QIODevice::ReadOnly))
        {
            const QRegularExpressionMatch named = QRegularExpression(QStringLiteral("^X-WR-CALNAME:(.*?)\\r?$"), QRegularExpression::MultilineOption)
                                                      .match(QString::fromUtf8(file.read(64 * 1024)));
            title = named.hasMatch() ? named.captured(1) : title;
        }
        found.append({name.chopped(4), title});
    }
    return found;
}

// an hour as the region writes it, midnight and noon by name as the original's lists do
QString hour_name(int minute)
{
    if (minute == 0 || minute == 1440)
    {
        return QStringLiteral("Midnight");
    }
    if (minute == 720)
    {
        return QStringLiteral("Noon");
    }
    return QLocale::system().toString(QTime(minute / 60, 0), QLocale::ShortFormat);
}
}

CalendarPane::CalendarPane(Choices& choices, QWidget* parent) : QWidget(parent), choices(choices)
{
    const QVariantMap words = choices.configTable(QStringLiteral("settings")).value(QStringLiteral("words")).toMap();
    const auto word = [&words](const char* key, const char* fallback) {
        return words.value(QLatin1String(key), QString::fromUtf8(fallback)).toString();
    };
    const auto stored = [this](const char* key, const QVariant& fallback) {
        const QVariant value = this->choices.value(table, QLatin1String(key));
        return value.isValid() && !value.isNull() ? value : fallback;
    };
    // a list whose items carry the stored values, saved as one is picked
    const auto list = [this, stored](const char* key, const QList<std::pair<QString, QVariant>>& items, const QVariant& fallback) {
        auto* popup = new QComboBox;
        for (const auto& [label, value] : items)
        {
            popup->addItem(label, value);
        }
        popup->setCurrentIndex(qMax(0, popup->findData(stored(key, fallback))));
        connect(popup, &QComboBox::activated, this, [this, popup, key](int index) {
            this->choices.set(table, QLatin1String(key), popup->itemData(index));
        });
        return popup;
    };
    const auto tick = [this, stored](const QString& words, const char* key, bool fallback) {
        auto* box = new QCheckBox(words);
        box->setChecked(stored(key, fallback).toBool());
        keep_to_pitch(box);
        connect(box, &QCheckBox::toggled, this, [this, key](bool on) { this->choices.set(table, QLatin1String(key), on); });
        return box;
    };

    auto* form = new PaneForm(this);

    form->addRow(word("calendar_days_per_week", "Days per week:"), list("days_per_week", {{QStringLiteral("5"), 5}, {QStringLiteral("7"), 7}}, 7));

    // the region's first day until one is picked, choosing the region again forgets the pick
    auto* first = new QComboBox;
    first->addItem(word("calendar_region_weekday", "As in the region"), QStringLiteral("region"));
    for (int day = 1; day <= 7; ++day)
    {
        first->addItem(QLocale::system().standaloneDayName(day, QLocale::LongFormat), day);
    }
    first->setCurrentIndex(qMax(0, first->findData(stored("first_weekday", QStringLiteral("region")))));
    connect(first, &QComboBox::activated, this, [this, first](int index) {
        if (first->itemData(index).toString() == QLatin1String("region"))
        {
            this->choices.remove({{table, QStringLiteral("first_weekday")}});
        }
        else
        {
            this->choices.set(table, QStringLiteral("first_weekday"), first->itemData(index));
        }
    });
    form->addRow(word("calendar_first_weekday", "Start week on:"), first);

    QList<std::pair<QString, QVariant>> mornings;
    QList<std::pair<QString, QVariant>> evenings;
    for (int hour = 0; hour <= 12; ++hour)
    {
        mornings.append({hour_name(hour * 60), hour * 60});
        evenings.append({hour_name((hour + 12) * 60), (hour + 12) * 60});
    }
    form->addRow(word("calendar_day_starts", "Day starts at:"), list("day_start_minute", mornings, 480));
    form->addRow(word("calendar_day_ends", "Day ends at:"), list("day_end_minute", evenings, 1080));

    QList<std::pair<QString, QVariant>> hours;
    for (int count = 6; count <= 24; ++count)
    {
        hours.append({word("calendar_hours", "%1 hours at a time").arg(count), count});
    }
    form->addRow(word("calendar_show", "Show:"), list("hours_shown", hours, 12));

    form->addCheck(tick(word("calendar_holidays", "Show Holidays calendar"), "holidays", true));
    form->addCheck(tick(word("calendar_year_events", "Show events in year view"), "year_heat_map", false));
    form->addDivider();

    QList<std::pair<QString, QVariant>> calendars = {{word("calendar_selected", "Selected calendar"), QString()}};
    for (const auto& [id, name] : almanac_calendars())
    {
        calendars.append({name, id});
    }
    form->addRow(word("calendar_default", "Default calendar:"), list("default_calendar", calendars, QString()));

    form->addRow(word("calendar_clock_click", "Clicking the clock shows:"),
        list("clock_click", {{word("calendar_clock_calendar", "A calendar"), QStringLiteral("calendar")},
                             {word("calendar_clock_app", "Almanac"), QStringLiteral("app")},
                             {word("calendar_clock_date", "The date"), QStringLiteral("date")}}, QStringLiteral("calendar")));
    form->addCheck(tick(word("calendar_icon_date", "Show today’s date on the Almanac icon"), "icon_date", true));
}
