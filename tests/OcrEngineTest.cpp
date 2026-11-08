#include <QtTest>
#include <QImage>
#include <QTemporaryDir>
#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>
#include <stdexcept>
#include <vector>

#include "OcrEngine.h"

class OcrEngineTest : public QObject {
    Q_OBJECT

private slots:
    void pixel_channels_data() {
        QTest::addColumn<int>("format");
        QTest::newRow("rgba") << int(QImage::Format_RGBA8888);
        QTest::newRow("argb") << int(QImage::Format_ARGB32);
        QTest::newRow("rgb-with-row-padding") << int(QImage::Format_RGB888);
    }

    void pixel_channels() {
        QFETCH(int, format);
        QImage image(3, 2, QImage::Format(format));
        const QList<QColor> colors{Qt::red, Qt::green, Qt::blue, Qt::black, Qt::white, QColor(17, 83, 149)};
        for (int i = 0; i < colors.size(); ++i)
            image.setPixelColor(i % image.width(), i / image.width(), colors[i]);
        const auto pix = showthis::ocr::OcrEngine::to_pix(image);
        QVERIFY(pix);
        QCOMPARE(pixGetWidth(pix.get()), image.width());
        QCOMPARE(pixGetHeight(pix.get()), image.height());
        for (int i = 0; i < colors.size(); ++i) {
            l_uint32 pixel;
            QCOMPARE(pixGetPixel(pix.get(), i % image.width(), i / image.width(), &pixel), 0);
            l_int32 red, green, blue;
            extractRGBValues(pixel, &red, &green, &blue);
            QCOMPARE(red, colors[i].red());
            QCOMPARE(green, colors[i].green());
            QCOMPARE(blue, colors[i].blue());
        }
    }

    void discovery_and_missing_data_do_not_require_english() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        // Discovery must not load any model (these intentionally are not models).
        for (const auto *name : {"deu.traineddata", "fra.traineddata", "osd.traineddata"}) {
            QFile file(directory.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("not a trained model");
        }
        showthis::ocr::OcrEngine engine({{}, directory.path()});
        QCOMPARE(engine.available_languages(), QStringList({"deu", "fra"}));
        QImage blank(100, 100, QImage::Format_RGB32);
        blank.fill(Qt::white);
        QVERIFY_EXCEPTION_THROWN(engine.scan_image(blank), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(engine.set_languages("deu"), std::runtime_error);
        QCOMPARE(engine.available_languages(), QStringList({"deu", "fra"}));

        QTemporaryDir empty;
        showthis::ocr::OcrEngine no_data({{}, empty.path()});
        QVERIFY(no_data.available_languages().isEmpty());
        QVERIFY_EXCEPTION_THROWN(no_data.set_languages(""), std::runtime_error);
    }

    void real_non_english_model_without_english_pack() {
        tesseract::TessBaseAPI discovery;
        QCOMPARE(discovery.Init(nullptr, ""), 0);
        const auto data_path = QString::fromLocal8Bit(discovery.GetDatapath());
        std::vector<std::string> languages;
        discovery.GetAvailableLanguagesAsVector(&languages);
        QString model;
        for (const auto &language : languages) {
            if (language != "eng" && language != "osd") {
                model = QString::fromStdString(language);
                break;
            }
        }
        if (model.isEmpty())
            QSKIP("No installed non-English tessdata; model-independent OCR tests still run.");
        QTemporaryDir directory;
        QVERIFY(QDir().mkpath(QFileInfo(directory.filePath(model)).absolutePath()));
        QVERIFY(QFile::link(QDir(data_path).filePath(model + ".traineddata"), directory.filePath(model + ".traineddata")));
        showthis::ocr::OcrEngine engine({{}, directory.path()});
        QCOMPARE(engine.available_languages(), QStringList{model});
        engine.set_languages(model);
        QImage blank(200, 100, QImage::Format_RGB32);
        blank.fill(Qt::white);
        QVERIFY(engine.scan_image(blank).isEmpty());
        // A failed language change must leave the working engine usable.
        QVERIFY_EXCEPTION_THROWN(engine.set_languages("missing"), std::runtime_error);
        QVERIFY(engine.scan_image(blank).isEmpty());
    }
};

QTEST_GUILESS_MAIN(OcrEngineTest)
#include "OcrEngineTest.moc"
