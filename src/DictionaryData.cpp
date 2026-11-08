#include "DictionaryData.h"
#include "NotebookData.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QRegularExpression>
#include <QUrl>

namespace showthis {
namespace {

QJsonArray form_links(const QJsonObject &sense) {
    auto value = sense.value("form_of");
    if (value.isUndefined())
        value = sense.value("formOf");
    auto links = value.toArray();
    if (value.isString() || value.isObject())
        links.append(value);
    QJsonArray result;
    for (const auto &value : links) {
        const auto word = value.isString() ? value.toString() : value.toObject().value("word").toString();
        if (!word.trimmed().isEmpty())
            result.append(QJsonObject{{"word", word.trimmed()}});
    }
    // FreeDictionary currently flattens Wiktionary's form_of into a tagged
    // English definition. Follow only an explicit form relationship, not an
    // arbitrary definition containing the word "of".
    const auto tags = sense.value("tags").toVariant().toStringList();
    if (result.isEmpty() && (tags.contains("form of") || tags.contains("form-of"))) {
        static const QRegularExpression reference("\\bof\\s+(.+?)\\.?$");
        const auto match = reference.match(sense.value("definition").toString());
        if (match.hasMatch())
            result.append(QJsonObject{{"word", match.captured(1).trimmed()}});
    }
    return result;
}

QJsonArray normalize_free_senses(const QJsonArray &senses, int depth = 0) {
    QJsonArray result;
    if (depth > 8)
        return result;
    for (const auto &value : senses) {
        auto sense = value.toObject();
        sense.insert("form_of", form_links(sense));
        sense.insert("subsenses", normalize_free_senses(sense.value("subsenses").toArray(), depth + 1));
        result.append(sense);
        if (result.size() >= 100)
            break;
    }
    return result;
}

QJsonObject source_info(const QString &provider, const QString &edition, const QString &word) {
    QUrl page;
    page.setScheme("https");
    page.setHost(edition + ".wiktionary.org");
    page.setPath("/wiki/" + word);
    return {{"provider", provider}, {"name", provider == "kaikki" ? "Kaikki.org" : "FreeDictionaryAPI.com"},
        {"definition_language", edition}, {"url", page.toString()},
        {"provider_url", provider == "kaikki" ? "https://kaikki.org/" : "https://freedictionaryapi.com/"},
        {"license", QJsonObject{{"name", "CC BY-SA 4.0"}, {"url", "https://creativecommons.org/licenses/by-sa/4.0/"}}}};
}

}  // namespace

QJsonObject free_dictionary_document(const QJsonObject &data, const QString &word, const QString &language) {
    auto source = source_info("freedictionary", "en", word);
    const auto supplied_source = data.value("source").toObject();
    for (auto it = supplied_source.constBegin(); it != supplied_source.constEnd(); ++it)
        source.insert(it.key(), it.value());
    QJsonArray entries;
    for (const auto &value : data.value("entries").toArray()) {
        auto entry = value.toObject();
        if (dictionary_language_code(entry.value("language").toObject().value("code").toString()) != dictionary_language_code(language))
            continue;
        entry.insert("headword", data.value("word").toString(word));
        entry.insert("provider", "freedictionary");
        entry.insert("definition_language", "en");
        entry.insert("source", source);
        entry.insert("senses", normalize_free_senses(entry.value("senses").toArray()));
        entries.append(entry);
    }
    return {{"word", word}, {"entries", entries}, {"source", source}};
}

QJsonObject kaikki_dictionary_document(const QByteArray &jsonl, const QString &word,
    const QString &language, const QString &edition, QString &error) {
    QJsonArray entries;
    error.clear();
    int records = 0;
    for (const auto &line : jsonl.split('\n')) {
        if (line.trimmed().isEmpty())
            continue;
        QJsonParseError parse_error;
        const auto document = QJsonDocument::fromJson(line, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject() || ++records > 1000) {
            error = QObject::tr("Kaikki returned invalid JSONL data.");
            return {};
        }
        const auto raw = document.object();
        if (dictionary_language_code(raw.value("lang_code").toString()) != dictionary_language_code(language))
            continue;
        const auto headword = raw.value("word").toString();
        if (headword.isEmpty() || headword.compare(word, Qt::CaseInsensitive) != 0)
            continue;
        const auto source = source_info("kaikki", edition, headword);
        QJsonArray pronunciations;
        for (const auto &value : raw.value("sounds").toArray()) {
            const auto sound = value.toObject();
            if (!sound.value("ipa").toString().isEmpty())
                pronunciations.append(QJsonObject{{"text", sound.value("ipa")}});
        }
        QJsonArray forms;
        for (const auto &value : raw.value("forms").toArray()) {
            auto form = value.toObject();
            form.insert("word", form.value("form"));
            forms.append(form);
        }
        QJsonArray senses;
        for (const auto &value : raw.value("senses").toArray()) {
            const auto raw_sense = value.toObject();
            auto sense = raw_sense;
            const auto links = form_links(raw_sense);
            auto definition = raw_sense.value("glosses").toVariant().toStringList().join("; ");
            if (definition.isEmpty() && !links.isEmpty())
                definition = QObject::tr("Inflected form of %1").arg(links.first().toObject().value("word").toString());
            sense.insert("definition", definition);
            sense.insert("form_of", links);
            auto tags = raw.value("tags").toVariant().toStringList();
            tags.append(raw_sense.value("tags").toVariant().toStringList());
            tags.removeDuplicates();
            sense.insert("tags", QJsonArray::fromStringList(tags));
            QJsonArray examples;
            for (const auto &example : raw_sense.value("examples").toArray()) {
                const auto object = example.toObject();
                auto text = example.isString() ? example.toString() : object.value("text").toString();
                if (!object.value("translation").toString().isEmpty())
                    text += " — " + object.value("translation").toString();
                if (!text.isEmpty())
                    examples.append(text);
            }
            sense.insert("examples", examples);
            QJsonArray translations;
            auto raw_translations = raw_sense.value("translations").toArray();
            const auto glosses = raw_sense.value("glosses").toVariant().toStringList();
            for (const auto &translation : raw.value("translations").toArray()) {
                // Entry-level translation tables can cover unrelated senses.
                // Attach only translations with an explicitly matching label.
                const auto label = translation.toObject().value("sense").toString();
                if (!label.isEmpty() && glosses.contains(label))
                    raw_translations.append(translation);
            }
            for (const auto &translation : raw_translations) {
                const auto item = translation.toObject();
                translations.append(QJsonObject{{"word", item.value("word")},
                    {"language", QJsonObject{{"code", item.value("code")}}}});
            }
            sense.insert("translations", translations);
            senses.append(sense);
        }
        auto entry = QJsonObject{{"language", QJsonObject{{"code", dictionary_language_code(language)}}},
            {"headword", headword}, {"provider", "kaikki"}, {"definition_language", edition}, {"source", source},
            {"partOfSpeech", raw.value("pos")}, {"pronunciations", pronunciations}, {"forms", forms}, {"senses", senses}};
        for (const auto &key : {"article", "articles", "prefix"}) {
            if (raw.contains(key))
                entry.insert(key, raw.value(key));
        }
        entries.append(entry);
    }
    if (records == 0)
        error = QObject::tr("Kaikki returned empty JSONL data.");
    return {{"word", word}, {"entries", entries}, {"source", source_info("kaikki", edition, word)}};
}

QStringList dictionary_form_targets(const QJsonObject &document, const QString &language) {
    QStringList result;
    for (const auto &value : dictionary_senses(document, language)) {
        for (const auto &link : value.toMap().value("form_of").toList()) {
            const auto word = link.toMap().value("word").toString().trimmed();
            if (!word.isEmpty() && word.size() <= 200 && !word.contains(QRegularExpression("[\\r\\n\\t]|://"))
                && !result.contains(word))
                result.append(word);
            if (result.size() >= 4)
                return result;
        }
    }
    return result;
}

bool has_dictionary_meanings(const QJsonObject &document, const QString &language) {
    for (const auto &value : dictionary_senses(document, language)) {
        const auto sense = value.toMap();
        const auto tags = sense.value("tags").toStringList();
        if (sense.value("form_of").toList().isEmpty() && !tags.contains("form-of") && !tags.contains("form of"))
            return true;
    }
    return false;
}

}  // namespace showthis
