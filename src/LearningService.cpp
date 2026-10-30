#include "LearningService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QSet>
#include <QRegularExpression>
#include <QLocale>
#include <algorithm>

namespace showthis {
namespace {

QString single_article(const QVariantMap &sense) {
    // Preserve explicit dictionary metadata, but never infer an article from
    // gender using language-specific rules.
    return sense.value("article").toString();
}

QString normalized_language_code(QString code) {
    code = code.trimmed().toLower().replace('_', '-');
    static const QRegularExpression pattern("^[a-z]{2,3}(?:-[a-z0-9]{2,8})*$");
    if (!pattern.match(code).hasMatch())
        return {};
    const auto base = code.section('-', 0, 0);
    const auto language = QLocale::codeToLanguage(base);
    if (language != QLocale::C && language != QLocale::AnyLanguage) {
        const auto short_code = QLocale::languageToCode(language, QLocale::ISO639Part1);
        if (!short_code.isEmpty())
            code.replace(0, base.size(), short_code);
    }
    return code;
}

QString word_with_article(const QString &word, const QString &article) {
    if (article.isEmpty() || word.startsWith(article + ' ', Qt::CaseInsensitive))
        return word;
    if (article.endsWith('\'') || article.endsWith(QChar(0x2019)) || article.endsWith('-'))
        return word.startsWith(article, Qt::CaseInsensitive) ? word : article + word;
    return article + ' ' + word;
}

void append_senses(const QJsonArray &senses, const QJsonObject &entry,
                   QVariantList &result, int depth = 0, const QStringList &inherited_tags = {}) {
    if (depth > 8)
        return;
    QStringList pronunciations;
    for (const auto &value : entry.value("pronunciations").toArray())
        pronunciations.append(value.toObject().value("text").toString());
    for (const auto &value : senses) {
        if (result.size() >= 100)
            break;
        const auto sense = value.toObject();
        auto tags = sense.value("tags").toVariant().toStringList();
        const QList<QStringList> grammar_groups{
            {"masculine", "feminine", "neuter"}, {"plural-only", "plural", "singular", "dual"}};
        for (const auto &group : grammar_groups) {
            const bool has_own_tags = std::any_of(group.begin(), group.end(),
                [&](const QString &tag) { return tags.contains(tag); });
            if (!has_own_tags) {
                for (const auto &tag : inherited_tags) {
                    if (group.contains(tag))
                        tags.append(tag);
                }
            }
        }
        if (!sense.value("definition").toString().isEmpty()) {
            QVariantMap item = sense.toVariantMap();
            item.insert("tags", tags);
            item.insert("part_of_speech", entry.value("partOfSpeech").toString());
            item.insert("pronunciation", pronunciations.join(" · "));
            item.insert("forms", entry.value("forms").toArray().toVariantList());
            auto articles = sense.value("articles").toVariant().toStringList();
            if (articles.isEmpty() && !sense.value("article").toString().isEmpty())
                articles.append(sense.value("article").toString());
            item.insert("articles", articles);
            item.insert("article", articles.size() == 1 ? articles.first() : QString{});
            result.append(item);
        }
        append_senses(sense.value("subsenses").toArray(), entry, result, depth + 1, tags);
    }
}

QString request_error(QNetworkReply *reply, const QString &service) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 404)
        return QObject::tr("No dictionary entry found. Try the dictionary form or correct the OCR text.");
    if (status == 429)
        return QObject::tr("%1 rate limit reached. Please try again later.").arg(service);
    if (status == 400)
        return QObject::tr("%1 rejected the request. Check the language codes and whether this language pair is supported.").arg(service);
    if (status == 403)
        return QObject::tr("DeepL rejected the API key. Check the key and Free/Pro selection.");
    if (status == 456)
        return QObject::tr("DeepL quota exhausted.");
    return QObject::tr("%1 request failed (%2). Check your connection or try again later.")
        .arg(service).arg(status);
}

QString anki_html(QString text) {
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    return text.toHtmlEscaped().replace('\n', "<br>").replace('\t', "&#9;");
}

QString tsv_field(QString text) {
    return '"' + text.replace('"', "\"\"") + '"';
}

}  // namespace

LearningService::LearningService(QObject *parent, const QString &file_name,
                                 QNetworkAccessManager *network)
    : QObject(parent), m_network(network ? network : new QNetworkAccessManager(this)),
      m_file_name(file_name.isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/vocabulary.json"
          : file_name) {
    m_source_language = normalized_language_code(m_settings.value("learning/source_language", "en").toString());
    if (m_source_language.isEmpty())
        m_source_language = "en";
    m_target_language = normalized_language_code(m_settings.value("learning/target_language",
        QLocale::languageToCode(QLocale::system().language())).toString());
    if (m_target_language.isEmpty())
        m_target_language = "en";
    m_deepl_pro = m_settings.value("learning/deepl_pro", false).toBool();
    m_api_key = qEnvironmentVariable("SHOWTHIS_DEEPL_API_KEY").trimmed();
    m_history_limit = qBound(0, m_settings.value("learning/history_limit", 1000).toInt(), 10000);

    QFile file(m_file_name);
    if (!file.exists())
        return;
    QJsonParseError error;
    const bool opened = file.open(QIODevice::ReadOnly);
    const auto document = opened ? QJsonDocument::fromJson(file.readAll(), &error) : QJsonDocument{};
    const auto object = document.object();
    if (!opened || error.error != QJsonParseError::NoError || !document.isObject()
        || object.value("version").toInt() != 1 || !object.value("cards").isArray()
        || !object.value("dictionary_cache").isObject()) {
        m_store_readable = false;
        m_storage_error = tr("Cannot read vocabulary file: %1. It will not be overwritten.").arg(m_file_name);
        return;
    }
    m_cards = object.value("cards").toArray().toVariantList();
    // Retain explicit article metadata from older cards, for any language.
    for (auto &value : m_cards) {
        auto card = value.toMap();
        if (!card.contains("article")) {
            card.insert("article", single_article(card.value("sense").toMap()));
            value = card;
        }
    }
    m_cache = object.value("dictionary_cache").toObject();
    // Version 1 files predate history. Preserve their cached lookups on migration.
    if (object.contains("dictionary_history") && !object.value("dictionary_history").isArray()) {
        m_store_readable = false;
        m_storage_error = tr("Cannot read dictionary history. The existing file will not be overwritten.");
        return;
    }
    const auto stored_history = object.value("dictionary_history").toArray();
    QSet<QString> seen;
    for (const auto &value : stored_history) {
        const auto item = value.toObject();
        const auto key = item.value("key").toString();
        if (key.isEmpty() || seen.contains(key) || !m_cache.value(key).isObject())
            continue;
        seen.insert(key);
        m_history.append(item.toVariantMap());
    }
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it) {
        if (seen.contains(it.key()) || !it.value().isObject())
            continue;
        const auto separator = it.key().indexOf(':');
        if (separator <= 0)
            continue;
        m_history.append(QVariantMap{{"key", it.key()}, {"word", it.key().mid(separator + 1)},
            {"language", it.key().left(separator)}, {"context", ""}, {"last_lookup", ""}});
    }
    const auto old_count = m_cache.size();
    trim_history(m_history, m_cache, m_history_limit);
    if (old_count != m_cache.size())
        write_store(m_cards, m_cache, m_history);
}

void LearningService::set_message(const QString &message) {
    m_message = message;
    emit message_changed();
}

void LearningService::cancel(QPointer<QNetworkReply> &reply) {
    if (!reply)
        return;
    auto *pending = reply.data();
    reply.clear();
    pending->disconnect(this);
    pending->abort();
    pending->deleteLater();
}

void LearningService::clear() {
    cancel(m_dictionary_reply);
    cancel(m_translation_reply);
    m_senses.clear();
    m_source.clear();
    m_lookup_word.clear();
    m_translation.clear();
    set_message({});
    emit lookup_changed();
    emit translation_changed();
}

QVariantList LearningService::language_options() const {
    static const QVariantList options = []() {
        QVariantList result;
        QSet<QString> seen;
        for (const auto &locale : QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyTerritory)) {
            auto code = QLocale::languageToCode(locale.language(), QLocale::ISO639Part1);
            if (code.isEmpty())
                code = QLocale::languageToCode(locale.language(), QLocale::ISO639Part3);
            if (code.isEmpty() || seen.contains(code) || locale.language() == QLocale::C)
                continue;
            seen.insert(code);
            result.append(QVariantMap{{"code", code},
                {"name", QLocale::languageToString(locale.language()) + " (" + code + ")"}});
        }
        std::sort(result.begin(), result.end(), [](const QVariant &left, const QVariant &right) {
            return left.toMap().value("name").toString() < right.toMap().value("name").toString();
        });
        return result;
    }();
    return options;
}

void LearningService::set_source_language(const QString &language) {
    const auto code = normalized_language_code(language);
    if (code.isEmpty()) {
        set_message(tr("Choose a language or enter a language code, for example fr, es, or ja."));
        return;
    }
    if (code == m_source_language)
        return;
    m_source_language = code;
    m_settings.setValue("learning/source_language", code);
    clear();
    emit preferences_changed();
}

void LearningService::set_target_language(const QString &language) {
    const auto code = normalized_language_code(language);
    if (code.isEmpty()) {
        set_message(tr("Choose a translation language or enter its language code."));
        return;
    }
    if (code == m_target_language)
        return;
    m_target_language = code;
    m_settings.setValue("learning/target_language", code);
    cancel(m_translation_reply);
    m_translation.clear();
    emit translation_changed();
    emit preferences_changed();
}

void LearningService::set_deepl_pro(bool pro) {
    if (m_deepl_pro == pro)
        return;
    m_deepl_pro = pro;
    m_settings.setValue("learning/deepl_pro", pro);
    emit preferences_changed();
}

void LearningService::set_translation_key(const QString &key) {
    m_api_key = key.trimmed();
    emit preferences_changed();
}

void LearningService::trim_history(QVariantList &history, QJsonObject &cache, int limit) const {
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

void LearningService::set_history_limit(int limit) {
    limit = qBound(0, limit, 10000);
    if (limit == m_history_limit)
        return;
    auto history = m_history;
    auto cache = m_cache;
    trim_history(history, cache, limit);
    if (!write_store(m_cards, cache, history))
        return;
    m_history = history;
    m_cache = cache;
    m_history_limit = limit;
    m_settings.setValue("learning/history_limit", limit);
    m_settings.sync();
    emit preferences_changed();
    emit history_changed();
}

void LearningService::remember_lookup(const QString &key, const QString &context, const QJsonObject &data) {
    if (m_history_limit == 0)
        return;
    auto history = m_history;
    for (int i = history.size() - 1; i >= 0; --i) {
        if (history[i].toMap().value("key").toString() == key)
            history.removeAt(i);
    }
    history.prepend(QVariantMap{{"key", key}, {"word", m_lookup_word},
        {"language", m_source_language}, {"context", context.trimmed()},
        {"summary", m_senses.value(0).toMap().value("definition").toString()},
        {"last_lookup", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    auto cache = m_cache;
    cache.insert(key, data);
    trim_history(history, cache, m_history_limit);
    if (write_store(m_cards, cache, history)) {
        m_history = history;
        m_cache = cache;
        emit history_changed();
    }
}

bool LearningService::clear_history() {
    // Do not let an already-running lookup repopulate history after clearing.
    cancel(m_dictionary_reply);
    emit lookup_changed();
    if (!write_store(m_cards, {}, {}))
        return false;
    m_cache = {};
    m_history.clear();
    emit history_changed();
    set_message(tr("Lookup history cleared. Saved vocabulary was kept."));
    return true;
}

void LearningService::display_dictionary(const QJsonObject &data) {
    m_senses.clear();
    for (const auto &value : data.value("entries").toArray()) {
        const auto entry = value.toObject();
        if (normalized_language_code(entry.value("language").toObject().value("code").toString()) == m_source_language)
            append_senses(entry.value("senses").toArray(), entry, m_senses);
    }
    m_source = data.value("source").toObject().toVariantMap();
    if (m_senses.isEmpty())
        set_message(tr("No meanings found. Try the dictionary form, or enter your own meaning."));
    emit lookup_changed();
}

void LearningService::lookup(const QString &word, const QString &context) {
    clear();
    m_lookup_word = word.trimmed();
    if (m_lookup_word.isEmpty()) {
        set_message(tr("Enter a word first."));
        return;
    }
    const auto cache_key = m_source_language + ":" + m_lookup_word;
    if (m_cache.contains(cache_key)) {
        const auto data = m_cache.value(cache_key).toObject();
        display_dictionary(data);
        remember_lookup(cache_key, context, data);
        set_message(tr("Loaded from local dictionary cache."));
        return;
    }
    const auto url = QUrl::fromEncoded("https://freedictionaryapi.com/api/v1/entries/"
        + m_source_language.toUtf8() + "/" + QUrl::toPercentEncoding(m_lookup_word)
        + "?translations=true");
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setRawHeader("User-Agent", "ShoWThis/1.0 (personal vocabulary learner)");
    auto *reply = m_network->get(request);
    m_dictionary_reply = reply;
    emit lookup_changed();
    connect(reply, &QNetworkReply::finished, this, [this, reply, cache_key, context]() {
        if (m_dictionary_reply != reply) {
            reply->deleteLater();
            return;
        }
        m_dictionary_reply.clear();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            set_message(request_error(reply, tr("Dictionary")));
            emit lookup_changed();
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &error);
        const auto data = document.object();
        if (error.error != QJsonParseError::NoError || !data.value("entries").isArray()) {
            set_message(tr("The dictionary returned an invalid response."));
            emit lookup_changed();
            return;
        }
        display_dictionary(data);
        if (!m_senses.isEmpty())
            remember_lookup(cache_key, context, data);
    });
}

void LearningService::translate(const QString &word, const QString &context) {
    cancel(m_translation_reply);
    m_translation.clear();
    emit translation_changed();
    if (m_api_key.isEmpty() || word.trimmed().isEmpty()) {
        set_message(tr("Enter a word and configure your DeepL API key first."));
        return;
    }
    set_message({});
    const QUrl url(m_deepl_pro ? "https://api.deepl.com/v2/translate"
                              : "https://api-free.deepl.com/v2/translate");
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "DeepL-Auth-Key " + m_api_key.toUtf8());
    const QJsonObject payload{{"text", QJsonArray{word.trimmed()}},
                              {"source_lang", m_source_language.toUpper()},
                              {"target_lang", m_target_language.toUpper()},
                              {"context", context.trimmed()}};
    auto *reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_translation_reply = reply;
    emit translation_changed();
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_translation_reply != reply) {
            reply->deleteLater();
            return;
        }
        m_translation_reply.clear();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            set_message(request_error(reply, tr("DeepL")));
        } else {
            const auto data = QJsonDocument::fromJson(reply->readAll()).object();
            const auto translations = data.value("translations").toArray();
            m_translation = translations.isEmpty() ? QString{}
                : translations.first().toObject().value("text").toString();
            if (m_translation.isEmpty())
                set_message(tr("DeepL returned no translation."));
        }
        emit translation_changed();
    });
}

bool LearningService::write_store(const QVariantList &cards, const QJsonObject &cache,
                                  const QVariantList &history) {
    if (!m_store_readable) {
        emit vocabulary_changed();
        return false;
    }
    QSaveFile file(m_file_name);
    const QJsonDocument document(QJsonObject{{"version", 1},
        {"cards", QJsonArray::fromVariantList(cards)}, {"dictionary_cache", cache},
        {"dictionary_history", QJsonArray::fromVariantList(history)}});
    const auto bytes = document.toJson();
    if (!QDir().mkpath(QFileInfo(m_file_name).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        m_storage_error = tr("Cannot save vocabulary: %1").arg(m_file_name);
        emit vocabulary_changed();
        return false;
    }
    const bool had_storage_error = !m_storage_error.isEmpty();
    m_storage_error.clear();
    if (had_storage_error)
        emit vocabulary_changed();
    return true;
}

bool LearningService::save_word(const QString &id, const QString &word, const QString &context,
                               const QString &meaning, const QString &translation,
                               const QString &notes, int sense_index, const QString &article) {
    if (article.trimmed().size() > 80 || article.contains('\n') || article.contains('\r') || article.contains('\t')) {
        set_message(tr("Keep the optional word prefix to one line, at most 80 characters."));
        return false;
    }
    if (word.trimmed().isEmpty() || (meaning.trimmed().isEmpty() && translation.trimmed().isEmpty())) {
        set_message(tr("Enter a word and at least a meaning or translation before saving."));
        return false;
    }
    auto cards = m_cards;
    int existing = -1;
    for (int i = 0; i < cards.size(); ++i) {
        if (cards[i].toMap().value("id").toString() == id && !id.isEmpty())
            existing = i;
    }
    if (!id.isEmpty() && existing < 0) {
        set_message(tr("This saved word no longer exists. Reopen the vocabulary list."));
        return false;
    }
    const auto timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QVariantMap card{{"id", existing >= 0 ? id : QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"word", word.trimmed()}, {"language", m_source_language}, {"target_language", m_target_language},
        {"context", context.trimmed()}, {"meaning", meaning.trimmed()},
        {"translation", translation.trimmed()}, {"notes", notes.trimmed()}, {"updated", timestamp},
        {"created", existing >= 0 ? cards[existing].toMap().value("created").toString() : timestamp}};
    const bool matching = word.trimmed() == m_lookup_word && !m_source.isEmpty();
    const auto old = existing >= 0 ? cards[existing].toMap() : QVariantMap{};
    const bool unchanged = old.value("word").toString() == word.trimmed()
        && old.value("language").toString() == m_source_language;
    card.insert("source", matching ? m_source : unchanged ? old.value("source").toMap() : QVariantMap{});
    card.insert("sense", matching && sense_index >= 0 && sense_index < m_senses.size()
        ? m_senses[sense_index].toMap() : unchanged ? old.value("sense").toMap() : QVariantMap{});
    const auto word_prefix = article != "auto" ? article.trimmed()
        : unchanged && old.contains("article") && sense_index < 0 ? old.value("article").toString()
        : single_article(card.value("sense").toMap());
    card.insert("article", word_prefix);
    if (existing >= 0)
        cards[existing] = card;
    else
        cards.prepend(card);
    if (!write_store(cards, m_cache, m_history))
        return false;
    m_cards = cards;
    emit vocabulary_changed();
    set_message(tr("Saved to your vocabulary."));
    return true;
}

bool LearningService::remove_word(const QString &id) {
    auto cards = m_cards;
    for (int i = 0; i < cards.size(); ++i) {
        if (cards[i].toMap().value("id").toString() == id) {
            cards.removeAt(i);
            if (!write_store(cards, m_cache, m_history))
                return false;
            m_cards = cards;
            emit vocabulary_changed();
            return true;
        }
    }
    return false;
}

bool LearningService::export_anki(const QUrl &destination, const QStringList &card_ids) {
    if (!destination.isLocalFile() || destination.toLocalFile().isEmpty()) {
        set_message(tr("Choose a local file for the Anki export."));
        return false;
    }
    const auto path = QFileInfo(destination.toLocalFile()).absoluteFilePath();
    const auto store_path = QFileInfo(m_file_name).absoluteFilePath();
    if (path == store_path || (QFileInfo::exists(path)
        && QFileInfo(path).canonicalFilePath() == QFileInfo(m_file_name).canonicalFilePath())) {
        set_message(tr("Choose another file: the export must not overwrite your notebook."));
        return false;
    }
    const QSet<QString> requested(card_ids.begin(), card_ids.end());
    QSet<QString> found;
    QString output = "#separator:Tab\n#html:true\n#columns:Front\tBack\tTags\n#tags column:3\n";
    int count = 0;
    for (const auto &value : m_cards) {
        const auto card = value.toMap();
        const auto id = card.value("id").toString();
        if (!requested.isEmpty() && !requested.contains(id))
            continue;
        found.insert(id);
        const auto article = card.value("article").toString();
        QString front = "<b>" + anki_html(word_with_article(card.value("word").toString(), article)) + "</b> <small>("
            + anki_html(card.value("language").toString().toUpper()) + ")</small>";
        const auto context = card.value("context").toString();
        if (!context.isEmpty())
            front += "<br><br>" + anki_html(context);
        QStringList back;
        for (const auto &field : {QStringLiteral("translation"), QStringLiteral("meaning"),
                                 QStringLiteral("notes")}) {
            const auto text = card.value(field).toString();
            if (!text.isEmpty())
                back.append(anki_html(text));
        }
        const auto sense = card.value("sense").toMap();
        const auto pronunciation = sense.value("pronunciation").toString();
        if (!pronunciation.isEmpty())
            back.append(anki_html(pronunciation));
        for (const auto &example : sense.value("examples").toList())
            back.append(anki_html(example.toString()));
        const auto source = card.value("source").toMap();
        if (!source.isEmpty()) {
            const auto license = source.value("license").toMap();
            back.append("<small>Wiktionary via FreeDictionaryAPI.com<br>"
                + anki_html(source.value("url").toString()) + "<br>"
                + anki_html(license.value("name").toString()) + " "
                + anki_html(license.value("url").toString()) + "</small>");
        }
        QString language_tag = card.value("language").toString();
        language_tag.remove(QRegularExpression("[^a-zA-Z0-9_-]"));
        output += tsv_field(front) + '\t' + tsv_field(back.join("<br><br>")) + '\t'
            + tsv_field("showthis lang::" + language_tag) + '\n';
        ++count;
    }
    if (count == 0 || (!requested.isEmpty() && found != requested)) {
        set_message(tr("No words to export, or the selected words no longer exist."));
        return false;
    }
    QSaveFile file(path);
    const auto bytes = output.toUtf8();
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        set_message(tr("Could not write Anki export: %1").arg(path));
        return false;
    }
    set_message(tr("Exported %1 words. In Anki, import this file using the Basic note type.").arg(count));
    return true;
}

}  // namespace showthis
