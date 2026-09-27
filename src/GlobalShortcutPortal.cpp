#include "GlobalShortcutPortal.h"

#include <QDBusMetaType>
#include <QDBusReply>
#include <QUuid>

#include "ShoWThisConstants.h"

namespace {

using namespace Qt::Literals::StringLiterals;


}  // namespace

namespace showthis {

QDBusArgument &operator<<(QDBusArgument &arg, const ShortcutDescription &shortcut) {
    arg.beginStructure();
    arg << shortcut.id << shortcut.options;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, ShortcutDescription &shortcut) {
    arg.beginStructure();
    arg >> shortcut.id >> shortcut.options;
    arg.endStructure();
    return arg;
}

GlobalShortcutPortal::GlobalShortcutPortal(QObject *parent) : QObject(parent) {
    // Must happen before any BindShortcuts call; safe to call more than once.
    qDBusRegisterMetaType<ShortcutDescription>();
    qDBusRegisterMetaType<QList<ShortcutDescription>>();

    m_portal = new QDBusInterface(showthis::DESKTOP_PORTAL_SERVICE,
                                  showthis::DESKTOP_PORTAL_PATH,
                                  showthis::GSHORTCUT_PORTAL_IFACE,
                                  QDBusConnection::sessionBus(),
                                  this);
}

QString GlobalShortcutPortal::gen_new_token() {
    return "t" + QUuid::createUuid().toString(QUuid::Id128);
}

void GlobalShortcutPortal::start() {
    // Subscribe once; every bound shortcut's activation arrives here.
    QDBusConnection::sessionBus().connect(showthis::DESKTOP_PORTAL_SERVICE,
                                          showthis::DESKTOP_PORTAL_PATH,
                                          showthis::GSHORTCUT_PORTAL_IFACE,
                                          "Activated",
                                          this,
                                          SLOT(on_activated(QDBusMessage)));

    QString token = gen_new_token();
    QVariantMap options{{"handle_token", token}, {"session_handle_token", token}};

    QDBusReply<QDBusObjectPath> reply = m_portal->call("CreateSession", options);
    if (!reply.isValid()) {
        qWarning() << "CreateSession call failed:" << reply.error().message();
        return;
    }

    // The real result comes asynchronously via Request::Response on this path.
    QDBusConnection::sessionBus().connect(QString(),
                                          reply.value().path(),
                                          showthis::REQUEST_IFACE,
                                          "Response",
                                          this,
                                          SLOT(on_session_created(QDBusMessage)));
}

void GlobalShortcutPortal::on_session_created(const QDBusMessage &msg) {
    const QList<QVariant> args = msg.arguments();
    const uint responseCode    = args.at(0).toUInt();
    const QVariantMap results  = qdbus_cast<QVariantMap>(args.at(1));

    if (responseCode != 0) {
        qWarning() << "CreateSession request denied, code" << responseCode;
        return;
    }
    m_sessionHandle = results.value("session_handle").toString();
    qDebug() << "GlobalShortcuts session created:" << m_sessionHandle;

    ShortcutDescription toggleWindow;
    toggleWindow.id      = "toggle-window";
    toggleWindow.options = QVariantMap{{"description", "Show or hide the main window"},
                                       {"preferred_trigger", "CTRL+ALT+SPACE"}};
    QList<ShortcutDescription> shortcuts{toggleWindow};

    QVariantMap bindOptions{{"handle_token", gen_new_token()}};

    QDBusMessage call = QDBusMessage::createMethodCall(showthis::DESKTOP_PORTAL_SERVICE,
                                                       showthis::DESKTOP_PORTAL_PATH,
                                                       showthis::GSHORTCUT_PORTAL_IFACE,
                                                       "BindShortcuts");
    call << QVariant::fromValue(QDBusObjectPath(m_sessionHandle)) << QVariant::fromValue(shortcuts)
         << QString("")  // parent_window, empty = none
         << bindOptions;

    QDBusMessage reply = QDBusConnection::sessionBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qWarning() << "BindShortcuts failed:" << reply.errorMessage();
        return;
    }

    QDBusObjectPath requestPath = reply.arguments().at(0).value<QDBusObjectPath>();
    QDBusConnection::sessionBus().connect(QString(),
                                          requestPath.path(),
                                          showthis::REQUEST_IFACE,
                                          "Response",
                                          this,
                                          SLOT(on_shortcuts_bound(QDBusMessage)));
}

void GlobalShortcutPortal::on_shortcuts_bound(const QDBusMessage &msg) {
    qDebug() << "BindShortcuts response:" << msg.arguments();
}

void GlobalShortcutPortal::on_activated(const QDBusMessage &msg) {
    // Signature: session_handle(o), shortcut_id(s), timestamp(t), options(a{sv})
    const QList<QVariant> args = msg.arguments();
    const QString session      = args.at(0).value<QDBusObjectPath>().path();
    if (session != m_sessionHandle)
        return;  // not our session

    const QString shortcutId = args.at(1).toString();
    qDebug() << "Shortcut activated:" << shortcutId;
    emit shortcut_activated(shortcutId);
}

}  // namespace showthis
