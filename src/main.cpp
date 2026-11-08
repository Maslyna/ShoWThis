#include <LayerShellQt/Window>

#include <QApplication>
#include <QDBusConnection>
#include <QDebug>
#include <QFile>
#include <QMenu>
#include <QString>
#include <QSystemTrayIcon>
#include <QtLogging>
#include <exception>

#include "GlobalShortcutPortal.h"
#include "AppSettings.h"
#include "KWinScreenCaptureBackend.h"
#include "ShoWSurface.h"
#include "ShoWThisConstants.h"
#include "OverlayController.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("maslyna"));
    app.setOrganizationDomain(QStringLiteral("maslyna.dev"));
    app.setApplicationName(QStringLiteral("ShoWThis"));
    app.setDesktopFileName(showthis::APP_ID);
    app.setQuitOnLastWindowClosed(false);
    showthis::AppSettings settings;

    QDBusConnection bus = QDBusConnection::sessionBus();

    if (!bus.registerService(showthis::APP_ID)) {
        auto activate = QDBusMessage::createMethodCall(showthis::APP_ID, "/dev/maslyna/ShoWThis",
            "dev.maslyna.ShoWThis", "show_notebook");
        bus.call(activate, QDBus::NoBlock);
        return 0;
    }

    showthis::ocr::OcrEngine ocr_engine;
    showthis::KWinScreenCaptureBackend backend;
    showthis::GlobalShortcutPortal portal;
    showthis::ShoWSurface w;

    if (w.status() == QQuickView::Error)
        return 1;
    QStringList languages;
    try {
        languages = ocr_engine.available_languages();
    } catch (const std::exception &error) {
        qWarning() << "OCR language discovery failed:" << error.what();
        w.set_error(QObject::tr("Cannot find OCR languages. Install Tesseract language data or check TESSDATA_PREFIX."));
    }
    w.set_languages(languages);
    w.set_language(settings.ocr_language(languages));
    if (languages.isEmpty() && w.error().isEmpty())
        w.set_error(QObject::tr("No OCR language data installed. Install a Tesseract language pack to enable screen recognition."));

    w.setFlags(Qt::FramelessWindowHint);

    auto *layerShell = LayerShellQt::Window::get(&w);

    layerShell->setLayer(LayerShellQt::Window::LayerOverlay);

    layerShell->setAnchors(static_cast<LayerShellQt::Window::Anchors>(
        LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
        LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));

    layerShell->setMargins(QMargins{0, 0, 0, 0});

    layerShell->setExclusiveZone(-1);

    layerShell->setDesiredSize(QSize{0, 0});

    // The full-screen overlay needs Escape immediately, without requiring a
    // mouse click first. Hiding the layer releases keyboard focus to the desktop.
    layerShell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);

    layerShell->setWantsToBeOnActiveScreen(true);

    showthis::OverlayController controller(w, backend,
        [&ocr_engine](const QImage &image, const QString &language) {
            ocr_engine.set_languages(language);
            return showthis::ScanResult{ocr_engine.scan_image(image), {}};
        });
    controller.set_cancel_handler([&ocr_engine]() { ocr_engine.request_cancel(); });
    bus.registerObject("/dev/maslyna/ShoWThis", "dev.maslyna.ShoWThis", &controller, QDBusConnection::ExportScriptableSlots);

    QSystemTrayIcon tray(QIcon::fromTheme("edit-find"));
    QMenu menu;
    QAction *capture_action = menu.addAction("Capture screen");
    QObject::connect(capture_action, &QAction::triggered, &controller, &showthis::OverlayController::toggle_overlay);
    QAction *retry_action = menu.addAction("Retry shortcut setup");
    QObject::connect(retry_action, &QAction::triggered, &portal, &showthis::GlobalShortcutPortal::start);
    QObject::connect(&portal, &showthis::GlobalShortcutPortal::state_changed, &app, [&]() {
        retry_action->setEnabled(!portal.ready());
        if (!portal.error().isEmpty()) {
            w.set_error(portal.error());
            tray.showMessage("ShoWThis shortcut", portal.error(), QSystemTrayIcon::Warning);
        }
    });
    QAction *vocabulary_action = menu.addAction("My vocabulary");
    QObject::connect(vocabulary_action, &QAction::triggered,
        &controller, &showthis::OverlayController::show_notebook);
    QAction *quitAction = menu.addAction("Quit");

    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
    tray.setContextMenu(&menu);
    tray.setToolTip("Screen Translator (running)");
    tray.show();

    QObject::connect(&w, &showthis::ShoWSurface::selectedLanguageChanged, &app, [&]() {
        settings.set_ocr_language(w.language());
    });

    // Don't let the engine be destroyed under a running scan.
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
        &controller, &showthis::OverlayController::wait_for_finished);

    QObject::connect(&portal, &showthis::GlobalShortcutPortal::shortcut_activated, [&](const QString &id) {
        if (id == QStringLiteral("toggle-window"))
            controller.toggle_overlay();
    });
    portal.start();

    w.show();
    w.setVisible(false);
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        controller.show_notebook();
    return QApplication::exec();
}
