#pragma once

#include <QImage>
#include <QPointF>

struct IconFit
{
    // 0 when nothing in the picture is solid enough to measure
    double scale = 0;
    QPointF corner;
};

// where a program's own icon goes on its square to match the theme's art
IconFit fit_icon(const QImage& drawn, int side);
