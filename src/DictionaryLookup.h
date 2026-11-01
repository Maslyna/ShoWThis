#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>
#include <functional>

namespace showthis {

struct DictionaryOptions {
    QString mode = QStringLiteral("freedictionary");
    QString edition = QStringLiteral("en");
    bool fallback = false;
    bool follow_forms = true;
};

class DictionaryLookup : public QObject {
    Q_OBJECT
public:
    explicit DictionaryLookup(QNetworkAccessManager *network, QObject *parent = nullptr);
    ~DictionaryLookup() override;
    void lookup(const QString &word, const QString &language, const DictionaryOptions &options);
    void cancel();
    bool busy() const { return m_pending_words > 0; }
    static QVariantList edition_options();

signals:
    void finished(const QJsonObject &data, const QString &message, bool cacheable);

private:
    using ResponseHandler = std::function<void(const QByteArray &, int, const QString &)>;
    void get(const QUrl &url, ResponseHandler handler);
    void fetch_word(const QString &provider, const QString &word, int depth);
    void fetch_kaikki_index(const QString &word, int depth, int prefix_length);
    void consume(const QString &provider, const QString &word, int depth, const QJsonObject &document);
    void fail_word(const QString &provider, const QString &word, const QString &message, bool missing = false);
    void finish_word();
    void maybe_finish();
    QString edition_path() const;

    QNetworkAccessManager *m_network;
    QSet<QNetworkReply *> m_replies;
    quint64 m_generation = 0;
    QString m_word;
    QString m_language;
    DictionaryOptions m_options;
    QStringList m_source_order;
    QHash<QString, QSet<QString>> m_visited;
    QHash<QString, QJsonArray> m_entries;
    QStringList m_errors;
    QHash<QString, QStringList> m_missing;
    int m_pending_words = 0;
    bool m_starting = false;
};

}  // namespace showthis
