#include "FreeDictionaryProvider.h"
#include "DictionaryData.h"

#include <QJsonDocument>
#include <QObject>

namespace showthis {

QString FreeDictionaryProvider::description() const {
    return QObject::tr("Multilingual word lookup with definitions from English Wiktionary.");
}

QVariantList FreeDictionaryProvider::definition_languages() const {
    return {QVariantMap{{"code", "en"}, {"name", "English"}}};
}

DictionaryRequest FreeDictionaryProvider::request(const QString &word, const QString &word_language, const QString &) const {
    const auto url = QUrl::fromEncoded("https://freedictionaryapi.com/api/v1/entries/"
        + word_language.toUtf8() + '/' + QUrl::toPercentEncoding(word) + "?translations=true");
    return {QNetworkRequest(url), "GET", {}, {}};
}

DictionaryResponse FreeDictionaryProvider::read_response(const DictionaryRequest &, const QByteArray &body,
    const QString &word, const QString &word_language, const QString &) const {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || !document.object().value("entries").isArray())
        return {{}, {}, QObject::tr("The dictionary returned an invalid response.")};
    return {free_dictionary_document(document.object(), word, word_language), {}, {}};
}

}  // namespace showthis
