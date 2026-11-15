#include "GlobalShortcutPortal.h"
#include "ShoWThisConstants.h"

#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QFile>
#include <QUuid>
#include <QRegularExpression>

namespace showthis {
namespace {
QString token() { return "t" + QUuid::createUuid().toString(QUuid::Id128); }
}

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

QString GlobalShortcutPortal::request_path(const QString &unique_name, const QString &handle) {
    auto sender = unique_name;
    sender.remove(':');
    sender.replace('.', '_');
    return "/org/freedesktop/portal/desktop/request/" + sender + '/' + handle;
}

GlobalShortcutPortal::GlobalShortcutPortal(QObject *parent, int timeout_ms,
    const QDBusConnection &bus, const QString &service)
    : QObject(parent), m_bus(bus), m_service(service) {
    qDBusRegisterMetaType<ShortcutDescription>();
    qDBusRegisterMetaType<QList<ShortcutDescription>>();
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(qMax(1, timeout_ms));
    connect(&m_deadline, &QTimer::timeout, this, [this]() {
        if (m_phase == Phase::Configuring)
            configuration_failed(tr("The desktop portal did not answer the configuration request."));
        else
            fail(tr("Global shortcut setup timed out. Use Capture screen or Retry shortcut setup in the tray menu."));
    });
    m_bus.connect(m_service, DESKTOP_PORTAL_PATH, GSHORTCUT_PORTAL_IFACE, "Activated", this,
        SLOT(on_activated(QDBusMessage)));
    m_bus.connect(m_service, DESKTOP_PORTAL_PATH, GSHORTCUT_PORTAL_IFACE, "ShortcutsChanged", this,
        SLOT(on_shortcuts_changed(QDBusMessage)));
    auto *watcher = new QDBusServiceWatcher(m_service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
        [this](const QString &, const QString &, const QString &owner) {
            fail(tr("Desktop portal restarted or disconnected. The tray's Capture screen action remains available."));
            if (!owner.isEmpty())
                start();
        });
}

GlobalShortcutPortal::~GlobalShortcutPortal() { reset(); }

void GlobalShortcutPortal::reset() {
    ++m_generation;
    m_deadline.stop();
    if (!m_request.isEmpty()) {
        m_bus.disconnect(m_service, m_request, REQUEST_IFACE, "Response", this, SLOT(on_response(QDBusMessage)));
        auto close = QDBusMessage::createMethodCall(m_service, m_request, REQUEST_IFACE, "Close");
        m_bus.asyncCall(close);
    }
    if (!m_session.isEmpty()) {
        m_bus.disconnect(m_service, m_session, "org.freedesktop.portal.Session", "Closed", this, SLOT(on_session_closed(QDBusMessage)));
        auto close = QDBusMessage::createMethodCall(m_service, m_session, "org.freedesktop.portal.Session", "Close");
        m_bus.asyncCall(close);
    }
    m_request.clear();
    m_session.clear();
    m_ready = false;
    m_phase = Phase::Idle;
}

void GlobalShortcutPortal::fail(const QString &message) {
    reset();
    m_error = message;
    emit state_changed();
}

void GlobalShortcutPortal::start() { begin(false); }

void GlobalShortcutPortal::retry() {
    if (m_phase != Phase::Idle && m_phase != Phase::Active)
        return;
    if (!m_session.isEmpty())
        configure_shortcuts();
    else
        begin(true);
}

void GlobalShortcutPortal::begin(bool configure) {
    reset();
    m_configure_on_bind = configure;
    m_error.clear();
    emit state_changed();
    if (QFile::exists("/.flatpak-info")) {
        create_session();
        return;
    }
    // Register the host identity again after a portal restart. This call and
    // all subsequent setup calls are asynchronous, including service startup.
    m_phase = Phase::Registering;
    m_deadline.start();
    auto message = QDBusMessage::createMethodCall(m_service, DESKTOP_PORTAL_PATH, REGISTRY_IFACE, "Register");
    message << APP_ID << QVariantMap{};
    const auto generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, m_deadline.interval()), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        // Older portals lack Registry; CreateSession supplies the useful error
        // if registration actually was required or unavailable.
        create_session();
    });
}

void GlobalShortcutPortal::configuration_failed(const QString &detail) {
    ++m_generation;
    m_deadline.stop();
    m_phase = Phase::Active;
    m_error = tr("Open System Settings > Keyboard > Shortcuts, select ShoWThis, and assign a key to "
        "Show or hide the screen overlay. Capture screen in the tray remains available.");
    if (!detail.isEmpty())
        m_error = detail + ' ' + m_error;
    emit state_changed();
}

void GlobalShortcutPortal::configure_shortcuts() {
    m_phase = Phase::Configuring;
    m_error.clear();
    emit state_changed();
    const auto generation = ++m_generation;
    m_deadline.start();
    auto message = QDBusMessage::createMethodCall(m_service, DESKTOP_PORTAL_PATH,
        "org.freedesktop.DBus.Properties", "Get");
    message << GSHORTCUT_PORTAL_IFACE << QStringLiteral("version");
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, m_deadline.interval()), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        const QDBusPendingReply<QDBusVariant> version = *watcher;
        if (version.isError() || version.value().variant().toUInt() < 2) {
            configuration_failed(tr("This desktop portal cannot open shortcut configuration."));
            return;
        }
        auto configure = QDBusMessage::createMethodCall(m_service, DESKTOP_PORTAL_PATH,
            GSHORTCUT_PORTAL_IFACE, "ConfigureShortcuts");
        configure << QVariant::fromValue(QDBusObjectPath(m_session)) << QString{} << QVariantMap{};
        auto *configured = new QDBusPendingCallWatcher(m_bus.asyncCall(configure, m_deadline.interval()), this);
        connect(configured, &QDBusPendingCallWatcher::finished, this, [this, configured, generation]() {
            configured->deleteLater();
            if (generation != m_generation)
                return;
            const QDBusPendingReply<> reply = *configured;
            if (reply.isError()) {
                configuration_failed(tr("Cannot open shortcut configuration: %1").arg(reply.error().message()));
                return;
            }
            ++m_generation;
            m_deadline.stop();
            m_phase = Phase::Active;
            // ConfigureShortcuts only opens settings; it does not grant a binding.
            // Keep this session alive to receive the user's ShortcutsChanged update.
            if (!m_ready)
                m_error = tr("Shortcut configuration opened. Assign a key to Show or hide the screen overlay. "
                    "Capture screen in the tray remains available.");
            emit state_changed();
        });
    });
}

void GlobalShortcutPortal::update_shortcuts(const QList<ShortcutDescription> &shortcuts) {
    int matches = 0;
    bool usable = false;
    for (const auto &shortcut : shortcuts) {
        if (shortcut.id != QStringLiteral("toggle-window"))
            continue;
        ++matches;
        const auto trigger = shortcut.options.value("trigger_description");
        // This is localized display text, not a portable key-sequence string.
        usable = trigger.metaType() == QMetaType::fromType<QString>() && !trigger.toString().trimmed().isEmpty();
    }
    m_ready = matches == 1 && usable;
    m_error = m_ready ? QString{} : tr("No usable global shortcut is assigned. Choose Retry shortcut setup in the tray "
        "to configure it, or open System Settings > Keyboard > Shortcuts > ShoWThis. "
        "Capture screen in the tray remains available.");
    emit state_changed();
}

void GlobalShortcutPortal::create_session() {
    const auto handle = token();
    request("CreateSession", {QVariantMap{{"handle_token", handle}, {"session_handle_token", token()}}},
        handle, Phase::Creating);
}

void GlobalShortcutPortal::request(const QString &method, QList<QVariant> arguments,
    const QString &handle, Phase phase) {
    m_phase = phase;
    m_request = request_path(m_bus.baseService(), handle);
    // Subscribe BEFORE making the call: a fast portal may emit Response before
    // the method reply arrives. Each phase has an independent generation.
    const auto generation = ++m_generation;
    if (!m_bus.connect(m_service, m_request, REQUEST_IFACE, "Response", this, SLOT(on_response(QDBusMessage)))) {
        fail(tr("Cannot subscribe to desktop portal responses."));
        return;
    }
    m_deadline.start();
    auto message = QDBusMessage::createMethodCall(m_service, DESKTOP_PORTAL_PATH, GSHORTCUT_PORTAL_IFACE, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, m_deadline.interval()), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError())
            fail(tr("Global shortcut setup failed: %1").arg(reply.error().message()));
        else if (reply.value().path() != m_request)
            fail(tr("The desktop portal returned an unexpected request path."));
    });
}

void GlobalShortcutPortal::on_response(const QDBusMessage &message) {
    if (message.path() != m_request || (m_phase != Phase::Creating && m_phase != Phase::Binding))
        return;
    if (message.signature() != "ua{sv}" || message.arguments().size() != 2) {
        fail(tr("The desktop portal returned an invalid response."));
        return;
    }
    const auto args = message.arguments();
    if (args[0].toUInt() != 0) {
        if (m_phase == Phase::Binding) {
            m_bus.disconnect(m_service, m_request, REQUEST_IFACE, "Response", this, SLOT(on_response(QDBusMessage)));
            m_request.clear();
            ++m_generation;
            m_deadline.stop();
            m_phase = Phase::Active;
            m_configure_on_bind = false;
            update_shortcuts({});
        } else {
            fail(tr("Global shortcut permission was denied or cancelled. Use the tray menu to capture or retry."));
        }
        return;
    }
    m_bus.disconnect(m_service, m_request, REQUEST_IFACE, "Response", this, SLOT(on_response(QDBusMessage)));
    m_request.clear();
    if (m_phase == Phase::Creating) {
        const auto results = qdbus_cast<QVariantMap>(args[1]);
        const auto session = results.value("session_handle");
        m_session = session.metaType() == QMetaType::fromType<QDBusObjectPath>()
            ? session.value<QDBusObjectPath>().path() : session.toString();
        static const QRegularExpression valid_session("^/org/freedesktop/portal/desktop/session/[A-Za-z0-9_/]+$");
        if (!valid_session.match(m_session).hasMatch()) {
            m_session.clear();
            fail(tr("The desktop portal returned an invalid session handle."));
            return;
        }
        m_bus.connect(m_service, m_session, "org.freedesktop.portal.Session", "Closed", this, SLOT(on_session_closed(QDBusMessage)));
        const auto handle = token();
        const QList<ShortcutDescription> shortcuts{{"toggle-window", {{"description", tr("Show or hide the screen overlay")},
            {"preferred_trigger", "CTRL+ALT+SPACE"}}}};
        request("BindShortcuts", {QVariant::fromValue(QDBusObjectPath(m_session)), QVariant::fromValue(shortcuts),
            QString{}, QVariantMap{{"handle_token", handle}}}, handle, Phase::Binding);
    } else {
        ++m_generation;
        m_deadline.stop();
        m_phase = Phase::Active;
        const auto results = qdbus_cast<QVariantMap>(args[1]);
        update_shortcuts(qdbus_cast<QList<ShortcutDescription>>(results.value("shortcuts")));
        if (m_configure_on_bind && !m_ready)
            configure_shortcuts();
        m_configure_on_bind = false;
    }
}

void GlobalShortcutPortal::on_session_closed(const QDBusMessage &message) {
    if (message.path() != m_session)
        return;
    fail(tr("The global shortcut session was closed. Retry setup from the tray menu."));
}

void GlobalShortcutPortal::on_activated(const QDBusMessage &message) {
    const auto args = message.arguments();
    if (!m_ready || args.size() != 4 || message.signature() != "osta{sv}")
        return;
    if (args[0].value<QDBusObjectPath>().path() == m_session && args[1].toString() == "toggle-window")
        emit shortcut_activated(args[1].toString());
}

void GlobalShortcutPortal::on_shortcuts_changed(const QDBusMessage &message) {
    const auto args = message.arguments();
    if (args.size() != 2 || message.signature() != "oa(sa{sv})"
        || args[0].value<QDBusObjectPath>().path() != m_session
        || (m_phase != Phase::Active && m_phase != Phase::Configuring))
        return;
    const auto shortcuts = qdbus_cast<QList<ShortcutDescription>>(args[1]);
    // Changed may describe only a subset; an unrelated action cannot revoke ours.
    for (const auto &shortcut : shortcuts) {
        if (shortcut.id == QStringLiteral("toggle-window")) {
            ++m_generation;
            m_deadline.stop();
            m_phase = Phase::Active;
            update_shortcuts(shortcuts);
            return;
        }
    }
}

}  // namespace showthis
