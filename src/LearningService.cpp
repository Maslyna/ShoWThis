#include "LearningService.h"
#include "AnkiExporter.h"
#include "NotebookData.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>
#include <QSet>
#include <QLocale>
#include <algorithm>

namespace showthis {
namespace {

QString single_article(const QVariantMap &sense) {
    // Preserve explicit dictionary metadata, but never infer an article from
    // gender using language-specific rules.
    return sense.value("article").toString();
}

QString request_error(QNetworkReply *reply, const QString &service) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 404)
        return QObject::tr("No dictionary entry found. Check the spelling or correct the OCR text.");
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

}  // namespace

LearningService::LearningService(QObject *parent, const QString &file_name,
                                 QNetworkAccessManager *network)
    : QObject(parent), m_network(network ? network : new QNetworkAccessManager(this)),
      m_store(file_name) {
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

    const auto contents = m_store.load(m_history_limit);
    m_cards = contents.cards;
    m_cache = contents.cache;
    m_history = contents.history;
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

void LearningService::set_history_limit(int limit) {
    limit = qBound(0, limit, 10000);
    if (limit == m_history_limit)
        return;
    auto history = m_history;
    auto cache = m_cache;
    LearningStore::trim_history(history, cache, limit);
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
    LearningStore::trim_history(history, cache, m_history_limit);
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
    m_senses = dictionary_senses(data, m_source_language);
    m_source = data.value("source").toObject().toVariantMap();
    if (m_senses.isEmpty())
        set_message(tr("No meanings found. Check the spelling, or enter your own meaning."));
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
    const auto previous_error = m_store.error();
    const bool success = m_store.save(cards, cache, history);
    if (!success || previous_error != m_store.error())
        emit vocabulary_changed();
    return success;
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
    // Preserve unknown/retired metadata when editing existing cards. It is no
    // longer interpreted or exported, but removing a feature must not erase data.
    const auto old = existing >= 0 ? cards[existing].toMap() : QVariantMap{};
    auto card = old;
    const QVariantMap fields{{"id", existing >= 0 ? id : QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"word", word.trimmed()}, {"language", m_source_language}, {"target_language", m_target_language},
        {"context", context.trimmed()}, {"meaning", meaning.trimmed()},
        {"translation", translation.trimmed()}, {"notes", notes.trimmed()}, {"updated", timestamp},
        {"created", existing >= 0 ? cards[existing].toMap().value("created").toString() : timestamp}};
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        card.insert(it.key(), it.value());
    const bool matching = word.trimmed() == m_lookup_word && !m_source.isEmpty();
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
    const auto result = AnkiExporter::write(destination, storage_path(), m_cards, card_ids);
    set_message(result.message);
    return result.success;
}

QString LearningService::format_word(const QString &word, const QString &article) const {
    return word_with_article(word, article);
}

}  // namespace showthis
