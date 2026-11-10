#include "item-picture.hpp"

#include <QPainter>

#include <cmath>

ItemPicture::ItemPicture(QQuickItem* parent) : QQuickPaintedItem(parent) {}

bool ItemPicture::take(QQuickItem* item, qreal pixelRatio)
{
    if (!item || item->width() < 1 || item->height() < 1)
    {
        return false;
    }
    const QSize pixels(int(std::ceil(item->width() * pixelRatio)), int(std::ceil(item->height() * pixelRatio)));
    pending = item->grabToImage(pixels);
    if (!pending)
    {
        return false;
    }
    connect(pending.data(), &QQuickItemGrabResult::ready, this, [this, asked = pending.data()] {
        // an older request can still answer after a newer one or a clear
        if (pending.data() != asked)
        {
            return;
        }
        image = pending->image();
        pending.clear();
        update();
        emit taken();
    });
    return true;
}

void ItemPicture::clear()
{
    pending.clear();
    image = QImage();
    update();
}

void ItemPicture::paint(QPainter* painter)
{
    if (!image.isNull())
    {
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawImage(boundingRect(), image);
    }
}
