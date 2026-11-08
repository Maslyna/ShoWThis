#include "LookupCacheStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QtConcurrentRun>

namespace showthis {

LookupCacheStore::LookupCacheStore(const QString &path, QObject *parent) : QObject(parent), m_path(path) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(200);
    connect(&m_debounce, &QTimer::timeout, this, &LookupCacheStore::start_write);
    connect(&m_writer, &QFutureWatcher<QString>::finished, this, &LookupCacheStore::finished);
}

LookupCacheStore::~LookupCacheStore() { flush(); }

NotebookContents LookupCacheStore::load(const NotebookContents &legacy) {
    QFile file(m_path);
    if (QFileInfo(m_path).isSymLink()) {
        m_blocked = true;
        m_error = tr("Refusing to write a lookup cache through a symbolic link: %1").arg(m_path);
        return {};
    }
    if (!file.exists()) return legacy;
    QJsonParseError error;
    const auto opened = file.size() <= 32 * 1024 * 1024 && file.open(QIODevice::ReadOnly);
    const auto document = opened ? QJsonDocument::fromJson(file.readAll(), &error) : QJsonDocument{};
    const auto root = document.object();
    if (!opened || error.error != QJsonParseError::NoError || root.value("version").toInt() != 1
        || root.value("format").toString() != "showthis-lookup-cache"
        || !root.value("dictionary_cache").isObject() || !root.value("dictionary_history").isArray()) {
        m_blocked = true;
        m_error = tr("Cannot read lookup cache: %1. Saved vocabulary is unaffected. Clear history to reset the cache.").arg(m_path);
        return {};
    }
    NotebookContents result;
    result.cache = root.value("dictionary_cache").toObject();
    result.history = root.value("dictionary_history").toArray().toVariantList();
    return result;
}

void LookupCacheStore::trim(QVariantList &history, QJsonObject &cache, int limit, QHash<QString, qint64> &sizes,
    qint64 budget, qint64 entry_limit) {
    QVariantList retained;
    QJsonObject documents;
    QHash<QString, qint64> retained_sizes;
    qint64 used = 1024;
    for (const auto &value : history) {
        if (retained.size() >= qMax(0, limit)) break;
        const auto entry = value.toMap();
        const auto key = entry.value("key").toString();
        if (key.isEmpty() || documents.contains(key) || !cache.value(key).isObject()) continue;
        const auto size = sizes.contains(key) ? sizes.value(key)
            : QJsonDocument(cache.value(key).toObject()).toJson(QJsonDocument::Compact).size();
        const auto metadata_size = QJsonDocument(QJsonObject::fromVariantMap(entry)).toJson(QJsonDocument::Compact).size() + key.size() * 6 + 64;
        if (size > entry_limit || size + metadata_size > budget - used) continue;
        retained.append(value);
        documents.insert(key, cache.value(key));
        retained_sizes.insert(key, size);
        used += size + metadata_size;
    }
    history = retained;
    cache = documents;
    sizes = retained_sizes;
}

bool LookupCacheStore::submit(const QJsonObject &cache, const QVariantList &history, bool synchronous) {
    if (m_blocked) return false;
    m_snapshot = {{"version", 1}, {"format", "showthis-lookup-cache"}, {"dictionary_cache", cache},
        {"dictionary_history", QJsonArray::fromVariantList(history)}};
    ++m_revision;
    m_dirty = true;
    m_debounce.start();
    return !synchronous || flush();
}

void LookupCacheStore::start_write() {
    if (!m_dirty || m_writer.isRunning() || m_blocked) return;
    if (m_pending_result) finished();
    if (!m_dirty) return;
    const auto snapshot = m_snapshot;
    const auto path = m_path;
    m_running_revision = m_revision;
    m_pending_result = true;
    m_writer.setFuture(QtConcurrent::run([snapshot, path]() -> QString {
        const auto bytes = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
        QSaveFile file(path);
        if (QFileInfo(path).isSymLink() || bytes.size() > 32 * 1024 * 1024 || !QDir().mkpath(QFileInfo(path).absolutePath())
            || !file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            || file.write(bytes) != bytes.size() || !file.commit())
            return QObject::tr("Cannot save lookup cache: %1. Saved vocabulary is unaffected.").arg(path);
        return {};
    }));
}

void LookupCacheStore::finished() {
    if (!m_pending_result || m_writer.isRunning()) return;
    m_pending_result = false;
    const auto previous_error = m_error;
    m_error = m_writer.result();
    if (m_error != previous_error) emit error_changed();
    if (!m_error.isEmpty()) {
        emit error_occurred(m_error);
        return;
    }
    if (m_running_revision == m_revision) m_dirty = false;
    else m_debounce.start();
}

bool LookupCacheStore::flush() {
    m_debounce.stop();
    if (m_blocked) return false;
    if (m_writer.isRunning()) m_writer.waitForFinished();
    if (m_pending_result) finished();
    if (m_dirty) {
        start_write();
        m_writer.waitForFinished();
        finished();
    }
    m_debounce.stop();
    return m_error.isEmpty();
}

}  // namespace showthis
