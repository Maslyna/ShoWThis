#include <QtTest>
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

class FakePortal : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.GlobalShortcuts")
public:
    QDBusConnection bus;
    QString mode;
    QString session = "/org/freedesktop/portal/desktop/session/test_session";
    explicit FakePortal(const QDBusConnection &connection) : bus(connection) {}
    QDBusObjectPath respond(const QVariantMap &options, const QDBusMessage &call, const QVariantMap &results) {
        const auto path = showthis::GlobalShortcutPortal::request_path(call.service(), options.value("handle_token").toString());
        if (mode != "timeout") {
            auto reply = QDBusMessage::createSignal(path, "org.freedesktop.portal.Request", "Response");
            if (mode == "malformed") reply << QString("wrong signature");
            else reply << uint(mode == "denied" ? 1 : 0) << results;
            bus.send(reply); // Deliberately before returning the method reply.
        }
        return QDBusObjectPath(path);
    }
public slots:
    QDBusObjectPath CreateSession(const QVariantMap &options, const QDBusMessage &call) {
        return respond(options, call, {{"session_handle", QVariant::fromValue(QDBusObjectPath(session))}});
    }
    QDBusObjectPath BindShortcuts(const QDBusObjectPath &, const QList<showthis::ShortcutDescription> &,
        const QString &, const QVariantMap &options, const QDBusMessage &call) {
        return respond(options, call, {});
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
        QVERIFY(m_service.registerObject("/org/freedesktop/portal/desktop", &m_portal, QDBusConnection::ExportAllSlots));
    }
    void init() { m_capture.release(); m_capture.mode.clear(); m_portal.mode.clear(); }
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
        auto signal = QDBusMessage::createSignal("/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.GlobalShortcuts", "Activated");
        signal << QVariant::fromValue(QDBusObjectPath(m_portal.session)) << QString("toggle-window") << quint64(1) << QVariantMap{};
        QVERIFY(m_service.send(signal));
        QTRY_COMPARE(activated.size(), 1);
        portal.start(); // Retry is safe and does not duplicate activation handlers.
        QTRY_VERIFY(portal.ready());
        QVERIFY(m_service.send(signal));
        QTRY_COMPARE(activated.size(), 2);
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
        portal.start();
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
        portal.start();
        QTRY_VERIFY(portal.ready());
        QVERIFY(m_service.unregisterService(service_name));
        QTRY_VERIFY(!portal.ready());
        QVERIFY(m_service.registerService(service_name));
        QTRY_VERIFY2(portal.ready(), qPrintable(portal.error()));
    }
};

QTEST_GUILESS_MAIN(DesktopIntegrationTest)
#include "DesktopIntegrationTest.moc"
