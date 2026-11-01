#pragma once

#include "TranslationProvider.h"

namespace showthis {

class DeepLTranslator final : public TranslationProvider {
public:
    QString id() const override { return QStringLiteral("deepl"); }
    QString name() const override { return QStringLiteral("DeepL"); }
    QVariantList configuration_fields() const override;
    bool supports_context() const override { return true; }
    TranslationRequest make_request(const QString &word, const QString &context,
        const QString &source_language, const QString &target_language, const QVariantMap &configuration) const override;
    TranslationResult parse_response(const QByteArray &body, int status,
        QNetworkReply::NetworkError error) const override;
};

}  // namespace showthis
