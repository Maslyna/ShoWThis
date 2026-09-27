#include <LayerShellQt/Window>

#include <QApplication>
#include <QColorTransform>
#include <QDBusConnection>
#include <QDebug>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QScreen>
#include <QString>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QtLogging>

#include "GlobalShortcutPortal.h"
#include "KWinScreenCaptureBackend.h"
#include "ShoWSurface.h"
#include "ShoWThisConstants.h"

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
    app.setQuitOnLastWindowClosed(false);

    QDBusConnection bus = QDBusConnection::sessionBus();

    register_host_app();

    if (!bus.registerService(showthis::APP_ID)) {
        qWarning("Failed to register the service's name. Probably, another "
                 "instance is already running");
        return -1;
    }

    QScreen *screen = nullptr;

    showthis::ShoWSurface w;
    w.setFlags(Qt::FramelessWindowHint);

    auto *layerShell = LayerShellQt::Window::get(&w);

    layerShell->setLayer(LayerShellQt::Window::LayerOverlay);

    layerShell->setAnchors(static_cast<LayerShellQt::Window::Anchors>(
        LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
        LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));

    layerShell->setMargins(QMargins{0, 0, 0, 0});

    layerShell->setExclusiveZone(-1);

    layerShell->setDesiredSize(QSize{0, 0});

    layerShell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);

    layerShell->setWantsToBeOnActiveScreen(true);

    QSystemTrayIcon tray(QIcon::fromTheme("edit-find"));
    QMenu menu;
    QAction *quitAction = menu.addAction("Quit");

    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
    tray.setContextMenu(&menu);
    tray.setToolTip("Screen Translator (running)");
    tray.show();

    showthis::ocr::OcrEngine ocr_engine{{"eng+ukr+rus+deu"}};
    showthis::KWinScreenCaptureBackend backend;
    QObject::connect(&backend, &showthis::AScreenCaptureBackend::captured, [&](const QImage &img) {
        QList<showthis::ocr::WordBox> words;

        try {
            words = ocr_engine.scan_image(img);
        } catch (const std::exception &ex) {
            qCritical() << ex.what();
        }

        w.set_image(img, words);
    });
    QObject::connect(
        &backend, &showthis::AScreenCaptureBackend::capture_failed, [&](const QString &reason) {
            qWarning() << "capture failed:" << reason;
            app.exit(1);
        });

    showthis::GlobalShortcutPortal portal;
    QObject::connect(&portal, &showthis::GlobalShortcutPortal::shortcut_activated, [&](const QString &id) {
        qDebug() << "Got shortcut:" << id << "-> toggle window here, even though the app is unfocused";

        w.setVisible(!w.isVisible());
        backend.capture();
    });
    portal.start();

    w.show();
    w.setVisible(false);
    return QApplication::exec();
}
