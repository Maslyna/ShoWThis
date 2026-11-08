#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>

namespace showthis {

struct NotebookContents {
    QVariantList cards;
    QJsonObject cache;
    QVariantList history;
};

// Owns the on-disk format, migrations, and atomic writes. Callers commit their
// in-memory changes only after save() succeeds.
class LearningStore {
public:
    explicit LearningStore(const QString &file_name = {});
    NotebookContents load(int history_limit);
    bool save(const QVariantList &cards, const QJsonObject &cache, const QVariantList &history);
    static void trim_history(QVariantList &history, QJsonObject &cache, int limit);

    QString file_name() const { return m_file_name; }
    QString error() const { return m_error; }
    bool readable() const { return m_readable; }

private:
    QString m_file_name;
    QString m_error;
    bool m_readable = true;
    QJsonObject m_document;
};

}  // namespace showthis
