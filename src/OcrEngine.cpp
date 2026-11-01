#include "OcrEngine.h"

#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace showthis {

namespace ocr {

void PixDeleter::operator()(PIX *pix) const noexcept {
    pixDestroy(&pix);
}

OcrEngine::OcrEngine(OcrEngineConfig config)
    : m_config(std::move(config)), m_api(std::make_unique<tesseract::TessBaseAPI>()) {
    const auto langs = m_config.langs.toStdString();

    if (m_api->Init(nullptr, langs.c_str()) != 0) {
        throw std::runtime_error("Could'nt initialize Tesseract for language: " + langs);
    }
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

        // assuming that RGBA8888 is 32 bits long
        std::memcpy(dst, line, rgba8888.width() * 4);
    }

    return pix;
}

QStringList OcrEngine::available_languages() const {
    std::vector<std::string> languages;
    m_api->GetAvailableLanguagesAsVector(&languages);
    QStringList result;
    for (const auto &language : languages) {
        if (language != "osd")
            result.append(QString::fromStdString(language));
    }
    result.sort();
    return result;
}

void OcrEngine::set_languages(const QString &languages) {
    if (m_config.langs == languages)
        return;
    auto api = std::make_unique<tesseract::TessBaseAPI>();
    if (api->Init(nullptr, languages.toUtf8().constData()) != 0)
        throw std::runtime_error("Could not initialize Tesseract for language: " + languages.toStdString());
    m_api = std::move(api);
    m_config.langs = languages;
}

QRect OcrEngine::qrect_from_tesseract_coord(int x1, int y1, int x2, int y2) {
    return QRect(x1, y1, x2 - x1, y2 - y1);
}

QList<WordBox> OcrEngine::scan_image(const QImage &img) {
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
