#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QTimer>

namespace showthis {

struct ShortcutDescription {
    QString id;
    QVariantMap options;
};
QDBusArgument &operator<<(QDBusArgument &arg, const ShortcutDescription &shortcut);
const QDBusArgument &operator>>(const QDBusArgument &arg, ShortcutDescription &shortcut);

class GlobalShortcutPortal : public QObject {
    Q_OBJECT
public:
    explicit GlobalShortcutPortal(QObject *parent = nullptr, int timeout_ms = 120000,
        const QDBusConnection &bus = QDBusConnection::sessionBus(),
        const QString &service = QStringLiteral("org.freedesktop.portal.Desktop"));
    ~GlobalShortcutPortal() override;
    void start();
    bool ready() const { return m_ready; }
    QString error() const { return m_error; }
    static QString request_path(const QString &unique_name, const QString &token);

signals:
    void shortcut_activated(const QString &shortcutId);
    void state_changed();

private slots:
    void on_response(const QDBusMessage &message);
    void on_activated(const QDBusMessage &message);
    void on_session_closed();

private:
    enum class Phase { Idle, Registering, Creating, Binding, Ready };
    void create_session();
    void request(const QString &method, QList<QVariant> arguments, const QString &token, Phase phase);
    void reset();
    void fail(const QString &message);
    QDBusConnection m_bus;
    QString m_service;
    QString m_session;
    QString m_request;
    QString m_error;
    QTimer m_deadline;
    quint64 m_generation = 0;
    Phase m_phase = Phase::Idle;
    bool m_ready = false;
};

}  // namespace showthis

Q_DECLARE_METATYPE(showthis::ShortcutDescription)
