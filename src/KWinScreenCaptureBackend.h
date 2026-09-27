#ifndef KWINSCREENCAPTUREBACKEND_H
#define KWINSCREENCAPTUREBACKEND_H

#include <QObject>
#include <QSocketNotifier>

#include "./AScreenCaptureBackend.h"

class QDBusPendingCallWatcher;

namespace showthis {

class KWinScreenCaptureBackend : public AScreenCaptureBackend {
    Q_OBJECT
public:
    explicit KWinScreenCaptureBackend(QObject *parent = nullptr);
    ~KWinScreenCaptureBackend() override;

    void capture() override;

private:
    void on_call_finished(QDBusPendingCallWatcher *watcher);
    void on_pipe_ready_read();
    void reset();
    void finish_with_failure(const QString &reason);

    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_buffer;
    int m_width = 0, m_height = 0, m_stride = 0, m_format = 0;
    int m_pipe_read_fd = -1;
};

}  // namespace showthis
#endif  // KWINSCREENCAPTUREBACKEND_H
