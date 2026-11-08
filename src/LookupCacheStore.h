#pragma once

#include "LearningStore.h"
#include <QFutureWatcher>
#include <QTimer>
#include <QHash>

namespace showthis {

// Disposable lookup data is independently bounded and written off the UI thread.
class LookupCacheStore : public QObject {
    Q_OBJECT
public:
    explicit LookupCacheStore(const QString &path, QObject *parent = nullptr);
    ~LookupCacheStore() override;
    NotebookContents load(const NotebookContents &legacy);
    bool submit(const QJsonObject &cache, const QVariantList &history, bool flush = false);
    bool flush();
    QString path() const { return m_path; }
    QString error() const { return m_error; }
    void allow_reset() { m_blocked = false; }
    static void trim(QVariantList &history, QJsonObject &cache, int limit, QHash<QString, qint64> &sizes,
        qint64 budget = 16 * 1024 * 1024, qint64 entry_limit = 1024 * 1024);
signals:
    void error_occurred(const QString &message);
    void error_changed();
private:
    void start_write();
    void finished();
    QString m_path;
    QString m_error;
    QJsonObject m_snapshot;
    QFutureWatcher<QString> m_writer;
    QTimer m_debounce;
    quint64 m_revision = 0, m_running_revision = 0;
    bool m_dirty = false, m_pending_result = false, m_blocked = false;
};

}  // namespace showthis
