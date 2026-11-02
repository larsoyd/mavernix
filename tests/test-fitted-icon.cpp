// the picture the dock gets for a program's own icon, placed by its art and
// drawn at the final size, from a drawing and from a picture file

#include <QTemporaryDir>
#include <QtTest>

#include "icon-files.hpp"

static const int art_alpha = 128;

class TestFittedIcon : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir folder;

    QString written_drawing(const QString& body)
    {
        const QString path = folder.filePath(QStringLiteral("icon.svg"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            qFatal("cannot write the drawing");
        }
        file.write(QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 128 128'>%1</svg>")
                       .arg(body)
                       .toUtf8());
        return path;
    }

    static QImage fitted(const QString& path, int side = 128)
    {
        FittedIconProvider provider;
        QSize reported;
        const QImage image = provider.requestImage(QStringLiteral("0/") + path, &reported, QSize(side, side));
        if (reported != image.size())
        {
            qFatal("the provider reported a size other than the image's");
        }
        return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    static QRect art_of(const QImage& image)
    {
        QRect art;
        for (int y = 0; y < image.height(); y++)
        {
            for (int x = 0; x < image.width(); x++)
            {
                if (qAlpha(image.pixel(x, y)) >= art_alpha)
                {
                    art |= QRect(x, y, 1, 1);
                }
            }
        }
        return art;
    }

    static void expect_near(double got, double wanted, const char* what)
    {
        if (qAbs(got - wanted) > 1.01)
        {
            QFAIL(qPrintable(QStringLiteral("%1 got=%2 wanted=%3").arg(what).arg(got).arg(wanted)));
        }
    }

  private slots:
    void initTestCase()
    {
        QVERIFY(folder.isValid());
    }

    void small_drawing_is_enlarged_and_centred()
    {
        const QString ring = QStringLiteral(
            "<rect x='10' y='70' width='50' height='50' fill='none' stroke='red' stroke-width='2'/>");
        const QImage image = fitted(written_drawing(ring));
        QCOMPARE(image.size(), QSize(128, 128));
        const QRect art = art_of(image);
        expect_near(art.width(), 118, "width");
        expect_near(art.height(), 118, "height");
        expect_near(QRectF(art).center().x(), 64, "centre x");
        expect_near(QRectF(art).center().y(), 63, "centre y");
    }

    // an enlarged bitmap would smear this hairline over several pixels
    void enlarged_drawing_stays_sharp()
    {
        const QString line = QStringLiteral(
            "<rect x='44' y='44' width='40' height='40' fill='none' stroke='black' stroke-width='0.68'/>");
        const QImage image = fitted(written_drawing(line));
        const int y = 63;
        int inked = 0;
        for (int x = 0; x < 16; x++)
        {
            inked += qAlpha(image.pixel(x, y)) > 8 ? 1 : 0;
        }
        QVERIFY2(inked > 0 && inked <= 3, qPrintable(QStringLiteral("inked=%1").arg(inked)));
        QVERIFY2(qAlpha(image.pixel(5, y)) + qAlpha(image.pixel(4, y)) + qAlpha(image.pixel(6, y)) >= 200,
                 "the hairline lost its strength");
    }

    void faint_shadow_stays_outside_the_measure()
    {
        const QString shape = QStringLiteral(
            "<rect x='0' y='0' width='128' height='128' fill='black' fill-opacity='0.2'/>"
            "<rect x='24' y='24' width='80' height='80' fill='none' stroke='red' stroke-width='4'/>");
        const QRect art = art_of(fitted(written_drawing(shape)));
        expect_near(art.width(), 118, "width");
    }

    void heavy_drawing_is_kept_smaller()
    {
        const QString block = QStringLiteral("<rect x='14' y='14' width='100' height='100' fill='red'/>");
        const QRect art = art_of(fitted(written_drawing(block)));
        expect_near(art.width(), 104, "width");
        expect_near(art.height(), 104, "height");
        expect_near(QRectF(art).center().y(), 63, "centre y");
    }

    void picture_file_is_fitted_too()
    {
        QImage source(64, 64, QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::transparent);
        for (int i = 8; i < 40; i++)
        {
            source.setPixelColor(i, 8, Qt::red);
            source.setPixelColor(i, 39, Qt::red);
            source.setPixelColor(8, i, Qt::red);
            source.setPixelColor(39, i, Qt::red);
        }
        const QString path = folder.filePath(QStringLiteral("icon.png"));
        QVERIFY(source.save(path));
        const QRect art = art_of(fitted(path));
        expect_near(art.width(), 118, "width");
        expect_near(QRectF(art).center().x(), 64, "centre x");
        expect_near(QRectF(art).center().y(), 63, "centre y");
    }

    void any_size_keeps_the_proportions()
    {
        const QString ring = QStringLiteral(
            "<rect x='10' y='70' width='50' height='50' fill='none' stroke='red' stroke-width='2'/>");
        const QImage image = fitted(written_drawing(ring), 256);
        QCOMPARE(image.size(), QSize(256, 256));
        const QRect art = art_of(image);
        expect_near(art.width(), 236, "width");
        expect_near(QRectF(art).center().y(), 126, "centre y");
    }

    void drawing_with_nothing_solid_comes_back_as_it_is()
    {
        const QString mist = QStringLiteral("<rect x='0' y='0' width='128' height='128' fill='red' fill-opacity='0.2'/>");
        const QImage image = fitted(written_drawing(mist));
        QCOMPARE(image.size(), QSize(128, 128));
        QVERIFY(qAlpha(image.pixel(64, 64)) > 40);
    }
};

QTEST_GUILESS_MAIN(TestFittedIcon)
#include "test-fitted-icon.moc"
