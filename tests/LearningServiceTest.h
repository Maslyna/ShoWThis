#pragma once

#include <QObject>
#include <QTemporaryDir>

class LearningServiceTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void dictionary_cache_and_vocabulary();
    void contextual_translation();
    void failures_and_cancellation();
    void corrupt_store_is_preserved();
    void history_limit_and_recency();
    void default_history_capacity();
    void history_disabled_and_cleared();
    void legacy_history_migration();
    void anki_export();
    void dictionary_grammar_data();
    void dictionary_grammar();
    void article_persistence_and_legacy_cards();
    void flexible_languages();
    void retired_metadata_is_preserved();
    void failed_write_does_not_commit_changes();

private:
    QTemporaryDir m_settings_directory;
};
