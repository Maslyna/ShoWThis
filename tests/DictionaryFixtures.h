#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

inline QByteArray free_entry(const QString &word, const QString &definition, const QString &base = {}) {
    QJsonObject sense{{"definition", definition}};
    if (!base.isEmpty()) {
        sense.insert("tags", QJsonArray{"form of", "plural", "genitive"});
        sense.insert("form_of", QJsonArray{QJsonObject{{"word", base}}});
    }
    return QJsonDocument(QJsonObject{{"word", word}, {"entries", QJsonArray{
        QJsonObject{{"language", QJsonObject{{"code", "de"}}}, {"partOfSpeech", "noun"},
            {"senses", QJsonArray{sense}}}}}}).toJson(QJsonDocument::Compact);
}

inline QByteArray kaikki_index(const QString &word, const QString &edition) {
    QUrl url;
    url.setScheme("https");
    url.setHost("kaikki.org");
    url.setPath((edition == "en" ? QString("/dictionary/") : '/' + edition + "wiktionary/")
        + "All languages combined/meaning/x/xx/" + word + ".html");
    return QJsonDocument(QJsonArray{false, QJsonArray{QJsonArray{word, url.toString()}}}).toJson(QJsonDocument::Compact);
}

inline QByteArray kaikki_entry(const QString &word, const QString &gloss, const QString &language = "de",
    const QString &base = {}) {
    QJsonObject sense{{"glosses", QJsonArray{gloss}}, {"tags", QJsonArray{"neuter"}}};
    if (!base.isEmpty())
        sense.insert("form_of", QJsonArray{QJsonObject{{"word", base}}});
    return QJsonDocument(QJsonObject{{"word", word}, {"lang_code", language}, {"pos", "noun"},
        {"senses", QJsonArray{sense}}}).toJson(QJsonDocument::Compact) + '\n';
}
