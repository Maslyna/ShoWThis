#include "LearningService.h"
#include "AnkiExporter.h"
#include "NotebookData.h"

#include <QDateTime>
#include <QJsonDocument>
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

}  // namespace

LearningService::LearningService(QObject *parent, const QString &file_name,
                                 QNetworkAccessManager *network)
    : QObject(parent), m_network(network ? network : new QNetworkAccessManager(this)),
      m_dictionary(new DictionaryLookup(m_network, this)),
      m_translator(new TranslationService(this, m_network)),
      m_store(file_name) {
    connect(m_translator, &TranslationService::error_occurred, this, &LearningService::set_message);
    connect(m_translator, &TranslationService::configuration_changed, this, [this]() { set_message({}); });
    connect(m_dictionary, &DictionaryLookup::finished, this,
        [this](const QJsonObject &data, const QString &message, bool cacheable) {
            display_dictionary(data);
            if (!message.isEmpty())
                set_message(message);
            if (cacheable && !m_senses.isEmpty())
                remember_lookup(m_lookup_key, m_lookup_context, data);
        });
    m_source_language = normalized_language_code(m_settings.value("learning/source_language", "en").toString());
    if (m_source_language.isEmpty())
        m_source_language = "en";
    m_target_language = normalized_language_code(m_settings.value("learning/target_language",
        QLocale::languageToCode(QLocale::system().language())).toString());
    if (m_target_language.isEmpty())
        m_target_language = "en";
    m_dictionary_options.mode = m_settings.value("dictionary/mode", "freedictionary").toString();
    if (m_dictionary_options.mode != "kaikki" && m_dictionary_options.mode != "freedictionary" && m_dictionary_options.mode != "both")
        m_dictionary_options.mode = "freedictionary";
    const auto edition = m_settings.value("dictionary/definition_language",
        QLocale::languageToCode(QLocale::system().language())).toString();
    for (const auto &option : definition_languages()) {
        if (option.toMap().value("code").toString() == edition)
            m_dictionary_options.edition = edition;
    }
    m_dictionary_options.fallback = m_settings.value("dictionary/fallback", false).toBool();
    m_dictionary_options.follow_forms = m_settings.value("dictionary/follow_forms", true).toBool();
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

void LearningService::clear() {
    m_dictionary->cancel();
    m_translator->clear();
    m_senses.clear();
    m_source.clear();
    m_lookup_word.clear();
    m_lookup_context.clear();
    m_lookup_key.clear();
    set_message({});
    emit lookup_changed();
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

QVariantList LearningService::dictionary_modes() const {
    return {QVariantMap{{"code", "kaikki"}, {"name", tr("Kaikki")}},
        QVariantMap{{"code", "freedictionary"}, {"name", tr("FreeDictionaryAPI")}},
        QVariantMap{{"code", "both"}, {"name", tr("Both at once")}}};
}

void LearningService::set_dictionary_mode(const QString &mode) {
    if (mode == dictionary_mode() || (mode != "kaikki" && mode != "freedictionary" && mode != "both"))
        return;
    clear();
    m_dictionary_options.mode = mode;
    m_settings.setValue("dictionary/mode", mode);
    emit preferences_changed();
}

void LearningService::set_definition_language(const QString &language) {
    if (language == definition_language())
        return;
    for (const auto &option : definition_languages()) {
        if (option.toMap().value("code").toString() != language)
            continue;
        clear();
        m_dictionary_options.edition = language;
        m_settings.setValue("dictionary/definition_language", language);
        emit preferences_changed();
        return;
    }
}

void LearningService::set_dictionary_fallback(bool enabled) {
    if (enabled == dictionary_fallback())
        return;
    clear();
    m_dictionary_options.fallback = enabled;
    m_settings.setValue("dictionary/fallback", enabled);
    emit preferences_changed();
}

void LearningService::set_follow_forms(bool enabled) {
    if (enabled == follow_forms())
        return;
    clear();
    m_dictionary_options.follow_forms = enabled;
    m_settings.setValue("dictionary/follow_forms", enabled);
    emit preferences_changed();
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
    m_translator->clear();
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
        {"dictionary_mode", dictionary_mode()}, {"definition_language", definition_language()},
        {"dictionary_fallback", dictionary_fallback()}, {"follow_forms", follow_forms()},
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
    m_dictionary->cancel();
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
    m_lookup_context = context;
    m_lookup_key = m_source_language + ':' + m_lookup_word + '|' + dictionary_mode() + '|'
        + definition_language() + '|' + (dictionary_fallback() ? "fallback" : "only") + '|'
        + (follow_forms() ? "forms" : "exact");
    if (m_cache.contains(m_lookup_key)) {
        const auto data = m_cache.value(m_lookup_key).toObject();
        display_dictionary(data);
        remember_lookup(m_lookup_key, context, data);
        set_message(tr("Loaded from local dictionary cache."));
        return;
    }
    m_dictionary->lookup(m_lookup_word, m_source_language, m_dictionary_options);
    emit lookup_changed();
}

void LearningService::translate(const QString &word, const QString &context) {
    set_message({});
    m_translator->translate(word, context, m_source_language, m_target_language);
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
        {"dictionary_mode", dictionary_mode()}, {"definition_language", definition_language()},
        {"created", existing >= 0 ? cards[existing].toMap().value("created").toString() : timestamp}};
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        card.insert(it.key(), it.value());
    const auto selected_sense = sense_index >= 0 && sense_index < m_senses.size() ? m_senses[sense_index].toMap() : QVariantMap{};
    const bool matching = !m_source.isEmpty() && (word.trimmed() == m_lookup_word
        || selected_sense.value("headword").toString() == word.trimmed());
    const bool unchanged = old.value("word").toString() == word.trimmed()
        && old.value("language").toString() == m_source_language;
    card.insert("source", matching ? selected_sense.value("source", m_source).toMap()
        : unchanged ? old.value("source").toMap() : QVariantMap{});
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
