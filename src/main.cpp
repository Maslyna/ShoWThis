#include <LayerShellQt/Window>
#include <QApplication>

#include <LayerShellQt/Window>
#include <QColorTransform>
#include <QDebug>
#include <QFile>
#include <QMenu>
#include <QScreen>
#include <QString>
#include <QLabel>
#include <QTimer>
#include <QSystemTrayIcon>
#include <QtLogging>

#include <QDBusConnection>

#include "GlobalShortcutPortal.h"
#include "KWinScreenCaptureBackend.h"

#include "ShoWSurface.h"
#include "ShoWThisConstants.h"


void registerHostApp() {
  // The Registry interface only exists for unsandboxed ("host") apps.
  // Under Flatpak the app id is already known via .flatpak-info, and
  // calling Register() there is unsupported/unnecessary.
  if (QFile::exists(QStringLiteral("/.flatpak-info")))
    return;
  QDBusMessage call = QDBusMessage::createMethodCall(
      showthis::desktop_portal_service(), showthis::desktop_portal_path(), showthis::registry_iface(), "Register");
  call << showthis::app_id() << QVariantMap{};
  
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
  ShoWSurface w;
  app.setQuitOnLastWindowClosed(false);

  QDBusConnection bus = QDBusConnection::sessionBus();

  registerHostApp();

  if (!bus.registerService(showthis::app_id())) {
    qWarning("Failed to register the service's name. Probably, another "
             "instance is already running");
    return -1;
  }

  QScreen *screen = nullptr;

  w.setFlags(Qt::FramelessWindowHint);

  auto *layerShell = LayerShellQt::Window::get(&w);

  layerShell->setLayer(LayerShellQt::Window::LayerOverlay);

  layerShell->setAnchors(static_cast<LayerShellQt::Window::Anchors>(
      LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
      LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));

  layerShell->setMargins(QMargins{0, 0, 0, 0});

  layerShell->setExclusiveZone(-1);

  layerShell->setDesiredSize(QSize{0, 0});

  layerShell->setKeyboardInteractivity(
      LayerShellQt::Window::KeyboardInteractivityNone);

  layerShell->setWantsToBeOnActiveScreen(true);

  QSystemTrayIcon tray(QIcon::fromTheme("edit-find"));
  QMenu menu;
  QAction *quitAction = menu.addAction("Quit");

  QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);
  tray.setContextMenu(&menu);
  tray.setToolTip("Screen Translator (running)");
  tray.show();

  KWinScreenCaptureBackend backend;
  QObject::connect(&backend, &AScreenCaptureBackend::captured,
                   [&](const QImage &img) {
                     w.setImage(img);
                     w.setVisible(!w.isVisible());
                   });
  QObject::connect(&backend, &AScreenCaptureBackend::captureFailed,
                   [&](const QString &reason) {
                     qWarning() << "capture failed:" << reason;
                     app.exit(1);
                   });

  GlobalShortcutPortal portal;
  QObject::connect(
      &portal, &GlobalShortcutPortal::shortcutActivated,
      [&](const QString &id) {
        qDebug() << "Got shortcut:" << id
                 << "-> toggle window here, even though the app is unfocused";
        backend.capture();
      });
  portal.start();

  w.show();
  w.setVisible(false);
  return QApplication::exec();
}
