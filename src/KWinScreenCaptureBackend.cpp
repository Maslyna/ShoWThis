#include "KWinScreenCaptureBackend.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QImage>
#include <QSocketNotifier>
#include <QString>
#include <QStringView>

#include "ShoWThisConstants.h"

namespace showthis {

KWinScreenCaptureBackend::KWinScreenCaptureBackend(QObject *parent)
    : AScreenCaptureBackend(parent) {}

KWinScreenCaptureBackend::~KWinScreenCaptureBackend() {
    reset();
}

void KWinScreenCaptureBackend::capture() {
    reset();

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) == -1) {
        finish_with_failure(QStringLiteral("failed to create pipe"));
        return;
    }
    m_pipe_read_fd     = fds[0];
    const int write_fd = fds[1];

    QVariantMap options;
    options.insert(QStringLiteral("include-cursor"), false);
    options.insert(QStringLiteral("native-resolution"), true);

    QDBusMessage message = QDBusMessage::createMethodCall(showthis::KWIN_SCREENSHOT2_SERVICE,
                                                          showthis::KWIN_SCREENSHOT2_OBJECT_PATH,
                                                          showthis::KWIN_SCREENSHOT2_IFACE,
                                                          QStringLiteral("CaptureActiveScreen"));
    message << QVariant(options) << QVariant::fromValue(QDBusUnixFileDescriptor(write_fd));

    // QDBusUnixFileDescriptor dup()s writeFd when it's marshalled; close our copy.
    ::close(write_fd);

    QDBusPendingCall capture_call = QDBusConnection::sessionBus().asyncCall(message);
    auto *watcher                 = new QDBusPendingCallWatcher(capture_call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &KWinScreenCaptureBackend::on_call_finished);
}

void KWinScreenCaptureBackend::on_call_finished(QDBusPendingCallWatcher *watcher) {
    watcher->deleteLater();
    QDBusPendingReply<QVariantMap> reply = *watcher;

    if (reply.isError()) {
        ::close(m_pipe_read_fd);
        m_pipe_read_fd = -1;
        finish_with_failure(reply.error().message());
        return;
    }

    const QVariantMap metadata = reply.value();
    if (metadata.value(QStringLiteral("type")).toString() != QLatin1String("raw")) {
        finish_with_failure(QStringLiteral("unexpected screenshot payload type"));
        return;
    }

    m_width  = metadata.value(QStringLiteral("width")).toUInt();
    m_height = metadata.value(QStringLiteral("height")).toUInt();
    m_stride = metadata.value(QStringLiteral("stride")).toUInt();
    m_format = metadata.value(QStringLiteral("format")).toUInt();

    fcntl(m_pipe_read_fd, F_SETFL, fcntl(m_pipe_read_fd, F_GETFL, 0) | O_NONBLOCK);
    m_notifier = new QSocketNotifier(m_pipe_read_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &KWinScreenCaptureBackend::on_pipe_ready_read);
}

void KWinScreenCaptureBackend::on_pipe_ready_read() {
    char chunk[65536] = {};
    while (true) {
        const qint64 n = ::read(m_pipe_read_fd, chunk, sizeof(chunk));
        if (n > 0) {
            m_buffer.append(chunk, n);
            continue;
        }
        if (n == 0)
            break;  // EOF: KWin's writer closed the fd, frame is complete
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;  // wait for next activation
        finish_with_failure(QStringLiteral("error reading screenshot pipe"));
        return;
    }

    const auto expected = static_cast<qsizetype>(m_stride) * m_height;
    if (m_buffer.size() < expected) {
        finish_with_failure(QStringLiteral("incomplete screenshot data"));
        return;
    }

    QImage image(reinterpret_cast<const uchar *>(m_buffer.constData()),
                 m_width,
                 m_height,
                 m_stride,
                 static_cast<QImage::Format>(m_format));
    QImage result = image.copy();  // detach before m_buffer is freed

    reset();
    emit captured(result);
}

void KWinScreenCaptureBackend::finish_with_failure(const QString &reason) {
    reset();
    emit capture_failed(reason);
}

void KWinScreenCaptureBackend::reset() {
    delete m_notifier;
    m_notifier = nullptr;
    if (m_pipe_read_fd != -1) {
        ::close(m_pipe_read_fd);
        m_pipe_read_fd = -1;
    }
    m_buffer.clear();
    m_width = m_height = m_stride = m_format = 0;
}

}
