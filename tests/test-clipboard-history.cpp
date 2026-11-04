#include <QtTest>

#include <QBuffer>
#include <QImage>
#include <QMimeData>
#include <QTemporaryDir>

#include <sys/stat.h>

#include "clipboard-history.hpp"

namespace
{
QMimeData* textData(const QString& text)
{
    auto* data = new QMimeData;
    data->setData("text/plain", text.toUtf8());
    data->setData("text/plain;charset=utf-8", text.toUtf8());
    data->setData("UTF8_STRING", text.toUtf8());
    return data;
}

QByteArray pngOf(int width, int height, QRgb colour = qRgb(200, 30, 30))
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(colour);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QStringList labels(const QAbstractItemModel& model)
{
    QStringList out;
    for (int row = 0; row < model.rowCount(); ++row)
        out << model.data(model.index(row, 0), ClipboardHistory::LabelRole).toString();
    return out;
}

QByteArray pictureOf(int width, int height, const char* format)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(qRgb(40, 90, 160));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format);
    return bytes;
}

QString idAt(const ClipboardHistory& history, int row)
{
    return history.data(history.index(row), ClipboardHistory::IdRole).toString();
}

int modeOf(const QString& path)
{
    struct stat info {};
    return ::stat(QFile::encodeName(path).constData(), &info) == 0 ? int(info.st_mode & 07777) : -1;
}

QString sectionAt(const ClipboardHistory& history, int row)
{
    return history.data(history.index(row), ClipboardHistory::SectionRole).toString();
}
}

class ClipboardHistoryTest : public QObject
{
    Q_OBJECT

    QTemporaryDir folder;
    QList<QMimeData*> offered;
    int clears = 0;

    void begin(ClipboardHistory& history, const QString& path = {})
    {
        history.setFolder(path.isEmpty() ? folder.path() + "/clipboard" : path);
        history.offer = [this](QMimeData* data) {
            if (data)
                offered << data;
            else
                ++clears;
        };
        history.start();
    }

    void take(ClipboardHistory& history, QMimeData* data, bool ours = false)
    {
        history.take(data, ours);
        delete data;
    }

  private slots:
    void init()
    {
        QVERIFY(folder.isValid());
        QDir(folder.path()).removeRecursively();
        QDir().mkpath(folder.path());
        qDeleteAll(offered);
        offered.clear();
        clears = 0;
    }

    void newest_copy_comes_first()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("one"));
        take(history, textData("two"));
        QCOMPARE(labels(history), QStringList({"two", "one"}));
        QCOMPARE(sectionAt(history, 0), QString("recent"));
    }

    void a_repeat_moves_to_the_top_once()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("one"));
        take(history, textData("two"));
        auto* again = new QMimeData;
        again->setData("text/plain", "one");
        again->setData("text/html", "<b>one</b>");
        take(history, again);
        QCOMPARE(labels(history), QStringList({"one", "two"}));
        history.choose(idAt(history, 0));
        QCOMPARE(offered.size(), 1);
        QCOMPARE(offered[0]->data("text/html"), QByteArray("<b>one</b>"));
    }

    void label_is_the_first_line_simplified()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("\n\n   first    line  \nsecond"));
        QCOMPARE(labels(history), QStringList({"first line"}));
    }

    void blank_text_is_not_kept()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("  \n\t "));
        QCOMPARE(history.rowCount(), 0);
    }

    void the_limit_drops_the_oldest_unpinned()
    {
        ClipboardHistory history;
        history.setKeepItems(3);
        begin(history);
        take(history, textData("a"));
        history.pin(idAt(history, 0), true);
        take(history, textData("b"));
        take(history, textData("c"));
        take(history, textData("d"));
        take(history, textData("e"));
        QCOMPARE(labels(history), QStringList({"a", "e", "d", "c"}));
        QCOMPARE(sectionAt(history, 0), QString("pinned"));
    }

    void a_lower_limit_prunes_at_once()
    {
        ClipboardHistory history;
        begin(history);
        for (const char* text : {"a", "b", "c", "d"})
            take(history, textData(text));
        history.setKeepItems(2);
        QCOMPARE(labels(history), QStringList({"d", "c"}));
    }

    void a_pinned_entry_copied_again_stays_pinned()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("pinned"));
        history.pin(idAt(history, 0), true);
        take(history, textData("other"));
        take(history, textData("pinned"));
        QCOMPARE(labels(history), QStringList({"pinned", "other"}));
        QCOMPARE(history.pinnedCount(), 1);
    }

    void choosing_without_a_clipboard_is_refused()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("one"));
        history.offer = nullptr;
        history.choose(idAt(history, 0));
        QCOMPARE(labels(history), QStringList({"one"}));
    }

    void rows_are_reached_by_position()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("one"));
        take(history, textData("two"));
        QCOMPARE(history.idAt(1), idAt(history, 1));
        QCOMPARE(history.idAt(2), QString());
        QCOMPARE(history.idAt(-1), QString());
        history.togglePin(history.idAt(1));
        QCOMPARE(labels(history), QStringList({"one", "two"}));
        history.togglePin(history.idAt(0));
        QCOMPARE(labels(history), QStringList({"two", "one"}));
    }

    void clear_keeps_pins()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("keep"));
        history.pin(idAt(history, 0), true);
        take(history, textData("drop"));
        history.clearHistory();
        QCOMPARE(labels(history), QStringList({"keep"}));
        QCOMPARE(history.pinnedCount(), 1);
    }

    void unpinning_returns_to_recent_by_time()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("old"));
        take(history, textData("new"));
        history.pin(idAt(history, 1), true);
        QCOMPARE(labels(history), QStringList({"old", "new"}));
        history.pin(idAt(history, 0), false);
        QCOMPARE(labels(history), QStringList({"new", "old"}));
        QCOMPARE(sectionAt(history, 1), QString("recent"));
    }

    void remove_forgets_the_entry_and_its_payload()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("secretish"));
        history.save();
        const QString blobs = folder.path() + "/clipboard/blobs";
        QVERIFY(!QDir(blobs).isEmpty());
        history.remove(idAt(history, 0));
        QCOMPARE(history.rowCount(), 0);
        QVERIFY(QDir(blobs).isEmpty());
    }

    void secret_hint_is_never_stored_data()
    {
        QTest::addColumn<QByteArray>("mime");
        QTest::addColumn<QByteArray>("value");
        QTest::addColumn<bool>("kept");
        QTest::newRow("kde secret") << QByteArray("x-kde-passwordManagerHint") << QByteArray("secret") << false;
        QTest::newRow("kde other value") << QByteArray("x-kde-passwordManagerHint") << QByteArray("plain") << true;
        QTest::newRow("concealed") << QByteArray("org.nspasteboard.ConcealedType") << QByteArray("") << false;
        QTest::newRow("transient") << QByteArray("org.nspasteboard.TransientType") << QByteArray("") << false;
    }

    void secret_hint_is_never_stored()
    {
        QFETCH(QByteArray, mime);
        QFETCH(QByteArray, value);
        QFETCH(bool, kept);
        ClipboardHistory history;
        begin(history);
        auto* data = textData("hunter2");
        data->setData(QString::fromLatin1(mime), value);
        take(history, data);
        history.save();
        QCOMPARE(history.rowCount(), kept ? 1 : 0);
        QCOMPARE(history.sensitive(), !kept);
        QFile file(folder.path() + "/clipboard/history.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll().contains("hunter2"), kept);
        QCOMPARE(QDir(folder.path() + "/clipboard/blobs").isEmpty(), !kept);
        if (kept) {
            history.choose(idAt(history, 0));
            QCOMPARE(offered.size(), 1);
            QVERIFY(!offered[0]->formats().contains(QString::fromLatin1(mime)));
        }
    }

    void the_next_copy_ends_sensitive()
    {
        ClipboardHistory history;
        begin(history);
        auto* data = textData("hunter2");
        data->setData("x-kde-passwordManagerHint", "secret");
        take(history, data);
        QVERIFY(history.sensitive());
        history.clearSensitive();
        QCOMPARE(clears, 1);
        take(history, textData("plain"));
        QVERIFY(!history.sensitive());
    }

    void our_own_offer_is_not_taken_again()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("one"));
        take(history, textData("two"));
        take(history, textData("echo"), true);
        QCOMPARE(labels(history), QStringList({"two", "one"}));
    }

    void choosing_moves_an_entry_up()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("one"));
        take(history, textData("two"));
        history.choose(idAt(history, 1));
        QCOMPARE(offered.size(), 1);
        QCOMPARE(offered[0]->data("text/plain"), QByteArray("one"));
        QCOMPARE(labels(history), QStringList({"one", "two"}));
    }

    void plain_choice_offers_only_text()
    {
        ClipboardHistory history;
        begin(history);
        auto* data = new QMimeData;
        data->setData("text/plain", "word");
        data->setData("text/html", "<i>word</i>");
        take(history, data);
        history.choose(idAt(history, 0), true);
        QCOMPARE(offered.size(), 1);
        QCOMPARE(offered[0]->formats(), QStringList({"text/plain"}));
    }

    void an_emptied_clipboard_gets_the_newest_back()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("pinned"));
        history.pin(idAt(history, 0), true);
        take(history, textData("newest"));
        take(history, new QMimeData);
        QCOMPARE(offered.size(), 1);
        QCOMPARE(offered[0]->data("text/plain"), QByteArray("newest"));
    }

    void an_emptied_clipboard_gets_a_chosen_pin_back()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("pinned"));
        history.pin(idAt(history, 0), true);
        take(history, textData("newest"));
        history.choose(idAt(history, 0));
        take(history, new QMimeData);
        QCOMPARE(offered.size(), 2);
        QCOMPARE(offered[1]->data("text/plain"), QByteArray("pinned"));
    }

    void a_removed_entry_does_not_come_back()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("older"));
        take(history, textData("gone"));
        history.remove(idAt(history, 0));
        QTest::failOnWarning(QRegularExpression("choose refused"));
        take(history, new QMimeData);
        QCOMPARE(offered.size(), 0);
    }

    void nothing_comes_back_while_paused()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("one"));
        history.setPaused(true);
        take(history, textData("unseen"));
        take(history, new QMimeData);
        QCOMPARE(labels(history), QStringList({"one"}));
        QCOMPARE(offered.size(), 0);
    }

    void an_image_keeps_png_only_with_its_size()
    {
        ClipboardHistory history;
        begin(history);
        auto* data = new QMimeData;
        data->setData("image/png", pngOf(300, 200));
        data->setData("image/bmp", QByteArray(1000, 'x'));
        data->setData("text/html", "<img src=x>");
        take(history, data);
        QCOMPARE(history.rowCount(), 1);
        const QModelIndex row = history.index(0);
        QCOMPARE(history.data(row, ClipboardHistory::KindRole).toString(), QString("image"));
        QCOMPARE(history.data(row, ClipboardHistory::PictureSizeRole).toSize(), QSize(300, 200));
        const QUrl thumbnail = history.data(row, ClipboardHistory::ThumbnailRole).toUrl();
        QImage small(thumbnail.toLocalFile());
        QVERIFY2(!small.isNull(), qPrintable(thumbnail.toString()));
        QCOMPARE(small.size(), QSize(256, 170));
        history.choose(idAt(history, 0));
        QCOMPARE(offered[0]->formats().contains("image/bmp"), false);
        QCOMPARE(offered[0]->formats().contains("text/html"), true);
    }

    void images_over_the_limit_are_skipped()
    {
        ClipboardHistory history;
        history.setImageLimitMb(1);
        begin(history);
        auto* data = new QMimeData;
        data->setData("image/png", QByteArray(1024 * 1024 + 1, 'x'));
        take(history, data);
        QCOMPARE(history.rowCount(), 0);
    }

    void images_can_be_left_out()
    {
        ClipboardHistory history;
        history.setKeepImages(false);
        begin(history);
        auto* data = new QMimeData;
        data->setData("image/png", pngOf(10, 10));
        take(history, data);
        QCOMPARE(history.rowCount(), 0);
    }

    void files_and_links_get_their_kind()
    {
        ClipboardHistory history;
        begin(history);
        auto* files = new QMimeData;
        files->setUrls({QUrl::fromLocalFile("/srv/a/report.pdf"), QUrl::fromLocalFile("/srv/b/notes.txt")});
        take(history, files);
        take(history, textData("https://example.org/page"));
        QCOMPARE(history.data(history.index(0), ClipboardHistory::KindRole).toString(), QString("link"));
        QCOMPARE(history.data(history.index(1), ClipboardHistory::KindRole).toString(), QString("files"));
        QCOMPARE(history.data(history.index(1), ClipboardHistory::LabelRole).toString(), QString("report.pdf"));
        QCOMPARE(history.data(history.index(1), ClipboardHistory::FileCountRole).toInt(), 2);
        auto* remote = new QMimeData;
        remote->setUrls({QUrl("https://example.org/other")});
        remote->setData("text/plain", "https://example.org/other");
        take(history, remote);
        QCOMPARE(history.data(history.index(0), ClipboardHistory::KindRole).toString(), QString("link"));
    }

    void search_needs_every_word_in_any_case()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("Alpha beta"));
        take(history, textData("beta gamma"));
        take(history, textData("delta\nALPHA on the second line"));
        ClipboardSearch search;
        search.setHistory(&history);
        search.setFilter("alpha");
        QCOMPARE(labels(search), QStringList({"delta", "Alpha beta"}));
        search.setFilter("BETA  alpha");
        QCOMPARE(labels(search), QStringList({"Alpha beta"}));
        QCOMPARE(search.idAt(0), idAt(history, 2));
        search.setFilter("");
        QCOMPARE(search.rowCount(), 3);
    }

    void a_search_leaves_the_history_whole()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("apple"));
        take(history, textData("banana"));
        ClipboardSearch search;
        search.setHistory(&history);
        search.setFilter("apple");
        QCOMPARE(search.rowCount(), 1);
        QCOMPARE(history.rowCount(), 2);
        take(history, textData("pineapple"));
        QCOMPARE(labels(search), QStringList({"pineapple", "apple"}));
    }

    void a_filter_change_keeps_rows_that_stay()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("apple"));
        take(history, textData("apricot"));
        take(history, textData("banana"));
        ClipboardSearch search;
        search.setHistory(&history);
        QSignalSpy resets(&search, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&search, &QAbstractItemModel::rowsRemoved);
        search.setFilter("ap");
        QCOMPARE(resets.count(), 0);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(labels(search), QStringList({"apricot", "apple"}));
    }

    void other_picture_formats_become_png()
    {
        ClipboardHistory history;
        begin(history);
        auto* data = new QMimeData;
        data->setData("image/jpeg", pictureOf(64, 48, "JPEG"));
        data->setData("image/bmp", pictureOf(64, 48, "BMP"));
        take(history, data);
        QCOMPARE(history.rowCount(), 1);
        QCOMPARE(history.data(history.index(0), ClipboardHistory::KindRole).toString(), QString("image"));
        QCOMPARE(history.data(history.index(0), ClipboardHistory::PictureSizeRole).toSize(), QSize(64, 48));
        history.choose(idAt(history, 0));
        QCOMPARE(offered[0]->formats(), QStringList({"image/png"}));
        QCOMPARE(QImage::fromData(offered[0]->data("image/png"), "PNG").size(), QSize(64, 48));
    }

    void a_picture_with_only_its_address_is_a_picture()
    {
        ClipboardHistory history;
        begin(history);
        auto* data = new QMimeData;
        data->setData("image/png", pngOf(30, 20));
        data->setData("text/plain", "https://example.org/cat.png");
        take(history, data);
        QCOMPARE(history.data(history.index(0), ClipboardHistory::KindRole).toString(), QString("image"));
        QCOMPARE(history.data(history.index(0), ClipboardHistory::LabelRole).toString(), QString("https://example.org/cat.png"));
        auto* cells = new QMimeData;
        cells->setData("image/png", pngOf(30, 20, qRgb(1, 2, 3)));
        cells->setData("text/plain", "a\tb\n1\t2");
        take(history, cells);
        QCOMPARE(history.data(history.index(0), ClipboardHistory::KindRole).toString(), QString("text"));
    }

    void text_offered_only_under_its_own_kind_is_text()
    {
        ClipboardHistory history;
        begin(history);
        const QByteArray ruby = "def stack\n" + QByteArray("  cells\n").repeated(10000) + "end\n";
        auto* code = new QMimeData;
        code->setData("application/x-ruby", ruby);
        take(history, code);
        auto* json = new QMimeData;
        json->setData("application/json", "{\"a\": 1}");
        take(history, json);
        auto* blob = new QMimeData;
        blob->setData("application/octet-stream", QByteArray("\x00\x01\x02", 3));
        take(history, blob);
        QCOMPARE(labels(history), QStringList({"{\"a\": 1}", "def stack"}));
        QCOMPARE(history.data(history.index(1), ClipboardHistory::KindRole).toString(), QString("text"));
        history.choose(idAt(history, 1));
        QCOMPARE(offered[0]->formats(), QStringList({"application/x-ruby"}));
        history.choose(idAt(history, 0), true);
        QCOMPARE(offered[1]->data("text/plain"), ruby);
    }

    void a_broken_picture_is_not_kept()
    {
        ClipboardHistory history;
        begin(history);
        auto* data = new QMimeData;
        data->setData("image/png", QByteArray(500, 'x'));
        take(history, data);
        QCOMPARE(history.rowCount(), 0);
    }

    void thumbnails_shrink_to_256_and_never_grow()
    {
        ClipboardHistory history;
        begin(history);
        auto* big = new QMimeData;
        big->setData("image/png", pngOf(1000, 500));
        take(history, big);
        QCOMPARE(QImage(history.data(history.index(0), ClipboardHistory::ThumbnailRole).toUrl().toLocalFile()).size(),
                 QSize(256, 128));
        auto* small = new QMimeData;
        small->setData("image/png", pngOf(40, 20, qRgb(9, 9, 9)));
        take(history, small);
        QCOMPARE(QImage(history.data(history.index(0), ClipboardHistory::ThumbnailRole).toUrl().toLocalFile()).size(),
                 QSize(40, 20));
    }

    void plain_text_of_a_long_copy_is_whole()
    {
        ClipboardHistory history;
        begin(history);
        const QString text = QString("word ").repeated(30000);
        take(history, textData(text));
        history.choose(idAt(history, 0), true);
        QCOMPARE(offered[0]->data("text/plain").size(), text.size());
    }

    void the_same_copy_announced_twice_changes_nothing()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("once"));
        QSignalSpy counts(&history, &ClipboardHistory::countsChanged);
        QSignalSpy changed(&history, &QAbstractItemModel::dataChanged);
        take(history, textData("once"));
        QCOMPARE(counts.count(), 0);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(history.rowCount(), 1);
    }

    void a_preview_holds_the_first_lines()
    {
        ClipboardHistory history;
        begin(history);
        QStringList lines;
        for (int at = 0; at < 30; ++at)
            lines << QString("line %1\t%2").arg(at).arg(QString(300, 'z'));
        take(history, textData("\n\n" + lines.join("\n")));
        const QString preview = history.data(history.index(0), ClipboardHistory::PreviewRole).toString();
        const QStringList shown = preview.split('\n');
        QCOMPARE(shown.size(), 12);
        QVERIFY(shown[0].startsWith("line 0 "));
        QVERIFY(shown[0].size() <= 120);
        QVERIFY(!preview.contains('\t'));
    }

    void an_excerpt_holds_a_long_line_whole_and_stops_with_an_ellipsis()
    {
        ClipboardHistory history;
        begin(history);
        const QString line = QString("a long single line of copied text that keeps going ").repeated(8).trimmed();
        take(history, textData("\n\n" + line + "\n\tsecond line"));
        QCOMPARE(history.data(history.index(0), ClipboardHistory::ExcerptRole).toString(),
                 line + "\n    second line");
        take(history, textData(QString("word ").repeated(20000)));
        const QString cut = history.data(history.index(0), ClipboardHistory::ExcerptRole).toString();
        QVERIFY2(cut.size() <= 2001 && cut.size() > 1900, qPrintable(QString::number(cut.size())));
        QVERIFY(cut.endsWith(QChar(0x2026)));
        const QVariantMap cell = history.stack()[0].toMap();
        QCOMPARE(cell["excerpt"].toString(), cut);
        QVERIFY(cell["added"].toDateTime().isValid());
    }

    void the_stack_lists_rows_with_what_a_cell_draws()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("pinned words"));
        history.pin(idAt(history, 0), true);
        auto* files = new QMimeData;
        files->setUrls({QUrl::fromLocalFile("/srv/a/report.pdf")});
        take(history, files);
        auto* image = new QMimeData;
        image->setData("image/png", pngOf(30, 20));
        take(history, image);
        const QVariantList stack = history.stack();
        QCOMPARE(stack.size(), 3);
        const QVariantMap first = stack[0].toMap();
        QCOMPARE(first["path"].toString(), idAt(history, 0));
        QCOMPARE(first["pinned"].toBool(), true);
        QCOMPARE(first["preview"].toString(), QString("pinned words"));
        QCOMPARE(stack[1].toMap()["kind"].toString(), QString("image"));
        QVERIFY(stack[1].toMap()["picture"].toString().startsWith("file://"));
        QCOMPARE(stack[1].toMap()["width"].toInt(), 30);
        QCOMPARE(stack[2].toMap()["icon"].toString(), QString("application-pdf"));
        QCOMPARE(stack[2].toMap()["name"].toString(), QString("report.pdf"));
        QCOMPARE(stack[2].toMap()["preview"].toString(), QString("/srv/a/report.pdf"));
        const QDateTime added = history.data(history.index(0), ClipboardHistory::AddedRole).toDateTime();
        QVERIFY(added.isValid() && qAbs(added.secsTo(QDateTime::currentDateTimeUtc())) < 60);
    }

    void history_survives_a_restart()
    {
        const QString path = folder.path() + "/clipboard";
        {
            ClipboardHistory history;
            begin(history, path);
            take(history, textData("pinned one"));
            history.pin(idAt(history, 0), true);
            auto* image = new QMimeData;
            image->setData("image/png", pngOf(40, 30));
            take(history, image);
            take(history, textData("recent one"));
            history.setPaused(true);
        }
        ClipboardHistory again;
        begin(again, path);
        QCOMPARE(labels(again), QStringList({"pinned one", "recent one", ""}));
        QVERIFY(again.paused());
        again.setPaused(false);
        take(again, new QMimeData);
        QCOMPARE(offered.size(), 1);
        QCOMPARE(offered[0]->data("text/plain"), QByteArray("recent one"));
        again.choose(idAt(again, 2));
        QCOMPARE(offered.size(), 2);
        QCOMPARE(QImage::fromData(offered[1]->data("image/png")).size(), QSize(40, 30));
    }

    void turning_remember_off_starts_with_only_pins()
    {
        const QString path = folder.path() + "/clipboard";
        {
            ClipboardHistory history;
            begin(history, path);
            take(history, textData("pinned one"));
            history.pin(idAt(history, 0), true);
            take(history, textData("passing"));
        }
        ClipboardHistory again;
        again.setRemember(false);
        begin(again, path);
        QCOMPARE(labels(again), QStringList({"pinned one"}));
    }

    void forgetting_on_restart_keeps_only_pins()
    {
        const QString path = folder.path() + "/clipboard";
        {
            ClipboardHistory history;
            history.setRemember(false);
            begin(history, path);
            take(history, textData("pinned one"));
            history.pin(idAt(history, 0), true);
            take(history, textData("passing"));
        }
        QFile file(path + "/history.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("passing"));
        ClipboardHistory again;
        again.setRemember(false);
        begin(again, path);
        QCOMPARE(labels(again), QStringList({"pinned one"}));
        QCOMPARE(QDir(path + "/blobs").entryList(QDir::Files).size(), 1);
    }

    void storage_is_private()
    {
        ClipboardHistory history;
        begin(history);
        take(history, textData("mine"));
        history.save();
        const QString path = folder.path() + "/clipboard";
        QCOMPARE(modeOf(path), 0700);
        QCOMPARE(modeOf(path + "/blobs"), 0700);
        QCOMPARE(modeOf(path + "/history.json"), 0600);
        const QFileInfoList blobs = QDir(path + "/blobs").entryInfoList(QDir::Files);
        QVERIFY(!blobs.isEmpty());
        for (const QFileInfo& blob : blobs)
            QCOMPARE(modeOf(blob.filePath()), 0600);
        auto* image = new QMimeData;
        image->setData("image/png", pngOf(20, 20));
        take(history, image);
        QCOMPARE(modeOf(path + "/thumbnails"), 0700);
        const QUrl thumbnail = history.data(history.index(0), ClipboardHistory::ThumbnailRole).toUrl();
        QCOMPARE(modeOf(thumbnail.toLocalFile()), 0600);
    }

    void a_payload_name_with_a_path_is_refused()
    {
        const QString path = folder.path() + "/clipboard";
        QDir().mkpath(path + "/blobs");
        QFile outside(folder.path() + "/outside");
        QVERIFY(outside.open(QIODevice::WriteOnly));
        outside.write("outside");
        outside.close();
        QFile file(path + "/history.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"entries":[{"id":"abc","kind":"text","label":"x","text":"x",)"
                   R"("formats":[{"mime":"text/plain","blob":"../../outside"}]}]})");
        file.close();
        ClipboardHistory history;
        begin(history, path);
        QCOMPARE(history.rowCount(), 0);
    }

    void a_broken_file_is_set_aside()
    {
        const QString path = folder.path() + "/clipboard";
        QDir().mkpath(path);
        QFile file(path + "/history.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not json");
        file.close();
        ClipboardHistory history;
        begin(history, path);
        QCOMPARE(history.rowCount(), 0);
        QVERIFY(QFile::exists(path + "/history.json.bad"));
        take(history, textData("fresh"));
        QCOMPARE(history.rowCount(), 1);
    }

    void an_entry_whose_payload_is_gone_is_dropped()
    {
        const QString path = folder.path() + "/clipboard";
        {
            ClipboardHistory history;
            begin(history, path);
            take(history, textData("lost"));
            take(history, textData("kept"));
        }
        const QStringList blobs = QDir(path + "/blobs").entryList(QDir::Files);
        QCOMPARE(blobs.size(), 2);
        QString lostBlob;
        for (const QString& name : blobs) {
            QFile blob(path + "/blobs/" + name);
            QVERIFY(blob.open(QIODevice::ReadOnly));
            if (blob.readAll() == "lost")
                lostBlob = name;
        }
        QVERIFY(QFile::remove(path + "/blobs/" + lostBlob));
        ClipboardHistory again;
        begin(again, path);
        QCOMPARE(labels(again), QStringList({"kept"}));
    }
};

QTEST_MAIN(ClipboardHistoryTest)
#include "test-clipboard-history.moc"
