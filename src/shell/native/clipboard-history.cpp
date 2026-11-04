#include "clipboard-history.hpp"

#include <KSystemClipboard>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <optional>

namespace
{
const QString secretHint = QStringLiteral("x-kde-passwordManagerHint");
const QStringList privateTypes = {QStringLiteral("org.nspasteboard.ConcealedType"),
                                  QStringLiteral("org.nspasteboard.TransientType")};
const auto privateFile = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser;
const auto privateDir = privateFile | QFile::ExeOwner | QFile::ExeUser;
constexpr int searchedChars = 65536;
constexpr int labelChars = 200;
constexpr int thumbnailSide = 256;
constexpr int previewLines = 12;
constexpr int previewChars = 120;
constexpr int excerptChars = 2000;

QString hashOf(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool makePrivateDir(const QString& path)
{
    return QDir().mkpath(path) && QFile::setPermissions(path, privateDir);
}

QString firstLine(const QString& text)
{
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const QString simple = line.simplified();
        if (!simple.isEmpty())
            return simple.left(labelChars);
    }
    return {};
}

bool isLink(const QString& text)
{
    static const QRegularExpression link(QStringLiteral("^https?://\\S+$"));
    return link.match(text.trimmed()).hasMatch();
}

QString previewOf(const QString& text)
{
    QStringList lines;
    for (const QString& line : text.left(previewLines * 1024).split(QLatin1Char('\n'))) {
        if (lines.isEmpty() && line.trimmed().isEmpty())
            continue;
        QString shown = line.left(previewChars);
        shown.replace(QLatin1Char('\t'), QStringLiteral("    "));
        shown.truncate(previewChars);
        while (!shown.isEmpty() && shown.back().isSpace())
            shown.chop(1);
        lines << shown;
        if (lines.size() == previewLines)
            break;
    }
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines.join(QLatin1Char('\n'));
}

// enough of the copy to read in a preview, line breaks kept, cut with an ellipsis
QString excerptOf(const QString& text)
{
    QString shown = text.left(excerptChars + 1);
    shown.replace(QLatin1Char('\t'), QStringLiteral("    "));
    while (shown.startsWith(QLatin1Char('\n')) || shown.startsWith(QLatin1Char('\r')))
        shown.remove(0, 1);
    if (text.size() > excerptChars)
        shown = shown.left(excerptChars - 1) + QChar(0x2026);
    while (!shown.isEmpty() && shown.back().isSpace())
        shown.chop(1);
    return shown;
}

// plain text under a kind of its own, such as code or json, but not markup
bool isTextual(const QString& mime)
{
    static const QMimeDatabase kinds;
    static const QStringList markup = {QStringLiteral("text/html"), QStringLiteral("application/xhtml+xml"),
                                       QStringLiteral("text/rtf"), QStringLiteral("text/richtext"),
                                       QStringLiteral("text/uri-list")};
    return !markup.contains(mime) && kinds.mimeTypeForName(mime).inherits(QStringLiteral("text/plain"));
}

QByteArray asPng(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    return image.save(&buffer, "PNG") ? bytes : QByteArray();
}
}

ClipboardHistory::ClipboardHistory(QObject* parent) : QAbstractListModel(parent)
{
    storage = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/mavernix/clipboard");
    saving.setSingleShot(true);
    saving.setInterval(400);
    connect(&saving, &QTimer::timeout, this, &ClipboardHistory::save);
}

ClipboardHistory::~ClipboardHistory()
{
    if (saving.isActive())
        save();
}

int ClipboardHistory::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(shown.size());
}

QVariant ClipboardHistory::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= shown.size())
        return {};
    const Entry& entry = entries[indexOf(shown[index.row()])];
    switch (role) {
    case IdRole:
        return entry.id;
    case KindRole:
        return entry.kind;
    case LabelRole:
        return entry.label;
    case PinnedRole:
        return entry.pinned;
    case SectionRole:
        return entry.pinned ? QStringLiteral("pinned") : QStringLiteral("recent");
    case ThumbnailRole:
        return entry.kind == QLatin1String("image") ? QUrl::fromLocalFile(thumbnailPath(entry)) : QUrl();
    case PictureSizeRole:
        return entry.pictureSize;
    case FileCountRole:
        return entry.fileCount;
    case PreviewRole:
        return entry.kind == QLatin1String("image") ? QString() : previewOf(entry.text);
    case AddedRole:
        return entry.added;
    case ExcerptRole:
        return entry.kind == QLatin1String("image") ? QString() : excerptOf(entry.text);
    }
    return {};
}

QHash<int, QByteArray> ClipboardHistory::roleNames() const
{
    return {{IdRole, "entryId"},       {KindRole, "kind"},           {LabelRole, "label"},
            {PinnedRole, "pinned"},    {SectionRole, "section"},     {ThumbnailRole, "thumbnail"},
            {PictureSizeRole, "pictureSize"}, {FileCountRole, "fileCount"}, {PreviewRole, "preview"}, {AddedRole, "added"},
            {ExcerptRole, "excerpt"}};
}

void ClipboardHistory::componentComplete()
{
    start();
    listen();
}

void ClipboardHistory::listen()
{
    KSystemClipboard* board = KSystemClipboard::instance();
    if (!board) {
        qWarning("[clipboard] listen refused reason=no_clipboard");
        return;
    }
    qInfo("[clipboard] listen backend=%s", board->metaObject()->className());
    offer = [board](QMimeData* data) {
        if (data)
            board->setMimeData(data, QClipboard::Clipboard);
        else
            board->clear(QClipboard::Clipboard);
    };
    connect(board, &KSystemClipboard::changed, this, [this, board](QClipboard::Mode mode) {
        if (mode == QClipboard::Clipboard)
            take(board->mimeData(mode), board->ownsClipboard());
    });
}

void ClipboardHistory::start()
{
    if (!makePrivateDir(storage) || !makePrivateDir(storage + QStringLiteral("/blobs"))
        || !makePrivateDir(storage + QStringLiteral("/thumbnails"))) {
        qWarning("[clipboard] start refused reason=folder folder=%s", qPrintable(storage));
        return;
    }
    load();
    started = true;
    showRows();
    emit countsChanged();
}

void ClipboardHistory::load()
{
    QFile file(storage + QStringLiteral("/history.json"));
    QJsonParseError error{};
    const QJsonDocument document = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll(), &error)
                                                                 : QJsonDocument();
    file.close();
    if (file.exists() && !document.isObject()) {
        qWarning("[clipboard] load refused reason=%s file=%s", qPrintable(error.errorString()), qPrintable(file.fileName()));
        QFile::remove(file.fileName() + QStringLiteral(".bad"));
        file.rename(file.fileName() + QStringLiteral(".bad"));
    }
    const QJsonObject root = document.object();
    pausing = root.value(QStringLiteral("paused")).toBool();
    current = root.value(QStringLiteral("current")).toString();
    for (const QJsonValue& value : root.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject item = value.toObject();
        Entry entry;
        entry.id = item.value(QStringLiteral("id")).toString();
        entry.kind = item.value(QStringLiteral("kind")).toString();
        entry.label = item.value(QStringLiteral("label")).toString();
        entry.text = item.value(QStringLiteral("text")).toString();
        entry.pinned = item.value(QStringLiteral("pinned")).toBool();
        entry.added = QDateTime::fromString(item.value(QStringLiteral("added")).toString(), Qt::ISODateWithMs);
        entry.pictureSize = QSize(item.value(QStringLiteral("width")).toInt(), item.value(QStringLiteral("height")).toInt());
        entry.fileCount = item.value(QStringLiteral("files")).toInt();
        bool whole = !entry.id.isEmpty();
        for (const QJsonValue& format : item.value(QStringLiteral("formats")).toArray()) {
            const QString blob = format.toObject().value(QStringLiteral("blob")).toString();
            whole = whole && !blob.isEmpty() && !blob.contains(QLatin1Char('/')) && QFile::exists(blobPath(blob));
            entry.formats << Format{format.toObject().value(QStringLiteral("mime")).toString(), blob};
        }
        if (!whole || entry.formats.isEmpty()) {
            qWarning("[clipboard] load dropped id=%s reason=payload_missing", qPrintable(entry.id));
            continue;
        }
        if (remembering || entry.pinned)
            entries << entry;
    }
    QSet<QString> used;
    QSet<QString> ids;
    for (const Entry& entry : std::as_const(entries)) {
        ids << entry.id + QStringLiteral(".png");
        for (const Format& format : entry.formats)
            used << format.blob;
    }
    for (const QString& name : QDir(storage + QStringLiteral("/blobs")).entryList(QDir::Files))
        if (!used.contains(name))
            QFile::remove(blobPath(name));
    for (const QString& name : QDir(storage + QStringLiteral("/thumbnails")).entryList(QDir::Files))
        if (!ids.contains(name))
            QFile::remove(storage + QStringLiteral("/thumbnails/") + name);
    qInfo("[clipboard] load entries=%lld pinned=%d paused=%d", qint64(entries.size()), pinnedCount(), int(pausing));
}

void ClipboardHistory::take(const QMimeData* data, bool ours)
{
    const QStringList offered = data ? data->formats() : QStringList();
    if (ours) {
        setSecretShown(false);
        return;
    }
    if (offered.isEmpty()) {
        setSecretShown(false);
        if (!pausing && indexOf(current) >= 0) {
            qInfo("[clipboard] reoffer id=%s", qPrintable(current));
            choose(current);
        }
        return;
    }
    const bool secret = (offered.contains(secretHint) && data->data(secretHint) == "secret")
                        || std::any_of(privateTypes.cbegin(), privateTypes.cend(),
                                       [&](const QString& type) { return offered.contains(type); });
    setSecretShown(secret);
    if (secret || pausing) {
        qInfo("[clipboard] skipped reason=%s", secret ? "sensitive" : "paused");
        return;
    }

    QByteArray picture;
    QImage decoded;
    if (imagesKept) {
        QStringList pictures = offered.filter(QRegularExpression(QStringLiteral("^image/")));
        pictures.removeAll(QStringLiteral("image/png"));
        if (offered.contains(QStringLiteral("image/png")))
            pictures.prepend(QStringLiteral("image/png"));
        for (const QString& mime : std::as_const(pictures)) {
            const QByteArray bytes = data->data(mime);
            if (decoded.loadFromData(bytes)) {
                picture = mime == QLatin1String("image/png") ? bytes : asPng(decoded);
                break;
            }
        }
    }
    QList<QPair<QString, QByteArray>> kept;
    qint64 bytes = 0;
    for (const QString& mime : offered) {
        if (!mime.contains(QLatin1Char('/')) || mime.startsWith(QLatin1String("image/")))
            continue;
        kept << qMakePair(mime, data->data(mime));
        bytes += kept.last().second.size();
    }
    if (!picture.isEmpty()) {
        kept << qMakePair(QStringLiteral("image/png"), picture);
        bytes += picture.size();
    }
    if (bytes > qint64(imageMb) * 1024 * 1024) {
        qInfo("[clipboard] skipped reason=too_big bytes=%lld", bytes);
        return;
    }

    Entry entry;
    QByteArray key;
    const auto bytesOf = [&](const QString& mime) {
        for (const auto& [name, value] : std::as_const(kept))
            if (name == mime)
                return value;
        return QByteArray();
    };
    const QList<QUrl> urls = data->hasUrls() ? data->urls() : QList<QUrl>();
    QByteArray textBytes = bytesOf(QStringLiteral("text/plain;charset=utf-8"));
    if (textBytes.isEmpty())
        textBytes = bytesOf(QStringLiteral("text/plain"));
    for (const auto& [mime, value] : std::as_const(kept))
        if (textBytes.isEmpty() && isTextual(mime))
            textBytes = value;
    const QString text = QString::fromUtf8(textBytes);
    const bool pictureFirst = !picture.isEmpty() && (firstLine(text).isEmpty() || isLink(text));
    if (!urls.isEmpty() && std::all_of(urls.cbegin(), urls.cend(), [](const QUrl& url) { return url.isLocalFile(); })) {
        entry.kind = QStringLiteral("files");
        entry.label = QFileInfo(urls.first().toLocalFile()).fileName();
        entry.fileCount = int(urls.size());
        for (const QUrl& url : urls)
            entry.text += url.toLocalFile() + QLatin1Char('\n');
        key = "files:" + entry.text.toUtf8();
    } else if (pictureFirst) {
        entry.kind = QStringLiteral("image");
        entry.label = firstLine(text);
        entry.pictureSize = decoded.size();
        key = "image:" + picture;
    } else if (!firstLine(text).isEmpty()) {
        entry.kind = isLink(text) ? QStringLiteral("link") : QStringLiteral("text");
        entry.label = firstLine(text);
        entry.text = text.left(searchedChars);
        key = "text:" + textBytes;
    } else {
        qInfo("[clipboard] skipped reason=no_known_format formats=%s", qPrintable(offered.join(QLatin1Char(','))));
        return;
    }
    entry.id = hashOf(key).left(16);
    if (!entries.isEmpty() && entries.first().id == entry.id) {
        current = entry.id;
        return;
    }
    entry.added = QDateTime::currentDateTimeUtc();

    for (const auto& [mime, value] : std::as_const(kept)) {
        const QString blob = hashOf(value);
        if (!QFile::exists(blobPath(blob))) {
            QSaveFile file(blobPath(blob));
            if (!file.open(QIODevice::WriteOnly) || file.write(value) != value.size() || !file.commit()) {
                qWarning("[clipboard] skipped reason=write_failed file=%s", qPrintable(file.fileName()));
                return;
            }
            QFile::setPermissions(blobPath(blob), privateFile);
        }
        entry.formats << Format{mime, blob};
    }

    const int old = indexOf(entry.id);
    if (old >= 0) {
        entry.pinned = entries[old].pinned;
        const Entry gone = entries.takeAt(old);
        entries.prepend(entry);
        forgetBlobs(gone);
    } else {
        entries.prepend(entry);
    }
    if (entry.kind == QLatin1String("image")) {
        const QImage thumbnail = decoded.width() <= thumbnailSide && decoded.height() <= thumbnailSide
            ? decoded
            : decoded.scaled(thumbnailSide, thumbnailSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (thumbnail.save(thumbnailPath(entry), "PNG"))
            QFile::setPermissions(thumbnailPath(entry), privateFile);
    }
    current = entry.id;
    qInfo("[clipboard] stored kind=%s bytes=%lld id=%s", qPrintable(entry.kind), bytes, qPrintable(entry.id));
    prune();
    showRows();
    changed();
}

void ClipboardHistory::choose(const QString& id, bool plain)
{
    const int at = indexOf(id);
    if (at < 0 || !offer) {
        qWarning("[clipboard] choose refused id=%s reason=%s", qPrintable(id), at < 0 ? "unknown" : "not_listening");
        return;
    }
    auto* data = new QMimeData;
    if (plain) {
        data->setData(QStringLiteral("text/plain"), wholeText(entries[at]).toUtf8());
    } else {
        for (const Format& format : std::as_const(entries[at].formats)) {
            QFile file(blobPath(format.blob));
            if (!file.open(QIODevice::ReadOnly)) {
                qWarning("[clipboard] choose refused id=%s reason=payload_missing blob=%s", qPrintable(id),
                         qPrintable(format.blob));
                delete data;
                return;
            }
            data->setData(format.mime, file.readAll());
        }
    }
    offer(data);
    current = id;
    if (!entries[at].pinned) {
        Entry entry = entries.takeAt(at);
        entry.added = QDateTime::currentDateTimeUtc();
        entries.prepend(entry);
        showRows();
        changed();
    }
}

QString ClipboardHistory::idAt(int row) const
{
    return row >= 0 && row < shown.size() ? shown[row] : QString();
}

void ClipboardHistory::togglePin(const QString& id)
{
    const int at = indexOf(id);
    if (at >= 0)
        pin(id, !entries[at].pinned);
}

void ClipboardHistory::pin(const QString& id, bool on)
{
    const int at = indexOf(id);
    if (at < 0 || entries[at].pinned == on)
        return;
    entries[at].pinned = on;
    prune();
    showRows();
    changed();
}

void ClipboardHistory::remove(const QString& id)
{
    const int at = indexOf(id);
    if (at < 0)
        return;
    forgetBlobs(entries.takeAt(at));
    showRows();
    changed();
}

void ClipboardHistory::clearHistory()
{
    QList<Entry> gone;
    for (int at = int(entries.size()) - 1; at >= 0; --at)
        if (!entries[at].pinned)
            gone << entries.takeAt(at);
    for (const Entry& entry : std::as_const(gone))
        forgetBlobs(entry);
    showRows();
    changed();
}

void ClipboardHistory::clearSensitive()
{
    if (offer)
        offer(nullptr);
    setSecretShown(false);
}

void ClipboardHistory::save()
{
    saving.stop();
    if (!started)
        return;
    QJsonArray list;
    for (const Entry& entry : std::as_const(entries)) {
        if (!remembering && !entry.pinned)
            continue;
        QJsonArray formats;
        for (const Format& format : entry.formats)
            formats << QJsonObject{{QStringLiteral("mime"), format.mime}, {QStringLiteral("blob"), format.blob}};
        list << QJsonObject{{QStringLiteral("id"), entry.id},
                            {QStringLiteral("kind"), entry.kind},
                            {QStringLiteral("label"), entry.label},
                            {QStringLiteral("text"), entry.text},
                            {QStringLiteral("pinned"), entry.pinned},
                            {QStringLiteral("added"), entry.added.toString(Qt::ISODateWithMs)},
                            {QStringLiteral("width"), entry.pictureSize.width()},
                            {QStringLiteral("height"), entry.pictureSize.height()},
                            {QStringLiteral("files"), entry.fileCount},
                            {QStringLiteral("formats"), formats}};
    }
    const QJsonObject root{{QStringLiteral("version"), 1}, {QStringLiteral("paused"), pausing},
                           {QStringLiteral("current"), current}, {QStringLiteral("entries"), list}};
    QSaveFile file(storage + QStringLiteral("/history.json"));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0
        || !file.commit()) {
        qWarning("[clipboard] save failed file=%s reason=%s", qPrintable(file.fileName()), qPrintable(file.errorString()));
        return;
    }
    QFile::setPermissions(file.fileName(), privateFile);
}

void ClipboardHistory::setFolder(const QString& path)
{
    if (path == storage)
        return;
    storage = path;
    emit folderChanged();
}

void ClipboardHistory::setKeepItems(int count)
{
    if (count == limit)
        return;
    limit = count;
    emit keepItemsChanged();
    if (started) {
        prune();
        showRows();
        changed();
    }
}

void ClipboardHistory::setRemember(bool on)
{
    if (on == remembering)
        return;
    remembering = on;
    emit rememberChanged();
    changed();
}

void ClipboardHistory::setKeepImages(bool on)
{
    if (on == imagesKept)
        return;
    imagesKept = on;
    emit keepImagesChanged();
}

void ClipboardHistory::setImageLimitMb(int megabytes)
{
    if (megabytes == imageMb)
        return;
    imageMb = megabytes;
    emit imageLimitMbChanged();
}

void ClipboardHistory::setPaused(bool on)
{
    if (on == pausing)
        return;
    pausing = on;
    qInfo("[clipboard] paused=%d", int(on));
    emit pausedChanged();
    changed();
}

int ClipboardHistory::pinnedCount() const
{
    return int(std::count_if(entries.cbegin(), entries.cend(), [](const Entry& e) { return e.pinned; }));
}

void ClipboardHistory::prune()
{
    qsizetype unpinned = std::count_if(entries.cbegin(), entries.cend(), [](const Entry& e) { return !e.pinned; });
    for (qsizetype at = entries.size() - 1; at >= 0 && unpinned > limit; --at) {
        if (entries[at].pinned)
            continue;
        forgetBlobs(entries.takeAt(at));
        --unpinned;
    }
}

void ClipboardHistory::showRows()
{
    QStringList wanted;
    for (const bool pinned : {true, false})
        for (const Entry& entry : std::as_const(entries))
            if (entry.pinned == pinned)
                wanted << entry.id;
    for (qsizetype row = shown.size() - 1; row >= 0; --row) {
        if (wanted.contains(shown[row]))
            continue;
        beginRemoveRows({}, int(row), int(row));
        shown.removeAt(row);
        endRemoveRows();
    }
    for (int row = 0; row < wanted.size(); ++row) {
        const int at = int(shown.indexOf(wanted[row], row));
        if (at == row)
            continue;
        if (at < 0) {
            beginInsertRows({}, row, row);
            shown.insert(row, wanted[row]);
            endInsertRows();
        } else {
            beginMoveRows({}, at, at, {}, row);
            shown.move(at, row);
            endMoveRows();
        }
    }
    if (!shown.isEmpty())
        emit dataChanged(index(0), index(int(shown.size()) - 1));
}

void ClipboardHistory::changed()
{
    emit countsChanged();
    if (started)
        saving.start();
}

void ClipboardHistory::setSecretShown(bool on)
{
    if (on == secretShown)
        return;
    secretShown = on;
    emit sensitiveChanged();
}

void ClipboardHistory::forgetBlobs(const Entry& gone)
{
    for (const Format& format : gone.formats) {
        const bool used = std::any_of(entries.cbegin(), entries.cend(), [&](const Entry& entry) {
            return std::any_of(entry.formats.cbegin(), entry.formats.cend(),
                               [&](const Format& other) { return other.blob == format.blob; });
        });
        if (!used)
            QFile::remove(blobPath(format.blob));
    }
    if (indexOf(gone.id) < 0)
        QFile::remove(thumbnailPath(gone));
}

int ClipboardHistory::indexOf(const QString& id) const
{
    for (qsizetype at = 0; at < entries.size(); ++at)
        if (entries[at].id == id)
            return int(at);
    return -1;
}

QString ClipboardHistory::blobPath(const QString& blob) const
{
    return storage + QStringLiteral("/blobs/") + blob;
}

QString ClipboardHistory::thumbnailPath(const Entry& entry) const
{
    return storage + QStringLiteral("/thumbnails/") + entry.id + QStringLiteral(".png");
}

QString ClipboardHistory::wholeText(const Entry& entry) const
{
    const auto read = [&](const auto& wanted) -> std::optional<QString> {
        for (const Format& format : entry.formats) {
            QFile file(blobPath(format.blob));
            if (wanted(format.mime) && file.open(QIODevice::ReadOnly))
                return QString::fromUtf8(file.readAll());
        }
        return std::nullopt;
    };
    for (const QString& plain : {QStringLiteral("text/plain;charset=utf-8"), QStringLiteral("text/plain")})
        if (const auto found = read([&](const QString& mime) { return mime == plain; }))
            return *found;
    if (entry.kind == QLatin1String("files"))
        return entry.text;
    return read(isTextual).value_or(entry.text);
}

bool ClipboardHistory::rowMatches(int row, const QStringList& words) const
{
    if (row < 0 || row >= shown.size())
        return false;
    const Entry& entry = entries[indexOf(shown[row])];
    const QString& haystack = entry.text.isEmpty() ? entry.label : entry.text;
    return std::all_of(words.cbegin(), words.cend(),
                       [&](const QString& word) { return haystack.contains(word, Qt::CaseInsensitive); });
}

QVariantList ClipboardHistory::stack() const
{
    static const QMimeDatabase kinds;
    QVariantList list;
    for (int row = 0; row < shown.size(); ++row) {
        const Entry& entry = entries[indexOf(shown[row])];
        QVariantMap cell{{QStringLiteral("path"), entry.id},
                         {QStringLiteral("kind"), entry.kind},
                         {QStringLiteral("name"), entry.label},
                         {QStringLiteral("pinned"), entry.pinned},
                         {QStringLiteral("preview"), data(index(row), PreviewRole)},
                         {QStringLiteral("excerpt"), data(index(row), ExcerptRole)},
                         {QStringLiteral("added"), entry.added},
                         {QStringLiteral("picture"), data(index(row), ThumbnailRole).toUrl().toString()},
                         {QStringLiteral("width"), entry.pictureSize.width()},
                         {QStringLiteral("height"), entry.pictureSize.height()},
                         {QStringLiteral("files"), entry.fileCount}};
        if (entry.kind == QLatin1String("files")) {
            const QString first = entry.text.section(QLatin1Char('\n'), 0, 0);
            const QMimeType type = QFileInfo(first).isDir() ? kinds.mimeTypeForName(QStringLiteral("inode/directory"))
                                                            : kinds.mimeTypeForFile(first, QMimeDatabase::MatchExtension);
            cell[QStringLiteral("icon")] = type.iconName();
            cell[QStringLiteral("genericIcon")] = type.genericIconName();
        }
        list << cell;
    }
    return list;
}

ClipboardSearch::ClipboardSearch(QObject* parent) : QSortFilterProxyModel(parent) {}

void ClipboardSearch::setHistory(ClipboardHistory* history)
{
    if (history == source)
        return;
    source = history;
    setSourceModel(history);
    emit historyChanged();
}

void ClipboardSearch::setFilter(const QString& text)
{
    if (text == search)
        return;
    search = text;
    beginFilterChange();
    words = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    endFilterChange(Direction::Rows);
    emit filterChanged();
}

QString ClipboardSearch::idAt(int row) const
{
    return source && row >= 0 && row < rowCount() ? source->idAt(mapToSource(index(row, 0)).row()) : QString();
}

bool ClipboardSearch::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    return !parent.isValid() && source && source->rowMatches(row, words);
}
