#pragma once

#include <QJsonObject>
#include <QNetworkRequest>
#include <QVariantList>
#include <memory>
#include <optional>
#include <vector>
#include <functional>
#include <QObject>
#include <atomic>

class QNetworkAccessManager;

namespace showthis {

struct DictionaryTarget {
    QString provider;
    QString language;
    bool fallback = false;
    QString key() const { return provider + ':' + language; }
};

struct DictionaryOptions {
    QList<DictionaryTarget> targets{{QStringLiteral("freedictionary"), QStringLiteral("en"), false}};
    bool follow_forms = true;
    QString data_revision;
};

struct DictionaryRequest {
    QNetworkRequest request;
    QByteArray method = "GET";
    QByteArray body;
    QVariantMap state;
};

struct DictionaryResponse {
    QJsonObject document;
    std::optional<DictionaryRequest> next;
    QString error;
    bool missing = false;
};

enum class DictionaryAccess { NetworkOnly, NetworkFirst, LocalFirst, LocalOnly };
using LocalDictionaryLookup = std::function<DictionaryResponse()>;
using LookupCancellation = std::shared_ptr<std::atomic_bool>;
class DictionaryProviderState : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void data_changed();
};
class KaikkiOfflineStore;

// A provider owns its protocol, available definition languages, and presentation
// metadata. The picker and lookup scheduler do not branch on provider IDs.
class DictionaryProvider {
public:
    virtual ~DictionaryProvider() = default;
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QUrl website() const = 0;
    virtual QVariantList definition_languages() const = 0;
    virtual DictionaryRequest request(const QString &word, const QString &word_language,
                                      const QString &definition_language) const = 0;
    virtual DictionaryResponse read_response(const DictionaryRequest &request, const QByteArray &body,
        const QString &word, const QString &word_language, const QString &definition_language) const = 0;
    virtual DictionaryAccess access() const { return DictionaryAccess::NetworkOnly; }
    virtual void initialize(QNetworkAccessManager *, const QString &) {}
    virtual DictionaryProviderState *state() const { return nullptr; }
    virtual QString settings_component() const { return {}; }
    virtual QString revision(const QString &, const QString &) const { return {}; }
    virtual std::optional<LocalDictionaryLookup> local_lookup(const QString &, const QString &, const QString &,
        const LookupCancellation & = {}) const { return {}; }
    QVariantMap metadata() const;
};

using DictionaryProviders = std::vector<std::unique_ptr<DictionaryProvider>>;
DictionaryProviders default_dictionary_providers(KaikkiOfflineStore *offline = nullptr);

}  // namespace showthis
