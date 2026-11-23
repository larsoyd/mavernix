#include "thumbnails.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>

static QString cache_folder(const char* kind)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/thumbnails/")
        + QLatin1String(kind);
}

// a thumbnail counts only for the version of the file it was made from
static QImage current(const QString& path, qint64 mtime)
{
    const QImage picture(path);
    return picture.text(QStringLiteral("Thumb::MTime")) == QString::number(mtime) ? picture : QImage();
}

static void mark_failed(const QString& mark, const QUrl& url, qint64 mtime)
{
    QDir().mkpath(QFileInfo(mark).path());
    QImage nothing(1, 1, QImage::Format_ARGB32);
    nothing.fill(Qt::transparent);
    nothing.setText(QStringLiteral("Thumb::URI"), QString::fromUtf8(url.toEncoded()));
    nothing.setText(QStringLiteral("Thumb::MTime"), QString::number(mtime));
    nothing.save(mark, "PNG");
}

QImage thumbnail_for(const QString& file, QString* error, int timeout_ms)
{
    const QFileInfo info(file);
    const QUrl url = QUrl::fromLocalFile(info.absoluteFilePath());
    const qint64 mtime = info.lastModified().toSecsSinceEpoch();
    const QString name =
        QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Md5).toHex()) + ".png";
    const QString target = cache_folder("large/") + name;
    const QString mark = cache_folder("fail/mavernix/") + name;

    if (QImage found = current(target, mtime); !found.isNull())
    {
        return found;
    }
    if (!current(mark, mtime).isNull())
    {
        *error = QStringLiteral("no preview before");
        return {};
    }
    QDir().mkpath(cache_folder("large"));
    QFile::setPermissions(cache_folder(""), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QProcess helper;
    helper.setStandardInputFile(QProcess::nullDevice());
    helper.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    helper.start(QStringLiteral("mavernix-thumbnail"), {info.absoluteFilePath(), target});
    // a helper still running is killed with its QProcess
    if (!helper.waitForStarted(timeout_ms))
    {
        *error = QStringLiteral("cannot start mavernix-thumbnail: %1").arg(helper.errorString());
    }
    else if (!helper.waitForFinished(timeout_ms))
    {
        *error = QStringLiteral("timeout after %1 ms").arg(timeout_ms);
    }
    else if (QImage made = current(target, mtime); !made.isNull())
    {
        return made;
    }
    else
    {
        *error = QStringLiteral("none made, exit %1").arg(helper.exitCode());
    }
    qWarning("[thumbnail] file=%s refused=%s", qPrintable(file), qPrintable(*error));
    mark_failed(mark, url, mtime);
    return {};
}

namespace
{
class ThumbnailResponse : public QQuickImageResponse, public QRunnable
{
  public:
    ThumbnailResponse(const QString& file, const QSize& requested) : file(file), requested(requested)
    {
        setAutoDelete(false);
    }

    void run() override
    {
        picture = thumbnail_for(file, &failure);
        if (!picture.isNull() && requested.isValid())
        {
            picture = picture.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        emit finished();
    }

    QQuickTextureFactory* textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(picture);
    }

    QString errorString() const override { return picture.isNull() ? failure : QString(); }

  private:
    QString file;
    QSize requested;
    QImage picture;
    QString failure;
};
}

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& id, const QSize& requested)
{
    auto* response = new ThumbnailResponse(QUrl::fromPercentEncoding(id.toUtf8()), requested);
    QThreadPool::globalInstance()->start(response);
    return response;
}
