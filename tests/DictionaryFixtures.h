#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include "DictionaryLookup.h"

inline QVariantMap dictionary_selection(const QString &provider, const QStringList &languages, bool fallback = false) {
    return {{"provider", provider}, {"languages", languages}, {"fallback", fallback}};
}

inline QString dictionary_cache_key(const QString &language, const QString &word) {
    return showthis::DictionaryLookup::cache_key(language, word, {});
}

// Deliberately not known to the production registry, scheduler, or QML.
class SampleDictionaryProvider : public showthis::DictionaryProvider {
public:
    QString id() const override { return "sample"; }
    QString name() const override { return "Sample Dictionary"; }
    QString description() const override { return "An injected provider for extensibility tests."; }
    QUrl website() const override { return QUrl("https://dictionary.example/"); }
    QVariantList definition_languages() const override {
        return {QVariantMap{{"code", "fr"}, {"name", "Français"}},
            QVariantMap{{"code", "en"}, {"name", "English"}}};
    }
    showthis::DictionaryRequest request(const QString &word, const QString &, const QString &language) const override {
        QUrl url = website();
        url.setPath('/' + language + '/' + word);
        return {QNetworkRequest(url), "GET", {}, {}};
    }
    showthis::DictionaryResponse read_response(const showthis::DictionaryRequest &, const QByteArray &body,
        const QString &, const QString &, const QString &) const override {
        return {QJsonDocument::fromJson(body).object(), {}, {}};
    }
};

inline showthis::DictionaryProviders sample_dictionary_providers() {
    auto providers = showthis::default_dictionary_providers();
    providers.push_back(std::make_unique<SampleDictionaryProvider>());
    return providers;
}

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
