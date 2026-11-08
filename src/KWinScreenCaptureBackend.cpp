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

KWinScreenCaptureBackend::KWinScreenCaptureBackend(QObject *parent, int timeout_ms,
    const QDBusConnection &bus, const QString &service)
    : AScreenCaptureBackend(parent), m_bus(bus), m_service(service) {
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(qMax(1, timeout_ms));
    connect(&m_deadline, &QTimer::timeout, this, [this]() {
        finish_with_failure(tr("Screen capture timed out. Try capturing again."));
    });
}

qint64 KWinScreenCaptureBackend::frame_size(const QVariantMap &metadata) {
    if (metadata.value("type").toString() != "raw")
        return 0;
    bool width_ok, height_ok, stride_ok, format_ok;
    const auto width = metadata.value("width").toLongLong(&width_ok);
    const auto height = metadata.value("height").toLongLong(&height_ok);
    const auto stride = metadata.value("stride").toLongLong(&stride_ok);
    const auto format = metadata.value("format").toInt(&format_ok);
    if (!width_ok || !height_ok || !stride_ok || !format_ok || width <= 0 || height <= 0
        || width > 32768 || height > 32768 || stride <= 0
        || format <= QImage::Format_Invalid || format >= QImage::NImageFormats)
        return 0;
    const QImage sample(1, 1, QImage::Format(format));
    if (sample.isNull() || sample.depth() < 8 || stride < (width * sample.depth() + 7) / 8
        || stride > (256LL * 1024 * 1024) / height)
        return 0;
    return stride * height;
}

KWinScreenCaptureBackend::~KWinScreenCaptureBackend() {
    reset();
}

void KWinScreenCaptureBackend::capture() {
    reset();
    m_deadline.start();

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

    QDBusMessage message = QDBusMessage::createMethodCall(m_service,
                                                          showthis::KWIN_SCREENSHOT2_OBJECT_PATH,
                                                          showthis::KWIN_SCREENSHOT2_IFACE,
                                                          QStringLiteral("CaptureActiveScreen"));
    message << QVariant(options) << QVariant::fromValue(QDBusUnixFileDescriptor(write_fd));

    // QDBusUnixFileDescriptor dup()s writeFd when it's marshalled; close our copy.
    ::close(write_fd);

    QDBusPendingCall capture_call = m_bus.asyncCall(message, m_deadline.interval());
    auto *watcher                 = new QDBusPendingCallWatcher(capture_call, this);
    m_call = watcher;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &KWinScreenCaptureBackend::on_call_finished);
}

void KWinScreenCaptureBackend::on_call_finished(QDBusPendingCallWatcher *watcher) {
    if (m_call != watcher)
        return;
    watcher->deleteLater();
    m_call.clear();
    QDBusPendingReply<QVariantMap> reply = *watcher;

    if (reply.isError()) {
        finish_with_failure(reply.error().message());
        return;
    }

    const QVariantMap metadata = reply.value();
    m_expected = frame_size(metadata);
    if (!m_expected) {
        finish_with_failure(tr("Screen capture returned invalid or oversized image metadata."));
        return;
    }

    m_width  = metadata.value(QStringLiteral("width")).toUInt();
    m_height = metadata.value(QStringLiteral("height")).toUInt();
    m_stride = metadata.value(QStringLiteral("stride")).toUInt();
    m_format = metadata.value(QStringLiteral("format")).toUInt();

    if (fcntl(m_pipe_read_fd, F_SETFL, fcntl(m_pipe_read_fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
        finish_with_failure(tr("Cannot read the screenshot pipe."));
        return;
    }
    m_notifier = new QSocketNotifier(m_pipe_read_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &KWinScreenCaptureBackend::on_pipe_ready_read);
}

void KWinScreenCaptureBackend::on_pipe_ready_read() {
    char chunk[65536] = {};
    while (true) {
        const qint64 n = ::read(m_pipe_read_fd, chunk, sizeof(chunk));
        if (n > 0) {
            if (n > m_expected - m_buffer.size()) {
                finish_with_failure(tr("Screen capture exceeded its declared image size."));
                return;
            }
            m_buffer.append(chunk, n);
            continue;
        }
        if (n == 0)
            break;  // EOF: KWin's writer closed the fd, frame is complete
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;  // wait for next activation
        if (errno == EINTR)
            continue;
        finish_with_failure(QStringLiteral("error reading screenshot pipe"));
        return;
    }

    if (m_buffer.size() != m_expected) {
        finish_with_failure(QStringLiteral("incomplete screenshot data"));
        return;
    }

    QImage image(reinterpret_cast<const uchar *>(m_buffer.constData()),
                 m_width,
                 m_height,
                 m_stride,
                 static_cast<QImage::Format>(m_format));
    QImage result = image.copy();  // detach before m_buffer is freed
    if (result.isNull()) {
        finish_with_failure(tr("Screen capture returned an unreadable image."));
        return;
    }

    reset();
    emit captured(result);
}

void KWinScreenCaptureBackend::finish_with_failure(const QString &reason) {
    reset();
    emit capture_failed(reason);
}

void KWinScreenCaptureBackend::reset() {
    m_deadline.stop();
    if (m_call) {
        m_call->disconnect(this);
        m_call->deleteLater();
        m_call.clear();
    }
    delete m_notifier;
    m_notifier = nullptr;
    if (m_pipe_read_fd != -1) {
        ::close(m_pipe_read_fd);
        m_pipe_read_fd = -1;
    }
    m_buffer.clear();
    m_width = m_height = m_stride = m_format = 0;
    m_expected = 0;
}

}
