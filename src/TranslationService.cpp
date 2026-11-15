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
    cancel_pending(m_word);
    cancel_pending(m_context);
    cancel_pending(m_session);
}

QVariantList TranslationService::providers() const {
    QVariantList result{QVariantMap{{"id", ""}, {"name", tr("Off (manual translations)")}}};
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
    return provider ? provider->name() : tr("Off (manual translations)");
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
    const auto *provider = active_provider();
    if (!provider)
        return false;
    const auto configuration = m_configurations.value(m_provider_id);
    for (const auto &value : provider->configuration_fields()) {
        const auto field = value.toMap();
        const auto key = field.value("key").toString();
        const auto text = configuration.value(key).toString().trimmed();
        if ((field.value("required").toBool() && text.isEmpty()) || !provider->option_error(key, text).isEmpty())
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
    clear_all();
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
        const auto error = active_provider()->option_error(key, normalized);
        if (!error.isEmpty()) {
            emit error_occurred(error);
            return;
        }
        auto &configuration = m_configurations[m_provider_id];
        if (configuration.value(key).toString() == normalized)
            return;
        clear_all();
        configuration.insert(key, normalized);
        if (!field.value("secret").toBool())
            m_settings.setValue("translation/" + m_provider_id + "/" + key, normalized);
        emit configuration_changed();
        return;
    }
}

void TranslationService::forget_credentials() {
    clear_all();
    for (const auto &item : configuration_fields()) {
        const auto field = item.toMap();
        if (field.value("secret").toBool())
            m_configurations[m_provider_id].remove(field.value("key").toString());
    }
    emit configuration_changed();
}

void TranslationService::cancel_pending(Job &job) {
    if (job.reply) {
        auto *reply = job.reply.data();
        job.reply.clear();
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void TranslationService::reset(Job &job) {
    cancel_pending(job);
    job.text.clear();
    job.error.clear();
}

void TranslationService::clear_word() {
    reset(m_word);
    emit state_changed();
}

void TranslationService::clear_context() {
    reset(m_context);
    emit state_changed();
}

void TranslationService::clear() {
    reset(m_word);
    reset(m_context);
    emit state_changed();
}

void TranslationService::clear_session() {
    reset(m_session);
    emit state_changed();
}

void TranslationService::clear_all() {
    reset(m_session);
    clear();
}

void TranslationService::translate(const QString &word, const QString &context,
    const QString &source_language, const QString &target_language) {
    start(m_word, Channel::Word, word, supports_context() ? context : QString{}, source_language, target_language);
}

void TranslationService::translate_context(const QString &text,
    const QString &source_language, const QString &target_language) {
    // A passage is primary input, not a provider's optional word-disambiguation context.
    start(m_context, Channel::Context, text, {}, source_language, target_language);
}

void TranslationService::translate_session(const QString &text,
    const QString &source_language, const QString &target_language) {
    start(m_session, Channel::Session, text, {}, source_language, target_language);
}

void TranslationService::start(Job &job, Channel channel, const QString &text, const QString &context,
    const QString &source_language, const QString &target_language) {
    reset(job);
    const auto *provider = active_provider();
    if (!configured() || text.trimmed().isEmpty()) {
        if (channel == Channel::Word)
            job.error = tr("Enter a word and configure a translator in Settings first.");
        else if (channel == Channel::Context)
            job.error = tr("Enter context and configure a translator in Settings first.");
        else
            job.error = tr("Enter text and configure a translator first.");
        emit state_changed();
        if (channel != Channel::Session)
            emit error_occurred(job.error);
        return;
    }
    auto request = provider->make_request(text, context, source_language, target_language,
        m_configurations.value(m_provider_id));
    request.request.setTransferTimeout(15000);
    auto *reply = m_network->sendCustomRequest(request.request, request.method, request.body);
    job.reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, &job, reply, provider, channel]() {
        if (job.reply != reply) {
            reply->deleteLater();
            return;
        }
        job.reply.clear();
        const auto result = provider->parse_response(reply->readAll(),
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->error());
        reply->deleteLater();
        job.text = result.text;
        job.error = result.error;
        emit state_changed();
        if (!result.error.isEmpty()) {
            if (channel != Channel::Session)
                emit error_occurred(result.error);
        } else if (channel == Channel::Session)
            emit session_translated(result.text);
        else if (channel == Channel::Context)
            emit context_translated(result.text);
        else
            emit translated(result.text);
    });
    emit state_changed();
}

}  // namespace showthis
