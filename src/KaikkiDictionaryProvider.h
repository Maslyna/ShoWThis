#pragma once

#include "DictionaryProvider.h"
#include "KaikkiOfflineStore.h"

namespace showthis {

class KaikkiDictionaryProvider final : public DictionaryProvider {
public:
    explicit KaikkiDictionaryProvider(KaikkiOfflineStore *offline = nullptr) : m_offline(offline) {}
    void initialize(QNetworkAccessManager *network, const QString &directory) override;
    DictionaryProviderState *state() const override { return m_offline; }
    QString settings_component() const override { return QStringLiteral("NotebookOfflineDictionaries.qml"); }
    QString revision(const QString &language, const QString &edition) const override;
    QString id() const override { return QStringLiteral("kaikki"); }
    QString name() const override { return QStringLiteral("Kaikki"); }
    QString description() const override;
    QUrl website() const override { return QUrl(QStringLiteral("https://kaikki.org/")); }
    QVariantList definition_languages() const override;
    DictionaryRequest request(const QString &word, const QString &, const QString &definition_language) const override;
    DictionaryResponse read_response(const DictionaryRequest &request, const QByteArray &body,
        const QString &word, const QString &word_language, const QString &definition_language) const override;
    DictionaryAccess access() const override;
    std::optional<LocalDictionaryLookup> local_lookup(const QString &word, const QString &word_language,
        const QString &definition_language, const LookupCancellation &cancellation = {}) const override;

private:
    DictionaryRequest index_request(const QString &word, const QString &edition, int prefix_length) const;
    static QString edition_path(const QString &edition);
    QPointer<KaikkiOfflineStore> m_offline;
    std::unique_ptr<KaikkiOfflineStore> m_owned_offline;
};

}  // namespace showthis
