#pragma once

#include <QString>

namespace showthis {

inline const QString APP_ID         = QStringLiteral("dev.maslyna.ShoWThis");
inline const QString REGISTRY_IFACE = QStringLiteral("org.freedesktop.host.portal.Registry");
inline const QString DESKTOP_PORTAL_SERVICE = QStringLiteral("org.freedesktop.portal.Desktop");
inline const QString DESKTOP_PORTAL_PATH    = QStringLiteral("/org/freedesktop/portal/desktop");

inline const QString KWIN_SCREENSHOT2_SERVICE = QStringLiteral(u"org.kde.KWin.ScreenShot2");
inline const QString KWIN_SCREENSHOT2_OBJECT_PATH = QStringLiteral(u"/org/kde/KWin/ScreenShot2");
inline const QString KWIN_SCREENSHOT2_INTERFACE = QStringLiteral(u"org.kde.KWin.ScreenShot2");
}  // namespace showthis
