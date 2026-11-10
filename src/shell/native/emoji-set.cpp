#include "emoji-set.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QGlyphRun>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextLayout>

namespace
{
const auto privateFile = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser;
const auto privateDir = privateFile | QFile::ExeOwner | QFile::ExeUser;

QStringList wordsOf(const QString& text)
{
    static const QRegularExpression gaps(QStringLiteral("[\\s:,\\-()“”\"’'.!&]+"));
    return text.toLower().split(gaps, Qt::SkipEmptyParts);
}
} // namespace

EmojiSet::EmojiSet(QObject* parent) : QObject(parent)
{
    storage = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/mavernix/emoji");
}

// one glyph means the font has the whole sequence, more means it fell apart
bool EmojiSet::fontDraws(const QString& text)
{
    QTextLayout layout(text, QFont(QStringLiteral("emoji")));
    layout.beginLayout();
    layout.createLine();
    layout.endLayout();
    int glyphs = 0;
    for (const QGlyphRun& run : layout.glyphRuns())
    {
        for (quint32 glyph : run.glyphIndexes())
        {
            if (glyph == 0)
                return false;
            ++glyphs;
        }
    }
    return glyphs == 1;
}

void EmojiSet::setDataFile(const QString& path)
{
    if (path == listFile)
        return;
    listFile = path;
    loaded = false;
    emit dataFileChanged();
    changed();
}

void EmojiSet::setFolder(const QString& path)
{
    if (path == storage)
        return;
    storage = path;
    loaded = false;
    emit folderChanged();
    changed();
}

void EmojiSet::setColumns(int count)
{
    count = qMax(1, count);
    if (count == across)
        return;
    across = count;
    emit columnsChanged();
    changed();
}

void EmojiSet::setKeepRecent(int count)
{
    count = qMax(0, count);
    if (count == recentLimit)
        return;
    recentLimit = count;
    emit keepRecentChanged();
    changed();
}

void EmojiSet::setFilter(const QString& text)
{
    if (text == search)
        return;
    search = text;
    emit filterChanged();
    changed();
}

void EmojiSet::changed()
{
    laid = false;
    emit shownChanged();
}

void EmojiSet::load() const
{
    if (loaded)
        return;
    loaded = true;
    laid = false;
    loadList();
    loadRecent();
}

void EmojiSet::loadList() const
{
    all.clear();
    groups.clear();
    byText.clear();
    if (listFile.isEmpty())
        return;
    QFile file(listFile);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning("[emoji] load refused reason=unreadable file=%s error=%s", qPrintable(listFile),
                 qPrintable(file.errorString()));
        return;
    }
    QElapsedTimer timer;
    timer.start();
    int skipped = 0;
    int undrawn = 0;
    for (const QString& line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
    {
        // a keycap emoji starts with the same sign as a note
        if (line.startsWith(QLatin1String("# ")))
            continue;
        if (line.startsWith(QLatin1Char('@')))
        {
            groups.append({line.mid(1), {}});
            continue;
        }
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() != 4 || fields[0].isEmpty() || groups.isEmpty())
        {
            ++skipped;
            continue;
        }
        if (drawable && !drawable(fields[0]))
        {
            ++undrawn;
            continue;
        }
        const int index = int(all.size());
        const QStringList tones = fields[3].split(QLatin1Char(' '), Qt::SkipEmptyParts);
        all.append({fields[0], fields[1], wordsOf(fields[1]), wordsOf(fields[2]), tones});
        groups.last().members.append(index);
        byText.insert(fields[0], index);
        for (const QString& tone : tones)
            byText.insert(tone, index);
    }
    if (skipped > 0)
        qWarning("[emoji] load skipped=%d file=%s", skipped, qPrintable(listFile));
    qInfo("[emoji] loaded count=%d undrawn=%d ms=%lld file=%s", int(all.size()), undrawn, timer.elapsed(),
          qPrintable(listFile));
}

void EmojiSet::loadRecent() const
{
    recent.clear();
    chosenTones.clear();
    QFile file(storage + QStringLiteral("/recent.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue& value : root.value(QStringLiteral("recent")).toArray())
        recent.append(value.toString());
    const QJsonObject tones = root.value(QStringLiteral("tones")).toObject();
    for (auto tone = tones.begin(); tone != tones.end(); ++tone)
        chosenTones.insert(tone.key(), tone.value().toString());
}

void EmojiSet::saveRecent() const
{
    QJsonObject tones;
    for (auto tone = chosenTones.begin(); tone != chosenTones.end(); ++tone)
        tones.insert(tone.key(), tone.value());
    const QJsonObject root{{QStringLiteral("recent"), QJsonArray::fromStringList(recent)},
                           {QStringLiteral("tones"), tones}};
    QSaveFile file(storage + QStringLiteral("/recent.json"));
    if (!QDir().mkpath(storage) || !QFile::setPermissions(storage, privateDir) || !file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0 || !file.commit())
    {
        qWarning("[emoji] save failed file=%s reason=%s", qPrintable(file.fileName()), qPrintable(file.errorString()));
        return;
    }
    QFile::setPermissions(file.fileName(), privateFile);
}

int EmojiSet::baseOf(const QString& text) const
{
    return byText.value(text, -1);
}

QString EmojiSet::shownText(int emoji) const
{
    const Emoji& base = all[emoji];
    const QString tone = chosenTones.value(base.text);
    return base.tones.contains(tone) ? tone : base.text;
}

// 0 when the name holds every word, 1 when search words were needed, -1 for no match
int EmojiSet::matchRank(const Emoji& emoji, const QStringList& words) const
{
    const auto begins = [](const QStringList& known, const QString& word) {
        return std::any_of(known.begin(), known.end(), [&](const QString& one) { return one.startsWith(word); });
    };
    int rank = 0;
    for (const QString& word : words)
    {
        if (begins(emoji.nameWords, word))
            continue;
        if (!begins(emoji.searchWords, word))
            return -1;
        rank = 1;
    }
    return rank;
}

void EmojiSet::lay() const
{
    load();
    if (laid)
        return;
    laid = true;
    shown.clear();
    shownRows.clear();
    const auto add = [this](const QString& group, const QList<int>& members) {
        if (members.isEmpty())
            return;
        shownRows.append({group, 0, 0});
        for (int start = 0; start < members.size(); start += across)
        {
            const int length = int(qMin<qsizetype>(across, members.size() - start));
            shownRows.append({QString(), int(shown.size()), length});
            shown.append(members.mid(start, length));
        }
    };

    const QStringList words = wordsOf(search);
    if (!words.isEmpty())
    {
        QList<int> inName;
        QList<int> inWords;
        for (int index = 0; index < all.size(); ++index)
        {
            const int rank = matchRank(all[index], words);
            if (rank == 0)
                inName.append(index);
            else if (rank == 1)
                inWords.append(index);
        }
        add(QStringLiteral("found"), inName + inWords);
        return;
    }

    QList<int> lately;
    for (const QString& text : recent)
    {
        const int index = baseOf(text);
        if (index >= 0 && lately.size() < recentLimit)
            lately.append(index);
    }
    add(QStringLiteral("recent"), lately);
    for (const Group& group : groups)
        add(group.id, group.members);
}

QVariantList EmojiSet::rows() const
{
    lay();
    QVariantList out;
    for (const Row& row : shownRows)
    {
        if (row.length == 0)
        {
            out.append(QVariantMap{{QStringLiteral("group"), row.group}});
            continue;
        }
        QVariantList cells;
        for (int index = row.first; index < row.first + row.length; ++index)
        {
            cells.append(QVariantMap{{QStringLiteral("text"), shownText(shown[index])},
                                     {QStringLiteral("toned"), !all[shown[index]].tones.isEmpty()}});
        }
        out.append(QVariantMap{{QStringLiteral("first"), row.first}, {QStringLiteral("cells"), cells}});
    }
    return out;
}

QVariantList EmojiSet::sections() const
{
    lay();
    QVariantList out;
    for (int row = 0; row < shownRows.size(); ++row)
    {
        if (shownRows[row].length == 0)
            out.append(QVariantMap{{QStringLiteral("group"), shownRows[row].group}, {QStringLiteral("row"), row}});
    }
    return out;
}

int EmojiSet::count() const
{
    lay();
    return int(shown.size());
}

QString EmojiSet::textAt(int index) const
{
    lay();
    return index >= 0 && index < shown.size() ? shownText(shown[index]) : QString();
}

QString EmojiSet::nameAt(int index) const
{
    lay();
    return index >= 0 && index < shown.size() ? all[shown[index]].name : QString();
}

QStringList EmojiSet::tonesAt(int index) const
{
    lay();
    if (index < 0 || index >= shown.size() || all[shown[index]].tones.isEmpty())
        return {};
    return QStringList{all[shown[index]].text} + all[shown[index]].tones;
}

int EmojiSet::rowOf(int index) const
{
    lay();
    for (int row = 0; row < shownRows.size(); ++row)
    {
        if (index >= shownRows[row].first && index < shownRows[row].first + shownRows[row].length)
            return row;
    }
    return -1;
}

int EmojiSet::moved(int index, int right, int down) const
{
    lay();
    if (shown.isEmpty())
        return -1;
    if (index < 0 || index >= shown.size())
        return 0;
    index = qBound(0, index + right, int(shown.size()) - 1);
    if (down == 0)
        return index;
    QList<Row> cellRows;
    int at = 0;
    for (const Row& row : shownRows)
    {
        if (row.length == 0)
            continue;
        if (index >= row.first)
            at = int(cellRows.size());
        cellRows.append(row);
    }
    const int target = qBound(0, at + down, int(cellRows.size()) - 1);
    if (target == at)
        return index;
    return cellRows[target].first + qMin(index - cellRows[at].first, cellRows[target].length - 1);
}

void EmojiSet::used(const QString& text)
{
    load();
    const int index = baseOf(text);
    if (index < 0)
    {
        qWarning("[emoji] used refused reason=unknown text=%s", qPrintable(text));
        return;
    }
    const QString base = all[index].text;
    if (text == base)
        chosenTones.remove(base);
    else
        chosenTones.insert(base, text);
    recent.removeAll(base);
    recent.prepend(base);
    recent = recent.mid(0, qMax(recentLimit, 0));
    saveRecent();
    changed();
}

void EmojiSet::clearRecent()
{
    load();
    recent.clear();
    saveRecent();
    changed();
}
