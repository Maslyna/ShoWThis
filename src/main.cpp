#include <LayerShellQt/Window>

#include <QApplication>
#include <QDBusConnection>
#include <QDebug>
#include <QFile>
#include <QMenu>
#include <QString>
#include <QSystemTrayIcon>
#include <QtLogging>

#include "GlobalShortcutPortal.h"
#include "AppSettings.h"
#include "KWinScreenCaptureBackend.h"
#include "ShoWSurface.h"
#include "ShoWThisConstants.h"
#include "OverlayController.h"

void register_host_app() {
    // The Registry interface only exists for unsandboxed ("host") apps.
    // Under Flatpak the app id is already known via .flatpak-info, and
    // calling Register() there is unsupported/unnecessary.
    if (QFile::exists(QStringLiteral("/.flatpak-info")))
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(showthis::DESKTOP_PORTAL_SERVICE,
                                                       showthis::DESKTOP_PORTAL_PATH,
                                                       showthis::REGISTRY_IFACE,
                                                       "Register");
    call << showthis::APP_ID << QVariantMap{};

    QDBusMessage reply = QDBusConnection::sessionBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        // Not fatal on older xdg-desktop-portal (<1.20) where the
        // interface doesn't exist yet, but on 1.21+ this means
        // GlobalShortcuts::CreateSession will fail right after.
        qWarning() << "Registry.Register failed:" << reply.errorMessage();
    }
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("maslyna"));
    app.setOrganizationDomain(QStringLiteral("maslyna.dev"));
    app.setApplicationName(QStringLiteral("ShoWThis"));
    app.setQuitOnLastWindowClosed(false);
    showthis::AppSettings settings;

    QDBusConnection bus = QDBusConnection::sessionBus();

    register_host_app();

    if (!bus.registerService(showthis::APP_ID)) {
        qWarning("Failed to register the service's name. Probably, another "
                 "instance is already running");
        return -1;
    }

    showthis::ocr::OcrEngine ocr_engine{{"eng"}};
    showthis::KWinScreenCaptureBackend backend;
    showthis::GlobalShortcutPortal portal;
    showthis::ShoWSurface w;

    if (w.status() == QQuickView::Error)
        return 1;
    QStringList languages = ocr_engine.available_languages();
    w.set_languages(languages);
    w.set_language(settings.ocr_language(languages));

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

    QSystemTrayIcon tray(QIcon::fromTheme("edit-find"));
    QMenu menu;
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
    return QApplication::exec();
}
