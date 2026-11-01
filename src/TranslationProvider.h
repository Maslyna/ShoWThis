#pragma once

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVariantList>
#include <memory>
#include <vector>

namespace showthis {

struct TranslationRequest {
    QNetworkRequest request;
    QByteArray method = "POST";
    QByteArray body;
};

struct TranslationResult {
    QString text;
    QString error;
};

// Providers describe their settings and own their protocol. The service and
// QML settings editor do not need to know provider-specific field names.
class TranslationProvider {
public:
    virtual ~TranslationProvider() = default;
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    // Fields: key, label, secret, required, default_value, optional choices
    // (a list of {value, label}). Credentials must be marked secret.
    virtual QVariantList configuration_fields() const = 0;
    virtual bool supports_context() const { return false; }
    virtual TranslationRequest make_request(const QString &word, const QString &context,
        const QString &source_language, const QString &target_language, const QVariantMap &configuration) const = 0;
    virtual TranslationResult parse_response(const QByteArray &body, int status,
        QNetworkReply::NetworkError error) const = 0;
};

using TranslationProviders = std::vector<std::unique_ptr<TranslationProvider>>;
TranslationProviders default_translation_providers();

}  // namespace showthis
