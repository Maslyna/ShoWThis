#include "KaikkiOfflineStore.h"
#include "DictionaryData.h"
#include "KaikkiDictionaryProvider.h"
#include "NotebookData.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLocale>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSet>
#include <QUuid>
#include <QtConcurrentRun>
#include <QtEndian>
#include <zlib.h>

namespace showthis {
namespace {

constexpr qint64 max_record_size = 8 * 1024 * 1024;
constexpr qint64 max_download_size = 32LL * 1024 * 1024 * 1024;

struct Database {
    QString name;
    QSqlDatabase connection;
    ~Database() { close(); }
    void close() {
        if (name.isEmpty())
            return;
        connection.close();
        connection = {};
        QSqlDatabase::removeDatabase(name);
        name.clear();
    }
    bool open(const QString &path, bool read_only) {
        name = QUuid::createUuid().toString();
        connection = QSqlDatabase::addDatabase("QSQLITE", name);
        connection.setConnectOptions(read_only ? "QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=500" : "QSQLITE_BUSY_TIMEOUT=500");
        connection.setDatabaseName(path);
        return connection.open();
    }
    bool exec(const char *sql) {
        const auto statements = QString::fromUtf8(sql).split(';', Qt::SkipEmptyParts);
        for (const auto &statement : statements) {
            if (!QSqlQuery(connection).exec(statement))
                return false;
        }
        return true;
    }
};

struct Statement {
    QSqlQuery query;
    bool prepare(const Database &db, const char *sql) {
        query = QSqlQuery(db.connection);
        return query.prepare(QString::fromUtf8(sql));
    }
    void text(int index, const QString &value) { query.bindValue(index - 1, value); }
};

QVariantMap pack_metadata(const QString &path) {
    Database db;
    Statement statement;
    if (!db.open(path, true) || !statement.prepare(db, "SELECT json FROM metadata LIMIT 1")
        || !statement.query.exec() || !statement.query.next())
        return {};
    const auto bytes = statement.query.value(0).toByteArray();
    const auto result = QJsonDocument::fromJson(bytes).object().toVariantMap();
    const auto format = result.value("format").toInt();
    return format == 1 || format == 2 ? result : QVariantMap{};
}

QString language_name(const QString &code) {
    if (code.isEmpty())
        return QObject::tr("All word languages");
    const auto language = QLocale::codeToLanguage(code.section('-', 0, 0));
    return language == QLocale::C || language == QLocale::AnyLanguage ? code : QLocale::languageToString(language);
}

QNetworkRequest download_request(const QUrl &url) {
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setRawHeader("User-Agent", "ShoWThis/1.0 (personal vocabulary learner)");
    request.setRawHeader("Accept-Encoding", "identity");
    return request;
}

}  // namespace

KaikkiOfflineStore::KaikkiOfflineStore(QNetworkAccessManager *network, const QString &directory, QObject *parent)
    : DictionaryProviderState(parent), m_network(network),
      m_directory(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/kaikki" : directory),
      m_mode(m_settings.value("dictionary/offline_mode", "fallback").toString()),
      m_watcher(new QFutureWatcher<ImportResult>(this)) {
    if (m_mode != "fallback" && m_mode != "prefer_local" && m_mode != "local_only" && m_mode != "online_only")
        m_mode = "fallback";
    refresh_packs();
    m_timer.setInterval(150);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (!m_job)
            return;
        m_progress = m_job->building_index || m_total <= 0 ? -1 : qMin(1.0, double(m_job->compressed_read) / m_total);
        m_message = m_job->cancelled ? tr("Cancelling…") : m_job->building_index ? tr("Finalizing and installing the word index…")
            : tr("Indexing downloaded data… %1 entries").arg(m_job->records.load());
        emit changed();
    });
    connect(m_watcher, &QFutureWatcher<ImportResult>::finished, this, [this]() {
        const auto result = m_watcher->result();
        if (!result.cancelled && result.error.isEmpty()) {
            refresh_packs();
            finish(tr("Kaikki dictionary installed. Select its word language and Kaikki definition language for lookup."));
            emit data_changed();
        } else {
            finish(result.cancelled ? tr("Download cancelled. Previously installed data was kept.") : result.error);
        }
    });
}

KaikkiOfflineStore::~KaikkiOfflineStore() {
    // Cancellation may update progress; never notify QML while provider-owned
    // state (or its containing service) is already being torn down.
    blockSignals(true);
    m_timer.stop();
    cancel();
    m_watcher->disconnect(this);
    m_watcher->waitForFinished();
}

QVariantList KaikkiOfflineStore::editions() const { return KaikkiDictionaryProvider{}.definition_languages(); }

bool KaikkiOfflineStore::valid_edition(const QString &edition) const {
    for (const auto &value : editions()) {
        if (value.toMap().value("code").toString() == edition)
            return true;
    }
    return false;
}

QUrl KaikkiOfflineStore::download_url(const QString &edition) {
    return QUrl("https://kaikki.org" + (edition == "en" ? QString("/dictionary/") : '/' + edition + "wiktionary/")
        + "raw-wiktextract-data.jsonl.gz");
}

bool KaikkiOfflineStore::valid_word_language(const QString &language) const {
    return language.isEmpty() || !dictionary_language_code(language).isEmpty();
}

QString KaikkiOfflineStore::normalize_word_language(const QString &language) const {
    return dictionary_language_code(language);
}

QString KaikkiOfflineStore::pack_path(const QString &edition, const QString &word_language) const {
    return QDir(m_directory).filePath((word_language.isEmpty() ? QString{} : word_language + "--") + edition + ".sqlite");
}

void KaikkiOfflineStore::refresh_packs() {
    m_packs.clear();
    const auto files = QDir(m_directory).entryInfoList({"*.sqlite"}, QDir::Files, QDir::Name);
    for (const auto &file : files) {
        const auto path = file.absoluteFilePath();
        auto metadata = pack_metadata(path);
        const auto code = metadata.value("edition").toString();
        const auto word_language = metadata.value("word_language").toString();
        if (!valid_edition(code) || !valid_word_language(word_language)
            || file.fileName() != QFileInfo(pack_path(code, word_language)).fileName())
            continue;
        QString name;
        for (const auto &edition : editions()) {
            if (edition.toMap().value("code").toString() == code)
                name = edition.toMap().value("name").toString();
        }
        metadata.insert("name", name);
        metadata.insert("word_language", word_language);
        metadata.insert("word_language_name", language_name(word_language));
        metadata.insert("bytes", QFileInfo(path).size());
        metadata.insert("size_text", QLocale{}.formattedDataSize(QFileInfo(path).size()));
        m_packs.append(metadata);
    }
}

void KaikkiOfflineStore::set_mode(const QString &mode) {
    if (mode == m_mode || (mode != "fallback" && mode != "prefer_local" && mode != "local_only" && mode != "online_only"))
        return;
    m_mode = mode;
    m_settings.setValue("dictionary/offline_mode", mode);
    emit changed();
    emit data_changed();
}

QString KaikkiOfflineStore::cache_token(const QString &word_language, const QString &edition) const {
    QVariantList packs;
    if (m_mode != "online_only") {
        for (const auto &value : m_packs) {
            const auto pack = value.toMap();
            if ((edition.isEmpty() || pack.value("edition").toString() == edition)
                && (word_language.isEmpty() || pack.value("word_language").toString().isEmpty()
                    || pack.value("word_language").toString() == word_language))
                packs.append(QVariantMap{{"edition", pack.value("edition")}, {"word_language", pack.value("word_language")},
                    {"revision", pack.value("revision")}});
        }
    }
    if (!word_language.isEmpty() && !edition.isEmpty()) {
        // Match lookup's precedence: a specific-language pack shadows an
        // all-language pack, so updating the unused pack is irrelevant.
        for (const auto &value : packs) {
            if (value.toMap().value("word_language").toString() == word_language) {
                packs = {value};
                break;
            }
        }
    }
    if (packs.isEmpty() && m_mode == "fallback")
        return {};
    const QJsonObject data{{"mode", m_mode}, {"packs", QJsonArray::fromVariantList(packs)}};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(data).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

void KaikkiOfflineStore::finish(const QString &message) {
    m_timer.stop();
    m_file.reset();
    m_job.reset();
    m_busy = false;
    m_progress = -1;
    m_message = message;
    emit changed();
}

void KaikkiOfflineStore::inspect(const QString &edition, const QString &requested_language) {
    if (busy())
        return;
    if (!valid_edition(edition) || !valid_word_language(requested_language)) {
        m_offer.clear();
        finish(tr("Choose a definition language and a valid word-language code, such as en or de."));
        return;
    }
    const auto word_language = dictionary_language_code(requested_language);
    m_offer.clear();
    m_busy = true;
    m_message = tr("Checking download size…");
    emit changed();
    auto *reply = m_network->head(download_request(download_url(edition)));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, edition, word_language]() {
        m_reply.clear();
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            finish(tr("Could not check this Kaikki download (%1). Check your internet connection or try another edition.").arg(status));
            return;
        }
        const auto length = reply->header(QNetworkRequest::ContentLengthHeader);
        const qint64 size = length.isValid() ? length.toLongLong() : -1;
        QString name = edition;
        for (const auto &value : editions()) {
            if (value.toMap().value("code").toString() == edition)
                name = value.toMap().value("name").toString();
        }
        m_offer = {{"edition", edition}, {"name", name}, {"bytes", size},
            {"word_language", word_language}, {"word_language_name", language_name(word_language)},
            {"size_text", size >= 0 ? QLocale{}.formattedDataSize(size) : tr("Size unknown")}};
        finish(tr("Download size checked. No dictionary data has been downloaded yet."));
    });
}

void KaikkiOfflineStore::download(const QString &edition, const QString &requested_language) {
    const auto word_language = dictionary_language_code(requested_language);
    if (busy() || !valid_edition(edition) || !valid_word_language(requested_language)
        || m_offer.value("edition").toString() != edition || m_offer.value("word_language").toString() != word_language)
        return;
    if (!QDir().mkpath(m_directory)) {
        finish(tr("Cannot create the offline dictionary directory."));
        return;
    }
    const auto path = pack_path(edition, word_language);
    const auto existing = QFileInfo::exists(path) ? pack_metadata(path) : QVariantMap{};
    if (QFileInfo::exists(path) && (existing.value("edition").toString() != edition
            || existing.value("word_language").toString() != word_language)) {
        finish(tr("An unrecognized file occupies this dictionary's location. Move it aside before downloading; it was not overwritten."));
        return;
    }
    const qint64 size = m_offer.value("bytes", -1).toLongLong();
    const QStorageInfo storage(m_directory);
    if (size > max_download_size || (size > 0 && storage.isValid() && storage.bytesAvailable() < size + 64 * 1024 * 1024)) {
        finish(tr("Not enough disk space for this download, or the download is too large."));
        return;
    }
    m_job = std::make_shared<Job>();
    m_job->temporary = std::make_shared<QTemporaryDir>(QDir(m_directory).filePath(".download-XXXXXX"));
    m_job->edition = edition;
    m_job->word_language = word_language;
    m_job->url = download_url(edition);
    if (!m_job->temporary->isValid()) {
        finish(tr("Cannot create temporary download files."));
        return;
    }
    m_file = std::make_unique<QFile>(m_job->temporary->filePath("data.jsonl.gz"));
    if (!m_file->open(QIODevice::WriteOnly)) {
        finish(tr("Cannot write the dictionary download."));
        return;
    }
    m_file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_received = 0;
    m_total = size;
    m_progress = size > 0 ? 0 : -1;
    m_download_error.clear();
    m_busy = true;
    m_message = tr("Downloading Kaikki…");
    emit changed();
    auto *reply = m_network->get(download_request(m_job->url));
    reply->setReadBufferSize(1024 * 1024);
    m_reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, &KaikkiOfflineStore::drain_download);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64, qint64 total) {
        if (total > 0)
            m_total = total;
        m_progress = m_total > 0 ? qMin(1.0, double(m_received) / m_total) : -1;
        emit changed();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        drain_download();
        m_reply.clear();
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto response_length = reply->header(QNetworkRequest::ContentLengthHeader);
        if (!m_download_error.isEmpty() || reply->error() != QNetworkReply::NoError || status != 200
            || (response_length.isValid() && response_length.toLongLong() >= 0 && m_received != response_length.toLongLong())) {
            finish(!m_download_error.isEmpty() ? m_download_error
                : tr("Kaikki download failed or was incomplete (%1). Previously installed data was kept.").arg(status));
            return;
        }
        m_total = m_received;
        if (!m_file->flush()) {
            finish(tr("Could not finish writing the dictionary download."));
            return;
        }
        m_file.reset();
        start_indexing();
    });
}

void KaikkiOfflineStore::drain_download() {
    if (!m_reply || !m_file || !m_download_error.isEmpty())
        return;
    while (m_reply->bytesAvailable() > 0) {
        const auto data = m_reply->read(256 * 1024);
        if (data.isEmpty())
            break;
        m_received += data.size();
        if (m_received > max_download_size || m_file->write(data) != data.size()) {
            m_download_error = tr("Dictionary download exceeded its size limit or could not be written. Check disk space.");
            // Do not abort synchronously inside readyRead: a reply can emit
            // finished immediately from abort(), including in network tests.
            const auto reply = m_reply;
            QTimer::singleShot(0, reply.data(), [reply]() { if (reply) reply->abort(); });
            return;
        }
    }
}

void KaikkiOfflineStore::start_indexing() {
    m_message = tr("Indexing downloaded data…");
    m_progress = 0;
    emit changed();
    m_timer.start();
    const auto job = m_job;
    const auto destination = pack_path(job->edition, job->word_language);
    m_watcher->setFuture(QtConcurrent::run([job, destination]() { return import_pack(job, destination); }));
}

void KaikkiOfflineStore::cancel() {
    if (m_job)
        m_job->cancelled = true;
    if (m_reply) {
        auto *reply = m_reply.data();
        m_reply.clear();
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
        finish(tr("Download cancelled. Previously installed data was kept."));
    } else if (m_watcher->isRunning()) {
        m_message = tr("Cancelling…");
        emit changed();
    }
}

bool KaikkiOfflineStore::remove(const QString &edition, const QString &requested_language) {
    const auto word_language = dictionary_language_code(requested_language);
    if (busy() || !valid_edition(edition) || !valid_word_language(requested_language))
        return false;
    const auto path = pack_path(edition, word_language);
    const auto metadata = pack_metadata(path);
    if (metadata.value("edition").toString() != edition || metadata.value("word_language").toString() != word_language
        || !QFile::remove(path))
        return false;
    refresh_packs();
    m_message = tr("Downloaded Kaikki edition removed. Saved word cards were kept.");
    emit changed();
    emit data_changed();
    return true;
}

KaikkiOfflineStore::ImportResult KaikkiOfflineStore::import_pack(const std::shared_ptr<Job> &job, const QString &destination) {
    const auto temporary_db = job->temporary->filePath("index.sqlite");
    QFile compressed_file(job->temporary->filePath("data.jsonl.gz"));
    if (!compressed_file.open(QIODevice::ReadOnly) || compressed_file.read(2) != QByteArray::fromHex("1f8b"))
        return {tr("The download is not a gzip dictionary file.")};
    compressed_file.close();
    Database db;
    if (!db.open(temporary_db, false))
        return {tr("Cannot create the local dictionary index.")};
    QFile::setPermissions(temporary_db, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!db.exec("PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF; PRAGMA temp_store=FILE; PRAGMA cache_size=-8192;"
                 "CREATE TABLE records(language TEXT, word TEXT, folded TEXT, payload BLOB);"
                 "CREATE INDEX exact_words ON records(language,word); CREATE INDEX folded_words ON records(language,folded);"
                 "CREATE TABLE redirects(title TEXT PRIMARY KEY, folded TEXT, target TEXT); CREATE INDEX folded_redirects ON redirects(folded);"
                 "CREATE TABLE metadata(json TEXT); BEGIN;"))
        return {tr("Could not initialize the local index. Check disk space.")};
    QString error;
    {
        Statement insert;
        if (!insert.prepare(db, "INSERT INTO records VALUES(?,?,?,?)"))
            return {tr("Could not prepare the local dictionary index.")};
        Statement redirect;
        if (!redirect.prepare(db, "INSERT OR REPLACE INTO redirects VALUES(?,?,?)"))
            return {tr("Could not prepare the dictionary redirect index.")};
        const auto gzip_path = QFile::encodeName(job->temporary->filePath("data.jsonl.gz"));
        gzFile gzip = gzopen(gzip_path.constData(), "rb");
        if (!gzip)
            return {tr("Cannot open the compressed dictionary download.")};
        QByteArray pending;
        char buffer[256 * 1024];
        qint64 decoded = 0;
        QHash<QString, QString> languages;
        qint64 line_number = 0;
        const auto record = [&](const QByteArray &line) -> bool {
            ++line_number;
            if (line.trimmed().isEmpty())
                return true;
            if (line.size() > max_record_size) {
                error = tr("A dictionary record exceeded the safety limit.");
                return false;
            }
            QJsonParseError parse_error;
            auto json = line;
            if (line_number == 1 && json.startsWith(QByteArray::fromHex("efbbbf")))
                json.remove(0, 3);
            const auto document = QJsonDocument::fromJson(json, &parse_error);
            const auto object = document.object();
            if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
                error = tr("Invalid JSONL at line %1, byte %2: %3. Previously installed data was kept.")
                    .arg(line_number).arg(parse_error.offset).arg(document.isObject() ? parse_error.errorString()
                        : parse_error.error == QJsonParseError::NoError ? tr("Expected a JSON object.") : parse_error.errorString());
                return false;
            }
            if (object.value("pos").toString() == "hard-redirect" && !object.value("title").toString().isEmpty()
                && !object.value("redirect").toString().isEmpty()) {
                const auto title = object.value("title").toString();
                redirect.text(1, title);
                redirect.text(2, title.toCaseFolded());
                redirect.text(3, object.value("redirect").toString());
                if (!redirect.query.exec()) {
                    error = tr("Could not index dictionary redirects. Check disk space.");
                    return false;
                }
                ++job->redirects;
                return true;
            }
            const auto word = object.value("word").toString();
            const auto code = object.value("lang_code").toString();
            if (word.isEmpty() || code.isEmpty()) {
                ++job->skipped;
                return true;
            }
            if (!languages.contains(code))
                languages.insert(code, dictionary_language_code(code));
            if (languages.value(code).isEmpty() || (!job->word_language.isEmpty() && languages.value(code) != job->word_language))
                return true;
            insert.text(1, languages.value(code));
            insert.text(2, word);
            insert.text(3, word.toCaseFolded());
            const auto compressed = qCompress(json, 1);
            insert.query.bindValue(3, compressed);
            if (!insert.query.exec()) {
                error = tr("Could not build the dictionary index. Check disk space.");
                return false;
            }
            ++job->records;
            return true;
        };
        while (!job->cancelled && error.isEmpty()) {
            const int count = gzread(gzip, buffer, sizeof(buffer));
            if (count <= 0) {
                int code = Z_OK;
                gzerror(gzip, &code);
                if (count < 0 || (code != Z_OK && code != Z_STREAM_END))
                    error = tr("The compressed download is damaged or incomplete.");
                break;
            }
            decoded += count;
            if (decoded > 128LL * 1024 * 1024 * 1024) {
                error = tr("The expanded dictionary exceeded its safety limit.");
                break;
            }
            job->compressed_read = gzoffset(gzip);
            pending.append(buffer, count);
            qsizetype start = 0;
            qsizetype end;
            while (!job->cancelled && (end = pending.indexOf('\n', start)) >= 0) {
                if (!record(pending.mid(start, end - start)))
                    break;
                start = end + 1;
            }
            pending.remove(0, start);
            if (pending.size() > max_record_size)
                error = tr("A dictionary record exceeded the safety limit.");
        }
        if (!job->cancelled && error.isEmpty() && !pending.trimmed().isEmpty())
            record(pending);
        if (gzclose(gzip) != Z_OK && error.isEmpty())
            error = tr("The compressed download is damaged or incomplete.");
    }
    if (job->cancelled)
        return {{}, true};
    if (!error.isEmpty())
        return {error};
    if (job->records == 0)
        return {tr("The download contains no entries for %1 in this definition-language edition. Previously installed data was kept.")
            .arg(language_name(job->word_language))};
    job->building_index = true;
    if (!db.exec("COMMIT;"))
        return job->cancelled ? ImportResult{{}, true} : ImportResult{tr("Could not finish the local index. Check disk space.")};
    {
        Statement metadata;
        const QJsonObject data{{"format", 2}, {"edition", job->edition}, {"word_language", job->word_language},
            {"records", job->records.load()}, {"redirects", job->redirects.load()}, {"unsupported_records", job->skipped.load()},
            {"downloaded", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"revision", QUuid::createUuid().toString()},
            {"source_url", job->url.toString()}};
        if (!metadata.prepare(db, "INSERT INTO metadata VALUES(?)"))
            return {tr("Could not save dictionary metadata.")};
        metadata.text(1, QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
        if (!metadata.query.exec())
            return {tr("Could not save dictionary metadata.")};
    }
    db.close();
    QFile input(temporary_db);
    QSaveFile output(destination);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
        return {tr("Could not install the dictionary. Previously installed data was kept.")};
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    while (!input.atEnd()) {
        if (job->cancelled)
            return {{}, true};
        const auto bytes = input.read(256 * 1024);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size())
            return {tr("Could not install the dictionary. Check disk space. Previously installed data was kept.")};
    }
    if (job->cancelled)
        return {{}, true};
    if (!output.commit())
        return {tr("Could not install the dictionary. Previously installed data was kept.")};
    return {};
}

std::optional<KaikkiOfflineStore::Lookup> KaikkiOfflineStore::lookup(const QString &edition, const QString &requested_language,
    const QString &word, const LookupCancellation &cancellation) const {
    const auto language = dictionary_language_code(requested_language);
    if (language.isEmpty())
        return {};
    QString scope;
    bool installed = false;
    for (const auto &value : m_packs) {
        const auto pack = value.toMap();
        if (pack.value("edition").toString() != edition)
            continue;
        const auto word_language = pack.value("word_language").toString();
        if (word_language == language) {
            installed = true;
            scope = language;
            break;
        }
        if (word_language.isEmpty())
            installed = true;
    }
    if (!installed)
        return {};
    const auto path = pack_path(edition, scope);
    return [path, edition, language, word, cancellation]() { return read_word(path, edition, language, word, cancellation); };
}

DictionaryResponse KaikkiOfflineStore::read_word(const QString &path, const QString &edition, const QString &language,
    const QString &word, const LookupCancellation &cancellation) {
    const auto cancelled = [&]() { return cancellation && cancellation->load(); };
    if (cancelled()) return {{}, {}, tr("Lookup cancelled.")};
    Database db;
    if (!db.open(path, true))
        return {{}, {}, tr("Cannot read the downloaded dictionary. Download it again.")};
    QString resolved = word;
    QSet<QString> visited;
    for (int hop = 0; hop < 5; ++hop) {
        if (cancelled()) return {{}, {}, tr("Lookup cancelled.")};
        if (visited.contains(resolved))
            return {{}, {}, tr("The downloaded dictionary contains a redirect cycle."), true};
        visited.insert(resolved);
        QByteArray records;
        for (bool folded : {false, true}) {
            Statement statement;
            if (!statement.prepare(db, folded
                    ? "SELECT payload FROM records WHERE language=? AND folded=? LIMIT 1001"
                    : "SELECT payload FROM records WHERE language=? AND word=? LIMIT 1001"))
                return {{}, {}, tr("The downloaded dictionary index is invalid. Download it again.")};
            statement.text(1, language);
            statement.text(2, folded ? resolved.toCaseFolded() : resolved);
            if (!statement.query.exec())
                return {{}, {}, tr("Could not read the downloaded dictionary index.")};
            int count = 0;
            while (statement.query.next()) {
                if (cancelled()) return {{}, {}, tr("Lookup cancelled.")};
                const auto payload = statement.query.value(0).toByteArray();
                const auto *bytes = reinterpret_cast<const uchar *>(payload.constData());
                const int size = payload.size();
                if (++count > 1000 || size < 4 || size > max_record_size + 1024 * 1024
                    || qFromBigEndian<quint32>(bytes) > max_record_size)
                    return {{}, {}, tr("Downloaded dictionary entry exceeded its safety limit.")};
                const auto line = qUncompress(bytes, size);
                if (line.isEmpty() || records.size() + line.size() + 1 > max_record_size)
                    return {{}, {}, tr("Downloaded dictionary entry is damaged or too large.")};
                records += line + '\n';
            }
            if (statement.query.lastError().isValid())
                return {{}, {}, tr("Could not read the downloaded dictionary index.")};
            if (!records.isEmpty())
                break;
        }
        if (records.isEmpty()) {
            QString target;
            for (bool folded : {false, true}) {
                Statement redirect;
                // Version-1 indexes predate redirect support.
                if (!redirect.prepare(db, folded ? "SELECT target FROM redirects WHERE folded=? LIMIT 1"
                        : "SELECT target FROM redirects WHERE title=? LIMIT 1"))
                    break;
                redirect.text(1, folded ? resolved.toCaseFolded() : resolved);
                if (redirect.query.exec() && redirect.query.next()) {
                    target = redirect.query.value(0).toString();
                    break;
                }
            }
            if (target.isEmpty())
                return {{}, {}, tr("No entry in the downloaded dictionary."), true};
            const auto fragment = target.indexOf('#');
            resolved = fragment > 0 ? target.left(fragment) : target;
            continue;
        }
        QString error;
        auto document = kaikki_dictionary_document(records, resolved, language, edition, error);
        document.insert("word", word);
        auto entries = document.value("entries").toArray();
        for (qsizetype i = 0; i < entries.size(); ++i) {
            auto entry = entries[i].toObject();
            auto source = entry.value("source").toObject();
            source.insert("offline", true);
            entry.insert("source", source);
            entries[i] = entry;
        }
        document.insert("entries", entries);
        return {document, {}, error};
    }
    return {{}, {}, tr("The downloaded dictionary redirect chain is too long."), true};
}

}  // namespace showthis
