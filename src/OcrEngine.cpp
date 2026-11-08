#include "OcrEngine.h"

#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>
#include <stdexcept>
#include <vector>

namespace showthis {

namespace ocr {

void PixDeleter::operator()(PIX *pix) const noexcept {
    pixDestroy(&pix);
}

OcrEngine::OcrEngine(OcrEngineConfig config)
    : m_config{{}, config.data_path}, m_api(std::make_unique<tesseract::TessBaseAPI>()) {
    // Language discovery and notebook startup do not require a loaded model.
    if (!config.langs.isEmpty())
        set_languages(config.langs);
}

OcrEngine::~OcrEngine() {
    if (m_api) {
        m_api->End();
    }
}

PixPtr OcrEngine::to_pix(const QImage &img) {
    QImage rgba8888 = img.convertToFormat(QImage::Format_RGBA8888);
    PixPtr pix{pixCreate(rgba8888.width(), rgba8888.height(), 32)};

    if (!pix)
        throw std::runtime_error("Failed to create PIX image");

    for (int y = 0; y < rgba8888.height(); ++y) {
        const uchar *line = rgba8888.constScanLine(y);
        auto *dst         = pixGetData(pix.get()) + y * pixGetWpl(pix.get());

        // Leptonica stores numeric 0xRRGGBB00 pixels, not RGBA byte arrays.
        // Explicit packing is correct on both little- and big-endian hosts.
        for (int x = 0; x < rgba8888.width(); ++x)
            composeRGBPixel(line[4 * x], line[4 * x + 1], line[4 * x + 2], &dst[x]);
    }

    return pix;
}

QStringList OcrEngine::available_languages() const {
    tesseract::TessBaseAPI catalogue;
    const auto path = m_config.data_path.toLocal8Bit();
    // An explicitly empty language initializes the data path without loading
    // recognition models (as used by Tesseract's --list-langs mode).
    if (catalogue.Init(path.isEmpty() ? nullptr : path.constData(), "") != 0)
        throw std::runtime_error("Cannot discover OCR languages. Check TESSDATA_PREFIX and installed Tesseract language data.");
    std::vector<std::string> languages;
    catalogue.GetAvailableLanguagesAsVector(&languages);
    QStringList result;
    for (const auto &language : languages) {
        if (language != "osd")
            result.append(QString::fromStdString(language));
    }
    result.sort();
    return result;
}

void OcrEngine::set_languages(const QString &languages) {
    if (languages.trimmed().isEmpty())
        throw std::runtime_error("No OCR language selected. Install Tesseract language data and choose an OCR language.");
    if (m_config.langs == languages)
        return;
    auto api = std::make_unique<tesseract::TessBaseAPI>();
    const auto path = m_config.data_path.toLocal8Bit();
    if (api->Init(path.isEmpty() ? nullptr : path.constData(), languages.toUtf8().constData()) != 0)
        throw std::runtime_error("Could not initialize Tesseract for language: " + languages.toStdString());
    m_api = std::move(api);
    m_config.langs = languages;
}

QRect OcrEngine::qrect_from_tesseract_coord(int x1, int y1, int x2, int y2) {
    return QRect(x1, y1, x2 - x1, y2 - y1);
}

QList<WordBox> OcrEngine::scan_image(const QImage &img) {
    if (m_config.langs.isEmpty())
        throw std::runtime_error("No OCR language selected. Install Tesseract language data and choose an OCR language.");
    PixPtr pix = to_pix(img);
    m_api->SetImage(pix.get());

    QList<WordBox> res;

    if (m_api->Recognize(nullptr) != 0) {
        throw std::runtime_error("OCR recognition failed.");
    }

    std::unique_ptr<tesseract::ResultIterator> it(m_api->GetIterator());
    if (it) {
        QString context;
        do {
            if (it->IsAtBeginningOf(tesseract::RIL_TEXTLINE)) {
                std::unique_ptr<char[]> line(it->GetUTF8Text(tesseract::RIL_TEXTLINE));
                context = line ? QString::fromUtf8(line.get()).simplified() : QString{};
            }
            if (char *text = it->GetUTF8Text(tesseract::RIL_WORD)) {
                int x1, y1, x2, y2;
                it->BoundingBox(tesseract::RIL_WORD, &x1, &y1, &x2, &y2);
                const auto word_text = QString::fromUtf8(text).trimmed();
                res.append({
                    word_text,
                    qrect_from_tesseract_coord(x1, y1, x2, y2),
                    context,
                });

                delete[] text;
            }
        } while (it->Next(tesseract::RIL_WORD));
    }

    return res;
}

}  // namespace ocr
}  // namespace showthis
