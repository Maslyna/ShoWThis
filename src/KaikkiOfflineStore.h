#pragma once

#include "DictionaryProvider.h"

#include <QFile>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <atomic>
#include <functional>

namespace showthis {

class KaikkiOfflineStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList editions READ editions CONSTANT)
    Q_PROPERTY(QVariantList packs READ packs NOTIFY changed)
    Q_PROPERTY(QVariantMap download_info READ download_info NOTIFY changed)
    Q_PROPERTY(QString mode READ mode WRITE set_mode NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString storage_path READ storage_path CONSTANT)
public:
    using Lookup = std::function<DictionaryResponse()>;
    explicit KaikkiOfflineStore(QNetworkAccessManager *network, const QString &directory = {}, QObject *parent = nullptr);
    ~KaikkiOfflineStore() override;
    QVariantList editions() const;
    QVariantList packs() const { return m_packs; }
    QVariantMap download_info() const { return m_offer; }
    QString mode() const { return m_mode; }
    void set_mode(const QString &mode);
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString message() const { return m_message; }
    QString storage_path() const { return m_directory; }
    QString cache_token() const;
    Q_INVOKABLE QString normalize_word_language(const QString &language) const;
    std::optional<Lookup> lookup(const QString &edition, const QString &word_language, const QString &word) const;
    Q_INVOKABLE void inspect(const QString &edition, const QString &word_language = {});
    Q_INVOKABLE void download(const QString &edition, const QString &word_language = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool remove(const QString &edition, const QString &word_language = {});
    static QUrl download_url(const QString &edition);

signals:
    void changed();
    void data_changed();

private:
    struct Job {
        std::shared_ptr<QTemporaryDir> temporary;
        QString edition;
        QString word_language;
        QUrl url;
        std::atomic_bool cancelled{false};
        std::atomic_bool building_index{false};
        std::atomic<qint64> records{0};
        std::atomic<qint64> redirects{0};
        std::atomic<qint64> skipped{0};
        std::atomic<qint64> compressed_read{0};
    };
    struct ImportResult { QString error; bool cancelled = false; };
    bool valid_edition(const QString &edition) const;
    bool valid_word_language(const QString &language) const;
    QString pack_path(const QString &edition, const QString &word_language = {}) const;
    void refresh_packs();
    void finish(const QString &message);
    void drain_download();
    void start_indexing();
    static ImportResult import_pack(const std::shared_ptr<Job> &job, const QString &destination);
    static DictionaryResponse read_word(const QString &path, const QString &edition, const QString &language, const QString &word);

    QNetworkAccessManager *m_network;
    QString m_directory;
    QSettings m_settings;
    QString m_mode;
    QVariantList m_packs;
    QVariantMap m_offer;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QFile> m_file;
    std::shared_ptr<Job> m_job;
    QFutureWatcher<ImportResult> *m_watcher;
    QTimer m_timer;
    qint64 m_received = 0;
    qint64 m_total = -1;
    bool m_busy = false;
    double m_progress = -1;
    QString m_message;
    QString m_download_error;
};

}  // namespace showthis
