#ifndef KWINSCREENCAPTUREBACKEND_H
#define KWINSCREENCAPTUREBACKEND_H

#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <QPointer>
#include <QVariantMap>
#include <QDBusConnection>

#include "./AScreenCaptureBackend.h"

class QDBusPendingCallWatcher;

namespace showthis {

class KWinScreenCaptureBackend : public AScreenCaptureBackend {
    Q_OBJECT
public:
    explicit KWinScreenCaptureBackend(QObject *parent = nullptr, int timeout_ms = 15000,
        const QDBusConnection &bus = QDBusConnection::sessionBus(),
        const QString &service = QStringLiteral("org.kde.KWin.ScreenShot2"));
    ~KWinScreenCaptureBackend() override;

    void capture() override;
    static qint64 frame_size(const QVariantMap &metadata);

private:
    void on_call_finished(QDBusPendingCallWatcher *watcher);
    void on_pipe_ready_read();
    void reset();
    void finish_with_failure(const QString &reason);

    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_buffer;
    int m_width = 0, m_height = 0, m_stride = 0, m_format = 0;
    int m_pipe_read_fd = -1;
    QTimer m_deadline;
    QPointer<QDBusPendingCallWatcher> m_call;
    QDBusConnection m_bus;
    QString m_service;
    qint64 m_expected = 0;
};

}  // namespace showthis
#endif  // KWINSCREENCAPTUREBACKEND_H
