#include <LayerShellQt/Window>

#include <QApplication>
#include <QColorTransform>
#include <QDBusConnection>
#include <QDebug>
#include <QFile>
#include <QFutureWatcher>
#include <QLabel>
#include <QMenu>
#include <QScreen>
#include <QString>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QQuickItem>
#include <QtConcurrent/QtConcurrent>
#include <QtLogging>

#include "GlobalShortcutPortal.h"
#include "AppSettings.h"
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

    layerShell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);

    layerShell->setWantsToBeOnActiveScreen(true);

    QSystemTrayIcon tray(QIcon::fromTheme("edit-find"));
    QMenu menu;
    QAction *vocabulary_action = menu.addAction("My vocabulary");
    QObject::connect(vocabulary_action, &QAction::triggered, &app, [&]() {
        w.show();
        QMetaObject::invokeMethod(w.rootObject(), "open_vocabulary");
    });
    QAction *quitAction = menu.addAction("Quit");

    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
    tray.setContextMenu(&menu);
    tray.setToolTip("Screen Translator (running)");
    tray.show();

    struct ScanResult {
        QList<showthis::ocr::WordBox> words;
        QString error;
    };
    bool busy = false;  // GUI thread only
    QImage screenshot;
    QFuture<ScanResult> pending;  // for shutdown

    const auto scan = [&]() {
        busy = true;
        w.set_scanning(true);
        w.set_error({});
        w.set_words({});
        auto *watcher = new QFutureWatcher<ScanResult>(&app);
        QObject::connect(watcher, &QFutureWatcherBase::finished, &app, [&, watcher]() {
            const auto result = watcher->result();
            w.set_words(result.words);
            w.set_error(result.error);
            w.set_scanning(false);
            watcher->deleteLater();
            busy = false;  // allow the next capture
        });

        pending = QtConcurrent::run([&ocr_engine, owned = screenshot, language = w.language()]() {
            ScanResult result;
            try {
                ocr_engine.set_languages(language);
                result.words = ocr_engine.scan_image(owned);
            } catch (const std::exception &ex) {
                qCritical() << ex.what();
                result.error = QString::fromUtf8(ex.what());
            }
            return result;
        });
        watcher->setFuture(pending);
    };

    QObject::connect(&backend, &showthis::AScreenCaptureBackend::captured, [&](const QImage &img) {
        screenshot = img.copy();
        w.set_image(screenshot);
        scan();
        w.setVisible(true);
    });

    QObject::connect(&w, &showthis::ShoWSurface::selectedLanguageChanged, &app, [&]() {
        settings.set_ocr_language(w.language());
        if (!busy && !screenshot.isNull())
            scan();
    });
    QObject::connect(&w, &showthis::ShoWSurface::closeRequested, &app, [&]() { w.hide(); });

    QObject::connect(
        &backend, &showthis::AScreenCaptureBackend::capture_failed, [&](const QString &reason) {
            qWarning() << "capture failed:" << reason;
            busy = false;
            app.exit(1);
        });

    // Don't let the engine be destroyed under a running scan.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() { pending.waitForFinished(); });

    QObject::connect(&portal, &showthis::GlobalShortcutPortal::shortcut_activated, [&](const QString &id) {
        qDebug() << "Got shortcut:" << id;

        if (w.isVisible()) {
            w.setVisible(false);
        } else if (!busy) {
            busy = true;
            backend.capture();
        }
    });
    portal.start();

    w.show();
    w.setVisible(false);
    return QApplication::exec();
}
