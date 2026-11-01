#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSettings>
#include <QVariantList>
#include <QUrl>
#include "LearningStore.h"
#include "TranslationService.h"

namespace showthis {

class LearningService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList senses READ senses NOTIFY lookup_changed)
    Q_PROPERTY(QVariantMap source READ source NOTIFY lookup_changed)
    Q_PROPERTY(bool dictionary_busy READ dictionary_busy NOTIFY lookup_changed)
    Q_PROPERTY(TranslationService *translator READ translator CONSTANT)
    Q_PROPERTY(QString message READ message NOTIFY message_changed)
    Q_PROPERTY(QString storage_error READ storage_error NOTIFY vocabulary_changed)
    Q_PROPERTY(QString storage_path READ storage_path CONSTANT)
    Q_PROPERTY(QVariantList vocabulary READ vocabulary NOTIFY vocabulary_changed)
    Q_PROPERTY(QVariantList dictionary_history READ dictionary_history NOTIFY history_changed)
    Q_PROPERTY(int history_limit READ history_limit WRITE set_history_limit NOTIFY preferences_changed)
    Q_PROPERTY(QString source_language READ source_language WRITE set_source_language NOTIFY preferences_changed)
    Q_PROPERTY(QString target_language READ target_language WRITE set_target_language NOTIFY preferences_changed)
    Q_PROPERTY(QVariantList language_options READ language_options CONSTANT)

public:
    explicit LearningService(QObject *parent = nullptr, const QString &file_name = {},
                             QNetworkAccessManager *network = nullptr);
    QVariantList senses() const { return m_senses; }
    QVariantMap source() const { return m_source; }
    TranslationService *translator() const { return m_translator; }
    bool dictionary_busy() const { return !m_dictionary_reply.isNull(); }
    QString message() const { return m_message; }
    QString storage_error() const { return m_store.error(); }
    QString storage_path() const { return m_store.file_name(); }
    QVariantList vocabulary() const { return m_cards; }
    QVariantList dictionary_history() const { return m_history; }
    int history_limit() const { return m_history_limit; }
    QString source_language() const { return m_source_language; }
    QString target_language() const { return m_target_language; }
    QVariantList language_options() const;
    void set_source_language(const QString &language);
    void set_target_language(const QString &language);
    void set_history_limit(int limit);

    Q_INVOKABLE void clear();
    Q_INVOKABLE void lookup(const QString &word, const QString &context = {});
    Q_INVOKABLE void translate(const QString &word, const QString &context);
    Q_INVOKABLE bool save_word(const QString &id, const QString &word, const QString &context,
                             const QString &meaning, const QString &translation,
                             const QString &notes, int sense_index,
                             const QString &article = QStringLiteral("auto"));
    Q_INVOKABLE QString format_word(const QString &word, const QString &article) const;
    Q_INVOKABLE bool remove_word(const QString &id);
    Q_INVOKABLE bool clear_history();
    Q_INVOKABLE bool export_anki(const QUrl &destination, const QStringList &card_ids = {});

signals:
    void lookup_changed();
    void preferences_changed();
    void vocabulary_changed();
    void message_changed();
    void history_changed();

private:
    void set_message(const QString &message);
    void cancel(QPointer<QNetworkReply> &reply);
    bool write_store(const QVariantList &cards, const QJsonObject &cache, const QVariantList &history);
    void remember_lookup(const QString &key, const QString &context, const QJsonObject &data);
    void display_dictionary(const QJsonObject &data);
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_dictionary_reply;
    TranslationService *m_translator;
    QSettings m_settings;
    LearningStore m_store;
    QString m_source_language;
    QString m_target_language;
    QString m_message;
    QVariantList m_senses;
    QVariantMap m_source;
    QString m_lookup_word;
    QVariantList m_cards;
    QJsonObject m_cache;
    QVariantList m_history;
    int m_history_limit = 1000;
};

}  // namespace showthis
