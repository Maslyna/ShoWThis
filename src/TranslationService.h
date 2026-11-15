#pragma once

#include "TranslationProvider.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QSettings>

namespace showthis {

class TranslationService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList providers READ providers CONSTANT)
    Q_PROPERTY(QString provider_id READ provider_id WRITE set_provider_id NOTIFY configuration_changed)
    Q_PROPERTY(QString provider_name READ provider_name NOTIFY configuration_changed)
    Q_PROPERTY(QVariantList configuration_fields READ configuration_fields NOTIFY provider_changed)
    Q_PROPERTY(QVariantMap options READ options NOTIFY configuration_changed)
    Q_PROPERTY(bool configured READ configured NOTIFY configuration_changed)
    Q_PROPERTY(bool supports_context READ supports_context NOTIFY configuration_changed)
    Q_PROPERTY(bool busy READ busy NOTIFY state_changed)
    Q_PROPERTY(bool word_busy READ word_busy NOTIFY state_changed)
    Q_PROPERTY(bool context_busy READ context_busy NOTIFY state_changed)
    Q_PROPERTY(bool session_busy READ session_busy NOTIFY state_changed)
    Q_PROPERTY(QString session_text READ session_text NOTIFY state_changed)
    Q_PROPERTY(QString session_error READ session_error NOTIFY state_changed)
    Q_PROPERTY(QString text READ text NOTIFY state_changed)
    Q_PROPERTY(QString context_text READ context_text NOTIFY state_changed)
    Q_PROPERTY(QString word_error READ word_error NOTIFY state_changed)
    Q_PROPERTY(QString context_error READ context_error NOTIFY state_changed)

public:
    explicit TranslationService(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr,
                                TranslationProviders providers = default_translation_providers());
    ~TranslationService() override;
    QVariantList providers() const;
    QString provider_id() const { return m_provider_id; }
    QString provider_name() const;
    QVariantList configuration_fields() const;
    // Public options only. Secrets are held in memory and never exposed to QML.
    QVariantMap options() const;
    bool configured() const;
    bool supports_context() const;
    bool busy() const { return word_busy() || context_busy(); }
    bool word_busy() const { return !m_word.reply.isNull(); }
    bool context_busy() const { return !m_context.reply.isNull(); }
    QString text() const { return m_word.text; }
    QString context_text() const { return m_context.text; }
    QString word_error() const { return m_word.error; }
    QString context_error() const { return m_context.error; }
    bool session_busy() const { return !m_session.reply.isNull(); }
    QString session_text() const { return m_session.text; }
    QString session_error() const { return m_session.error; }
    void set_provider_id(const QString &id);
    Q_INVOKABLE void set_option(const QString &key, const QString &value);
    Q_INVOKABLE void forget_credentials();
    void translate(const QString &word, const QString &context,
                   const QString &source_language, const QString &target_language);
    Q_INVOKABLE void translate_context(const QString &text,
                                      const QString &source_language, const QString &target_language);
    Q_INVOKABLE void clear_word();
    Q_INVOKABLE void clear_context();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void translate_session(const QString &text,
                                      const QString &source_language, const QString &target_language);
    Q_INVOKABLE void clear_session();

signals:
    void provider_changed();
    void configuration_changed();
    void state_changed();
    void translated(const QString &text);
    void context_translated(const QString &text);
    void session_translated(const QString &text);
    void error_occurred(const QString &message);

private:
    enum class Channel { Word, Context, Session };
    struct Job {
        QPointer<QNetworkReply> reply;
        QString text;
        QString error;
    };
    void cancel_pending(Job &job);
    void reset(Job &job);
    void clear_all();
    void start(Job &job, Channel channel, const QString &text, const QString &context,
               const QString &source_language, const QString &target_language);
    const TranslationProvider *active_provider() const;
    TranslationProviders m_providers;
    QNetworkAccessManager *m_network;
    QSettings m_settings;
    QString m_provider_id;
    QHash<QString, QVariantMap> m_configurations;
    Job m_word;
    Job m_context;
    Job m_session;
};

}  // namespace showthis
