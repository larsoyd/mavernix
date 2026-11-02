#include "icon-fit.hpp"

#include <QRect>

#include <cmath>

// measured on the Dock icons of the installer, as shares of the square
static const double longest_share = 118.0 / 128;
static const double middle_height = 63.0 / 128;
static const double most_cover = 0.66;

// a speck of art would otherwise ask for a picture of gigabytes, real icons need under 4
static const double most_growth = 4;
static const double most_drawn_side = 4096;

// anything fainter is a shadow or a glow, not part of the shape
static const int art_alpha = 128;

IconFit fit_icon(const QImage& drawn, int side)
{
    const QImage image = drawn.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QRect art;
    qint64 covered = 0;
    for (int y = 0; y < image.height(); y++)
    {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x++)
        {
            if (qAlpha(row[x]) >= art_alpha)
            {
                art |= QRect(x, y, 1, 1);
                covered++;
            }
        }
    }
    if (covered == 0)
    {
        return {};
    }

    double scale = longest_share * side / qMax(art.width(), art.height());
    const double cover = covered * scale * scale / (double(side) * side);
    if (cover > most_cover)
    {
        scale *= std::sqrt(most_cover / cover);
    }
    scale = qMin(scale, qMin(most_growth, most_drawn_side / side));
    const double middle_x = art.x() + art.width() / 2.0;
    const double middle_y = art.y() + art.height() / 2.0;
    return {scale, QPointF(side / 2.0 - middle_x * scale, side * middle_height - middle_y * scale)};
}
