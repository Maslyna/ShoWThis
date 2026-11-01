#include "NotebookData.h"

#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <algorithm>

namespace showthis {
namespace {

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
            item.insert("headword", entry.value("headword").toString());
            item.insert("observed_word", entry.value("observed_word").toString());
            item.insert("provider", entry.value("provider").toString());
            item.insert("definition_language", entry.value("definition_language").toString());
            item.insert("source", entry.value("source").toObject().toVariantMap());
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

QVariantList dictionary_senses(const QJsonObject &data, const QString &language) {
    QVariantList result;
    for (const auto &value : data.value("entries").toArray()) {
        auto entry = value.toObject();
        if (!entry.contains("source"))
            entry.insert("source", data.value("source"));
        if (!entry.contains("headword"))
            entry.insert("headword", data.value("word"));
        if (!entry.contains("provider"))
            entry.insert("provider", "freedictionary");
        if (!entry.contains("definition_language"))
            entry.insert("definition_language", "en");
        if (normalized_language_code(entry.value("language").toObject().value("code").toString()) == language)
            append_senses(entry.value("senses").toArray(), entry, result);
    }
    return result;
}

}  // namespace showthis
