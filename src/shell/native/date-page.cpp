#include "date-page.hpp"

#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

namespace
{
// the page is laid out on a square of 128 with its origin at the bottom left, turned by its lean
const double page_units = 128.0;
const double lean_degrees = 10.0;

struct Face
{
    QString family;
    double day_size;
    double month_size;
    // how far under the top of its box each line's baseline sits
    double day_drop;
    double month_drop;
};

// the day is centred between its sides, the month starts at its left
const double day_left = 23;
const double day_right = 120;
const double day_top = 78;
const double month_left = 31;
const double month_top = 90;
const double day_tracking = -2.0;
const QColor day_ink(0, 0, 0, 204);
const QColor month_ink(255, 255, 255);

// outlines are laid out this large and scaled down, so every size gets the same shapes
const double layout_size = 1000.0;

Face face()
{
    if (QFontDatabase::hasFamily(QStringLiteral("Helvetica Neue")))
    {
        return {QStringLiteral("Helvetica Neue"), 54, 13.5, 54, 17};
    }
    return {QStringLiteral("Helvetica"), 54, 14, 53, 14};
}

struct Line
{
    QPainterPath shapes;
    double width;
};

// a line with no tracking is set without pair kerning either
Line line(const QString& text, const QString& family, double size, double tracking)
{
    QFont font(family);
    font.setPixelSize(qRound(layout_size));
    font.setWeight(QFont::Bold);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setKerning(tracking != 0);
    font.setLetterSpacing(QFont::AbsoluteSpacing, tracking * layout_size / size);
    Line made;
    made.shapes.addText(0, 0, font, text);
    made.width = QFontMetricsF(font).horizontalAdvance(text) * size / layout_size;
    return made;
}

void fill_line(QPainter& painter, const Line& made, double size, double left, double baseline, const QColor& ink)
{
    painter.save();
    painter.translate(left, baseline);
    painter.scale(size / layout_size, -size / layout_size);
    painter.fillPath(made.shapes, ink);
    painter.restore();
}
}

bool is_date_page(const QString& icon)
{
    static const QStringList pages = {QStringLiteral("almanac")};
    return pages.contains(QFileInfo(icon).completeBaseName());
}

DatePageWords date_page_words(const QDate& date, const QLocale& words)
{
    if (!date.isValid())
    {
        return {};
    }
    return {words.standaloneMonthName(date.month(), QLocale::ShortFormat).toUpper(), words.toString(date.day())};
}

void draw_date_page(QImage& page, const QString& month, const QString& day)
{
    if (page.isNull())
    {
        return;
    }
    const Face used = face();
    QPainter painter(&page);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.translate(0, page.height());
    painter.scale(page.width() / page_units, -page.height() / page_units);
    painter.rotate(lean_degrees);

    if (!month.isEmpty())
    {
        const Line made = line(month, used.family, used.month_size, 0);
        fill_line(painter, made, used.month_size, month_left, month_top - used.month_drop, month_ink);
    }
    if (!day.isEmpty())
    {
        // tracking counts after the last digit too, which sets the day a unit left of centre
        const Line made = line(day, used.family, used.day_size, day_tracking);
        const double left = (day_left + day_right - made.width) / 2;
        fill_line(painter, made, used.day_size, left, day_top - used.day_drop, day_ink);
    }
}
