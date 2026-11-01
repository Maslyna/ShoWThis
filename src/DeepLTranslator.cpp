#include "DeepLTranslator.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

namespace showthis {

QVariantList DeepLTranslator::configuration_fields() const {
    return {
        QVariantMap{{"key", "plan"}, {"label", QObject::tr("API plan")}, {"default_value", "free"},
            {"choices", QVariantList{
                QVariantMap{{"value", "free"}, {"label", QObject::tr("Free")}},
                QVariantMap{{"value", "pro"}, {"label", QObject::tr("Pro")}}}}},
        QVariantMap{{"key", "api_key"}, {"label", QObject::tr("API key")}, {"secret", true}, {"required", true}}
    };
}

TranslationRequest DeepLTranslator::make_request(const QString &word, const QString &context,
    const QString &source_language, const QString &target_language, const QVariantMap &configuration) const {
    const bool pro = configuration.value("plan").toString() == "pro";
    QNetworkRequest request(QUrl(pro ? "https://api.deepl.com/v2/translate" : "https://api-free.deepl.com/v2/translate"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "DeepL-Auth-Key " + configuration.value("api_key").toString().toUtf8());
    const QJsonObject payload{{"text", QJsonArray{word.trimmed()}},
        {"source_lang", source_language.toUpper()}, {"target_lang", target_language.toUpper()},
        {"context", context.trimmed()}};
    return {request, "POST", QJsonDocument(payload).toJson(QJsonDocument::Compact)};
}

TranslationResult DeepLTranslator::parse_response(const QByteArray &body, int status,
    QNetworkReply::NetworkError error) const {
    if (error != QNetworkReply::NoError) {
        if (status == 403)
            return {{}, QObject::tr("DeepL rejected the API key. Check the key and Free/Pro selection.")};
        if (status == 456)
            return {{}, QObject::tr("DeepL quota exhausted.")};
        if (status == 429)
            return {{}, QObject::tr("DeepL rate limit reached. Please try again later.")};
        if (status == 400)
            return {{}, QObject::tr("DeepL rejected the request. Check the selected languages.")};
        return {{}, QObject::tr("DeepL request failed (%1). Check your connection or try again later.").arg(status)};
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()
        || !document.object().value("translations").isArray())
        return {{}, QObject::tr("DeepL returned an invalid response.")};
    const auto translations = document.object().value("translations").toArray();
    const auto text = translations.isEmpty() ? QString{} : translations.first().toObject().value("text").toString();
    return text.isEmpty() ? TranslationResult{{}, QObject::tr("DeepL returned no translation.")}
                          : TranslationResult{text, {}};
}

}  // namespace showthis
