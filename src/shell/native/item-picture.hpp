// a still picture of another item, as it looked when taken
// for a window's content item, which the script side cannot grab

#pragma once

#include <QImage>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickPaintedItem>
#include <QSharedPointer>

class ItemPicture : public QQuickPaintedItem
{
    Q_OBJECT

  public:
    explicit ItemPicture(QQuickItem* parent = nullptr);

    // false when the item cannot be drawn now, as in a hidden window
    Q_INVOKABLE bool take(QQuickItem* item, qreal pixelRatio);
    Q_INVOKABLE void clear();

    void paint(QPainter* painter) override;

  signals:
    void taken();

  private:
    QSharedPointer<QQuickItemGrabResult> pending;
    QImage image;
};
