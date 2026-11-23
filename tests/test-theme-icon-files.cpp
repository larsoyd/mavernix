// which file of a theme icon the shell draws at each size

#include <QGuiApplication>
#include <QIcon>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QtTest>

#include "icon-files.hpp"

class TestThemeIconFiles : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir folder;

    void write(const QString& place, const QString& text)
    {
        const QString path = folder.filePath(QStringLiteral("probe/") + place);
        QDir().mkpath(QFileInfo(path).path());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QTest::qFail("cannot write the theme", __FILE__, __LINE__);
            return;
        }
        file.write(text.toUtf8());
    }

    // a square of one colour, as a drawing
    void drawing(const QString& place, const char* colour)
    {
        write(place, QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
                                    "<rect width='16' height='16' fill='%1'/></svg>").arg(QLatin1String(colour)));
    }

    // the colour in the middle of what the shell draws
    static QString colour_at(const QString& name, int side)
    {
        const QImage image = ThemeIconProvider::picture(name, QSize(side, side));
        if (image.isNull())
        {
            return QStringLiteral("none");
        }
        return QColor(image.pixel(image.width() / 2, image.height() / 2)).name();
    }

  private slots:
    void initTestCase()
    {
        QVERIFY(folder.isValid());
        write(QStringLiteral("index.theme"),
            QStringLiteral("[Icon Theme]\nName=probe\nDirectories=22x22/places,64x64/places,scalable/apps\n\n"
                           "[22x22/places]\nSize=22\nType=Fixed\n\n[64x64/places]\nSize=64\nType=Fixed\n\n"
                           "[scalable/apps]\nSize=64\nType=Scalable\nMinSize=16\nMaxSize=512\n"));
        drawing(QStringLiteral("22x22/places/glyph.svg"), "#ff0000");
        drawing(QStringLiteral("64x64/places/glyph.svg"), "#0000ff");
        drawing(QStringLiteral("scalable/apps/whole.svg"), "#00ff00");
        QIcon::setThemeSearchPaths({folder.path()});
        QIcon::setFallbackThemeName(QString());
        QIcon::setThemeName(QStringLiteral("probe"));
    }

    void drawings_of_fixed_sizes_are_picked_by_size()
    {
        IconFiles files;
        QVERIFY(files.sourceFor(QStringLiteral("glyph"), 0).startsWith(QLatin1String("image://theme-icon/")));
        QCOMPARE(colour_at(QStringLiteral("glyph"), 22), QStringLiteral("#ff0000"));
        QCOMPARE(colour_at(QStringLiteral("glyph"), 64), QStringLiteral("#0000ff"));
        QCOMPARE(colour_at(QStringLiteral("glyph"), 128), QStringLiteral("#0000ff"));
        QCOMPARE(colour_at(QStringLiteral("glyph"), 40), QStringLiteral("#0000ff"));
        QCOMPARE(colour_at(QStringLiteral("glyph"), 16), QStringLiteral("#ff0000"));
    }

    void a_scalable_drawing_still_serves_every_size_as_one_file()
    {
        IconFiles files;
        QCOMPARE(files.sourceFor(QStringLiteral("whole"), 0),
            QStringLiteral("image://svg-icon/") + folder.filePath(QStringLiteral("probe/scalable/apps/whole.svg")));
        QCOMPARE(colour_at(QStringLiteral("whole"), 300), QStringLiteral("#00ff00"));
    }

    // one switch for the whole shell, so every place an icon shows follows it
    void every_lookup_hears_dates_switched_off()
    {
        IconFiles one;
        IconFiles other;
        QVERIFY(one.datePagesShown() && other.datePagesShown());
        QSignalSpy heard(&other, &IconFiles::datePagesShownChanged);
        one.setDatePagesShown(false);
        one.setDatePagesShown(false);
        QCOMPARE(heard.count(), 1);
        QVERIFY(!other.datePagesShown());
        other.setDatePagesShown(true);
        QCOMPARE(heard.count(), 2);
        QVERIFY(one.datePagesShown());
    }

    void a_page_is_known_by_its_name()
    {
        IconFiles files;
        QVERIFY(files.isDatePage(QStringLiteral("almanac")));
        QVERIFY(!files.isDatePage(QStringLiteral("whole")));
    }

    void the_day_is_todays()
    {
        IconFiles files;
        QCOMPARE(files.today(), QDate::currentDate().toString(Qt::ISODate));
    }
};

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    TestThemeIconFiles test;
    return QTest::qExec(&test, argc, argv);
}
#include "test-theme-icon-files.moc"
