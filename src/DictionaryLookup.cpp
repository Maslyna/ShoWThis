#include "DictionaryLookup.h"
#include "DictionaryData.h"

#include <QJsonDocument>
#include <QNetworkRequest>

namespace showthis {

DictionaryLookup::DictionaryLookup(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network) {}

DictionaryLookup::~DictionaryLookup() {
    cancel();
}

QVariantList DictionaryLookup::edition_options() {
    return {
        QVariantMap{{"code", "en"}, {"name", "English"}},
        QVariantMap{{"code", "ru"}, {"name", QString::fromUtf8("Русский")}},
        QVariantMap{{"code", "de"}, {"name", "Deutsch"}},
        QVariantMap{{"code", "fr"}, {"name", QString::fromUtf8("Français")}},
        QVariantMap{{"code", "es"}, {"name", QString::fromUtf8("Español")}},
        QVariantMap{{"code", "pl"}, {"name", "Polski"}},
        QVariantMap{{"code", "it"}, {"name", "Italiano"}},
        QVariantMap{{"code", "pt"}, {"name", QString::fromUtf8("Português")}},
        QVariantMap{{"code", "nl"}, {"name", "Nederlands"}},
        QVariantMap{{"code", "zh"}, {"name", QString::fromUtf8("中文")}},
        QVariantMap{{"code", "ja"}, {"name", QString::fromUtf8("日本語")}},
        QVariantMap{{"code", "ko"}, {"name", QString::fromUtf8("한국어")}},
        QVariantMap{{"code", "el"}, {"name", QString::fromUtf8("Ελληνικά")}},
        QVariantMap{{"code", "tr"}, {"name", QString::fromUtf8("Türkçe")}},
        QVariantMap{{"code", "cs"}, {"name", QString::fromUtf8("Čeština")}},
        QVariantMap{{"code", "id"}, {"name", "Bahasa Indonesia"}},
        QVariantMap{{"code", "ms"}, {"name", "Bahasa Melayu"}},
        QVariantMap{{"code", "vi"}, {"name", QString::fromUtf8("Tiếng Việt")}},
        QVariantMap{{"code", "th"}, {"name", QString::fromUtf8("ไทย")}},
        QVariantMap{{"code", "ku"}, {"name", QString::fromUtf8("Kurdî")}}
    };
}

QString DictionaryLookup::edition_path() const {
    return m_options.edition == "en" ? QStringLiteral("/dictionary/") : '/' + m_options.edition + "wiktionary/";
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
    m_pending_words = 0;
}

void DictionaryLookup::lookup(const QString &word, const QString &language, const DictionaryOptions &options) {
    cancel();
    m_word = word;
    m_language = language;
    m_options = options;
    m_entries.clear();
    m_visited.clear();
    m_errors.clear();
    m_missing.clear();
    m_source_order = options.mode == "both" ? QStringList{"kaikki", "freedictionary"} : QStringList{options.mode};
    m_starting = true;
    for (const auto &provider : m_source_order)
        fetch_word(provider, word, 0);
    m_starting = false;
    maybe_finish();
}

void DictionaryLookup::get(const QUrl &url, ResponseHandler handler) {
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setRawHeader("User-Agent", "ShoWThis/1.0 (personal vocabulary learner)");
    auto *reply = m_network->get(request);
    m_replies.insert(reply);
    connect(reply, &QObject::destroyed, this, [this, reply]() { m_replies.remove(reply); });
    // Bound responses from remote/static endpoints, including search indexes.
    connect(reply, &QNetworkReply::readyRead, this, [reply]() {
        if (reply->bytesAvailable() > 8 * 1024 * 1024)
            reply->abort();
    });
    const auto generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, handler = std::move(handler)]() {
        m_replies.remove(reply);
        const auto bytes = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = bytes.size() > 8 * 1024 * 1024 ? tr("Dictionary response is too large.")
            : reply->error() == QNetworkReply::NoError ? QString{} : reply->errorString();
        reply->deleteLater();
        if (generation == m_generation)
            handler(bytes, status, error);
    });
}

void DictionaryLookup::fetch_word(const QString &provider, const QString &word, int depth) {
    if (depth > 2 || m_visited[provider].contains(word) || m_visited[provider].size() >= 5)
        return;
    m_visited[provider].insert(word);
    ++m_pending_words;
    if (provider == "kaikki") {
        fetch_kaikki_index(word, depth, 2);
        return;
    }
    const auto url = QUrl::fromEncoded("https://freedictionaryapi.com/api/v1/entries/"
        + m_language.toUtf8() + '/' + QUrl::toPercentEncoding(word) + "?translations=true");
    get(url, [this, word, depth](const QByteArray &bytes, int status, const QString &error) {
        if (!error.isEmpty()) {
            const auto message = status == 404 ? tr("No dictionary entry found.")
                : status == 429 ? tr("Dictionary rate limit reached. Please try again later.")
                : status == 200 ? error : tr("Dictionary request failed (%1).").arg(status);
            fail_word("freedictionary", word, message, status == 404);
            return;
        }
        QJsonParseError parse_error;
        const auto document = QJsonDocument::fromJson(bytes, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject()
            || !document.object().value("entries").isArray()) {
            fail_word("freedictionary", word, tr("The dictionary returned an invalid response."));
            return;
        }
        consume("freedictionary", word, depth, free_dictionary_document(document.object(), word, m_language));
    });
}

void DictionaryLookup::fetch_kaikki_index(const QString &word, int depth, int prefix_length) {
    auto prefix = word.toLower().left(prefix_length);
    for (const auto &replacement : QList<QPair<QString, QString>>{{"/", "_slash_"}, {"\\", "_backslash_"},
        {"*", "_star_"}, {"?", "_ques_"}, {"#", "_hash_"}, {".", "_dot_"}})
        prefix.replace(replacement.first, replacement.second);
    const auto url = QUrl::fromEncoded("https://kaikki.org" + edition_path().toUtf8()
        + "search/start/" + QUrl::toPercentEncoding(prefix) + ".json");
    get(url, [this, word, depth, prefix_length](const QByteArray &bytes, int status, const QString &error) {
        if (!error.isEmpty()) {
            fail_word("kaikki", word, status == 404 ? tr("No entry in the selected Kaikki edition.")
                : status == 200 ? error : tr("Kaikki search failed (%1).").arg(status), status == 404);
            return;
        }
        QJsonParseError parse_error;
        const auto document = QJsonDocument::fromJson(bytes, &parse_error);
        const auto array = document.array();
        if (parse_error.error != QJsonParseError::NoError || array.size() != 2
            || !array[0].isBool() || !array[1].isArray()) {
            fail_word("kaikki", word, tr("Kaikki returned an invalid search index."));
            return;
        }
        QUrl match;
        for (const auto &value : array[1].toArray()) {
            const auto item = value.toArray();
            if (item.size() < 2 || item[0].toString().compare(word, Qt::CaseInsensitive) != 0)
                continue;
            match = QUrl(item[1].toString());
            if (item[0].toString() == word)
                break;
        }
        if (match.isEmpty() && array[0].toBool() && prefix_length == 2 && word.size() > 2) {
            fetch_kaikki_index(word, depth, 3);
            return;
        }
        // Follow only published per-word paths on the selected Kaikki edition.
        if (match.scheme() != "https" || match.host() != "kaikki.org"
            || !match.path().startsWith(edition_path() + "All languages combined/meaning/")
            || !match.path().endsWith(".html")) {
            fail_word("kaikki", word, tr("No entry in the selected Kaikki edition."), true);
            return;
        }
        auto path = match.path();
        path.chop(5);
        match.setPath(path + ".jsonl");
        match.setQuery({});
        match.setFragment({});
        get(match, [this, word, depth](const QByteArray &data, int status, const QString &error) {
            if (!error.isEmpty()) {
                fail_word("kaikki", word, tr("Kaikki entry request failed (%1).").arg(status), status == 404);
                return;
            }
            QString parse_error;
            const auto document = kaikki_dictionary_document(data, word, m_language, m_options.edition, parse_error);
            if (!parse_error.isEmpty()) {
                fail_word("kaikki", word, parse_error);
                return;
            }
            consume("kaikki", word, depth, document);
        });
    });
}

void DictionaryLookup::consume(const QString &provider, const QString &word, int depth, const QJsonObject &document) {
    if (document.value("entries").toArray().isEmpty()) {
        fail_word(provider, word, tr("No entry for this word language in the selected dictionary."), true);
        return;
    }
    for (const auto &value : document.value("entries").toArray()) {
        auto entry = value.toObject();
        entry.insert("observed_word", m_word);
        m_entries[provider].append(entry);
    }
    if (m_options.follow_forms) {
        for (const auto &target : dictionary_form_targets(document, m_language)) {
            if (target != word) {
                fetch_word(provider, target, depth + 1);
                // One edition may have a headword but no separate entry for
                // its inflection. Share explicit headword references with the
                // other source when both sources are participating.
                const auto other = provider == "kaikki" ? QStringLiteral("freedictionary") : QStringLiteral("kaikki");
                if (m_source_order.contains(other))
                    fetch_word(other, target, depth + 1);
            }
        }
    }
    finish_word();
}

void DictionaryLookup::fail_word(const QString &provider, const QString &word, const QString &message, bool missing) {
    const auto description = (provider == "kaikki" ? "Kaikki [" + m_options.edition + ']' : QStringLiteral("FreeDictionary [en]"))
        + ": " + word + " — " + message;
    if (missing)
        m_missing[provider].append(description);
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
    for (const auto &provider : m_source_order) {
        for (const auto &entry : m_entries.value(provider))
            entries.append(entry);
    }
    QJsonObject result{{"word", m_word}, {"entries", entries}, {"lookup_language", m_language}};
    if (m_options.fallback && m_source_order.size() == 1 && !has_dictionary_meanings(result, m_language)) {
        const auto other = m_source_order.first() == "kaikki" ? QStringLiteral("freedictionary") : QStringLiteral("kaikki");
        m_source_order.append(other);
        fetch_word(other, m_word, 0);
        return;
    }
    if (!entries.isEmpty())
        result.insert("source", entries.first().toObject().value("source"));
    auto messages = m_errors;
    for (const auto &provider : m_source_order) {
        // A missing inflection page is not a failure if the headword was
        // subsequently found, possibly via a reference from the other source.
        if (!has_dictionary_meanings(QJsonObject{{"entries", m_entries.value(provider)}}, m_language))
            messages.append(m_missing.value(provider));
    }
    emit finished(result, messages.join('\n'), messages.isEmpty());
}

}  // namespace showthis
