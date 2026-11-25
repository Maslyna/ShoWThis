#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "LearningService.h"
#include "GlobalShortcutPortal.h"
#include "ShoWSurface.h"
#include "ShoWThisConstants.h"

class FakeSettingsPortal : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Settings")
public:
    QDBusConnection bus;
    QVariant value = uint(0);
    QString mode;
    int read_calls = 0;
    QString read_namespace;
    QString read_key;
    QList<QDBusMessage> pending;

    explicit FakeSettingsPortal(const QDBusConnection &connection) : bus(connection) {}

    bool change(const QVariant &setting, const QString &name_space = QStringLiteral("org.freedesktop.appearance"),
                const QString &key = QStringLiteral("reduced-motion")) {
        auto signal = QDBusMessage::createSignal(showthis::DESKTOP_PORTAL_PATH,
            "org.freedesktop.portal.Settings", "SettingChanged");
        signal << name_space << key << QVariant::fromValue(QDBusVariant(setting));
        return bus.send(signal);
    }

    bool answer(const QDBusMessage &call, const QVariant &setting) {
        return bus.send(call.createReply(QVariantList{QVariant::fromValue(QDBusVariant(setting))}));
    }

    bool finish_pending(const QVariant &setting) {
        return !pending.isEmpty() && answer(pending.takeFirst(), setting);
    }

public slots:
    void Read(const QString &name_space, const QString &key, const QDBusMessage &call) {
        setDelayedReply(true);
        ++read_calls;
        read_namespace = name_space;
        read_key = key;
        if (mode == "hold")
            pending.append(call);
        else if (mode == "error")
            bus.send(call.createErrorReply("org.freedesktop.portal.Error.NotFound", "Setting unavailable"));
        else if (mode == "wrong-signature")
            bus.send(call.createReply(QVariantList{value}));
        else if (mode == "empty-reply")
            bus.send(call.createReply());
        else
            answer(call, value);
    }
};

class IdentityCachingPortal : public QDBusVirtualObject {
public:
    QHash<QString, QString> app_ids;
    QStringList settings_peers;
    QString session;

    QString introspect(const QString &) const override {
        return QStringLiteral(R"xml(
            <interface name="org.freedesktop.host.portal.Registry">
                <method name="Register"><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/></method>
            </interface>
            <interface name="org.freedesktop.portal.Settings">
                <method name="Read"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/></method>
            </interface>
            <interface name="org.freedesktop.portal.GlobalShortcuts">
                <method name="CreateSession"><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
                <method name="BindShortcuts"><arg type="o" direction="in"/><arg type="a(sa{sv})" direction="in"/><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
            </interface>)xml");
    }

    bool handleMessage(const QDBusMessage &call, const QDBusConnection &bus) override {
        const auto sender = call.service();
        if (call.interface() == showthis::REGISTRY_IFACE && call.member() == "Register") {
            // The real portal rejects registration after any frontend call cached this peer.
            if (app_ids.contains(sender))
                return bus.send(call.createErrorReply(QDBusError::Failed, "Application identity already cached"));
            app_ids.insert(sender, call.arguments().at(0).toString());
            return bus.send(call.createReply());
        }
        if (call.interface() == "org.freedesktop.portal.Settings" && call.member() == "Read") {
            if (!app_ids.contains(sender)) app_ids.insert(sender, {});
            settings_peers.append(sender);
            return bus.send(call.createReply(QVariantList{QVariant::fromValue(QDBusVariant(uint(0)))}));
        }
        if (call.interface() != showthis::GSHORTCUT_PORTAL_IFACE
            || (call.member() != "CreateSession" && call.member() != "BindShortcuts"))
            return false;
        if (app_ids.value(sender).isEmpty())
            return bus.send(call.createErrorReply(QDBusError::Failed, "An app id is required"));
        const bool creating = call.member() == "CreateSession";
        const auto options = qdbus_cast<QVariantMap>(call.arguments().at(creating ? 0 : 3));
        QVariantMap results;
        if (creating) {
            session = "/org/freedesktop/portal/desktop/session/" + options.value("session_handle_token").toString();
            results.insert("session_handle", QVariant::fromValue(QDBusObjectPath(session)));
        } else {
            const QList<showthis::ShortcutDescription> shortcuts{
                {"toggle-window", {{"trigger_description", "Ctrl+Alt+Space"}}}};
            results.insert("shortcuts", QVariant::fromValue(shortcuts));
        }
        const auto path = showthis::GlobalShortcutPortal::request_path(sender, options.value("handle_token").toString());
        auto response = QDBusMessage::createSignal(path, showthis::REQUEST_IFACE, "Response");
        response << uint(0) << results;
        return bus.send(call.createReply(QVariantList{QVariant::fromValue(QDBusObjectPath(path))})) && bus.send(response);
    }
};

class MotionPreferencesTest : public QObject {
    Q_OBJECT
    QDBusConnection m_service = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "motion-fixture");
    FakeSettingsPortal m_portal{m_service};
    QTemporaryDir m_directory;
    std::unique_ptr<showthis::LearningService> m_learning;

private slots:
    void initTestCase() {
        QVERIFY2(qEnvironmentVariableIntValue("SHOWTHIS_TEST_PRIVATE_DBUS") == 1,
            "Run this test through its dbus-run-session CTest entry, never on the desktop bus.");
        QVERIFY(m_service.isConnected());
        const auto services = m_service.interface()->registeredServiceNames();
        QVERIFY(services.isValid());
        for (const auto &name : services.value()) {
            QVERIFY2(name == "org.freedesktop.DBus" || name.startsWith(QLatin1Char(':')),
                "The fake portal requires an otherwise empty private session bus.");
        }
        QVERIFY(m_directory.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_directory.path());
        m_learning = std::make_unique<showthis::LearningService>(nullptr, m_directory.filePath("words.json"));
        QVERIFY(m_service.registerObject(showthis::DESKTOP_PORTAL_PATH, &m_portal, QDBusConnection::ExportAllSlots));
    }

    void init() {
        m_portal.value = uint(0);
        m_portal.mode.clear();
        m_portal.read_calls = 0;
        m_portal.pending.clear();
        // registerService never queues or replaces an existing owner.
        QVERIFY(m_service.registerService(showthis::DESKTOP_PORTAL_SERVICE));
    }

    void cleanup() {
        m_service.unregisterService(showthis::DESKTOP_PORTAL_SERVICE);
        m_portal.pending.clear();
    }

    void initial_preference_data() {
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<bool>("enabled");
        QTest::newRow("normal") << QVariant(uint(0)) << true;
        QTest::newRow("reduced") << QVariant(uint(1)) << false;
        QTest::newRow("unknown-uint-is-no-preference") << QVariant(uint(2)) << true;
        QTest::newRow("nested-normal") << QVariant::fromValue(QDBusVariant(uint(0))) << true;
        QTest::newRow("nested-reduced") << QVariant::fromValue(QDBusVariant(uint(1))) << false;
        QTest::newRow("doubly-nested-normal")
            << QVariant::fromValue(QDBusVariant(QVariant::fromValue(QDBusVariant(uint(0))))) << true;
    }

    void initial_preference() {
        QFETCH(QVariant, value);
        QFETCH(bool, enabled);
        m_portal.value = value;
        showthis::ShoWSurface surface(nullptr, m_learning.get());
        QCOMPARE(surface.status(), QQuickView::Ready);
        QVERIFY(!surface.motion_enabled());
        QSignalSpy changed(&surface, &showthis::ShoWSurface::motion_preferences_changed);
        const auto property = surface.metaObject()->property(surface.metaObject()->indexOfProperty("motion_enabled"));
        QVERIFY(property.isReadable());
        QVERIFY(!property.isWritable());
        QTRY_COMPARE(m_portal.read_calls, 1);
        QTRY_VERIFY(surface.findChildren<QDBusPendingCallWatcher *>().isEmpty());
        QCOMPARE(m_portal.read_namespace, "org.freedesktop.appearance");
        QCOMPARE(m_portal.read_key, "reduced-motion");
        QCOMPARE(surface.motion_enabled(), enabled);
        QCOMPARE(surface.property("motion_enabled").toBool(), enabled);
        QCOMPARE(changed.size(), enabled ? 1 : 0);
    }

    void safe_read_failure_data() {
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<QString>("mode");
        QTest::newRow("string") << QVariant(QStringLiteral("0")) << QString{};
        QTest::newRow("signed-int") << QVariant(0) << QString{};
        QTest::newRow("boolean") << QVariant(true) << QString{};
        QTest::newRow("wide-uint") << QVariant::fromValue(qulonglong(0)) << QString{};
        QTest::newRow("nested-string") << QVariant::fromValue(QDBusVariant(QStringLiteral("0"))) << QString{};
        QTest::newRow("missing-setting") << QVariant(uint(0)) << QStringLiteral("error");
        QTest::newRow("wrong-reply-signature") << QVariant(uint(0)) << QStringLiteral("wrong-signature");
        QTest::newRow("empty-reply") << QVariant(uint(0)) << QStringLiteral("empty-reply");
    }

    void safe_read_failure() {
        QFETCH(QVariant, value);
        QFETCH(QString, mode);
        m_portal.value = value;
        m_portal.mode = mode;
        showthis::ShoWSurface surface(nullptr, m_learning.get());
        QSignalSpy changed(&surface, &showthis::ShoWSurface::motion_preferences_changed);
        QVERIFY(!surface.motion_enabled());
        QTRY_COMPARE(m_portal.read_calls, 1);
        QTRY_VERIFY(surface.findChildren<QDBusPendingCallWatcher *>().isEmpty());
        QVERIFY(!surface.motion_enabled());
        QCOMPARE(changed.size(), 0);
        QVERIFY(m_portal.change(uint(0)));
        QTRY_VERIFY(surface.motion_enabled());
        QCOMPARE(m_portal.read_calls, 1);
    }

    void live_preference_changes() {
        showthis::ShoWSurface surface(nullptr, m_learning.get());
        QTRY_VERIFY(surface.motion_enabled());
        QSignalSpy changed(&surface, &showthis::ShoWSurface::motion_preferences_changed);
        QVERIFY(m_portal.change(uint(1), "unrelated.namespace"));
        QVERIFY(m_portal.change(uint(1), "org.freedesktop.appearance", "unrelated-key"));
        QVERIFY(m_portal.change(uint(0)));
        QVERIFY(m_portal.change(uint(1)));
        QTRY_VERIFY(!surface.motion_enabled());
        QCOMPARE(changed.size(), 1);
        QVERIFY(m_portal.change(uint(1)));
        QVERIFY(m_portal.change(QVariant::fromValue(QDBusVariant(uint(0)))));
        QTRY_VERIFY(surface.motion_enabled());
        QCOMPARE(changed.size(), 2);
        QVERIFY(m_portal.change(QStringLiteral("0")));
        QTRY_VERIFY(!surface.motion_enabled());
        QCOMPARE(changed.size(), 3);
        QVERIFY(m_portal.change(uint(99)));
        QTRY_VERIFY(surface.motion_enabled());
        QCOMPARE(changed.size(), 4);
        QCOMPARE(m_portal.read_calls, 1);
    }

    void signal_supersedes_initial_read_data() {
        QTest::addColumn<uint>("stale_value");
        QTest::addColumn<uint>("signal_value");
        QTest::newRow("late-normal-cannot-enable") << uint(0) << uint(1);
        QTest::newRow("late-reduced-cannot-disable") << uint(1) << uint(0);
    }

    void signal_supersedes_initial_read() {
        QFETCH(uint, stale_value);
        QFETCH(uint, signal_value);
        m_portal.mode = "hold";
        showthis::ShoWSurface surface(nullptr, m_learning.get());
        QVERIFY(!surface.motion_enabled());
        QTRY_COMPARE(m_portal.pending.size(), 1);
        QVERIFY(m_portal.change(uint(0)));
        QTRY_VERIFY(surface.motion_enabled());
        QVERIFY(m_portal.change(signal_value));
        QTRY_COMPARE(surface.motion_enabled(), signal_value != 1);
        QVERIFY(m_portal.finish_pending(stale_value));
        QTRY_VERIFY(surface.findChildren<QDBusPendingCallWatcher *>().isEmpty());
        QCOMPARE(surface.motion_enabled(), signal_value != 1);
        QCOMPARE(m_portal.read_calls, 1);
    }

    void service_loss_and_reappearance() {
        showthis::ShoWSurface surface(nullptr, m_learning.get());
        QTRY_VERIFY(surface.motion_enabled());
        QSignalSpy changed(&surface, &showthis::ShoWSurface::motion_preferences_changed);
        QVERIFY(m_service.unregisterService(showthis::DESKTOP_PORTAL_SERVICE));
        QTRY_VERIFY(!surface.motion_enabled());
        QCOMPARE(changed.size(), 1);
        m_portal.value = uint(1);
        QVERIFY(m_service.registerService(showthis::DESKTOP_PORTAL_SERVICE));
        QTRY_COMPARE(m_portal.read_calls, 2);
        QTRY_VERIFY(surface.findChildren<QDBusPendingCallWatcher *>().isEmpty());
        QVERIFY(!surface.motion_enabled());
        QVERIFY(m_service.unregisterService(showthis::DESKTOP_PORTAL_SERVICE));
        m_portal.value = uint(0);
        QVERIFY(m_service.registerService(showthis::DESKTOP_PORTAL_SERVICE));
        QTRY_VERIFY(surface.motion_enabled());
        QCOMPARE(m_portal.read_calls, 3);
        QCOMPARE(changed.size(), 2);
        QVERIFY(m_portal.change(uint(1)));
        QTRY_VERIFY(!surface.motion_enabled());
        QCOMPARE(changed.size(), 3);
    }

    void service_absent_at_start() {
        QVERIFY(m_service.unregisterService(showthis::DESKTOP_PORTAL_SERVICE));
        showthis::ShoWSurface surface(nullptr, m_learning.get());
        QTRY_VERIFY(surface.findChildren<QDBusPendingCallWatcher *>().isEmpty());
        QVERIFY(!surface.motion_enabled());
        QCOMPARE(m_portal.read_calls, 0);
        QVERIFY(m_service.registerService(showthis::DESKTOP_PORTAL_SERVICE));
        QTRY_VERIFY(surface.motion_enabled());
        QCOMPARE(m_portal.read_calls, 1);
    }

    void stale_read_after_service_restart() {
        m_portal.mode = "hold";
        showthis::ShoWSurface surface(nullptr, m_learning.get());
        QTRY_COMPARE(m_portal.pending.size(), 1);
        QVERIFY(m_service.unregisterService(showthis::DESKTOP_PORTAL_SERVICE));
        m_portal.mode.clear();
        m_portal.value = uint(1);
        QVERIFY(m_service.registerService(showthis::DESKTOP_PORTAL_SERVICE));
        QTRY_COMPARE(m_portal.read_calls, 2);
        QTRY_COMPARE(surface.findChildren<QDBusPendingCallWatcher *>().size(), 1);
        QVERIFY(m_portal.finish_pending(uint(0)));
        QTRY_VERIFY(surface.findChildren<QDBusPendingCallWatcher *>().isEmpty());
        QVERIFY(!surface.motion_enabled());
    }
    void live_preference_cancels_active_feedback() {
        m_learning->set_source_language("en");
        showthis::ShoWSurface surface(nullptr, m_learning.get());
        surface.open_notebook_word("Haus", "");
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QTRY_VERIFY(surface.motion_enabled());
        auto *language = surface.notebook_dialog()->findChild<QQuickItem *>("lookup_language");
        QVERIFY(language);
        auto *feedback = language->findChild<QQuickItem *>("language_feedback");
        QVERIFY(feedback);
        m_learning->set_source_language("de");
        QTRY_VERIFY(feedback->opacity() > 0);
        QVERIFY(m_portal.change(uint(1)));
        QTRY_VERIFY(!surface.motion_enabled());
        QCOMPARE(feedback->opacity(), 0.0);
        m_learning->set_source_language("fr");
        QCOMPARE(language->property("language_code").toString(), QString("fr"));
        QCOMPARE(feedback->opacity(), 0.0);
    }
    void motion_lookup_preserves_shortcut_identity_data() {
        QTest::addColumn<bool>("settings_first");
        QTest::newRow("settings-first") << true;
        QTest::newRow("concurrent-startup") << false;
    }

    void motion_lookup_preserves_shortcut_identity() {
        QFETCH(bool, settings_first);
        const QString shortcuts_service = "dev.maslyna.ShoWThis.MotionIdentityTest";
        IdentityCachingPortal frontend;
        const auto restore = qScopeGuard([&]() {
            m_service.unregisterService(shortcuts_service);
            m_service.unregisterObject(showthis::DESKTOP_PORTAL_PATH);
            m_service.registerObject(showthis::DESKTOP_PORTAL_PATH, &m_portal, QDBusConnection::ExportAllSlots);
        });
        m_service.unregisterObject(showthis::DESKTOP_PORTAL_PATH);
        QVERIFY(m_service.registerVirtualObject(showthis::DESKTOP_PORTAL_PATH, &frontend));
        QVERIFY(m_service.registerService(shortcuts_service));
        showthis::GlobalShortcutPortal shortcuts(nullptr, 1000, QDBusConnection::sessionBus(), shortcuts_service);
        showthis::ShoWSurface surface(nullptr, m_learning.get());
        if (settings_first)
            QTRY_VERIFY(surface.motion_enabled());
        shortcuts.start();
        QTRY_VERIFY2(shortcuts.ready(), qPrintable(shortcuts.error()));
        QTRY_VERIFY(surface.motion_enabled());
        const auto shortcut_peer = QDBusConnection::sessionBus().baseService();
        QCOMPARE(frontend.app_ids.value(shortcut_peer), showthis::APP_ID);
        QVERIFY(!frontend.settings_peers.isEmpty());
        for (const auto &peer : frontend.settings_peers)
            QVERIFY(peer != shortcut_peer);

        QVERIFY(m_service.unregisterService(shortcuts_service));
        QVERIFY(m_service.unregisterService(showthis::DESKTOP_PORTAL_SERVICE));
        QTRY_VERIFY(!shortcuts.ready());
        QTRY_VERIFY(!surface.motion_enabled());
        frontend.app_ids.clear();
        frontend.settings_peers.clear();
        QVERIFY(m_service.registerService(showthis::DESKTOP_PORTAL_SERVICE));
        QTRY_VERIFY(surface.motion_enabled());
        QVERIFY(m_service.registerService(shortcuts_service));
        QTRY_VERIFY2(shortcuts.ready(), qPrintable(shortcuts.error()));
        QCOMPARE(frontend.app_ids.value(shortcut_peer), showthis::APP_ID);

        QSignalSpy activated(&shortcuts, &showthis::GlobalShortcutPortal::shortcut_activated);
        auto signal = QDBusMessage::createSignal(showthis::DESKTOP_PORTAL_PATH, showthis::GSHORTCUT_PORTAL_IFACE, "Activated");
        signal << QVariant::fromValue(QDBusObjectPath(frontend.session)) << QString("toggle-window") << quint64(1) << QVariantMap{};
        QVERIFY(m_service.send(signal));
        QTRY_COMPARE(activated.size(), 1);
    }
};

QTEST_MAIN(MotionPreferencesTest)
#include "MotionPreferencesTest.moc"
