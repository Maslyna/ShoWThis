#include <QtTest>
#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QStandardPaths>

#include "DictionaryFixtures.h"
#include "LearningService.h"
#include "LookupCacheStore.h"
#include "NotebookListModel.h"
#include "OfflineDictionaryFixtures.h"
#include "TestNetwork.h"
#include "CardForms.h"

class CancellableProvider : public SampleDictionaryProvider {
public:
    QSemaphore *started;
    QSemaphore *stopped;
    CancellableProvider(QSemaphore *start, QSemaphore *stop) : started(start), stopped(stop) {}
    showthis::DictionaryAccess access() const override { return showthis::DictionaryAccess::LocalOnly; }
    std::optional<showthis::LocalDictionaryLookup> local_lookup(const QString &, const QString &, const QString &,
        const showthis::LookupCancellation &cancel) const override {
        return [started = started, stopped = stopped, cancel]() -> showthis::DictionaryResponse {
            started->release();
            QElapsedTimer timer;
            timer.start();
            while (!cancel->load() && timer.elapsed() < 5000) QThread::msleep(1);
            stopped->release();
            return {};
        };
    }
};

class NotebookArchitectureTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
    static QByteArray read(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
    static bool install(showthis::KaikkiOfflineStore *store, const QString &edition, const QString &language = "de") {
        store->inspect(edition, language);
        if (!QTest::qWaitFor([&]() { return !store->busy(); })) return false;
        store->download(edition, language);
        return QTest::qWaitFor([&]() { return !store->busy(); }) && !store->packs().isEmpty();
    }
private slots:
    void initTestCase() {
        QVERIFY(m_settings.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void init() { QSettings{}.clear(); }

    void cache_is_bounded_and_coalesces_writes() {
        QJsonObject cache;
        QVariantList history;
        for (const auto *key : {"a", "b", "c"}) {
            cache.insert(key, QJsonObject{{"text", QString(100, 'x')}});
            history.append(QVariantMap{{"key", key}});
        }
        QHash<QString, qint64> sizes;
        showthis::LookupCacheStore::trim(history, cache, 10, sizes, 1500, 200);
        QCOMPARE(cache.size(), 2);
        QVERIFY(cache.contains("a") && cache.contains("b"));
        QTemporaryDir directory;
        const auto path = directory.filePath("cache.json");
        showthis::LookupCacheStore store(path);
        QVERIFY(store.submit(cache, history));
        QVERIFY(store.submit({}, {}));
        QVERIFY(store.flush());
        const auto saved = QJsonDocument::fromJson(read(path)).object();
        QVERIFY(saved.value("dictionary_cache").toObject().isEmpty());
        QVERIFY(!(QFile::permissions(path) & (QFileDevice::ReadOther | QFileDevice::ReadGroup)));
        cache = {{"large", QJsonObject{{"text", QString(300, 'x')}}}};
        history = {QVariantMap{{"key", "large"}}};
        showthis::LookupCacheStore::trim(history, cache, 10, sizes, 1500, 200);
        QVERIFY(cache.isEmpty() && history.isEmpty());
    }

    void lookups_do_not_rewrite_vocabulary() {
        QTemporaryDir directory;
        TestNetwork network;
        network.response = free_entry("Haus", "house");
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.set_source_language("de");
        QVERIFY(service.save_word({}, "saved", "", "meaning", "", "", -1));
        const auto notebook = read(service.storage_path());
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        QVERIFY(service.flush_cache());
        QCOMPARE(read(service.storage_path()), notebook);
        QVERIFY(QJsonDocument::fromJson(read(service.cache_path())).object().value("dictionary_cache").toObject().size() == 1);
        const auto saved = service.vocabulary()[0].toMap();
        QVERIFY(QFile::remove(service.storage_path()));
        QVERIFY(service.save_word(saved.value("id").toString(), "saved", "", "meaning", "", "", -1));
        QVERIFY(QFile::exists(service.storage_path()));
    }

    void cache_corruption_does_not_block_vocabulary() {
        QTemporaryDir directory;
        const auto path = directory.filePath("words.json");
        QFile broken(path + ".lookups.json");
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("unrecognized cache");
        broken.close();
        showthis::LearningService service(nullptr, path);
        QVERIFY(!service.storage_error().isEmpty());
        QVERIFY(service.save_word({}, "word", "", "meaning", "", "", -1));
        QCOMPARE(read(broken.fileName()), QByteArray("unrecognized cache"));
        QVERIFY(service.clear_history());
        QCOMPARE(service.vocabulary().size(), 1);
    }

    void expiry_refresh_and_stale_fallback() {
        QTemporaryDir directory;
        const auto path = directory.filePath("words.json");
        TestNetwork network;
        network.response = free_entry("Haus", "old meaning");
        showthis::LearningService service(nullptr, path, &network);
        service.set_source_language("de");
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(network.requests.size(), 1);
        service.lookup("Haus");
        QCOMPARE(network.requests.size(), 1);
        network.response = free_entry("Haus", "fresh meaning");
        service.lookup("Haus", "", true);
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(service.senses()[0].toMap().value("definition").toString(), QString("fresh meaning"));
        network.status = 503;
        service.lookup("Haus", "", true);
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(service.senses()[0].toMap().value("definition").toString(), QString("fresh meaning"));
        QVERIFY(service.message().contains("Refresh failed"));
        QVERIFY(service.flush_cache());
        auto snapshot = QJsonDocument::fromJson(read(service.cache_path())).object();
        auto cache = snapshot.value("dictionary_cache").toObject();
        const auto key = cache.keys()[0];
        auto document = cache.value(key).toObject();
        document.insert("_cached_at", "2000-01-01T00:00:00Z");
        cache.insert(key, document);
        snapshot.insert("dictionary_cache", cache);
        QFile file(service.cache_path());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(snapshot).toJson());
        file.close();
        network.status = 200;
        showthis::LearningService reopened(nullptr, path, &network);
        reopened.lookup("Haus");
        QTRY_VERIFY(!reopened.dictionary_busy());
        QCOMPARE(network.requests.size(), 4);
    }

    void provider_revisions_are_scoped_and_history_is_offline() {
        QTemporaryDir directory;
        TestNetwork network;
        network.response = free_entry("Haus", "online meaning");
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.set_source_language("de");
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        const auto gzip = gzip_bytes(kaikki_entry("Haus", "local meaning"));
        network.responder = [gzip](const QNetworkRequest &) -> TestNetwork::Response {
            return {gzip, 200, QNetworkReply::NoError, gzip.size()};
        };
        QVERIFY(install(service.offline_dictionaries(), "ru"));
        const auto count = network.requests.size();
        service.lookup("Haus");
        QCOMPARE(network.requests.size(), count); // Kaikki data does not invalidate FreeDictionary.
        QVERIFY(service.configure_dictionaries({dictionary_selection("kaikki", {"ru"})}, true));
        service.offline_dictionaries()->set_mode("local_only");
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        const auto old_key = service.dictionary_history()[0].toMap().value("key").toString();
        QVERIFY(install(service.offline_dictionaries(), "de"));
        const auto scoped_revision = service.offline_dictionaries()->cache_token("de", "ru");
        QVERIFY(install(service.offline_dictionaries(), "ru", {}));
        QCOMPARE(service.offline_dictionaries()->cache_token("de", "ru"), scoped_revision);
        const auto requests = network.requests.size();
        service.lookup("Haus");
        QCOMPARE(network.requests.size(), requests);
        QVERIFY(service.message().contains("cache"));
        QVERIFY(service.offline_dictionaries()->remove("ru", "de"));
        QVERIFY(service.lookup_history(old_key));
        QCOMPARE(network.requests.size(), requests);
        QCOMPARE(service.senses()[0].toMap().value("definition").toString(), QString("local meaning"));
    }

    void draft_configuration_does_not_change_defaults() {
        QTemporaryDir directory;
        showthis::LearningService service(nullptr, directory.filePath("words.json"));
        service.set_source_language("de");
        service.set_target_language("ru");
        service.prepare_draft({{"language", "fr"}, {"target_language", "en-us"},
            {"dictionary_selection", QVariantList{dictionary_selection("kaikki", {"fr"})}}}, false);
        QCOMPARE(service.source_language(), QString("fr"));
        service.set_source_language("es");
        QCOMPARE(QSettings{}.value("learning/source_language").toString(), QString("de"));
        service.prepare_draft({}, true);
        QCOMPARE(service.source_language(), QString("de"));
        QCOMPARE(service.target_language(), QString("ru"));
        QCOMPARE(service.dictionary_selection()[0].toMap().value("provider").toString(), QString("freedictionary"));
        QSettings{}.clear();
        showthis::LearningService system_defaults(nullptr, directory.filePath("default.json"));
        const auto initial_target = system_defaults.target_language();
        system_defaults.prepare_draft({{"language", "fr"}, {"target_language", "ru"}}, false);
        system_defaults.prepare_draft({}, true);
        QCOMPARE(system_defaults.target_language(), initial_target);
        QCOMPARE(system_defaults.source_language(), QString("en"));
    }

    void large_collection_has_typed_searchable_model() {
        showthis::NotebookListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVariantList entries;
        for (int i = 0; i < 10000; ++i)
            entries.append(QVariantMap{{"id", QString::number(i)}, {"word", QString("word%1").arg(i)}, {"notes", "notes"}});
        model.replace(entries);
        QCOMPARE(model.rowCount(), 10000);
        model.set_filter("WORD9999");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), showthis::NotebookListModel::IdRole).toString(), QString("9999"));
        QCOMPARE(model.visible_ids(), QStringList{"9999"});
        QVERIFY(model.contains("0")); // Selection survives filtering.
    }

    void local_job_cancellation_reaches_worker() {
        TestNetwork network;
        QSemaphore started, stopped;
        showthis::DictionaryProviders providers;
        providers.push_back(std::make_unique<CancellableProvider>(&started, &stopped));
        showthis::DictionaryLookup lookup(&network, nullptr, std::move(providers));
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("word", "en", {{{"sample", "en"}}});
        QTRY_VERIFY(started.available() > 0);
        lookup.cancel();
        QTRY_VERIFY(stopped.available() > 0);
        QVERIFY(finished.isEmpty());
        QVERIFY(!lookup.busy());
    }
    void typed_forms_validate_and_round_trip() {
        QString error;
        const QVariantMap input{{"number_relevant", true}, {"plural_forms", QStringList{" forms ", "forms"}},
            {"plural_status", "unknown"}, {"singular_status", "unavailable"}, {"base_form", " word "},
            {"base_article", "the"}, {"base_article_origin", "manual"}};
        const auto forms = showthis::CardForms::parse(input, error);
        QVERIFY(forms);
        QCOMPARE(forms->plural.status, showthis::NumberStatus::Available);
        QCOMPARE(forms->singular.status, showthis::NumberStatus::Unavailable);
        QCOMPARE(forms->base_article_origin, showthis::ArticleOrigin::Manual);
        QCOMPARE(forms->fields("").value("plural_forms").toStringList(), QStringList{"forms"});
        auto invalid = input;
        invalid["plural_status"] = "invalid";
        QVERIFY(!showthis::CardForms::parse(invalid, error));
        invalid = input;
        invalid["base_article_origin"] = "invalid";
        QVERIFY(!showthis::CardForms::parse(invalid, error));
        invalid = input;
        invalid["base_form"] = "two\nlines";
        QVERIFY(!showthis::CardForms::parse(invalid, error));
        invalid = input;
        invalid["plural_forms"] = QVariantList{42};
        QVERIFY(!showthis::CardForms::parse(invalid, error));
        invalid = input;
        invalid["number_relevant"] = "yes";
        QVERIFY(!showthis::CardForms::parse(invalid, error));
    }
    void sidecar_cannot_overwrite_a_symlink_target() {
        QTemporaryDir directory;
        const auto notebook = directory.filePath("words.json");
        QFile file(notebook);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("valuable user data");
        file.close();
        const auto link = directory.filePath("cache.json");
        QVERIFY(QFile::link(notebook, link));
        showthis::LookupCacheStore store(link);
        store.load({});
        QVERIFY(!store.submit({}, {}, true));
        store.allow_reset();
        QVERIFY(!store.submit({}, {}, true));
        QCOMPARE(read(notebook), QByteArray("valuable user data"));
    }
    void file_size_limits_preserve_existing_data() {
        QTemporaryDir directory;
        const auto path = directory.filePath("large.json");
        QFile large(path);
        QVERIFY(large.open(QIODevice::WriteOnly));
        QVERIFY(large.resize(64LL * 1024 * 1024 + 1));
        large.close();
        showthis::LearningStore store(path);
        QVERIFY(store.load(1000).cards.isEmpty());
        QVERIFY(!store.readable());
        QVERIFY(!store.save({}, {}, {}));
        QCOMPARE(QFileInfo(path).size(), 64LL * 1024 * 1024 + 1);

        showthis::LearningService service(nullptr, directory.filePath("words.json"));
        QVERIFY(service.save_word({}, "word", "", "meaning", "", "original", -1));
        const auto bytes = read(service.storage_path());
        QVERIFY(!service.save_word({}, "large", "", "meaning", "", QString(17 * 1024 * 1024, 'x'), -1));
        QCOMPARE(read(service.storage_path()), bytes);
        QCOMPARE(service.vocabulary().size(), 1);
    }
};

QTEST_GUILESS_MAIN(NotebookArchitectureTest)
#include "NotebookArchitectureTest.moc"
