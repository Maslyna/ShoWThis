#include "NotebookData.h"
#include "GrammarProfile.h"

#include <QJsonArray>
#include <QLocale>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace showthis {
namespace {

QStringList explicit_prefixes(const QJsonObject &object) {
    auto result = object.value("articles").toVariant().toStringList();
    for (const auto &key : {"article", "prefix"}) {
        const auto prefix = object.value(key).toString().trimmed();
        if (!prefix.isEmpty() && !result.contains(prefix))
            result.append(prefix);
    }
    result.removeAll(QString{});
    result.removeDuplicates();
    return result;
}

bool citation_form(const QStringList &tags) {
    // Prefer the dictionary's unmarked/nominative form, not an oblique-case
    // spelling such as German Häusern. Never generate forms or articles.
    if (tags.contains("nominative"))
        return true;
    for (const auto &tag : {"accusative", "dative", "genitive", "instrumental", "locative", "vocative", "ablative"}) {
        if (tags.contains(tag))
            return false;
    }
    return true;
}

void append_unique(QStringList &values, const QString &value) {
    const auto text = value.trimmed();
    if (!text.isEmpty() && !values.contains(text) && values.size() < 20)
        values.append(text);
}

void add_word_forms(QVariantMap &item, const QJsonObject &sense, const QJsonObject &entry) {
    const auto headword = entry.value("headword").toString();
    QStringList bases;
    for (const auto &value : sense.value("form_of").toArray())
        append_unique(bases, value.isString() ? value.toString() : value.toObject().value("word").toString());
    const auto tags = item.value("tags").toStringList();
    const bool inflected = tags.contains("form-of") || tags.contains("form of");
    const auto base = bases.size() == 1 ? bases.first() : bases.isEmpty() && !inflected ? headword : QString{};
    item.insert("base_form", base);
    auto articles = explicit_prefixes(sense);
    if (articles.isEmpty())
        articles = explicit_prefixes(entry);
    QStringList base_articles;
    QStringList singulars;
    QStringList plurals;
    QStringList unavailable;
    for (const auto &value : entry.value("forms").toArray()) {
        const auto form = value.toObject();
        const auto tags = form.value("tags").toVariant().toStringList();
        const auto word = form.value("word").toString(form.value("form").toString()).trimmed();
        if (!citation_form(tags) || tags.contains("diminutive") || tags.contains("alternative") || tags.contains("abbreviation"))
            continue;
        if (missing_dictionary_form(word)) {
            if (!word.isEmpty()) {
                for (const auto &number : {"singular", "plural"}) {
                    if (tags.contains(number))
                        append_unique(unavailable, number);
                }
            }
            continue;
        }
        if (tags.contains("singular"))
            append_unique(singulars, word);
        if (tags.contains("plural"))
            append_unique(plurals, word);
        if (!base.isEmpty() && word == base && !tags.contains("plural")) {
            for (const auto &prefix : explicit_prefixes(form))
                append_unique(base_articles, prefix);
        }
    }
    // A standalone plural entry can itself be the citation plural of its link.
    if (bases.size() == 1 && tags.contains("plural") && citation_form(tags))
        append_unique(plurals, headword);
    if (base == headword) {
        if (articles.isEmpty())
            articles = base_articles;
        base_articles = articles;
    }
    item.insert("articles", articles);
    item.insert("article", articles.size() == 1 ? articles.first() : QString{});
    item.insert("base_article", base_articles.size() == 1 ? base_articles.first() : QString{});
    item.insert("singular_forms", singulars);
    item.insert("plural_forms", plurals);
    item.insert("unavailable_numbers", unavailable);
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
            {"masculine", "feminine", "neuter"},
            {"plural-only", "plural only", "singular-only", "singular only", "no-singular", "no singular",
                "no-plural", "no plural", "plural", "singular", "dual"},
            {"countable", "uncountable"}};
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
        const auto qualifiers = default_grammar_profiles().value("number").toObject().value("qualifier_tags").toVariant().toStringList();
        for (const auto &tag : qualifiers) {
            if (inherited_tags.contains(tag) && !tags.contains(tag))
                tags.append(tag);
        }
        if (!sense.value("definition").toString().isEmpty()) {
            QVariantMap item = sense.toVariantMap();
            item.insert("tags", tags);
            item.insert("part_of_speech", entry.value("partOfSpeech").toString());
            item.insert("pronunciation", pronunciations.join(" · "));
            item.insert("forms", entry.value("forms").toArray().toVariantList());
            item.insert("headword", entry.value("headword").toString());
            item.insert("observed_word", entry.value("observed_word").toString());
            item.insert("provider", entry.value("provider").toString());
            item.insert("definition_language", entry.value("definition_language").toString());
            item.insert("source", entry.value("source").toObject().toVariantMap());
            add_word_forms(item, sense, entry);
            apply_grammar_profile(item, entry.value("language").toObject().value("code").toString());
            result.append(item);
        }
        append_senses(sense.value("subsenses").toArray(), entry, result, depth + 1, tags);
    }
}

}  // namespace

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
    const auto prefix = article.trimmed();
    if (prefix.isEmpty())
        return word;
    const auto joined = prefix.endsWith('\'') || prefix.endsWith(QChar(0x2019)) || prefix.endsWith('-')
        ? prefix : prefix + ' ';
    return word.startsWith(joined, Qt::CaseInsensitive) ? word : joined + word;
}

QString number_availability_text(const QVariantMap &forms) {
    if (!forms.value("number_relevant").toBool())
        return {};
    QStringList result;
    const auto singular = forms.value("singular_status").toString();
    const auto plural = forms.value("plural_status").toString();
    if (singular == "unavailable")
        result.append(QObject::tr("No singular"));
    else if (singular == "unknown")
        result.append(QObject::tr("Singular not listed"));
    if (plural == "unavailable")
        result.append(QObject::tr("No plural"));
    else if (plural == "unknown")
        result.append(QObject::tr("Plural not listed"));
    return result.join(" · ");
}

QVariantList dictionary_senses(const QJsonObject &data, const QString &language) {
    QVariantList result;
    for (const auto &value : data.value("entries").toArray()) {
        auto entry = value.toObject();
        if (!entry.contains("source"))
            entry.insert("source", data.value("source"));
        if (!entry.contains("headword"))
            entry.insert("headword", data.value("word"));
        const auto source = entry.value("source").toObject();
        if (!entry.contains("provider"))
            entry.insert("provider", source.value("provider"));
        if (!entry.contains("definition_language"))
            entry.insert("definition_language", source.value("definition_language"));
        if (normalized_language_code(entry.value("language").toObject().value("code").toString()) == language)
            append_senses(entry.value("senses").toArray(), entry, result, 0, entry.value("tags").toVariant().toStringList());
    }
    // Enrich an inflection with metadata from its explicitly linked headword.
    // Stay within its provider/edition and part of speech to avoid borrowing
    // grammar from a different source or unrelated homonym.
    const auto originals = result;
    for (auto &value : result) {
        auto item = value.toMap();
        const auto base = item.value("base_form").toString();
        if (base.isEmpty() || base == item.value("headword").toString())
            continue;
        auto plurals = item.value("plural_forms").toStringList();
        auto singulars = item.value("singular_forms").toStringList();
        QStringList articles;
        QStringList origins;
        QSet<QString> singular_states;
        QSet<QString> plural_states;
        for (const auto &candidate_value : originals) {
            const auto candidate = candidate_value.toMap();
            if (candidate.value("headword").toString() != base || candidate.value("base_form").toString() != base
                || candidate.value("provider") != item.value("provider")
                || candidate.value("definition_language") != item.value("definition_language")
                || candidate.value("part_of_speech") != item.value("part_of_speech"))
                continue;
            append_unique(articles, candidate.value("base_article").toString());
            append_unique(origins, candidate.value("base_article_origin").toString());
            singular_states.insert(candidate.value("singular_status").toString());
            plural_states.insert(candidate.value("plural_status").toString());
            for (const auto &singular : candidate.value("singular_forms").toStringList())
                append_unique(singulars, singular);
            for (const auto &plural : candidate.value("plural_forms").toStringList())
                append_unique(plurals, plural);
        }
        if (item.value("base_article").toString().isEmpty() && articles.size() == 1) {
            item.insert("base_article", articles.first());
            item.insert("base_article_origin", origins.contains("dictionary") ? "dictionary" : "profile");
        }
        item.insert("singular_forms", singulars);
        item.insert("plural_forms", plurals);
        if (item.value("number_relevant").toBool()) {
            if (!singulars.isEmpty())
                item.insert("singular_status", "available");
            else if (singular_states == QSet<QString>{"unavailable"})
                item.insert("singular_status", "unavailable");
            if (!plurals.isEmpty())
                item.insert("plural_status", "available");
            else if (plural_states == QSet<QString>{"unavailable"})
                item.insert("plural_status", "unavailable");
        }
        value = item;
    }
    return result;
}

}  // namespace showthis
