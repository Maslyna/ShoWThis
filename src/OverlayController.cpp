#include "OverlayController.h"

#include <QDebug>
#include <QQuickItem>
#include <QtConcurrent/QtConcurrentRun>
#include <exception>

#include "AScreenCaptureBackend.h"
#include "ShoWSurface.h"

namespace showthis {

OverlayController::OverlayController(ShoWSurface &surface, AScreenCaptureBackend &backend,
                                     Scanner scanner, QObject *parent)
    : QObject(parent), m_surface(surface), m_backend(backend), m_scanner(std::move(scanner)) {
    connect(&m_backend, &AScreenCaptureBackend::captured, this, &OverlayController::captured);
    connect(&m_backend, &AScreenCaptureBackend::capture_failed, this, &OverlayController::capture_failed);
    connect(&m_surface, &ShoWSurface::hide_requested, this, &OverlayController::hide_overlay);
    connect(&m_surface, &QWindow::visibleChanged, this, [this](bool visible) {
        // Also honor native hides and restoration after the standard save dialog.
        m_wants_visible = visible;
    });
    connect(&m_watcher, &QFutureWatcherBase::finished, this, &OverlayController::scan_finished);
    connect(&m_surface, &ShoWSurface::selectedLanguageChanged, this, [this]() {
        m_rescan_pending = true;
        if (!m_capture_pending)
            start_scan();
    });
}

OverlayController::~OverlayController() {
    wait_for_finished();
}

void OverlayController::toggle_overlay() {
    if (m_surface.export_dialog_open()) {
        m_surface.raise_export_dialog();
        return;
    }
    if (m_wants_visible || m_surface.isVisible()) {
        hide_overlay();
        return;
    }
    m_wants_visible = true;
    if (m_capture_pending)
        return; // The existing capture will honor the latest visibility request.
    if (m_scan_running && !m_screenshot.isNull()) {
        // Reopening during OCR is instant; do not enqueue another capture/scan.
        m_surface.show();
        return;
    }
    m_capture_pending = true;
    m_surface.set_error({});
    m_backend.capture();
}

void OverlayController::hide_overlay() {
    m_wants_visible = false;
    m_surface.suppress_export_restore();
    m_surface.hide();
}

void OverlayController::show_notebook() {
    if (m_surface.export_dialog_open()) {
        m_surface.raise_export_dialog();
        return;
    }
    m_wants_visible = true;
    m_surface.show();
    QMetaObject::invokeMethod(m_surface.rootObject(), "open_vocabulary");
}

void OverlayController::captured(const QImage &image) {
    if (!m_capture_pending)
        return;
    m_capture_pending = false;
    if (!m_wants_visible)
        return; // A hidden overlay must not reappear when a slow capture finishes.
    if (image.isNull()) {
        capture_failed(tr("Screen capture returned an empty image."));
        return;
    }
    m_screenshot = image; // Implicit sharing: no full-screen deep copy on the UI thread.
    m_surface.set_image(m_screenshot);
    m_rescan_pending = true;
    start_scan();
    m_surface.show();
}

void OverlayController::capture_failed(const QString &reason) {
    m_capture_pending = false;
    qWarning() << "Capture failed:" << reason;
    m_surface.set_error(reason);
    // A transient capture failure is recoverable, not an application exit.
    if (m_wants_visible)
        m_surface.show();
}

void OverlayController::start_scan() {
    if (m_scan_running || m_screenshot.isNull())
        return;
    m_rescan_pending = false;
    m_scan_running = true;
    m_scan_language = m_surface.language();
    m_surface.set_scanning(true);
    m_surface.set_error({});
    m_surface.set_words({});
    m_watcher.setFuture(QtConcurrent::run([scanner = m_scanner, image = m_screenshot,
                                         language = m_scan_language]() {
        try {
            return scanner(image, language);
        } catch (const std::exception &error) {
            return ScanResult{{}, QString::fromUtf8(error.what())};
        } catch (...) {
            return ScanResult{{}, QObject::tr("OCR failed unexpectedly.")};
        }
    }));
}

void OverlayController::scan_finished() {
    m_scan_running = false;
    const auto result = m_watcher.result();
    if (m_scan_language == m_surface.language() && !m_rescan_pending) {
        m_surface.set_words(result.words);
        m_surface.set_error(result.error);
    }
    if (m_rescan_pending && !m_capture_pending) {
        start_scan(); // Coalesce language changes into one scan of the latest selection.
        return;
    }
    m_surface.set_scanning(false);
    // Visibility is deliberately not changed here: the user's latest intent wins.
}

void OverlayController::wait_for_finished() {
    m_watcher.waitForFinished();
}

}  // namespace showthis
