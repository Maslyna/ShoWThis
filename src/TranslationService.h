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
    Q_PROPERTY(QString text READ text NOTIFY state_changed)

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
    bool busy() const { return !m_reply.isNull(); }
    QString text() const { return m_text; }
    void set_provider_id(const QString &id);
    Q_INVOKABLE void set_option(const QString &key, const QString &value);
    Q_INVOKABLE void forget_credentials();
    void translate(const QString &word, const QString &context,
                   const QString &source_language, const QString &target_language);
    void clear();

signals:
    void provider_changed();
    void configuration_changed();
    void state_changed();
    void translated(const QString &text);
    void error_occurred(const QString &message);

private:
    void cancel_pending();
    const TranslationProvider *active_provider() const;
    TranslationProviders m_providers;
    QNetworkAccessManager *m_network;
    QSettings m_settings;
    QString m_provider_id;
    QHash<QString, QVariantMap> m_configurations;
    QPointer<QNetworkReply> m_reply;
    QString m_text;
};

}  // namespace showthis
