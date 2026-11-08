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
namespace {

bool string_array(const QJsonValue &value) {
    if (!value.isArray())
        return false;
    for (const auto &item : value.toArray()) {
        if (!item.isString())
            return false;
    }
    return true;
}

bool valid_card(const QJsonObject &card) {
    if (!card.value("id").isString() || card.value("id").toString().trimmed().isEmpty()
        || !card.value("word").isString() || card.value("word").toString().trimmed().isEmpty())
        return false;
    // Older cards may omit optional fields. Unknown fields are deliberately
    // unrestricted and retained; known fields must have the types the UI uses.
    for (const auto *key : {"language", "target_language", "context", "meaning", "translation", "notes",
             "article", "base_form", "base_article", "article_origin", "base_article_origin",
             "singular_status", "plural_status", "created", "updated"}) {
        if (card.contains(key) && !card.value(key).isString())
            return false;
    }
    // Earlier manual cards wrote a JSON null when no profile was available.
    if (card.contains("grammar_profile") && !card.value("grammar_profile").isString()
        && !card.value("grammar_profile").isNull())
        return false;
    for (const auto *key : {"singular_forms", "plural_forms"}) {
        if (card.contains(key) && !string_array(card.value(key)))
            return false;
    }
    for (const auto *key : {"number_relevant", "follow_forms"}) {
        if (card.contains(key) && !card.value(key).isBool())
            return false;
    }
    for (const auto *key : {"source", "sense"}) {
        if (card.contains(key) && !card.value(key).isObject())
            return false;
    }
    if (card.contains("dictionary_selection")) {
        if (!card.value("dictionary_selection").isArray())
            return false;
        for (const auto &value : card.value("dictionary_selection").toArray()) {
            const auto selection = value.toObject();
            if (!value.isObject() || !selection.value("provider").isString()
                || selection.value("provider").toString().isEmpty()
                || !string_array(selection.value("languages"))
                || (selection.contains("fallback") && !selection.value("fallback").isBool()))
                return false;
        }
    }
    return true;
}

bool valid_cards(const QJsonArray &cards, QString &error) {
    QSet<QString> ids;
    for (qsizetype i = 0; i < cards.size(); ++i) {
        const auto card = cards[i].toObject();
        if (!cards[i].isObject() || !valid_card(card)) {
            error = QObject::tr("Card %1 has missing or invalid fields.").arg(i + 1);
            return false;
        }
        const auto id = card.value("id").toString();
        if (ids.contains(id)) {
            error = QObject::tr("Card %1 has a duplicate ID.").arg(i + 1);
            return false;
        }
        ids.insert(id);
    }
    return true;
}

}  // namespace

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
    const bool opened = file.size() <= 64 * 1024 * 1024 && file.open(QIODevice::ReadOnly);
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
    QString card_error;
    if (!valid_cards(object.value("cards").toArray(), card_error)) {
        m_readable = false;
        m_error = QObject::tr("Cannot read vocabulary file: %1. %2 It will not be overwritten.")
            .arg(m_file_name, card_error);
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
    const auto card_array = QJsonArray::fromVariantList(cards);
    QString card_error;
    if (!valid_cards(card_array, card_error)) {
        m_error = QObject::tr("Cannot save vocabulary: %1").arg(card_error);
        return false;
    }
    auto object = m_document;
    object.insert("version", 1);
    object.insert("cards", card_array);
    object.insert("dictionary_cache", cache);
    object.insert("dictionary_history", QJsonArray::fromVariantList(history));
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() > 16 * 1024 * 1024) {
        m_error = QObject::tr("Vocabulary exceeds the 16 MiB safety limit. No data was overwritten.");
        return false;
    }
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
