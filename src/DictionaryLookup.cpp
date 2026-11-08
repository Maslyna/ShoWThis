#include "DictionaryLookup.h"
#include "DictionaryData.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <algorithm>
#include <QtConcurrentRun>

namespace showthis {

DictionaryLookup::DictionaryLookup(QNetworkAccessManager *network, QObject *parent, DictionaryProviders providers)
    : QObject(parent), m_providers(std::move(providers)), m_network(network) {}

DictionaryLookup::~DictionaryLookup() { cancel(); }

const DictionaryProvider *DictionaryLookup::provider(const QString &id) const {
    for (const auto &item : m_providers) {
        if (item->id() == id)
            return item.get();
    }
    return nullptr;
}

QVariantList DictionaryLookup::sources() const {
    QVariantList result;
    for (const auto &item : m_providers)
        result.append(item->metadata());
    return result;
}

bool DictionaryLookup::configure(const QVariantList &selection, bool follow_forms, DictionaryOptions &options,
    QVariantList &normalized, QString &error) const {
    QHash<QString, QStringList> languages;
    QHash<QString, bool> fallback;
    for (const auto &value : selection) {
        const auto item = value.toMap();
        const auto id = item.value("provider").toString();
        const auto *source = provider(id);
        if (!source) {
            error = tr("Unknown dictionary source: %1.").arg(id);
            return false;
        }
        if (fallback.contains(id) && fallback.value(id) != item.value("fallback").toBool()) {
            error = tr("A dictionary source cannot be both primary and fallback.");
            return false;
        }
        fallback[id] = item.value("fallback").toBool();
        const auto selected_languages = item.value("languages").toStringList();
        if (selected_languages.isEmpty()) {
            error = tr("Select at least one definition language for %1.").arg(source->name());
            return false;
        }
        for (const auto &language : selected_languages) {
            const auto supported = source->definition_languages();
            if (std::none_of(supported.begin(), supported.end(), [&](const QVariant &option) {
                return option.toMap().value("code").toString() == language;
            })) {
                error = tr("%1 does not offer definitions in %2.").arg(source->name(), language);
                return false;
            }
            if (!languages[id].contains(language))
                languages[id].append(language);
        }
    }
    options.targets.clear();
    options.follow_forms = follow_forms;
    normalized.clear();
    for (const auto &source : m_providers) {
        auto selected = languages.value(source->id());
        if (selected.isEmpty())
            continue;
        selected.sort();
        normalized.append(QVariantMap{{"provider", source->id()}, {"languages", selected},
            {"fallback", fallback.value(source->id())}});
        for (const auto &language : selected)
            options.targets.append({source->id(), language, fallback.value(source->id())});
    }
    if (options.targets.size() > 32) {
        error = tr("Select at most 32 source/language combinations.");
        return false;
    }
    error.clear();
    return true;
}

QString DictionaryLookup::cache_key(const QString &language, const QString &word, const DictionaryOptions &options) {
    QStringList targets;
    for (const auto &target : options.targets)
        targets.append(target.key() + (target.fallback ? ":fallback" : ":primary"));
    targets.sort();
    QJsonObject configuration{{"targets", QJsonArray::fromStringList(targets)}, {"forms", options.follow_forms}};
    if (!options.data_revision.isEmpty())
        configuration.insert("data_revision", options.data_revision);
    const auto digest = QCryptographicHash::hash(QJsonDocument(configuration).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex();
    return language + ':' + word + '|' + QString::fromLatin1(digest);
}

void DictionaryLookup::cancel() {
    ++m_generation;
    const auto replies = m_replies;
    m_replies.clear();
    for (auto *reply : replies) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    const auto jobs = m_local_jobs;
    m_local_jobs.clear();
    for (auto *job : jobs) {
        job->disconnect(this);
        job->deleteLater();
    }
    m_pending_words = 0;
}

void DictionaryLookup::lookup(const QString &word, const QString &language, const DictionaryOptions &options) {
    cancel();
    m_word = word;
    m_language = language;
    m_options = options;
    m_active.clear();
    m_entries.clear();
    m_visited.clear();
    m_errors.clear();
    m_missing.clear();
    m_fallback_started = false;
    m_local_attempted.clear();
    m_unavailable_network.clear();
    m_notices.clear();
    // Validate direct C++ callers as well as configurations originating in QML.
    for (const auto &target : options.targets) {
        const auto *source = provider(target.provider);
        const auto supported = source ? source->definition_languages() : QVariantList{};
        if (!source || std::none_of(supported.begin(), supported.end(), [&](const QVariant &value) {
            return value.toMap().value("code").toString() == target.language;
        }) || options.targets.size() > 32) {
            emit finished({}, tr("Invalid dictionary source/language selection."), false);
            return;
        }
        if (!target.fallback && std::none_of(m_active.begin(), m_active.end(), [&](const auto &item) {
            return item.key() == target.key();
        }))
            m_active.append(target);
    }
    m_starting = true;
    for (const auto &target : m_active)
        fetch_word(target, word, 0);
    m_starting = false;
    maybe_finish();
}

void DictionaryLookup::send(DictionaryRequest request, ResponseHandler handler) {
    request.request.setTransferTimeout(15000);
    request.request.setRawHeader("User-Agent", "ShoWThis/1.0 (personal vocabulary learner)");
    auto *reply = m_network->sendCustomRequest(request.request, request.method, request.body);
    m_replies.insert(reply);
    connect(reply, &QObject::destroyed, this, [this, reply]() { m_replies.remove(reply); });
    connect(reply, &QNetworkReply::readyRead, this, [reply]() {
        if (reply->bytesAvailable() > 8 * 1024 * 1024)
            reply->abort();
    });
    const auto generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, handler = std::move(handler)]() {
        m_replies.remove(reply);
        const auto body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = body.size() > 8 * 1024 * 1024 ? tr("Dictionary response is too large.")
            : reply->error() == QNetworkReply::NoError ? QString{} : reply->errorString();
        reply->deleteLater();
        if (generation == m_generation)
            handler(body, status, error);
    });
}

void DictionaryLookup::fetch_word(const DictionaryTarget &target, const QString &word, int depth) {
    const auto key = target.key();
    if (depth > 2 || m_visited[key].contains(word) || m_visited[key].size() >= 5)
        return;
    m_visited[key].insert(word);
    ++m_pending_words;
    const auto *source = provider(target.provider);
    const auto access = source->access();
    if ((access == DictionaryAccess::LocalFirst || access == DictionaryAccess::LocalOnly
            || (access != DictionaryAccess::NetworkOnly && m_unavailable_network.contains(target.provider)))
        && try_local(target, word, depth, access != DictionaryAccess::LocalOnly && !m_unavailable_network.contains(target.provider)))
        return;
    if (access == DictionaryAccess::LocalOnly) {
        fail_word(target, word, tr("No downloaded dictionary for words in %1 with definitions in %2. Download it in Settings.")
            .arg(m_language, target.language), true);
        return;
    }
    fetch_request(target, word, depth, source->request(word, m_language, target.language));
}

bool DictionaryLookup::try_local(const DictionaryTarget &target, const QString &word, int depth,
    bool allow_network, const QString &network_error) {
    const auto key = target.key() + '\n' + word;
    if (m_local_attempted.contains(key))
        return false;
    const auto job = provider(target.provider)->local_lookup(word, m_language, target.language);
    if (!job)
        return false;
    m_local_attempted.insert(key);
    const auto generation = m_generation;
    auto *watcher = new QFutureWatcher<DictionaryResponse>(this);
    m_local_jobs.insert(watcher);
    connect(watcher, &QFutureWatcher<DictionaryResponse>::finished, this,
        [this, watcher, target, word, depth, generation, allow_network, network_error]() {
            m_local_jobs.remove(watcher);
            const auto response = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation)
                return;
            if (response.error.isEmpty() && !response.document.value("entries").toArray().isEmpty()) {
                const auto notice = tr("%1: using downloaded data.").arg(provider(target.provider)->name());
                if (!m_notices.contains(notice))
                    m_notices.append(notice);
                consume(target, word, depth, response.document);
            } else if (allow_network) {
                fetch_request(target, word, depth, provider(target.provider)->request(word, m_language, target.language));
            } else {
                const auto message = response.error.isEmpty() ? tr("No entry in the downloaded dictionary.") : response.error;
                fail_word(target, word, network_error.isEmpty() ? message : network_error + " " + message,
                    network_error.isEmpty() && response.missing);
            }
        });
    watcher->setFuture(QtConcurrent::run(*job));
    return true;
}

void DictionaryLookup::fetch_request(const DictionaryTarget &target, const QString &word, int depth,
    const DictionaryRequest &request, int stage) {
    if (stage >= 8) {
        fail_word(target, word, tr("Dictionary request exceeded the stage limit."));
        return;
    }
    send(request, [this, target, word, depth, request, stage](const QByteArray &body, int status, const QString &error) {
        if (!error.isEmpty()) {
            const auto message = status == 404 ? tr("No dictionary entry found.")
                : status == 429 ? tr("Dictionary rate limit reached. Please try again later.")
                : status == 200 ? error : tr("Dictionary request failed (%1).").arg(status);
            if (status == 0 || status == 408 || status == 429 || status >= 500) {
                m_unavailable_network.insert(target.provider);
                if (provider(target.provider)->access() != DictionaryAccess::NetworkOnly && try_local(target, word, depth, false, message))
                    return;
            }
            fail_word(target, word, message, status == 404);
            return;
        }
        const auto response = provider(target.provider)->read_response(request, body, word, m_language, target.language);
        if (!response.error.isEmpty())
            fail_word(target, word, response.error, response.missing);
        else if (response.next)
            fetch_request(target, word, depth, *response.next, stage + 1);
        else
            consume(target, word, depth, response.document);
    });
}

void DictionaryLookup::consume(const DictionaryTarget &target, const QString &word, int depth, const QJsonObject &document) {
    if (document.value("entries").toArray().isEmpty()) {
        fail_word(target, word, tr("No entry for this word language."), true);
        return;
    }
    const auto *source = provider(target.provider);
    for (const auto &value : document.value("entries").toArray()) {
        auto entry = value.toObject();
        if (!entry.contains("headword"))
            entry.insert("headword", document.value("word").toString(word));
        entry.insert("observed_word", m_word);
        entry.insert("provider", target.provider);
        entry.insert("definition_language", target.language);
        auto attribution = entry.value("source").toObject(document.value("source").toObject());
        attribution.insert("provider", target.provider);
        if (attribution.value("name").toString().isEmpty())
            attribution.insert("name", source->name());
        attribution.insert("definition_language", target.language);
        attribution.insert("provider_url", source->website().toString());
        entry.insert("source", attribution);
        m_entries[target.key()].append(entry);
    }
    if (m_options.follow_forms) {
        for (const auto &base : dictionary_form_targets(document, m_language)) {
            if (base == word)
                continue;
            for (const auto &active : m_active)
                fetch_word(active, base, depth + 1);
        }
    }
    finish_word();
}

void DictionaryLookup::fail_word(const DictionaryTarget &target, const QString &word, const QString &message, bool missing) {
    const auto description = provider(target.provider)->name() + " [" + target.language + "]: " + word + " — " + message;
    if (missing)
        m_missing[target.key()].append(description);
    else
        m_errors.append(description);
    finish_word();
}

void DictionaryLookup::finish_word() {
    --m_pending_words;
    maybe_finish();
}

void DictionaryLookup::maybe_finish() {
    if (m_starting || m_pending_words != 0)
        return;
    QJsonArray entries;
    for (const auto &target : m_active) {
        for (const auto &entry : m_entries.value(target.key()))
            entries.append(entry);
    }
    QJsonObject result{{"word", m_word}, {"entries", entries}, {"lookup_language", m_language}};
    if (!m_fallback_started && !has_dictionary_meanings(result, m_language)) {
        m_fallback_started = true;
        QList<DictionaryTarget> fallbacks;
        for (const auto &target : m_options.targets) {
            if (target.fallback && std::none_of(m_active.begin(), m_active.end(), [&](const auto &item) {
                return item.key() == target.key();
            })) {
                m_active.append(target);
                fallbacks.append(target);
            }
        }
        if (!fallbacks.isEmpty()) {
            m_starting = true;
            for (const auto &target : fallbacks)
                fetch_word(target, m_word, 0);
            m_starting = false;
            maybe_finish();
            return;
        }
    }
    if (!entries.isEmpty())
        result.insert("source", entries.first().toObject().value("source"));
    auto messages = m_errors;
    for (const auto &target : m_active) {
        if (!has_dictionary_meanings(QJsonObject{{"entries", m_entries.value(target.key())}}, m_language))
            messages.append(m_missing.value(target.key()));
    }
    if (m_options.targets.isEmpty())
        messages.append(tr("Select a dictionary source and at least one definition language."));
    const bool cacheable = messages.isEmpty();
    messages.append(m_notices);
    emit finished(result, messages.join('\n'), cacheable);
}

}  // namespace showthis
