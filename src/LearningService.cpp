#include "LearningService.h"
#include "AnkiExporter.h"
#include "NotebookData.h"
#include "CardForms.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>
#include <QSet>
#include <QLocale>
#include <algorithm>

namespace showthis {
namespace {

QString single_article(const QVariantMap &sense) {
    // Dictionary normalization has already applied the word-language profile.
    return sense.value("article").toString();
}

}  // namespace

LearningService::LearningService(QObject *parent, const QString &file_name,
                                 QNetworkAccessManager *network, DictionaryProviders providers)
    : QObject(parent), m_network(network ? network : new QNetworkAccessManager(this)),
      m_dictionary(new DictionaryLookup(m_network, this, providers.empty() ? default_dictionary_providers() : std::move(providers),
          file_name.isEmpty() ? QString{} : QFileInfo(file_name).absolutePath())),
      m_translator(new TranslationService(this, m_network)),
      m_store(file_name), m_lookup_cache(m_store.file_name() + ".lookups.json") {
    connect(&m_lookup_cache, &LookupCacheStore::error_occurred, this, [this](const QString &message) {
        set_message(message);
    });
    connect(&m_lookup_cache, &LookupCacheStore::error_changed, this, &LearningService::vocabulary_changed);
    connect(this, &LearningService::vocabulary_changed, this, [this]() { m_vocabulary_model.replace(m_cards); });
    connect(this, &LearningService::history_changed, this, [this]() { m_history_model.replace(m_history); });
    connect(m_translator, &TranslationService::error_occurred, this, &LearningService::set_message);
    connect(m_translator, &TranslationService::configuration_changed, this, [this]() { set_message({}); });
    connect(m_dictionary, &DictionaryLookup::provider_data_changed, this, [this]() {
        const auto revision = m_dictionary->revision(m_source_language, m_dictionary_options);
        if (m_dictionary_options.data_revision != revision) {
            m_dictionary_options.data_revision = revision;
            clear();
        }
    });
    connect(m_dictionary, &DictionaryLookup::finished, this,
        [this](const QJsonObject &data, const QString &message, bool cacheable) {
            if (dictionary_senses(data, m_source_language).isEmpty() && !m_stale_cache.isEmpty()) {
                display_dictionary(m_stale_cache);
                set_message(tr("Refresh failed; showing the previous cached lookup. %1").arg(message));
                m_stale_cache = {};
                return;
            }
            display_dictionary(data);
            if (!message.isEmpty())
                set_message(message);
            if (cacheable && !m_senses.isEmpty())
                remember_lookup(m_lookup_key, m_lookup_context, data);
        });
    m_source_language = dictionary_language_code(m_settings.value("learning/source_language", "en").toString());
    if (m_source_language.isEmpty())
        m_source_language = "en";
    m_target_language = normalized_language_code(m_settings.value("learning/target_language",
        QLocale::languageToCode(QLocale::system().language())).toString());
    if (m_target_language.isEmpty())
        m_target_language = "en";
    const QVariantList defaults{QVariantMap{{"provider", "freedictionary"}, {"languages", QStringList{"en"}}, {"fallback", false}}};
    QString configuration_error;
    if (!m_dictionary->configure(m_settings.value("dictionary/selection", defaults).toList(),
            m_settings.value("dictionary/follow_forms", true).toBool(), m_dictionary_options,
            m_dictionary_selection, configuration_error)) {
        m_dictionary->configure(defaults, true, m_dictionary_options, m_dictionary_selection, configuration_error);
    }
    m_history_limit = qBound(0, m_settings.value("learning/history_limit", 1000).toInt(), 10000);

    const auto contents = m_store.load(m_history_limit);
    m_has_legacy_cache = !contents.cache.isEmpty() || !contents.history.isEmpty();
    m_cards = contents.cards;
    const auto lookups = m_lookup_cache.load(contents);
    m_cache = lookups.cache;
    m_history = lookups.history;
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (!it.value().isObject()) {
            it = m_cache.erase(it);
            continue;
        }
        auto document = it.value().toObject();
        if (!document.contains("_cached_at")) {
            document.insert("_cached_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            it.value() = document;
        }
        ++it;
    }
    trim_cache(m_history, m_cache, m_history_limit);
    m_vocabulary_model.replace(m_cards);
    m_history_model.replace(m_history);
}

LearningService::~LearningService() {
    m_lookup_cache.flush();
    m_lookup_cache.disconnect(this);
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
    m_stale_cache = {};
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

bool LearningService::configure_dictionaries(const QVariantList &selection, bool follow_forms) {
    DictionaryOptions options;
    QVariantList normalized;
    QString error;
    if (!m_dictionary->configure(selection, follow_forms, options, normalized, error)) {
        set_message(error);
        return false;
    }
    if (normalized == m_dictionary_selection && follow_forms == this->follow_forms())
        return true;
    clear();
    m_dictionary_options = options;
    m_dictionary_selection = normalized;
    if (!m_transient_preferences) {
        m_settings.setValue("dictionary/selection", normalized);
        m_settings.setValue("dictionary/follow_forms", follow_forms);
    }
    emit preferences_changed();
    return true;
}

void LearningService::prepare_draft(const QVariantMap &configuration, bool use_defaults) {
    // Viewing a saved card/history item is not an edit to application defaults.
    m_transient_preferences = true;
    auto source = dictionary_language_code(m_settings.value("learning/source_language", "en").toString());
    auto target = normalized_language_code(m_settings.value("learning/target_language",
        QLocale::languageToCode(QLocale::system().language())).toString());
    if (source.isEmpty()) source = "en";
    if (target.isEmpty()) target = "en";
    set_source_language(configuration.value("language", source).toString());
    set_target_language(configuration.value("target_language", target).toString());
    const QVariantList defaults{QVariantMap{{"provider", "freedictionary"}, {"languages", QStringList{"en"}}, {"fallback", false}}};
    if (!configure_dictionaries(configuration.value("dictionary_selection", m_settings.value("dictionary/selection", defaults)).toList(),
            configuration.value("follow_forms", m_settings.value("dictionary/follow_forms", true)).toBool())) {
        const auto error = m_message;
        configure_dictionaries(defaults, true);
        set_message(tr("%1 Using the default dictionary selection for this draft.").arg(error));
    }
    m_transient_preferences = !use_defaults;
}

void LearningService::set_source_language(const QString &language) {
    const auto code = dictionary_language_code(language);
    if (code.isEmpty()) {
        set_message(tr("Choose a language or enter a language code, for example fr, es, or ja."));
        return;
    }
    if (code == m_source_language)
        return;
    m_source_language = code;
    if (!m_transient_preferences)
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
    if (!m_transient_preferences)
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
    trim_cache(history, cache, limit);
    if (!write_store(m_cards, cache, history, true))
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
        {"language", m_source_language}, {"context", context.trimmed().left(4096)},
        {"dictionary_selection", dictionary_selection()}, {"follow_forms", follow_forms()},
        {"summary", m_senses.value(0).toMap().value("definition").toString()},
        {"last_lookup", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    auto cache = m_cache;
    auto document = data;
    if (!document.contains("_cached_at"))
        document.insert("_cached_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    cache.insert(key, document);
    m_cache_sizes.remove(key);
    trim_cache(history, cache, m_history_limit);
    if (write_store(m_cards, cache, history)) {
        m_history = history;
        m_cache = cache;
        emit history_changed();
        if (!m_cache.contains(key))
            set_message(tr("This lookup exceeded the history storage limits. Saved vocabulary is unaffected."));
    }
}

bool LearningService::clear_history() {
    // Do not let an already-running lookup repopulate history after clearing.
    m_dictionary->cancel();
    emit lookup_changed();
    m_lookup_cache.allow_reset();
    if (!write_store(m_cards, {}, {}, true))
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

void LearningService::lookup(const QString &word, const QString &context, bool refresh) {
    clear();
    m_lookup_word = word.trimmed();
    if (m_lookup_word.isEmpty()) {
        set_message(tr("Enter a word first."));
        return;
    }
    m_lookup_context = context;
    m_dictionary_options.data_revision = m_dictionary->revision(m_source_language, m_dictionary_options);
    m_lookup_key = DictionaryLookup::cache_key(m_source_language, m_lookup_word, m_dictionary_options);
    if (m_cache.contains(m_lookup_key)) {
        const auto data = m_cache.value(m_lookup_key).toObject();
        const auto fetched = QDateTime::fromString(data.value("_cached_at").toString(), Qt::ISODate);
        const auto age = fetched.secsTo(QDateTime::currentDateTimeUtc());
        const bool fresh = fetched.isValid() && age >= 0 && age <= 7 * 24 * 60 * 60;
        if (!refresh && fresh) {
            display_dictionary(data);
            remember_lookup(m_lookup_key, context, data);
            set_message(tr("Loaded from local dictionary cache."));
            return;
        }
        m_stale_cache = data;
    }
    m_dictionary->lookup(m_lookup_word, m_source_language, m_dictionary_options);
    emit lookup_changed();
}

bool LearningService::lookup_history(const QString &key) {
    clear();
    if (!m_cache.value(key).isObject()) {
        set_message(tr("This cached lookup is no longer available. Press Look up to fetch it again."));
        return false;
    }
    for (const auto &value : m_history) {
        const auto entry = value.toMap();
        if (entry.value("key").toString() != key) continue;
        prepare_draft(entry, false);
        m_lookup_word = entry.value("word").toString();
        m_lookup_context = entry.value("context").toString();
        m_lookup_key = key;
        display_dictionary(m_cache.value(key).toObject());
        set_message(tr("Loaded the saved lookup from history (no network request)."));
        return true;
    }
    return false;
}

void LearningService::translate(const QString &word, const QString &context) {
    set_message({});
    m_translator->translate(word, context, m_source_language, m_target_language);
}

KaikkiOfflineStore *LearningService::offline_dictionaries() const {
    return qobject_cast<KaikkiOfflineStore *>(m_dictionary->settings("kaikki"));
}

bool LearningService::write_store(const QVariantList &cards, const QJsonObject &cache,
                                  const QVariantList &history, bool synchronous_cache, bool vocabulary_write) {
    const auto previous_error = m_store.error();
    const bool persist_vocabulary = vocabulary_write || cards != m_cards;
    bool success = m_store.readable();
    if (success)
        success = m_lookup_cache.submit(cache, history, synchronous_cache || persist_vocabulary);
    // Vocabulary is not rewritten for lookups/history. Flush the sidecar before
    // removing legacy embedded caches during the next explicit vocabulary save.
    if (persist_vocabulary && m_store.readable() && (success || !m_has_legacy_cache)) {
        success = m_store.save(cards, {}, {});
        if (success) m_has_legacy_cache = false;
    }
    if (!success || previous_error != m_store.error())
        emit vocabulary_changed();
    return success;
}

void LearningService::trim_cache(QVariantList &history, QJsonObject &cache, int limit) {
    LookupCacheStore::trim(history, cache, limit, m_cache_sizes);
}

bool LearningService::save_word(const QString &id, const QString &word, const QString &context,
                               const QString &meaning, const QString &translation,
                                const QString &notes, int sense_index, const QString &article,
                                const QVariantMap &word_forms) {
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
        {"dictionary_selection", dictionary_selection()}, {"follow_forms", follow_forms()},
        {"created", existing >= 0 ? cards[existing].toMap().value("created").toString() : timestamp}};
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        card.insert(it.key(), it.value());
    const auto selected_sense = sense_index >= 0 && sense_index < m_senses.size() ? m_senses[sense_index].toMap() : QVariantMap{};
    // Looking up an existing word does not select a replacement meaning.
    // Keep its saved sense and attribution together until a sense is chosen.
    const bool matching = !selected_sense.isEmpty() && !m_source.isEmpty()
        && (word.trimmed() == m_lookup_word
            || selected_sense.value("headword").toString() == word.trimmed());
    const bool unchanged = old.value("word").toString() == word.trimmed()
        && dictionary_language_code(old.value("language").toString()) == m_source_language;
    card.insert("source", matching ? selected_sense.value("source", m_source).toMap()
        : unchanged ? old.value("source").toMap() : QVariantMap{});
    card.insert("sense", matching && sense_index >= 0 && sense_index < m_senses.size()
        ? m_senses[sense_index].toMap() : unchanged ? old.value("sense").toMap() : QVariantMap{});
    const auto word_prefix = article != "auto" ? article.trimmed()
        : unchanged && old.contains("article") && sense_index < 0 ? old.value("article").toString()
        : single_article(card.value("sense").toMap());
    card.insert("article", word_prefix);
    const auto sense = card.value("sense").toMap();
    const auto form_value = [&](const QString &key) {
        return word_forms.contains(key) ? word_forms.value(key)
            : unchanged && sense_index < 0 && old.contains(key) ? old.value(key) : sense.value(key);
    };
    QVariantMap form_fields;
    for (const auto *key : {"base_form", "base_article", "singular_forms", "plural_forms", "number_relevant",
             "grammar_profile", "singular_status", "plural_status", "article_origin", "base_article_origin"})
        form_fields.insert(key, form_value(key));
    for (const auto &field : {QStringLiteral("article"), QStringLiteral("base_article")}) {
        const auto origin_key = field + "_origin";
        if ((field == "article" && article != "auto" && !word_forms.contains(origin_key))
            || (field == "base_article" && word_forms.contains(field) && !word_forms.contains(origin_key)))
            form_fields.insert(origin_key, "manual");
    }
    QString form_error;
    const auto forms = CardForms::parse(form_fields, form_error);
    if (!forms) { set_message(form_error); return false; }
    const auto normalized_forms = forms->fields(word_prefix);
    for (auto it = normalized_forms.constBegin(); it != normalized_forms.constEnd(); ++it)
        card.insert(it.key(), it.value());
    if (existing >= 0)
        cards[existing] = card;
    else
        cards.prepend(card);
    if (!write_store(cards, m_cache, m_history, false, true))
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
            if (!write_store(cards, m_cache, m_history, false, true))
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

QString LearningService::number_availability(const QVariantMap &forms) const {
    return number_availability_text(forms);
}

}  // namespace showthis
