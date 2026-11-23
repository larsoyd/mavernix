// mavernix-thumbnail FILE OUT: writes a preview of FILE to OUT as the thumbnail spec's png
// run by the shell as its own process, so a thumbnailer that crashes takes only this down

#include <KFileItem>
#include <KIO/PreviewJob>

#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <cstdio>

static constexpr int large_side = 256;

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 3)
    {
        std::fprintf(stderr, "[thumbnail] refused: usage mavernix-thumbnail FILE OUT\n");
        return 2;
    }
    const QFileInfo file(arguments.at(1));
    const QString out = arguments.at(2);
    const QUrl url = QUrl::fromLocalFile(file.absoluteFilePath());

    // a folder's preview shows what it holds, which finder never does
    QStringList plugins = KIO::PreviewJob::availablePlugins();
    plugins.removeAll(QStringLiteral("directorythumbnail"));
    auto* job = new KIO::PreviewJob(KFileItemList{KFileItem(url)}, QSize(large_side, large_side), &plugins);
    job->setScaleType(KIO::PreviewJob::Scaled);

    int result = 1;
    QObject::connect(job, &KIO::PreviewJob::generated, [&](const KFileItem&, const QImage& preview) {
        QImage picture = preview;
        picture.setText(QStringLiteral("Thumb::URI"), QString::fromUtf8(url.toEncoded()));
        picture.setText(QStringLiteral("Thumb::MTime"), QString::number(file.lastModified().toSecsSinceEpoch()));
        picture.setText(QStringLiteral("Software"), QStringLiteral("mavernix-thumbnail"));
        QSaveFile saved(out);
        if (saved.open(QIODevice::WriteOnly) && picture.save(&saved, "PNG") && saved.commit())
        {
            result = 0;
        }
        else
        {
            std::fprintf(stderr, "[thumbnail] refused: cannot write %s\n", qPrintable(out));
        }
    });
    QObject::connect(job, &KIO::PreviewJob::failed, [&](const KFileItem&) {
        std::fprintf(stderr, "[thumbnail] none for %s\n", qPrintable(file.absoluteFilePath()));
    });
    QObject::connect(job, &KJob::result, &app, &QGuiApplication::quit);
    QTimer::singleShot(10000, &app, [&] {
        std::fprintf(stderr, "[thumbnail] refused: no answer in 10 s for %s\n", qPrintable(file.absoluteFilePath()));
        app.quit();
    });
    app.exec();
    return result;
}
