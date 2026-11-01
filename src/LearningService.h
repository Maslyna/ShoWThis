#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QVariantList>
#include <QUrl>
#include "LearningStore.h"
#include "DictionaryLookup.h"
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
    Q_PROPERTY(QString dictionary_mode READ dictionary_mode WRITE set_dictionary_mode NOTIFY preferences_changed)
    Q_PROPERTY(QString definition_language READ definition_language WRITE set_definition_language NOTIFY preferences_changed)
    Q_PROPERTY(bool dictionary_fallback READ dictionary_fallback WRITE set_dictionary_fallback NOTIFY preferences_changed)
    Q_PROPERTY(bool follow_forms READ follow_forms WRITE set_follow_forms NOTIFY preferences_changed)
    Q_PROPERTY(QVariantList dictionary_modes READ dictionary_modes CONSTANT)
    Q_PROPERTY(QVariantList definition_languages READ definition_languages CONSTANT)

public:
    explicit LearningService(QObject *parent = nullptr, const QString &file_name = {},
                             QNetworkAccessManager *network = nullptr);
    QVariantList senses() const { return m_senses; }
    QVariantMap source() const { return m_source; }
    TranslationService *translator() const { return m_translator; }
    bool dictionary_busy() const { return m_dictionary->busy(); }
    QString message() const { return m_message; }
    QString storage_error() const { return m_store.error(); }
    QString storage_path() const { return m_store.file_name(); }
    QVariantList vocabulary() const { return m_cards; }
    QVariantList dictionary_history() const { return m_history; }
    int history_limit() const { return m_history_limit; }
    QString source_language() const { return m_source_language; }
    QString target_language() const { return m_target_language; }
    QVariantList language_options() const;
    QString dictionary_mode() const { return m_dictionary_options.mode; }
    QString definition_language() const { return m_dictionary_options.edition; }
    bool dictionary_fallback() const { return m_dictionary_options.fallback; }
    bool follow_forms() const { return m_dictionary_options.follow_forms; }
    QVariantList dictionary_modes() const;
    QVariantList definition_languages() const { return DictionaryLookup::edition_options(); }
    void set_dictionary_mode(const QString &mode);
    void set_definition_language(const QString &language);
    void set_dictionary_fallback(bool enabled);
    void set_follow_forms(bool enabled);
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
    bool write_store(const QVariantList &cards, const QJsonObject &cache, const QVariantList &history);
    void remember_lookup(const QString &key, const QString &context, const QJsonObject &data);
    void display_dictionary(const QJsonObject &data);
    QNetworkAccessManager *m_network;
    DictionaryLookup *m_dictionary;
    TranslationService *m_translator;
    QSettings m_settings;
    LearningStore m_store;
    QString m_source_language;
    QString m_target_language;
    QString m_message;
    QVariantList m_senses;
    QVariantMap m_source;
    QString m_lookup_word;
    QString m_lookup_context;
    QString m_lookup_key;
    DictionaryOptions m_dictionary_options;
    QVariantList m_cards;
    QJsonObject m_cache;
    QVariantList m_history;
    int m_history_limit = 1000;
};

}  // namespace showthis
