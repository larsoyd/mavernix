// previews from the freedesktop thumbnail cache, made by mavernix-thumbnail when missing

#pragma once

#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QString>

// a file without one is marked in the fail folder and not asked about until it changes
QImage thumbnail_for(const QString& file, QString* error, int timeout_ms = 12000);

// image://thumbnail/ENCODED_PATH
class ThumbnailProvider : public QQuickAsyncImageProvider
{
  public:
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requested) override;
};
