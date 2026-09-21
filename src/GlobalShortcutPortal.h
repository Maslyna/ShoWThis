
#ifndef GLOBALSHORTCUTPORTAL_H
#define GLOBALSHORTCUTPORTAL_H

#include <QObject>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusInterface>

struct ShortcutDescription
{
    QString id;
    QVariantMap options;
};
Q_DECLARE_METATYPE(ShortcutDescription)

QDBusArgument &operator<<(QDBusArgument &arg, const ShortcutDescription &shortcut);
const QDBusArgument &operator>>(const QDBusArgument &arg, ShortcutDescription &shortcut);

class GlobalShortcutPortal : public QObject
{
    Q_OBJECT
public:
    explicit GlobalShortcutPortal(QObject *parent = nullptr);

    void start();

signals:
    void shortcutActivated(const QString &shortcutId);

private slots:
    void onSessionCreated(const QDBusMessage &msg);
    void onShortcutsBound(const QDBusMessage &msg);
    void onActivated(const QDBusMessage &msg);

private:
    static QString newToken();

    QDBusInterface *m_portal;
    QString m_sessionHandle;
};

#endif // GLOBALSHORTCUTPORTAL_H

