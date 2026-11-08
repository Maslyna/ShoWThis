#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <QList>
#include <memory>
#include <atomic>

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
    QString data_path;
    int timeout_ms = 15000;
};

struct PixDeleter {
    void operator()(Pix *pix) const noexcept;
};

using PixPtr = std::unique_ptr<Pix, PixDeleter>;

class OcrEngine {
public:
    explicit OcrEngine(OcrEngineConfig config = {});

    ~OcrEngine();

    static PixPtr to_pix(const QImage &img);

    static QRect qrect_from_tesseract_coord(int x1, int y1, int x2, int y2);

    QList<WordBox> scan_image(const QImage &img);
    QStringList available_languages() const;
    void set_languages(const QString &languages);
    void request_cancel() noexcept { m_cancelled.store(true); }

private:
    OcrEngineConfig m_config;
    std::unique_ptr<tesseract::TessBaseAPI> m_api;
    std::atomic_bool m_cancelled{false};
};

}  // namespace ocr
}  // namespace showthis
