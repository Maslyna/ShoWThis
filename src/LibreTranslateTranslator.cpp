#include "LibreTranslateTranslator.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

namespace showthis {

QVariantList LibreTranslateTranslator::configuration_fields() const {
    return {
        QVariantMap{{"key", "base_url"}, {"label", QObject::tr("Server URL")}, {"required", true},
            {"default_value", "http://127.0.0.1:5000"},
            {"description", QObject::tr("Run LibreTranslate separately. Use its base URL, including any hosting prefix, not /translate. HTTP is unencrypted; use HTTPS for remote servers.")}},
        QVariantMap{{"key", "api_key"}, {"label", QObject::tr("API key (optional)")}, {"secret", true}}
    };
}

QString LibreTranslateTranslator::option_error(const QString &key, const QString &value) const {
    if (key != "base_url")
        return {};
    const QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https")
        || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
        return QObject::tr("Enter an HTTP or HTTPS LibreTranslate server URL without credentials, query parameters, or a fragment.");
    return {};
}

TranslationRequest LibreTranslateTranslator::make_request(const QString &word, const QString &,
    const QString &source_language, const QString &target_language, const QVariantMap &configuration) const {
    QUrl url(configuration.value("base_url").toString(), QUrl::StrictMode);
    auto path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += "translate";
    url.setPath(path);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    QJsonObject payload{{"q", word.trimmed()}, {"source", source_language}, {"target", target_language}, {"format", "text"}};
    const auto api_key = configuration.value("api_key").toString();
    if (!api_key.isEmpty())
        payload.insert("api_key", api_key);
    return {request, "POST", QJsonDocument(payload).toJson(QJsonDocument::Compact)};
}

TranslationResult LibreTranslateTranslator::parse_response(const QByteArray &body, int status,
    QNetworkReply::NetworkError error) const {
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(body, &parse_error);
    const auto object = document.object();
    if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
        if (status == 401 || status == 403)
            return {{}, QObject::tr("LibreTranslate denied access. Check whether this server requires an API key.")};
        if (status == 429)
            return {{}, QObject::tr("LibreTranslate rate limit reached. Please try again later.")};
        if (error == QNetworkReply::TimeoutError)
            return {{}, QObject::tr("LibreTranslate timed out. Check that the server and its language models are ready.")};
        if (status == 0)
            return {{}, QObject::tr("Could not connect to LibreTranslate. Check the server URL and that the service is running.")};
        const auto detail = object.value("error").toString();
        return {{}, detail.isEmpty() ? QObject::tr("LibreTranslate request failed (%1). Check the server and installed languages.").arg(status)
                                    : QObject::tr("LibreTranslate: %1").arg(detail)};
    }
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return {{}, QObject::tr("LibreTranslate returned an invalid response.")};
    const auto detail = object.value("error").toString();
    if (!detail.isEmpty())
        return {{}, QObject::tr("LibreTranslate: %1").arg(detail)};
    if (!object.value("translatedText").isString())
        return {{}, QObject::tr("LibreTranslate returned an invalid response.")};
    const auto text = object.value("translatedText").toString();
    return text.trimmed().isEmpty() ? TranslationResult{{}, QObject::tr("LibreTranslate returned no translation.")}
                                    : TranslationResult{text, {}};
}

}  // namespace showthis
