#pragma once

#include "TranslationProvider.h"

namespace showthis {

class LibreTranslateTranslator final : public TranslationProvider {
public:
    QString id() const override { return QStringLiteral("libretranslate"); }
    QString name() const override { return QStringLiteral("LibreTranslate"); }
    QVariantList configuration_fields() const override;
    QString option_error(const QString &key, const QString &value) const override;
    TranslationRequest make_request(const QString &word, const QString &context,
        const QString &source_language, const QString &target_language, const QVariantMap &configuration) const override;
    TranslationResult parse_response(const QByteArray &body, int status,
        QNetworkReply::NetworkError error) const override;
};

}  // namespace showthis
