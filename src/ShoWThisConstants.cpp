#include "ShoWThisConstants.h"

#include <QString>

namespace showthis {

const QString &app_id() noexcept {
  static const QString APP_ID = QStringLiteral("dev.maslyna.ShoWThis");
  return APP_ID;
}
const QString &registry_iface() noexcept {
  static const QString REGISTRY_IFACE =
      QStringLiteral("org.freedesktop.host.portal.Registry");
  return REGISTRY_IFACE;
}
const QString &desktop_portal_service() noexcept {
  static const QString DESKTOP_PORTAL_SERVICE =
      QStringLiteral("org.freedesktop.portal.Desktop");
  return DESKTOP_PORTAL_SERVICE;
}
const QString &desktop_portal_path() noexcept {
  static const QString DESKTOP_PORTAL_PATH =
      QStringLiteral("/org/freedesktop/portal/desktop");
  return DESKTOP_PORTAL_PATH;
}

} // namespace showthis
