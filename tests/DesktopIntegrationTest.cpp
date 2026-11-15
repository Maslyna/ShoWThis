#include <QtTest>
#include <QDBusContext>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QSignalSpy>
#include <unistd.h>

#include "GlobalShortcutPortal.h"
#include "KWinScreenCaptureBackend.h"

static const QString service_name = "dev.maslyna.ShoWThis.TestDesktop";

class FakeCapture : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.ScreenShot2")
public:
    QString mode;
    int held_fd = -1;
    ~FakeCapture() override { release(); }
    void release() { if (held_fd >= 0) ::close(held_fd); held_fd = -1; }
public slots:
    QVariantMap CaptureActiveScreen(const QVariantMap &, const QDBusUnixFileDescriptor &fd) {
        release();
        if (mode == "stalled") held_fd = ::dup(fd.fileDescriptor());
        else if (mode != "metadata") {
            const QByteArray bytes(mode == "oversized" ? 17 : mode == "incomplete" ? 8 : 16, '\xff');
            ::write(fd.fileDescriptor(), bytes.constData(), bytes.size());
        }
        return {{"type", "raw"}, {"width", mode == "metadata" ? -1 : 2}, {"height", 2},
            {"stride", 8}, {"format", int(QImage::Format_RGB32)}};
    }
};

class FakePortal : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.GlobalShortcuts")
    Q_PROPERTY(uint version READ portal_version)
public:
    QDBusConnection bus;
    QString mode;
    QString session;
    uint version = 2;
    int create_calls = 0;
    int bind_calls = 0;
    int configure_calls = 0;
    bool denied_binding = false;
    QDBusMessage pending_configuration;
    explicit FakePortal(const QDBusConnection &connection) : bus(connection) {}
    uint portal_version() const { return version; }
    static QList<showthis::ShortcutDescription> binding(const QVariant &trigger = QString("Ctrl+Alt+Space")) {
        return {{"toggle-window", {{"trigger_description", trigger}}}};
    }
    bool change(const QList<showthis::ShortcutDescription> &shortcuts, const QString &handle = {}) {
        auto signal = QDBusMessage::createSignal("/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.GlobalShortcuts", "ShortcutsChanged");
        signal << QVariant::fromValue(QDBusObjectPath(handle.isEmpty() ? session : handle))
            << QVariant::fromValue(shortcuts);
        return bus.send(signal);
    }
    bool activate(const QString &id = "toggle-window", const QString &handle = {}) {
        auto signal = QDBusMessage::createSignal("/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.GlobalShortcuts", "Activated");
        signal << QVariant::fromValue(QDBusObjectPath(handle.isEmpty() ? session : handle))
            << id << quint64(1) << QVariantMap{};
        return bus.send(signal);
    }
    QDBusObjectPath respond(const QVariantMap &options, const QDBusMessage &call, const QVariantMap &results,
        uint response = 0) {
        const auto path = showthis::GlobalShortcutPortal::request_path(call.service(), options.value("handle_token").toString());
        if (mode != "timeout") {
            auto reply = QDBusMessage::createSignal(path, "org.freedesktop.portal.Request", "Response");
            if (mode == "malformed") reply << QString("wrong signature");
            else reply << uint(mode == "denied" ? 1 : response) << results;
            bus.send(reply); // Deliberately before returning the method reply.
        }
        return QDBusObjectPath(path);
    }
    bool close_session(const QString &handle = {}) {
        auto signal = QDBusMessage::createSignal(handle.isEmpty() ? session : handle,
            "org.freedesktop.portal.Session", "Closed");
        signal << QVariantMap{};
        return bus.send(signal);
    }
public slots:
    QDBusObjectPath CreateSession(const QVariantMap &options, const QDBusMessage &call) {
        ++create_calls;
        session = "/org/freedesktop/portal/desktop/session/" + options.value("session_handle_token").toString();
        return respond(options, call, {{"session_handle", QVariant::fromValue(QDBusObjectPath(session))}});
    }
    QDBusObjectPath BindShortcuts(const QDBusObjectPath &, const QList<showthis::ShortcutDescription> &,
        const QString &, const QVariantMap &options, const QDBusMessage &call) {
        ++bind_calls;
        if (mode == "bind-denied") {
            denied_binding = true; // KDE remembers denied ids with no key assigned.
            return respond(options, call, {}, 1);
        }
        if (mode == "missing-shortcuts")
            return respond(options, call, {});
        auto shortcuts = binding();
        if (mode == "empty-bindings") shortcuts.clear();
        else if (mode == "missing-toggle") shortcuts[0].id = "unrelated";
        else if (mode == "missing-trigger") shortcuts[0].options.clear();
        else if (mode == "duplicate-toggle") shortcuts.append(shortcuts[0]);
        else if (mode == "empty-trigger" || denied_binding) shortcuts = binding(QString{});
        else if (mode == "whitespace-trigger") shortcuts = binding(QString(" \t"));
        else if (mode == "wrong-trigger") shortcuts = binding(42);
        return respond(options, call, {{"shortcuts", QVariant::fromValue(shortcuts)}});
    }
    void ConfigureShortcuts(const QDBusObjectPath &handle, const QString &, const QVariantMap &) {
        ++configure_calls;
        if (handle.path() != session || mode == "configure-error") {
            sendErrorReply(QDBusError::Failed, "Configuration unavailable");
        } else if (mode == "configure-unsupported") {
            sendErrorReply(QDBusError::UnknownMethod, "ConfigureShortcuts unavailable");
        } else if (mode == "configure-timeout") {
            setDelayedReply(true);
            pending_configuration = message();
        }
    }
};

class DesktopIntegrationTest : public QObject {
    Q_OBJECT
    QDBusConnection m_service = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "desktop-fixture");
    FakeCapture m_capture;
    FakePortal m_portal{m_service};
private slots:
    void initTestCase() {
        qDBusRegisterMetaType<showthis::ShortcutDescription>();
        qDBusRegisterMetaType<QList<showthis::ShortcutDescription>>();
        QVERIFY(m_service.isConnected());
        QVERIFY(m_service.registerService(service_name));
        QVERIFY(m_service.registerObject("/org/kde/KWin/ScreenShot2", &m_capture, QDBusConnection::ExportAllSlots));
        QVERIFY(m_service.registerObject("/org/freedesktop/portal/desktop", &m_portal,
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties));
    }
    void init() {
        m_capture.release();
        m_capture.mode.clear();
        m_portal.mode.clear();
        m_portal.version = 2;
        m_portal.create_calls = m_portal.bind_calls = m_portal.configure_calls = 0;
        m_portal.denied_binding = false;
        m_portal.pending_configuration = QDBusMessage{};
    }
    void capture_failures_data() {
        QTest::addColumn<QString>("mode");
        for (const auto *mode : {"stalled", "metadata", "oversized", "incomplete"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void capture_failures() {
        QFETCH(QString, mode);
        m_capture.mode = mode;
        showthis::KWinScreenCaptureBackend capture(nullptr, 100, QDBusConnection::sessionBus(), service_name);
        QSignalSpy failed(&capture, &showthis::AScreenCaptureBackend::capture_failed);
        QSignalSpy completed(&capture, &showthis::AScreenCaptureBackend::captured);
        capture.capture();
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(completed.size(), 0);
        m_capture.mode.clear();
        capture.capture();
        QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed[0][0].value<QImage>().size(), QSize(2, 2));
        QCOMPARE(failed.size(), 1);
    }
    void metadata_limits() {
        QVariantMap metadata{{"type", "raw"}, {"width", 100}, {"height", 100},
            {"stride", 400}, {"format", int(QImage::Format_RGB32)}};
        QCOMPARE(showthis::KWinScreenCaptureBackend::frame_size(metadata), 40000);
        metadata["stride"] = 399;
        QCOMPARE(showthis::KWinScreenCaptureBackend::frame_size(metadata), 0);
        metadata["stride"] = 1LL << 40;
        QCOMPARE(showthis::KWinScreenCaptureBackend::frame_size(metadata), 0);
        metadata["format"] = -1;
        QCOMPARE(showthis::KWinScreenCaptureBackend::frame_size(metadata), 0);
    }
    void portal_fast_response_and_activation() {
        showthis::GlobalShortcutPortal portal(nullptr, 1000, QDBusConnection::sessionBus(), service_name);
        QSignalSpy activated(&portal, &showthis::GlobalShortcutPortal::shortcut_activated);
        portal.start();
        QTRY_VERIFY2(portal.ready(), qPrintable(portal.error()));
        QVERIFY(m_portal.activate());
        QTRY_COMPARE(activated.size(), 1);
        portal.retry(); // Configuration does not duplicate activation handlers.
        QTRY_COMPARE(m_portal.configure_calls, 1);
        QVERIFY(m_portal.activate());
        QTRY_COMPARE(activated.size(), 2);
        QCOMPARE(m_portal.create_calls, 1);
        QCOMPARE(m_portal.bind_calls, 1);
    }
    void portal_failure_and_retry_data() {
        QTest::addColumn<QString>("mode");
        for (const auto *mode : {"denied", "timeout", "malformed"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void portal_failure_and_retry() {
        QFETCH(QString, mode);
        m_portal.mode = mode;
        showthis::GlobalShortcutPortal portal(nullptr, 100, QDBusConnection::sessionBus(), service_name);
        portal.start();
        QTRY_VERIFY(!portal.error().isEmpty());
        QVERIFY(!portal.ready());
        m_portal.mode.clear();
        portal.retry();
        QTRY_VERIFY2(portal.ready(), qPrintable(portal.error()));
    }
    void portal_session_closure_and_service_restart() {
        showthis::GlobalShortcutPortal portal(nullptr, 1000, QDBusConnection::sessionBus(), service_name);
        portal.start();
        QTRY_VERIFY(portal.ready());
        auto closed = QDBusMessage::createSignal(m_portal.session, "org.freedesktop.portal.Session", "Closed");
        closed << QVariantMap{};
        QVERIFY(m_service.send(closed));
        QTRY_VERIFY(!portal.ready());
        QVERIFY(!portal.error().isEmpty());
        portal.retry();
        QTRY_VERIFY(portal.ready());
        QVERIFY(m_service.unregisterService(service_name));
        QTRY_VERIFY(!portal.ready());
        QVERIFY(m_service.registerService(service_name));
        QTRY_VERIFY2(portal.ready(), qPrintable(portal.error()));
    }
    void portal_unusable_binding_data() {
        QTest::addColumn<QString>("mode");
        for (const auto *mode : {"missing-shortcuts", "empty-bindings", "missing-toggle",
                 "missing-trigger", "duplicate-toggle", "empty-trigger", "whitespace-trigger", "wrong-trigger"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void portal_unusable_binding() {
        QFETCH(QString, mode);
        m_portal.mode = mode;
        showthis::GlobalShortcutPortal portal(nullptr, 1000, QDBusConnection::sessionBus(), service_name);
        QSignalSpy activated(&portal, &showthis::GlobalShortcutPortal::shortcut_activated);
        portal.start();
        QTRY_VERIFY(!portal.error().isEmpty());
        QVERIFY(!portal.ready());
        QVERIFY(m_portal.activate());
        QVERIFY(m_portal.change(FakePortal::binding()));
        QTRY_VERIFY(portal.ready()); // A later settings change repairs the live session.
        QCOMPARE(activated.size(), 0);
        QCOMPARE(m_portal.configure_calls, 0); // Startup never opens settings.
        QVERIFY(m_portal.activate());
        QTRY_COMPARE(activated.size(), 1);
    }
    void portal_denied_then_configured() {
        m_portal.mode = "bind-denied";
        showthis::GlobalShortcutPortal portal(nullptr, 1000, QDBusConnection::sessionBus(), service_name);
        QSignalSpy activated(&portal, &showthis::GlobalShortcutPortal::shortcut_activated);
        portal.start();
        QTRY_VERIFY(!portal.error().isEmpty());
        QVERIFY(!portal.ready());
        QCOMPARE(m_portal.configure_calls, 0);
        const auto denied_session = m_portal.session;
        m_portal.mode.clear();
        portal.retry();
        QTRY_COMPARE(m_portal.configure_calls, 1);
        QTRY_VERIFY(portal.error().contains("configuration opened"));
        QVERIFY(!portal.ready()); // A successful void reply is not a usable binding.
        QCOMPARE(m_portal.session, denied_session);
        QCOMPARE(m_portal.create_calls, 1);
        QCOMPARE(m_portal.bind_calls, 1);
        QVERIFY(m_portal.change(FakePortal::binding(QString{})));
        QTRY_VERIFY(portal.error().contains("No usable"));
        QVERIFY(!portal.ready());
        QVERIFY(m_portal.change(FakePortal::binding()));
        QTRY_VERIFY(portal.ready());
        QVERIFY(portal.error().isEmpty());
        QVERIFY(m_portal.activate());
        QTRY_COMPARE(activated.size(), 1);
        QVERIFY(m_portal.change(FakePortal::binding(QString{})));
        QTRY_VERIFY(!portal.ready()); // Clearing the key re-enables tray retry.
        QVERIFY(m_portal.activate());
        QVERIFY(m_portal.change(FakePortal::binding()));
        QTRY_VERIFY(portal.ready());
        QCOMPARE(activated.size(), 1);
    }
    void portal_persisted_denial_and_restart() {
        m_portal.denied_binding = true;
        showthis::GlobalShortcutPortal portal(nullptr, 1000, QDBusConnection::sessionBus(), service_name);
        portal.start();
        QTRY_VERIFY(!portal.error().isEmpty());
        QVERIFY(!portal.ready());
        const auto old_session = m_portal.session;
        portal.start(); // Automatic startup/restart must not open configuration.
        QTRY_COMPARE(m_portal.bind_calls, 2);
        QTRY_VERIFY(!portal.error().isEmpty());
        QCOMPARE(m_portal.configure_calls, 0);
        QVERIFY(m_portal.change(FakePortal::binding(), old_session));
        portal.retry();
        QTRY_COMPARE(m_portal.configure_calls, 1);
        QTRY_VERIFY(!portal.error().isEmpty());
        QVERIFY(!portal.ready());
        QVERIFY(m_portal.change(FakePortal::binding()));
        QTRY_VERIFY(portal.ready());
    }
    void portal_retry_after_session_loss_configures() {
        m_portal.mode = "bind-denied";
        showthis::GlobalShortcutPortal portal(nullptr, 1000, QDBusConnection::sessionBus(), service_name);
        portal.start();
        QTRY_VERIFY(!portal.error().isEmpty());
        const auto old_session = m_portal.session;
        QVERIFY(m_portal.close_session());
        QTRY_VERIFY(portal.error().contains("session was closed"));
        m_portal.mode.clear();
        portal.retry(); // A new session sees KDE's persisted empty shortcut.
        QTRY_COMPARE(m_portal.configure_calls, 1);
        QTRY_VERIFY(portal.error().contains("configuration opened"));
        QVERIFY(!portal.ready());
        QCOMPARE(m_portal.create_calls, 2);
        QCOMPARE(m_portal.bind_calls, 2);
        QVERIFY(m_portal.close_session(old_session));
        QVERIFY(m_portal.change(FakePortal::binding()));
        QTRY_VERIFY(portal.ready());
    }
    void portal_configuration_failure_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<uint>("version");
        QTest::newRow("unsupported-method") << QString("configure-unsupported") << uint(2);
        QTest::newRow("version-one") << QString{} << uint(1);
        QTest::newRow("method-error") << QString("configure-error") << uint(2);
        QTest::newRow("method-timeout") << QString("configure-timeout") << uint(2);
    }
    void portal_configuration_failure() {
        QFETCH(QString, mode);
        QFETCH(uint, version);
        m_portal.denied_binding = true;
        m_portal.version = version;
        showthis::GlobalShortcutPortal portal(nullptr, 100, QDBusConnection::sessionBus(), service_name);
        portal.start();
        QTRY_VERIFY(!portal.error().isEmpty());
        m_portal.mode = mode;
        portal.retry();
        QTRY_VERIFY(portal.error().contains("System Settings"));
        QVERIFY(!portal.ready());
        QCOMPARE(m_portal.configure_calls, version < 2 ? 0 : 1);
        // Keep the session: even v1/error/timeout can recover through desktop settings.
        QVERIFY(m_portal.change(FakePortal::binding()));
        QTRY_VERIFY(portal.ready());
        if (mode == "configure-timeout")
            QVERIFY(m_service.send(m_portal.pending_configuration.createReply()));
        QVERIFY(m_portal.change(FakePortal::binding(QString{})));
        QTRY_VERIFY(!portal.ready());
        m_portal.mode.clear();
        m_portal.version = 2;
        portal.retry();
        QTRY_COMPARE(m_portal.configure_calls, version < 2 ? 1 : 2);
        QVERIFY(m_portal.change(FakePortal::binding()));
        QTRY_VERIFY(portal.ready());
        QVERIFY(portal.error().isEmpty());
        QCOMPARE(m_portal.create_calls, 1);
        QCOMPARE(m_portal.bind_calls, 1);
    }
    void portal_changed_and_activation_filtering() {
        showthis::GlobalShortcutPortal portal(nullptr, 1000, QDBusConnection::sessionBus(), service_name);
        QSignalSpy activated(&portal, &showthis::GlobalShortcutPortal::shortcut_activated);
        portal.start();
        QTRY_VERIFY(portal.ready());
        const QString other_session = "/org/freedesktop/portal/desktop/session/unrelated";
        QVERIFY(m_portal.change(FakePortal::binding(QString{}), other_session));
        QVERIFY(m_portal.change({{"unrelated", {{"trigger_description", ""}}}}));
        QVERIFY(m_portal.activate("unrelated"));
        QVERIFY(m_portal.activate("toggle-window", other_session));
        QVERIFY(m_portal.activate());
        QTRY_COMPARE(activated.size(), 1);
        QVERIFY(portal.ready());
        // The fast settings signal may arrive before ConfigureShortcuts returns.
        m_portal.mode = "configure-timeout";
        portal.retry();
        QTRY_COMPARE(m_portal.configure_calls, 1);
        QSignalSpy changed(&portal, &showthis::GlobalShortcutPortal::state_changed);
        QVERIFY(m_portal.change(FakePortal::binding()));
        QTRY_COMPARE(changed.size(), 1);
        QVERIFY(m_service.send(m_portal.pending_configuration.createErrorReply(QDBusError::Failed, "Late failure")));
        QVERIFY(m_portal.change(FakePortal::binding(QString{})));
        QTRY_VERIFY(!portal.ready());
        QVERIFY(portal.error().contains("No usable"));
    }
};

QTEST_GUILESS_MAIN(DesktopIntegrationTest)
#include "DesktopIntegrationTest.moc"
