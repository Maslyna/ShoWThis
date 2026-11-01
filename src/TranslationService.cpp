#include "TranslationService.h"

#include <algorithm>

namespace showthis {

TranslationService::TranslationService(QObject *parent, QNetworkAccessManager *network, TranslationProviders providers)
    : QObject(parent), m_providers(std::move(providers)),
      m_network(network ? network : new QNetworkAccessManager(this)) {
    for (const auto &provider : m_providers) {
        QVariantMap configuration;
        for (const auto &value : provider->configuration_fields()) {
            const auto field = value.toMap();
            if (field.value("secret").toBool())
                continue;
            const auto key = field.value("key").toString();
            configuration.insert(key, m_settings.value("translation/" + provider->id() + "/" + key,
                field.value("default_value")));
        }
        m_configurations.insert(provider->id(), configuration);
    }
    m_provider_id = m_settings.value("translation/provider").toString();
    if (!active_provider())
        m_provider_id.clear();
}

TranslationService::~TranslationService() {
    cancel_pending();
}

QVariantList TranslationService::providers() const {
    QVariantList result{QVariantMap{{"id", ""}, {"name", tr("None (dictionary only)")}}};
    for (const auto &provider : m_providers)
        result.append(QVariantMap{{"id", provider->id()}, {"name", provider->name()}});
    return result;
}

const TranslationProvider *TranslationService::active_provider() const {
    for (const auto &provider : m_providers) {
        if (provider->id() == m_provider_id)
            return provider.get();
    }
    return nullptr;
}

QString TranslationService::provider_name() const {
    const auto *provider = active_provider();
    return provider ? provider->name() : QString{};
}

QVariantList TranslationService::configuration_fields() const {
    const auto *provider = active_provider();
    return provider ? provider->configuration_fields() : QVariantList{};
}

QVariantMap TranslationService::options() const {
    QVariantMap result;
    const auto configuration = m_configurations.value(m_provider_id);
    for (const auto &value : configuration_fields()) {
        const auto field = value.toMap();
        if (!field.value("secret").toBool()) {
            const auto key = field.value("key").toString();
            result.insert(key, configuration.value(key));
        }
    }
    return result;
}

bool TranslationService::configured() const {
    if (!active_provider())
        return false;
    const auto configuration = m_configurations.value(m_provider_id);
    for (const auto &value : configuration_fields()) {
        const auto field = value.toMap();
        if (field.value("required").toBool() && configuration.value(field.value("key").toString()).toString().trimmed().isEmpty())
            return false;
    }
    return true;
}

bool TranslationService::supports_context() const {
    const auto *provider = active_provider();
    return provider && provider->supports_context();
}

void TranslationService::set_provider_id(const QString &id) {
    if (id == m_provider_id)
        return;
    if (!id.isEmpty() && std::none_of(m_providers.begin(), m_providers.end(),
        [&](const auto &provider) { return provider->id() == id; })) {
        emit error_occurred(tr("Unknown translation provider."));
        return;
    }
    clear();
    m_provider_id = id;
    m_settings.setValue("translation/provider", id);
    emit provider_changed();
    emit configuration_changed();
}

void TranslationService::set_option(const QString &key, const QString &value) {
    for (const auto &item : configuration_fields()) {
        const auto field = item.toMap();
        if (field.value("key").toString() != key)
            continue;
        const auto normalized = value.trimmed();
        const auto choices = field.value("choices").toList();
        if (!choices.isEmpty() && std::none_of(choices.begin(), choices.end(),
            [&](const QVariant &choice) { return choice.toMap().value("value").toString() == normalized; })) {
            emit error_occurred(tr("Choose a valid value for %1.").arg(field.value("label").toString()));
            return;
        }
        auto &configuration = m_configurations[m_provider_id];
        if (configuration.value(key).toString() == normalized)
            return;
        clear();
        configuration.insert(key, normalized);
        if (!field.value("secret").toBool())
            m_settings.setValue("translation/" + m_provider_id + "/" + key, normalized);
        emit configuration_changed();
        return;
    }
}

void TranslationService::forget_credentials() {
    clear();
    for (const auto &item : configuration_fields()) {
        const auto field = item.toMap();
        if (field.value("secret").toBool())
            m_configurations[m_provider_id].remove(field.value("key").toString());
    }
    emit configuration_changed();
}

void TranslationService::cancel_pending() {
    if (m_reply) {
        auto *reply = m_reply.data();
        m_reply.clear();
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void TranslationService::clear() {
    cancel_pending();
    m_text.clear();
    emit state_changed();
}

void TranslationService::translate(const QString &word, const QString &context,
    const QString &source_language, const QString &target_language) {
    clear();
    const auto *provider = active_provider();
    if (!configured() || word.trimmed().isEmpty()) {
        emit error_occurred(tr("Enter a word and configure a translator in Settings first."));
        return;
    }
    auto request = provider->make_request(word, supports_context() ? context : QString{},
        source_language, target_language, m_configurations.value(m_provider_id));
    request.request.setTransferTimeout(15000);
    auto *reply = m_network->sendCustomRequest(request.request, request.method, request.body);
    m_reply = reply;
    emit state_changed();
    connect(reply, &QNetworkReply::finished, this, [this, reply, provider]() {
        if (m_reply != reply) {
            reply->deleteLater();
            return;
        }
        m_reply.clear();
        const auto result = provider->parse_response(reply->readAll(),
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->error());
        reply->deleteLater();
        m_text = result.text;
        emit state_changed();
        if (!result.error.isEmpty())
            emit error_occurred(result.error);
        else
            emit translated(m_text);
    });
}

}  // namespace showthis
