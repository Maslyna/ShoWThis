#include "NotebookListModel.h"

namespace showthis {

int NotebookListModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_visible.size(); }

QVariant NotebookListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visible.size()) return {};
    const auto &entry = m_entries[m_visible[index.row()]];
    switch (role) {
    case EntryRole: return entry.fields;
    case IdRole: return entry.id;
    case WordRole: return entry.word;
    default: return {};
    }
}

QHash<int, QByteArray> NotebookListModel::roleNames() const {
    return {{EntryRole, "modelData"}, {IdRole, "cardId"}, {WordRole, "word"}};
}

void NotebookListModel::replace(const QVariantList &entries) {
    beginResetModel();
    m_entries.clear();
    m_ids.clear();
    for (const auto &value : entries) {
        const auto fields = value.toMap();
        QStringList search;
        for (const auto *field : {"word", "meaning", "translation", "context", "notes", "summary"})
            search.append(fields.value(field).toString());
        Entry entry{fields.value("id").toString(), fields.value("word").toString(), search.join(' ').toCaseFolded(), fields};
        m_ids.insert(entry.id);
        m_entries.append(std::move(entry));
    }
    rebuild();
    endResetModel();
    emit contents_changed();
}

void NotebookListModel::rebuild() {
    m_visible.clear();
    const auto query = m_filter.toCaseFolded();
    for (int i = 0; i < m_entries.size(); ++i)
        if (query.isEmpty() || m_entries[i].search.contains(query)) m_visible.append(i);
}

void NotebookListModel::set_filter(const QString &filter) {
    if (filter == m_filter) return;
    beginResetModel();
    m_filter = filter;
    rebuild();
    endResetModel();
    emit filter_changed();
    emit contents_changed();
}

QStringList NotebookListModel::visible_ids() const {
    QStringList result;
    for (const auto index : m_visible) result.append(m_entries[index].id);
    return result;
}

}  // namespace showthis
