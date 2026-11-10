#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <functional>

// every emoji in rows for a grid, with search, recently used ones and skin tones
class EmojiSet : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString dataFile READ dataFile WRITE setDataFile NOTIFY dataFileChanged)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY columnsChanged)
    Q_PROPERTY(int keepRecent READ keepRecent WRITE setKeepRecent NOTIFY keepRecentChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY shownChanged)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY shownChanged)
    Q_PROPERTY(int count READ count NOTIFY shownChanged)

  public:
    explicit EmojiSet(QObject* parent = nullptr);

    QString dataFile() const { return listFile; }
    void setDataFile(const QString& path);
    QString folder() const { return storage; }
    void setFolder(const QString& path);
    int columns() const { return across; }
    void setColumns(int count);
    int keepRecent() const { return recentLimit; }
    void setKeepRecent(int count);
    QString filter() const { return search; }
    void setFilter(const QString& text);

    QVariantList rows() const;
    QVariantList sections() const;
    int count() const;

    Q_INVOKABLE QString textAt(int index) const;
    Q_INVOKABLE QString nameAt(int index) const;
    Q_INVOKABLE QStringList tonesAt(int index) const;
    Q_INVOKABLE int rowOf(int index) const;
    Q_INVOKABLE int moved(int index, int right, int down) const;
    Q_INVOKABLE void used(const QString& text);
    Q_INVOKABLE void clearRecent();

    static bool fontDraws(const QString& text);
    // null draws everything
    std::function<bool(const QString&)> drawable = fontDraws;

  signals:
    void dataFileChanged();
    void folderChanged();
    void columnsChanged();
    void keepRecentChanged();
    void filterChanged();
    void shownChanged();

  private:
    struct Emoji
    {
        QString text;
        QString name;
        QStringList nameWords;
        QStringList searchWords;
        QStringList tones;
    };
    struct Group
    {
        QString id;
        QList<int> members;
    };
    struct Row
    {
        QString group;
        int first = 0;
        int length = 0;
    };

    void load() const;
    void loadList() const;
    void loadRecent() const;
    void saveRecent() const;
    void lay() const;
    void changed();
    int baseOf(const QString& text) const;
    QString shownText(int emoji) const;
    int matchRank(const Emoji& emoji, const QStringList& words) const;

    QString listFile;
    QString storage;
    QString search;
    int across = 8;
    int recentLimit = 24;

    mutable bool loaded = false;
    mutable bool laid = false;
    mutable QList<Emoji> all;
    mutable QList<Group> groups;
    mutable QHash<QString, int> byText;
    mutable QStringList recent;
    mutable QHash<QString, QString> chosenTones;
    mutable QList<int> shown;
    mutable QList<Row> shownRows;
};
