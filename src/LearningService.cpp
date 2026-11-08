#include "LearningService.h"
#include "AnkiExporter.h"
#include "NotebookData.h"

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
      m_offline(new KaikkiOfflineStore(m_network, file_name.isEmpty() ? QString{} : QFileInfo(file_name).absolutePath() + "/kaikki", this)),
      m_dictionary(new DictionaryLookup(m_network, this, providers.empty() ? default_dictionary_providers(m_offline) : std::move(providers))),
      m_translator(new TranslationService(this, m_network)),
      m_store(file_name) {
    connect(m_translator, &TranslationService::error_occurred, this, &LearningService::set_message);
    connect(m_translator, &TranslationService::configuration_changed, this, [this]() { set_message({}); });
    connect(m_offline, &KaikkiOfflineStore::data_changed, this, [this]() {
        clear();
        emit preferences_changed();
    });
    connect(m_dictionary, &DictionaryLookup::finished, this,
        [this](const QJsonObject &data, const QString &message, bool cacheable) {
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
    m_settings.setValue("dictionary/selection", normalized);
    m_settings.setValue("dictionary/follow_forms", follow_forms);
    emit preferences_changed();
    return true;
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
        {"dictionary_selection", dictionary_selection()}, {"follow_forms", follow_forms()},
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
    m_dictionary_options.data_revision = m_offline->cache_token();
    m_lookup_key = DictionaryLookup::cache_key(m_source_language, m_lookup_word, m_dictionary_options);
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
    for (const auto &key : {"base_form", "base_article", "singular_forms", "plural_forms"}) {
        const auto data = form_value(key);
        if (QString(key) == "plural_forms" || QString(key) == "singular_forms") {
            QStringList plurals;
            for (const auto &plural : data.toStringList()) {
                const auto text = plural.trimmed();
                if (text.size() > 200 || text.contains(QRegularExpression("[\\r\\n\\t]"))) {
                    set_message(tr("Keep each number form to one line, at most 200 characters."));
                    return false;
                }
                if (!text.isEmpty() && !plurals.contains(text))
                    plurals.append(text);
            }
            if (plurals.size() > 20) {
                set_message(tr("Keep at most 20 alternatives per number form."));
                return false;
            }
            card.insert(key, plurals);
        } else {
            const auto text = data.toString().trimmed();
            const int limit = QString(key) == "base_article" ? 80 : 200;
            if (text.size() > limit || text.contains(QRegularExpression("[\\r\\n\\t]"))) {
                set_message(tr("Keep the base form and its prefix to one short line each."));
                return false;
            }
            card.insert(key, text);
        }
    }
    const bool number_relevant = form_value("number_relevant").toBool();
    card.insert("number_relevant", number_relevant);
    card.insert("grammar_profile", form_value("grammar_profile").toString());
    for (const auto &number : {QStringLiteral("singular"), QStringLiteral("plural")}) {
        auto status = form_value(number + "_status").toString();
        if (status.isEmpty())
            status = "unknown";
        if (status != "unknown" && status != "available" && status != "unavailable" && status != "not_applicable") {
            set_message(tr("Invalid number-form status."));
            return false;
        }
        const bool has_forms = !card.value(number + "_forms").toStringList().isEmpty();
        card.insert(number + "_status", !number_relevant ? "not_applicable" : has_forms ? "available"
            : status == "available" || status == "not_applicable" ? "unknown" : status);
    }
    for (const auto &field : {QStringLiteral("article"), QStringLiteral("base_article")}) {
        const auto origin_key = field + "_origin";
        auto origin = form_value(origin_key).toString();
        if ((field == "article" && article != "auto" && !word_forms.contains(origin_key))
            || (field == "base_article" && word_forms.contains(field) && !word_forms.contains(origin_key)))
            origin = "manual";
        if (origin != "" && origin != "dictionary" && origin != "profile" && origin != "manual") {
            set_message(tr("Invalid article origin."));
            return false;
        }
        card.insert(origin_key, card.value(field).toString().isEmpty() ? QString{} : origin);
    }
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

QString LearningService::number_availability(const QVariantMap &forms) const {
    return number_availability_text(forms);
}

}  // namespace showthis
