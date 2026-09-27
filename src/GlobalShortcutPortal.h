
#ifndef GLOBALSHORTCUTPORTAL_H
#define GLOBALSHORTCUTPORTAL_H

#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QObject>

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
    explicit GlobalShortcutPortal(QObject *parent = nullptr);

    void start();

signals:
    void shortcut_activated(const QString &shortcutId);

private slots:
    void on_session_created(const QDBusMessage &msg);
    void on_shortcuts_bound(const QDBusMessage &msg);
    void on_activated(const QDBusMessage &msg);

private:
    static QString gen_new_token();

    QDBusInterface *m_portal;
    QString m_sessionHandle;
};

}

Q_DECLARE_METATYPE(showthis::ShortcutDescription)

#endif  // GLOBALSHORTCUTPORTAL_H
