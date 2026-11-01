#include "LearningStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace showthis {

LearningStore::LearningStore(const QString &file_name)
    : m_file_name(file_name.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/vocabulary.json"
        : file_name) {}

NotebookContents LearningStore::load(int history_limit) {
    NotebookContents contents;
    QFile file(m_file_name);
    if (!file.exists())
        return contents;
    QJsonParseError error;
    const bool opened = file.open(QIODevice::ReadOnly);
    const auto document = opened ? QJsonDocument::fromJson(file.readAll(), &error) : QJsonDocument{};
    const auto object = document.object();
    if (!opened || error.error != QJsonParseError::NoError || !document.isObject()
        || object.value("version").toInt() != 1 || !object.value("cards").isArray()
        || !object.value("dictionary_cache").isObject()
        || (object.contains("dictionary_history") && !object.value("dictionary_history").isArray())) {
        m_readable = false;
        m_error = QObject::tr("Cannot read vocabulary file: %1. It will not be overwritten.").arg(m_file_name);
        return contents;
    }
    // Keep unknown fields, including metadata from discontinued features.
    m_document = object;
    contents.cards = object.value("cards").toArray().toVariantList();
    for (auto &value : contents.cards) {
        auto card = value.toMap();
        if (!card.contains("article")) {
            card.insert("article", card.value("sense").toMap().value("article").toString());
            value = card;
        }
    }
    contents.cache = object.value("dictionary_cache").toObject();
    QSet<QString> seen;
    for (const auto &value : object.value("dictionary_history").toArray()) {
        const auto item = value.toObject();
        const auto key = item.value("key").toString();
        if (key.isEmpty() || seen.contains(key) || !contents.cache.value(key).isObject())
            continue;
        seen.insert(key);
        contents.history.append(item.toVariantMap());
    }
    // Version 1 files predate history. Preserve their cached lookups on migration.
    for (auto it = contents.cache.constBegin(); it != contents.cache.constEnd(); ++it) {
        if (seen.contains(it.key()) || !it.value().isObject())
            continue;
        const auto separator = it.key().indexOf(':');
        if (separator <= 0)
            continue;
        contents.history.append(QVariantMap{{"key", it.key()}, {"word", it.value().toObject().value("word").toString(it.key().mid(separator + 1).section('|', 0, 0))},
            {"language", it.key().left(separator)}, {"context", ""}, {"last_lookup", ""}});
    }
    const auto old_count = contents.cache.size();
    trim_history(contents.history, contents.cache, history_limit);
    if (old_count != contents.cache.size())
        save(contents.cards, contents.cache, contents.history);
    return contents;
}

void LearningStore::trim_history(QVariantList &history, QJsonObject &cache, int limit) {
    limit = qMax(0, limit);
    while (history.size() > limit)
        history.removeLast();
    QJsonObject retained;
    for (const auto &value : history) {
        const auto key = value.toMap().value("key").toString();
        if (cache.contains(key))
            retained.insert(key, cache.value(key));
    }
    cache = retained;
}

bool LearningStore::save(const QVariantList &cards, const QJsonObject &cache, const QVariantList &history) {
    if (!m_readable)
        return false;
    auto object = m_document;
    object.insert("version", 1);
    object.insert("cards", QJsonArray::fromVariantList(cards));
    object.insert("dictionary_cache", cache);
    object.insert("dictionary_history", QJsonArray::fromVariantList(history));
    const auto bytes = QJsonDocument(object).toJson();
    QSaveFile file(m_file_name);
    if (!QDir().mkpath(QFileInfo(m_file_name).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = QObject::tr("Cannot save vocabulary: %1").arg(m_file_name);
        return false;
    }
    m_document = object;
    m_error.clear();
    return true;
}

}  // namespace showthis
