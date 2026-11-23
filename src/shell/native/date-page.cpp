#include "date-page.hpp"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace
{
// all in shares of the picture's side, measured on the page art this is drawn for
const double lean_degrees = -10.0;
// where the month's baseline starts, its letter size and the most room it may take
const QPointF month_start(0.159, 0.393);
const double month_size = 0.123;
const double month_room = 0.245;
// the middle of the day's digits and their size
const QPointF day_middle(0.514, 0.552);
const double day_size = 0.409;
// outlines are laid out this large and scaled down, so every size gets the same shapes
const int layout_size = 1000;

QPainterPath outline(const QString& text, QFont::Weight weight)
{
    QFont font(QStringLiteral("Helvetica"));
    font.setPixelSize(layout_size);
    font.setWeight(weight);
    font.setHintingPreference(QFont::PreferNoHinting);
    QPainterPath path;
    path.addText(0, 0, font, text);
    return path;
}
}

void draw_date_page(QImage& page, const QString& month, const QString& day)
{
    if (page.isNull())
    {
        return;
    }
    const double side = page.width();
    QPainter painter(&page);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    if (!month.isEmpty())
    {
        const QPainterPath letters = outline(month, QFont::Light);
        const QRectF box = letters.boundingRect();
        const double scale = qMin(month_size * side / layout_size, month_room * side / box.width());
        painter.save();
        painter.translate(month_start * side);
        painter.rotate(lean_degrees);
        painter.scale(scale, scale);
        painter.translate(-box.left(), 0);
        painter.fillPath(letters, QColor(255, 255, 255, 242));
        painter.restore();
    }

    if (!day.isEmpty())
    {
        const QPainterPath digits = outline(day, QFont::Bold);
        const QRectF box = digits.boundingRect();
        const double scale = day_size * side / layout_size;
        QLinearGradient shade(0, box.top(), 0, box.bottom());
        shade.setColorAt(0, QColor(0x5a, 0x5a, 0x5a));
        shade.setColorAt(1, QColor(0x2e, 0x2e, 0x2e));
        painter.translate(day_middle * side);
        painter.rotate(lean_degrees);
        painter.scale(scale, scale);
        painter.translate(-box.center());
        painter.fillPath(digits, shade);
    }
}
