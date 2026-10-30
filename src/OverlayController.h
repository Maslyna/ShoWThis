#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <functional>

#include "OcrEngine.h"

namespace showthis {

class AScreenCaptureBackend;
class ShoWSurface;

struct ScanResult {
    QList<ocr::WordBox> words;
    QString error;
};

// Capture, OCR, and requested visibility are separate states. A slow job must
// never prevent hiding the overlay or override a later hide request.
class OverlayController : public QObject {
    Q_OBJECT

public:
    using Scanner = std::function<ScanResult(const QImage &, const QString &)>;
    OverlayController(ShoWSurface &surface, AScreenCaptureBackend &backend,
                      Scanner scanner, QObject *parent = nullptr);
    ~OverlayController() override;

public slots:
    void toggle_overlay();
    void hide_overlay();
    void show_notebook();
    void wait_for_finished();

private:
    void captured(const QImage &image);
    void capture_failed(const QString &reason);
    void start_scan();
    void scan_finished();

    ShoWSurface &m_surface;
    AScreenCaptureBackend &m_backend;
    Scanner m_scanner;
    QFutureWatcher<ScanResult> m_watcher;
    QImage m_screenshot;
    QString m_scan_language;
    bool m_wants_visible = false;
    bool m_capture_pending = false;
    bool m_scan_running = false;
    bool m_rescan_pending = false;
};

}  // namespace showthis
