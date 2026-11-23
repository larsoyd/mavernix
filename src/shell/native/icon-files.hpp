// Theme icons drawn with librsvg. Qt's SVG reader lacks masks, filters and
// clip paths, which many application icons use. Which files an icon has is
// asked of QIconLoader, a private Qt class, on the main thread only: it is
// not thread-safe, and Qt runs the image providers below on threads of its
// own for images loaded asynchronously.
//
// Qt reads the icon folders only once, so they are watched here: on a change
// Qt rereads them and `revision` counts up.
//
// Usage in QML:
//   IconFiles { id: iconFiles }
//   source: iconFiles.sourceFor(name, iconFiles.revision) || Quickshell.iconPath(name, "fallback-name")

#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QTimer>

#include <functional>

class IconFiles : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString today READ today NOTIFY todayChanged)
    Q_PROPERTY(bool datePagesShown READ datePagesShown WRITE setDatePagesShown NOTIFY datePagesShownChanged)

  public:
    explicit IconFiles(QObject* parent = nullptr);

    int revision() const;
    // the day it is as 2026-11-23, for pictures that show it
    QString today() const;
    // whether calendar pages get the day written on them, one switch for every lookup
    bool datePagesShown() const;
    void setDatePagesShown(bool shown);

    // An Image source for the icon, or "" if the theme has none. `name` may
    // be a file path. Pass `revision` so the binding updates with it.
    Q_INVOKABLE QString sourceFor(const QString& name, int revision);

    // Whether the icon theme in use has this icon itself, rather than a
    // theme it inherits (hicolor, where programs put their own).
    Q_INVOKABLE bool isThemeOwn(const QString& name, int revision);

    // a blank calendar page is shown with today's date on it
    Q_INVOKABLE bool isDatePage(const QString& name) const;

  signals:
    void revisionChanged();
    void todayChanged();
    void datePagesShownChanged();

  private:
    void watchFolders();
    void foldersChanged();

    // Cache for the icon theme named in `forTheme`.
    QHash<QString, QString> found;
    QString forTheme;
    QFileSystemWatcher folders;
    // Waits for an installation's many changes to settle.
    QTimer settle;
    int currentRevision = 0;
};

// "image://theme-icon/<revision>/<name>", for themes that also ship PNGs.
// Only for names IconFiles::sourceFor has looked up.
class ThemeIconProvider : public QQuickImageProvider
{
  public:
    ThemeIconProvider();
    QImage requestImage(const QString& address, QSize* size, const QSize& requested_size) override;

    // The theme's icon, or the file if `name` is a path.
    static QImage picture(const QString& name, const QSize& wanted);
};

// "image://fitted-icon/<revision>/<name>": a program's own icon, moved and
// scaled to the proportions of the theme's art
class FittedIconProvider : public QQuickImageProvider
{
  public:
    FittedIconProvider();
    QImage requestImage(const QString& address, QSize* size, const QSize& requested_size) override;

    // the fitted icon, with finish run on the art before it is placed
    static QImage picture(const QString& name, int side, const std::function<void(QImage&)>& finish = {});
};

// "image://date-page/<fitted or plain>/<2026-11-23>/<name>": a blank calendar
// page with that day written on it
class DatePageProvider : public QQuickImageProvider
{
  public:
    DatePageProvider();
    QImage requestImage(const QString& address, QSize* size, const QSize& requested_size) override;
};

// "image://svg-icon/<path>": an SVG file drawn with librsvg.
class SvgIconProvider : public QQuickImageProvider
{
  public:
    SvgIconProvider();
    QImage requestImage(const QString& path, QSize* size, const QSize& requested_size) override;
};
