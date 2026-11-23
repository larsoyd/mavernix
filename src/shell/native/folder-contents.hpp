// A folder's contents, kept current. Each entry is { name, path, icon,
// genericIcon, isFolder, isLauncher }; an executable .desktop file shows its
// program's name and icon. Hidden files are left out.

#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QVariantList>

class FolderContents : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    // "name", "kind" (A to Z), "added", "modified", "created", "last_opened" (newest first),
    // "size" (largest first) or "tags" (tagged first)
    Q_PROPERTY(QString order READ order WRITE setOrder NOTIFY orderChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    // while on, infos maps each path to what finder's item info shows for it, measured aside
    Q_PROPERTY(bool itemInfo READ itemInfo WRITE setItemInfo NOTIFY itemInfoChanged)
    Q_PROPERTY(QVariantMap infos READ infos NOTIFY infosChanged)

  public:
    explicit FolderContents(QObject* parent = nullptr);

    QString path() const;
    void setPath(const QString& path);
    QString order() const;
    void setOrder(const QString& order);
    QVariantList entries() const;
    bool itemInfo() const;
    void setItemInfo(bool itemInfo);
    QVariantMap infos() const;

    Q_INVOKABLE QStringList pathsInOrder(const QString& order) const;

  signals:
    void pathChanged();
    void orderChanged();
    void entriesChanged();
    void itemInfoChanged();
    void infosChanged();

  private:
    void look();
    void measure();

    QString folderPath;
    QString wantedOrder = QStringLiteral("name");
    QVariantList found;
    bool wantsInfo = false;
    QVariantMap measured;
    int measuring = 0;
    QFileSystemWatcher watcher;
};
