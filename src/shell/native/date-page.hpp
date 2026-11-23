#pragma once

#include <QImage>
#include <QString>

// writes a month and a day on the picture of a tear-off calendar, leaning as its page does
void draw_date_page(QImage& page, const QString& month, const QString& day);
