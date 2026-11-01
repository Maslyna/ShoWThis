#pragma once

#include "DictionaryProvider.h"

namespace showthis {

class KaikkiDictionaryProvider final : public DictionaryProvider {
public:
    QString id() const override { return QStringLiteral("kaikki"); }
    QString name() const override { return QStringLiteral("Kaikki"); }
    QString description() const override;
    QUrl website() const override { return QUrl(QStringLiteral("https://kaikki.org/")); }
    QVariantList definition_languages() const override;
    DictionaryRequest request(const QString &word, const QString &, const QString &definition_language) const override;
    DictionaryResponse read_response(const DictionaryRequest &request, const QByteArray &body,
        const QString &word, const QString &word_language, const QString &definition_language) const override;

private:
    DictionaryRequest index_request(const QString &word, const QString &edition, int prefix_length) const;
    static QString edition_path(const QString &edition);
};

}  // namespace showthis
