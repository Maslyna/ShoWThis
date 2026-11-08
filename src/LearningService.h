#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QVariantList>
#include <QUrl>
#include "LearningStore.h"
#include "DictionaryLookup.h"
#include "KaikkiOfflineStore.h"
#include "TranslationService.h"
#include "NotebookListModel.h"
#include "LookupCacheStore.h"

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
    Q_PROPERTY(QString cache_path READ cache_path CONSTANT)
    Q_PROPERTY(QVariantList vocabulary READ vocabulary NOTIFY vocabulary_changed)
    Q_PROPERTY(NotebookListModel *vocabulary_model READ vocabulary_model CONSTANT)
    Q_PROPERTY(NotebookListModel *history_model READ history_model CONSTANT)
    Q_PROPERTY(QVariantList dictionary_history READ dictionary_history NOTIFY history_changed)
    Q_PROPERTY(int history_limit READ history_limit WRITE set_history_limit NOTIFY preferences_changed)
    Q_PROPERTY(QString source_language READ source_language WRITE set_source_language NOTIFY preferences_changed)
    Q_PROPERTY(QString target_language READ target_language WRITE set_target_language NOTIFY preferences_changed)
    Q_PROPERTY(QVariantList language_options READ language_options CONSTANT)
    Q_PROPERTY(QVariantList dictionary_sources READ dictionary_sources CONSTANT)
    Q_PROPERTY(QVariantList dictionary_selection READ dictionary_selection NOTIFY preferences_changed)
    Q_PROPERTY(bool follow_forms READ follow_forms NOTIFY preferences_changed)
    Q_PROPERTY(KaikkiOfflineStore *offline_dictionaries READ offline_dictionaries CONSTANT)

public:
    explicit LearningService(QObject *parent = nullptr, const QString &file_name = {},
                              QNetworkAccessManager *network = nullptr,
                               DictionaryProviders providers = {});
    ~LearningService() override;
    QVariantList senses() const { return m_senses; }
    QVariantMap source() const { return m_source; }
    TranslationService *translator() const { return m_translator; }
    bool dictionary_busy() const { return m_dictionary->busy(); }
    QString message() const { return m_message; }
    QString storage_error() const { return m_store.error().isEmpty() ? m_lookup_cache.error() : m_store.error(); }
    QString storage_path() const { return m_store.file_name(); }
    QString cache_path() const { return m_lookup_cache.path(); }
    QVariantList vocabulary() const { return m_cards; }
    NotebookListModel *vocabulary_model() { return &m_vocabulary_model; }
    NotebookListModel *history_model() { return &m_history_model; }
    QVariantList dictionary_history() const { return m_history; }
    int history_limit() const { return m_history_limit; }
    QString source_language() const { return m_source_language; }
    QString target_language() const { return m_target_language; }
    QVariantList language_options() const;
    QVariantList dictionary_sources() const { return m_dictionary->sources(); }
    QVariantList dictionary_selection() const { return m_dictionary_selection; }
    bool follow_forms() const { return m_dictionary_options.follow_forms; }
    // Compatibility accessor; provider state is owned by its implementation.
    KaikkiOfflineStore *offline_dictionaries() const;
    Q_INVOKABLE bool configure_dictionaries(const QVariantList &selection, bool follow_forms);
    Q_INVOKABLE void prepare_draft(const QVariantMap &configuration, bool use_defaults);
    void set_source_language(const QString &language);
    void set_target_language(const QString &language);
    void set_history_limit(int limit);

    Q_INVOKABLE void clear();
    Q_INVOKABLE void lookup(const QString &word, const QString &context = {}, bool refresh = false);
    Q_INVOKABLE bool lookup_history(const QString &key);
    Q_INVOKABLE bool flush_cache() { return m_lookup_cache.flush(); }
    Q_INVOKABLE void translate(const QString &word, const QString &context);
    Q_INVOKABLE bool save_word(const QString &id, const QString &word, const QString &context,
                             const QString &meaning, const QString &translation,
                             const QString &notes, int sense_index,
                              const QString &article = QStringLiteral("auto"),
                              const QVariantMap &word_forms = {});
    Q_INVOKABLE QString format_word(const QString &word, const QString &article) const;
    Q_INVOKABLE QString number_availability(const QVariantMap &forms) const;
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
    bool write_store(const QVariantList &cards, const QJsonObject &cache, const QVariantList &history,
        bool flush_cache = false, bool vocabulary_write = false);
    void trim_cache(QVariantList &history, QJsonObject &cache, int limit);
    void remember_lookup(const QString &key, const QString &context, const QJsonObject &data);
    void display_dictionary(const QJsonObject &data);
    QNetworkAccessManager *m_network;
    DictionaryLookup *m_dictionary;
    TranslationService *m_translator;
    QSettings m_settings;
    LearningStore m_store;
    LookupCacheStore m_lookup_cache;
    QString m_source_language;
    QString m_target_language;
    QString m_message;
    QVariantList m_senses;
    QVariantMap m_source;
    QString m_lookup_word;
    QString m_lookup_context;
    QString m_lookup_key;
    DictionaryOptions m_dictionary_options;
    QVariantList m_dictionary_selection;
    QVariantList m_cards;
    QJsonObject m_cache;
    QHash<QString, qint64> m_cache_sizes;
    QJsonObject m_stale_cache;
    QVariantList m_history;
    int m_history_limit = 1000;
    bool m_transient_preferences = false;
    bool m_has_legacy_cache = false;
    NotebookListModel m_vocabulary_model;
    NotebookListModel m_history_model;
};

}  // namespace showthis
