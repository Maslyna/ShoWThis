#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <QList>
#include <memory>

class Pix;
class QImage;

namespace tesseract {
class TessBaseAPI;
}

namespace showthis {

namespace ocr {

struct WordBox {
    QString text;
    QRect rect;
    QString context;
};

struct OcrEngineConfig {
    QString langs;
};

struct PixDeleter {
    void operator()(Pix *pix) const noexcept;
};

using PixPtr = std::unique_ptr<Pix, PixDeleter>;

class OcrEngine {
public:
    explicit OcrEngine(OcrEngineConfig config);

    ~OcrEngine();

    static PixPtr to_pix(const QImage &img);

    static QRect qrect_from_tesseract_coord(int x1, int y1, int x2, int y2);

    QList<WordBox> scan_image(const QImage &img);
    QStringList available_languages() const;
    void set_languages(const QString &languages);

private:
    OcrEngineConfig m_config;
    std::unique_ptr<tesseract::TessBaseAPI> m_api;
};

}  // namespace ocr
}  // namespace showthis
