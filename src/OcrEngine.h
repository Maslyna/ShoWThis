#pragma once

#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>

#include <QImage>
#include <QRect>
#include <QString>

namespace showthis {

namespace ocr {

struct WordBox {
    QString text;
    QRect rect;
};

struct OcrEngineConfig {
    QString langs;
};

struct PixDeleter {
    void operator()(PIX *pix) const noexcept {
        pixDestroy(&pix);
    }
};

using PixPtr = std::unique_ptr<PIX, PixDeleter>;

class OcrEngine {
public:
    explicit OcrEngine(OcrEngineConfig config)
        : m_config(std::move(config)), m_api(std::make_unique<tesseract::TessBaseAPI>()) {
        const auto langs = m_config.langs.toStdString();

        if (m_api->Init(nullptr, langs.c_str()) != 0) {
            throw std::runtime_error("Could'nt initialize Tesseract for language: " + langs);
        }
    }

    ~OcrEngine() {
        if (m_api) {
            m_api->End();
        }
    }

    static PixPtr to_pix(const QImage &img) {
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

    static QRect qrect_from_tesseract_coord(int x1, int y1, int x2, int y2) {
        return QRect(x1, y1, x2 - x1, y2 - y1);
    }

    QList<WordBox> scan_image(const QImage &img) {
        PixPtr pix = to_pix(img);
        m_api->SetImage(pix.get());

        QList<WordBox> res;

        if (m_api->Recognize(nullptr) != 0) {
            throw std::runtime_error("OCR recognition failed.");
        }

        // NOTE: 'it' is freed by api.End(); don't delete
        tesseract::ResultIterator *it = m_api->GetIterator();
        if (it) {
            do {
                if (char *text = it->GetUTF8Text(tesseract::RIL_WORD)) {
                    int x1, y1, x2, y2;
                    it->BoundingBox(tesseract::RIL_WORD, &x1, &y1, &x2, &y2);

                    res.append({
                        QString::fromUtf8(text),
                        qrect_from_tesseract_coord(x1, y1, x2, y2),
                    });

                    delete[] text;
                }
            } while (it->Next(tesseract::RIL_WORD));
        }

        return res;
    }

private:
    OcrEngineConfig m_config;
    std::unique_ptr<tesseract::TessBaseAPI> m_api;
};

}  // namespace ocr
}  // namespace showthis
