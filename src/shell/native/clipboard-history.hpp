#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QPointer>
#include <QQmlParserStatus>
#include <QSortFilterProxyModel>
#include <QSize>
#include <QTimer>
#include <QUrl>

#include <functional>

class QMimeData;

class ClipboardHistory : public QAbstractListModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(int keepItems READ keepItems WRITE setKeepItems NOTIFY keepItemsChanged)
    Q_PROPERTY(bool remember READ remember WRITE setRemember NOTIFY rememberChanged)
    Q_PROPERTY(bool keepImages READ keepImages WRITE setKeepImages NOTIFY keepImagesChanged)
    Q_PROPERTY(int imageLimitMb READ imageLimitMb WRITE setImageLimitMb NOTIFY imageLimitMbChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool sensitive READ sensitive NOTIFY sensitiveChanged)
    Q_PROPERTY(int pinnedCount READ pinnedCount NOTIFY countsChanged)
    Q_PROPERTY(int total READ total NOTIFY countsChanged)

  public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        KindRole,
        LabelRole,
        PinnedRole,
        SectionRole,
        ThumbnailRole,
        PictureSizeRole,
        FileCountRole,
        PreviewRole,
        AddedRole,
        ExcerptRole,
    };
    Q_ENUM(Role)

    explicit ClipboardHistory(QObject* parent = nullptr);
    ~ClipboardHistory() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void classBegin() override {}
    void componentComplete() override;

    QString folder() const { return storage; }
    void setFolder(const QString& path);
    int keepItems() const { return limit; }
    void setKeepItems(int count);
    bool remember() const { return remembering; }
    void setRemember(bool on);
    bool keepImages() const { return imagesKept; }
    void setKeepImages(bool on);
    int imageLimitMb() const { return imageMb; }
    void setImageLimitMb(int megabytes);
    bool paused() const { return pausing; }
    void setPaused(bool on);
    bool sensitive() const { return secretShown; }
    int pinnedCount() const;
    int total() const { return int(entries.size()); }

    Q_INVOKABLE QString idAt(int row) const;
    Q_INVOKABLE QVariantList stack() const;
    bool rowMatches(int row, const QStringList& words) const;
    Q_INVOKABLE void choose(const QString& id, bool plain = false);
    Q_INVOKABLE void togglePin(const QString& id);
    Q_INVOKABLE void pin(const QString& id, bool on);
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void clearSensitive();

    void start();
    void take(const QMimeData* data, bool ours);
    void save();

    // a null offer clears the clipboard
    std::function<void(QMimeData*)> offer;

  signals:
    void folderChanged();
    void keepItemsChanged();
    void rememberChanged();
    void keepImagesChanged();
    void imageLimitMbChanged();
    void pausedChanged();
    void sensitiveChanged();
    void countsChanged();

  private:
    struct Format
    {
        QString mime;
        QString blob;
    };
    struct Entry
    {
        QString id;
        QString kind;
        QString label;
        QString text;
        QList<Format> formats;
        QDateTime added;
        bool pinned = false;
        QSize pictureSize;
        int fileCount = 0;
    };

    void listen();
    void load();
    void prune();
    void showRows();
    void changed();
    void setSecretShown(bool on);
    void forgetBlobs(const Entry& gone);
    int indexOf(const QString& id) const;
    QString blobPath(const QString& blob) const;
    QString thumbnailPath(const Entry& entry) const;
    QString wholeText(const Entry& entry) const;

    QList<Entry> entries;
    QStringList shown;
    QString storage;
    QString current;
    int limit = 100;
    int imageMb = 20;
    bool remembering = true;
    bool imagesKept = true;
    bool pausing = false;
    bool secretShown = false;
    bool started = false;
    QTimer saving;
};

class ClipboardSearch : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(ClipboardHistory* history READ history WRITE setHistory NOTIFY historyChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)

  public:
    explicit ClipboardSearch(QObject* parent = nullptr);

    ClipboardHistory* history() const { return source; }
    void setHistory(ClipboardHistory* history);
    QString filter() const { return search; }
    void setFilter(const QString& text);
    Q_INVOKABLE QString idAt(int row) const;

  signals:
    void historyChanged();
    void filterChanged();

  protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

  private:
    QPointer<ClipboardHistory> source;
    QString search;
    QStringList words;
};
