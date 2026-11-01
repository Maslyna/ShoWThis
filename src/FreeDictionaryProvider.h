#pragma once

#include "DictionaryProvider.h"

namespace showthis {

class FreeDictionaryProvider final : public DictionaryProvider {
public:
    QString id() const override { return QStringLiteral("freedictionary"); }
    QString name() const override { return QStringLiteral("FreeDictionaryAPI"); }
    QString description() const override;
    QUrl website() const override { return QUrl(QStringLiteral("https://freedictionaryapi.com/")); }
    QVariantList definition_languages() const override;
    DictionaryRequest request(const QString &word, const QString &word_language, const QString &) const override;
    DictionaryResponse read_response(const DictionaryRequest &, const QByteArray &body,
        const QString &word, const QString &word_language, const QString &) const override;
};

}  // namespace showthis
