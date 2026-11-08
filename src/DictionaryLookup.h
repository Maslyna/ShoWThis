#pragma once

#include "DictionaryProvider.h"

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>
#include <functional>
#include <QFutureWatcher>

namespace showthis {

class DictionaryLookup : public QObject {
    Q_OBJECT
public:
    explicit DictionaryLookup(QNetworkAccessManager *network, QObject *parent = nullptr,
                              DictionaryProviders providers = default_dictionary_providers());
    ~DictionaryLookup() override;
    void lookup(const QString &word, const QString &language, const DictionaryOptions &options);
    void cancel();
    bool busy() const { return m_pending_words > 0; }
    QVariantList sources() const;
    bool configure(const QVariantList &selection, bool follow_forms, DictionaryOptions &options,
                   QVariantList &normalized, QString &error) const;
    static QString cache_key(const QString &language, const QString &word, const DictionaryOptions &options);

signals:
    void finished(const QJsonObject &data, const QString &message, bool cacheable);

private:
    using ResponseHandler = std::function<void(const QByteArray &, int, const QString &, QNetworkReply::NetworkError)>;
    const DictionaryProvider *provider(const QString &id) const;
    void send(DictionaryRequest request, ResponseHandler handler);
    void fetch_word(const DictionaryTarget &target, const QString &word, int depth);
    void fetch_request(const DictionaryTarget &target, const QString &word, int depth,
                        const DictionaryRequest &request, int stage = 0);
    bool try_local(const DictionaryTarget &target, const QString &word, int depth,
                   bool allow_network, const QString &network_error = {});
    void consume(const DictionaryTarget &target, const QString &word, int depth, const QJsonObject &document);
    void fail_word(const DictionaryTarget &target, const QString &word, const QString &message, bool missing = false);
    void finish_word();
    void maybe_finish();

    DictionaryProviders m_providers;
    QNetworkAccessManager *m_network;
    QSet<QNetworkReply *> m_replies;
    QSet<QFutureWatcher<DictionaryResponse> *> m_local_jobs;
    QSet<QString> m_local_attempted;
    QSet<QString> m_unavailable_network;
    QStringList m_notices;
    quint64 m_generation = 0;
    QString m_word;
    QString m_language;
    DictionaryOptions m_options;
    QList<DictionaryTarget> m_active;
    QHash<QString, QSet<QString>> m_visited;
    QHash<QString, QJsonArray> m_entries;
    QList<QPair<QString, int>> m_form_targets;
    QStringList m_errors;
    QHash<QString, QStringList> m_missing;
    int m_pending_words = 0;
    bool m_starting = false;
    bool m_fallback_started = false;
};

}  // namespace showthis
