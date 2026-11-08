#include "KaikkiDictionaryProvider.h"
#include "DictionaryData.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QStandardPaths>

namespace showthis {

void KaikkiDictionaryProvider::initialize(QNetworkAccessManager *network, const QString &directory) {
    if (!m_offline) {
        const auto root = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : directory;
        m_owned_offline = std::make_unique<KaikkiOfflineStore>(network, root + "/kaikki");
        m_offline = m_owned_offline.get();
    }
}

QString KaikkiDictionaryProvider::revision(const QString &language, const QString &edition) const {
    return m_offline ? m_offline->cache_token(language, edition) : QString{};
}

QString KaikkiDictionaryProvider::description() const {
    return QObject::tr("Wiktionary definitions in the languages you choose. Coverage varies by edition.");
}

DictionaryAccess KaikkiDictionaryProvider::access() const {
    if (!m_offline || m_offline->mode() == "online_only")
        return DictionaryAccess::NetworkOnly;
    if (m_offline->mode() == "prefer_local")
        return DictionaryAccess::LocalFirst;
    if (m_offline->mode() == "local_only")
        return DictionaryAccess::LocalOnly;
    return DictionaryAccess::NetworkFirst;
}

std::optional<LocalDictionaryLookup> KaikkiDictionaryProvider::local_lookup(const QString &word, const QString &word_language,
    const QString &definition_language, const LookupCancellation &cancellation) const {
    return m_offline ? m_offline->lookup(definition_language, word_language, word, cancellation) : std::nullopt;
}

QVariantList KaikkiDictionaryProvider::definition_languages() const {
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

QString KaikkiDictionaryProvider::edition_path(const QString &edition) {
    return edition == "en" ? QStringLiteral("/dictionary/") : '/' + edition + "wiktionary/";
}

DictionaryRequest KaikkiDictionaryProvider::index_request(const QString &word, const QString &edition, int prefix_length) const {
    auto prefix = word.toLower().left(prefix_length);
    for (const auto &replacement : QList<QPair<QString, QString>>{{"/", "_slash_"}, {"\\", "_backslash_"},
        {"*", "_star_"}, {"?", "_ques_"}, {"#", "_hash_"}, {".", "_dot_"}})
        prefix.replace(replacement.first, replacement.second);
    const auto url = QUrl::fromEncoded("https://kaikki.org" + edition_path(edition).toUtf8()
        + "search/start/" + QUrl::toPercentEncoding(prefix) + ".json");
    return {QNetworkRequest(url), "GET", {}, {{"prefix_length", prefix_length}}};
}

DictionaryRequest KaikkiDictionaryProvider::request(const QString &word, const QString &, const QString &definition_language) const {
    return index_request(word, definition_language, 2);
}

DictionaryResponse KaikkiDictionaryProvider::read_response(const DictionaryRequest &request, const QByteArray &body,
    const QString &word, const QString &word_language, const QString &definition_language) const {
    if (request.state.value("entry").toBool()) {
        QString error;
        const auto document = kaikki_dictionary_document(body, word, word_language, definition_language, error);
        return {document, {}, error};
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(body, &error);
    const auto array = document.array();
    if (error.error != QJsonParseError::NoError || array.size() != 2 || !array[0].isBool() || !array[1].isArray())
        return {{}, {}, QObject::tr("Kaikki returned an invalid search index.")};
    QUrl match;
    for (const auto &value : array[1].toArray()) {
        const auto item = value.toArray();
        if (item.size() < 2 || item[0].toString().compare(word, Qt::CaseInsensitive) != 0)
            continue;
        match = QUrl(item[1].toString());
        if (item[0].toString() == word)
            break;
    }
    if (match.isEmpty() && array[0].toBool() && request.state.value("prefix_length").toInt() == 2 && word.size() > 2)
        return {{}, index_request(word, definition_language, 3), {}};
    if (match.scheme() != "https" || match.host() != "kaikki.org"
        || !match.path().startsWith(edition_path(definition_language) + "All languages combined/meaning/")
        || !match.path().endsWith(".html"))
        return {{}, {}, QObject::tr("No entry in this definition language."), true};
    auto path = match.path();
    path.chop(5);
    match.setPath(path + ".jsonl");
    match.setQuery({});
    match.setFragment({});
    return {{}, DictionaryRequest{QNetworkRequest(match), "GET", {}, {{"entry", true}}}, {}};
}

}  // namespace showthis
