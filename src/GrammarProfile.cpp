#include "GrammarProfile.h"
#include "NotebookData.h"

#include <QFile>
#include <QJsonDocument>
#include <algorithm>

static void init_grammar_resources() {
    static const bool initialized = []() {
        Q_INIT_RESOURCE(grammar);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace showthis {
namespace {

bool has_any(const QStringList &tags, const QJsonValue &values) {
    const auto options = values.toVariant().toStringList();
    return std::any_of(options.begin(), options.end(), [&](const auto &tag) { return tags.contains(tag); });
}

bool nominative_or_unmarked(const QStringList &tags) {
    return tags.contains("nominative") || (!tags.contains("genitive") && !tags.contains("dative")
        && !tags.contains("accusative") && !tags.contains("instrumental") && !tags.contains("locative")
        && !tags.contains("vocative") && !tags.contains("ablative"));
}

}  // namespace

QJsonObject default_grammar_profiles() {
    static const auto profiles = []() {
        init_grammar_resources();
        QFile file(":/grammar/profiles.json");
        if (!file.open(QIODevice::ReadOnly))
            qFatal("Cannot load bundled grammar profiles");
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject())
            qFatal("Invalid bundled grammar profiles");
        return document.object();
    }();
    return profiles;
}

bool missing_dictionary_form(const QString &word) {
    return word.trimmed().isEmpty() || default_grammar_profiles().value("number").toObject()
        .value("missing_forms").toVariant().toStringList().contains(word.trimmed());
}

void apply_grammar_profile(QVariantMap &sense, const QString &word_language, const QJsonObject &profiles) {
    const auto language = normalized_language_code(word_language).section('-', 0, 0);
    const auto part = sense.value("part_of_speech").toString();
    const auto rule = profiles.value("languages").toObject().value(language).toObject().value(part).toObject();
    sense.insert("grammar_profile", rule.isEmpty() ? "generic" : language);
    if (!sense.value("article").toString().isEmpty() && sense.value("article_origin").toString().isEmpty())
        sense.insert("article_origin", "dictionary");
    if (!sense.value("base_article").toString().isEmpty() && sense.value("base_article_origin").toString().isEmpty())
        sense.insert("base_article_origin", "dictionary");
    const auto number = profiles.value("number").toObject();
    const bool relevant = number.value("parts_of_speech").toVariant().toStringList().contains(part);
    sense.insert("number_relevant", relevant);
    if (!relevant) {
        // Finite verb agreement is not a noun's singular/plural card field.
        // Original tagged forms remain available in sense["forms"].
        sense.insert("singular_forms", QStringList{});
        sense.insert("plural_forms", QStringList{});
        sense.insert("singular_status", "not_applicable");
        sense.insert("plural_status", "not_applicable");
        return;
    }
    const auto tags = sense.value("tags").toStringList();
    const bool qualified = has_any(tags, number.value("qualifier_tags"));
    const auto absent = sense.value("unavailable_numbers").toStringList();
    const bool no_singular = absent.contains("singular") || (!qualified && has_any(tags, number.value("singular_absent_tags")));
    const bool no_plural = absent.contains("plural") || (!qualified && has_any(tags, number.value("plural_absent_tags"))
        && !has_any(tags, number.value("plural_absence_blockers")));
    const auto base = sense.value("base_form").toString();
    const auto headword = sense.value("headword").toString();
    const bool plural_headword = tags.contains("plural") || no_singular;
    auto singulars = sense.value("singular_forms").toStringList();
    auto plurals = sense.value("plural_forms").toStringList();
    // Form tables are commonly shared by all senses. A rare countable sense's
    // plural must not overwrite an unqualified restriction on this sense.
    if (no_singular)
        singulars.clear();
    if (no_plural)
        plurals.clear();
    if (base == headword && !base.isEmpty()) {
        if (!no_singular && !plural_headword && rule.value("lemma_is_singular").toBool() && !singulars.contains(base))
            singulars.prepend(base);
        if (!no_plural && plural_headword && nominative_or_unmarked(tags) && !plurals.contains(headword))
            plurals.prepend(headword);
    }
    sense.insert("singular_forms", singulars);
    sense.insert("plural_forms", plurals);
    sense.insert("singular_status", !singulars.isEmpty() ? "available" : no_singular ? "unavailable" : "unknown");
    sense.insert("plural_status", !plurals.isEmpty() ? "available" : no_plural ? "unavailable" : "unknown");

    const auto articles = rule.value("articles").toObject();
    QString singular_article;
    int genders = 0;
    const auto gender_articles = articles.value("singular").toObject();
    for (auto it = gender_articles.constBegin(); it != gender_articles.constEnd(); ++it) {
        if (tags.contains(it.key())) {
            ++genders;
            singular_article = it.value().toString();
        }
    }
    if (genders != 1)
        singular_article.clear();
    const auto plural_article = articles.value("plural").toString();
    const auto base_article = base == headword && plural_headword ? plural_article : singular_article;
    const bool ambiguous_explicit = base == headword && sense.value("articles").toStringList().size() > 1;
    if (!base.isEmpty() && sense.value("base_article").toString().isEmpty() && !ambiguous_explicit && !base_article.isEmpty()) {
        sense.insert("base_article", base_article);
        sense.insert("base_article_origin", "profile");
    }
    const auto current_article = base == headword ? sense.value("base_article").toString()
        : plural_headword && nominative_or_unmarked(tags) ? plural_article : QString{};
    if (sense.value("articles").toStringList().isEmpty() && !current_article.isEmpty()) {
        sense.insert("article", current_article);
        sense.insert("articles", QStringList{current_article});
        sense.insert("article_origin", base == headword ? sense.value("base_article_origin").toString() : "profile");
    }
}

}  // namespace showthis
