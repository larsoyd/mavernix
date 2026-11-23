#pragma once

#include <QDate>
#include <QImage>
#include <QLocale>
#include <QString>

struct DatePageWords
{
    QString month;
    QString day;
};

// whether an icon, by name or by file, is a blank calendar page that wants a date
bool is_date_page(const QString& icon);

// what a calendar page says for a date, in the given language
DatePageWords date_page_words(const QDate& date, const QLocale& words);

// writes a month and a day on the picture of a tear-off calendar, leaning as its page does
void draw_date_page(QImage& page, const QString& month, const QString& day);
