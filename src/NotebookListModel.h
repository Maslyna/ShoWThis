#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QVariantMap>

namespace showthis {

// Typed model roles and a precomputed search index keep filtering in C++.
// Original maps remain the serialization/extension boundary for legacy cards.
class NotebookListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE set_filter NOTIFY filter_changed)
    Q_PROPERTY(QStringList visible_ids READ visible_ids NOTIFY contents_changed)
public:
    enum Role { EntryRole = Qt::UserRole + 1, IdRole, WordRole };
    explicit NotebookListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void replace(const QVariantList &entries);
    QString filter() const { return m_filter; }
    void set_filter(const QString &filter);
    QStringList visible_ids() const;
    Q_INVOKABLE bool contains(const QString &id) const { return m_ids.contains(id); }
signals:
    void filter_changed();
    void contents_changed();
private:
    struct Entry { QString id; QString word; QString search; QVariantMap fields; };
    void rebuild();
    QList<Entry> m_entries;
    QList<int> m_visible;
    QSet<QString> m_ids;
    QString m_filter;
};

}  // namespace showthis
