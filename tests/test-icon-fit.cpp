// where a program's own icon lands on its square, by the proportions of the
// theme's art, 118 of 128 long, 66 percent cover at most, centred on y 63

#include <QPainter>
#include <QtTest>

#include "icon-fit.hpp"

class TestIconFit : public QObject
{
    Q_OBJECT

  private:
    static QImage canvas(int side)
    {
        QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        return image;
    }

    // an outline, so its cover stays far under the limit
    static void frame(QImage& image, const QRect& box, int alpha = 255)
    {
        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(box, QColor(200, 30, 30, alpha));
        painter.fillRect(box.adjusted(4, 4, -4, -4), Qt::transparent);
    }

    static void solid(QImage& image, const QRect& box, int alpha = 255)
    {
        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(box, QColor(200, 30, 30, alpha));
    }

    static QRectF placed(const IconFit& fit, const QRect& box)
    {
        return QRectF(fit.corner + QPointF(box.topLeft()) * fit.scale, QSizeF(box.size()) * fit.scale);
    }

    static void expect_near(double got, double wanted, const char* what)
    {
        if (qAbs(got - wanted) > 0.01)
        {
            QFAIL(qPrintable(QStringLiteral("%1 got=%2 wanted=%3").arg(what).arg(got).arg(wanted)));
        }
    }

    static void expect_centred(const IconFit& fit, const QRect& box, int side)
    {
        const QRectF at = placed(fit, box);
        expect_near(at.center().x(), side / 2.0, "centre x");
        expect_near(at.center().y(), side * 63.0 / 128, "centre y");
    }

  private slots:
    void small_art_is_enlarged()
    {
        QImage image = canvas(128);
        const QRect box(34, 34, 60, 60);
        frame(image, box);
        const IconFit fit = fit_icon(image, 128);
        expect_near(fit.scale, 118.0 / 60, "scale");
        expect_centred(fit, box, 128);
    }

    void oversized_art_is_shrunk()
    {
        QImage image = canvas(128);
        const QRect box(0, 0, 128, 128);
        frame(image, box);
        const IconFit fit = fit_icon(image, 128);
        expect_near(fit.scale, 118.0 / 128, "scale");
        expect_centred(fit, box, 128);
    }

    void art_off_centre_is_brought_back()
    {
        QImage image = canvas(128);
        const QRect box(2, 60, 50, 60);
        frame(image, box);
        const IconFit fit = fit_icon(image, 128);
        expect_near(fit.scale, 118.0 / 60, "scale");
        expect_centred(fit, box, 128);
    }

    void longest_side_decides()
    {
        QImage image = canvas(128);
        const QRect box(44, 4, 40, 118);
        frame(image, box);
        const IconFit fit = fit_icon(image, 128);
        expect_near(fit.scale, 1, "scale");
        expect_centred(fit, box, 128);
    }

    void faint_shadow_is_not_art()
    {
        QImage image = canvas(128);
        solid(image, QRect(0, 0, 128, 128), 127);
        const QRect box(24, 24, 80, 80);
        frame(image, box);
        const IconFit fit = fit_icon(image, 128);
        expect_near(fit.scale, 118.0 / 80, "scale");
        expect_centred(fit, box, 128);
    }

    void half_opaque_counts_as_art()
    {
        QImage image = canvas(128);
        const QRect box(24, 24, 80, 80);
        frame(image, box, 128);
        expect_near(fit_icon(image, 128).scale, 118.0 / 80, "scale");
    }

    void heavy_art_stops_at_the_cover_limit()
    {
        QImage image = canvas(128);
        const QRect box(14, 14, 100, 100);
        solid(image, box);
        const IconFit fit = fit_icon(image, 128);
        const double side = 100 * fit.scale;
        expect_near(side * side / (128.0 * 128), 0.66, "cover");
        expect_centred(fit, box, 128);
    }

    void art_just_under_the_limit_keeps_its_length()
    {
        QImage image = canvas(128);
        const QRect box(4, 44, 118, 40);
        solid(image, box);
        expect_near(fit_icon(image, 128).scale, 1, "scale");
    }

    void same_answer_at_any_size()
    {
        QImage large = canvas(256);
        QImage small = canvas(64);
        frame(large, QRect(68, 68, 120, 120));
        frame(small, QRect(17, 17, 30, 30));
        const IconFit at_large = fit_icon(large, 256);
        const IconFit at_small = fit_icon(small, 64);
        expect_near(at_large.scale, at_small.scale, "scale");
        expect_centred(at_small, QRect(17, 17, 30, 30), 64);
    }

    void picture_shorter_than_the_square_still_lands_on_it()
    {
        QImage wide(128, 64, QImage::Format_ARGB32_Premultiplied);
        wide.fill(Qt::transparent);
        const QRect box(0, 0, 128, 64);
        frame(wide, box);
        const IconFit fit = fit_icon(wide, 128);
        expect_near(fit.scale, 118.0 / 128, "scale");
        expect_centred(fit, box, 128);
    }

    void nothing_solid_is_left_alone()
    {
        QImage image = canvas(128);
        solid(image, QRect(0, 0, 128, 128), 100);
        QCOMPARE(fit_icon(image, 128).scale, 0.0);
    }

    void empty_picture_is_left_alone()
    {
        QCOMPARE(fit_icon(QImage(), 128).scale, 0.0);
    }
};

QTEST_APPLESS_MAIN(TestIconFit)
#include "test-icon-fit.moc"
