#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include "KaikkiOfflineStore.h"
#include "LearningService.h"
#include "NotebookData.h"
#include "OfflineDictionaryFixtures.h"
#include "TestNetwork.h"

class KaikkiOfflineStoreTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;

    static void serve_download(TestNetwork &network, const QByteArray &data = offline_dictionary_data()) {
        const auto gzip = gzip_bytes(data);
        network.responder = [gzip](const QNetworkRequest &) -> TestNetwork::Response {
            return {gzip, 200, QNetworkReply::NoError, gzip.size()};
        };
    }
    static bool install(showthis::KaikkiOfflineStore &store, const QString &edition = "ru", const QString &word_language = {}) {
        store.inspect(edition, word_language);
        if (!QTest::qWaitFor([&]() { return !store.busy(); }, 5000) || store.download_info().isEmpty())
            return false;
        store.download(edition, word_language);
        return QTest::qWaitFor([&]() { return !store.busy(); }, 5000) && !store.packs().isEmpty();
    }

private slots:
    void initTestCase() {
        QVERIFY(m_settings.isValid());
        QVERIFY(QSqlDatabase::isDriverAvailable("QSQLITE"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void init() { QSettings{}.clear(); }

    void download_install_query_and_reopen() {
        QTemporaryDir directory;
        TestNetwork network;
        serve_download(network);
        showthis::KaikkiOfflineStore store(&network, directory.path());
        QVERIFY(store.packs().isEmpty());
        QVERIFY(install(store));
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(network.requests[0].url(), showthis::KaikkiOfflineStore::download_url("ru"));
        QCOMPARE(store.packs()[0].toMap().value("records").toInt(), 4);
        auto job = store.lookup("ru", "de", "Haus");
        QVERIFY(job);
        const auto response = (*job)();
        QVERIFY(response.error.isEmpty());
        const auto senses = showthis::dictionary_senses(response.document, "de");
        QCOMPARE(senses.size(), 1);
        QCOMPARE(senses[0].toMap().value("definition").toString(), QString::fromUtf8("дом"));
        QCOMPARE(senses[0].toMap().value("article").toString(), QString("das"));
        QVERIFY(senses[0].toMap().value("source").toMap().value("offline").toBool());
        const auto folded = (*store.lookup("ru", "de", "haus"))();
        QCOMPARE(showthis::dictionary_senses(folded.document, "de").size(), 1);
        const auto english = (*store.lookup("ru", "en", "house"))();
        QCOMPARE(showthis::dictionary_senses(english.document, "en").size(), 1);
        QVERIFY((*store.lookup("ru", "de", "missing"))().missing);
        QVERIFY(!store.lookup("de", "de", "Haus"));
        showthis::KaikkiOfflineStore reopened(&network, directory.path());
        QCOMPARE(reopened.packs().size(), 1);
        QCOMPARE(reopened.cache_token(), store.cache_token());
        QVERIFY(QDir(directory.path()).entryList({".download-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    }

    void offline_fallback_follows_forms_and_keeps_attribution() {
        QTemporaryDir directory;
        TestNetwork network;
        serve_download(network);
        showthis::KaikkiOfflineStore store(&network, directory.path());
        QVERIFY(install(store));
        network.requests.clear();
        network.responder = [](const QNetworkRequest &) -> TestNetwork::Response {
            return {{}, 0, QNetworkReply::HostNotFoundError};
        };
        showthis::DictionaryLookup lookup(&network, nullptr, showthis::default_dictionary_providers(&store));
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup(QString::fromUtf8("Häuser"), "de", {{{"kaikki", "ru"}}});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(network.requests.size(), 1); // Base form does not retry a known-unreachable server.
        const auto senses = showthis::dictionary_senses(finished[0][0].value<QJsonObject>(), "de");
        QCOMPARE(senses.size(), 2);
        QCOMPARE(senses[1].toMap().value("headword").toString(), QString("Haus"));
        QVERIFY(senses[1].toMap().value("source").toMap().value("offline").toBool());
        QVERIFY(finished[0][1].toString().contains("downloaded"));
        QVERIFY(finished[0][2].toBool());
    }

    void local_only_never_contacts_network_even_when_missing() {
        QTemporaryDir directory;
        TestNetwork network;
        serve_download(network);
        showthis::KaikkiOfflineStore store(&network, directory.path());
        QVERIFY(install(store));
        network.requests.clear();
        store.set_mode("local_only");
        showthis::DictionaryLookup lookup(&network, nullptr, showthis::default_dictionary_providers(&store));
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("Haus", "de", {{{"kaikki", "ru"}}});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(network.requests.isEmpty());
        finished.clear();
        lookup.lookup("missing", "de", {{{"kaikki", "ru"}}});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(network.requests.isEmpty());
        QVERIFY(finished[0][1].toString().contains("downloaded dictionary"));
        finished.clear();
        lookup.lookup("Haus", "de", {{{"kaikki", "de"}}});
        QCOMPARE(finished.size(), 1);
        QVERIFY(finished[0][1].toString().contains("Download it in Settings"));
        QVERIFY(network.requests.isEmpty());
    }

    void prefer_local_and_online_only() {
        QTemporaryDir directory;
        TestNetwork network;
        serve_download(network);
        showthis::KaikkiOfflineStore store(&network, directory.path());
        QVERIFY(install(store));
        store.set_mode("prefer_local");
        network.requests.clear();
        network.responder = [](const QNetworkRequest &request) -> TestNetwork::Response {
            if (request.url().path().endsWith(".json")) return {kaikki_index("Other", "ru")};
            return {kaikki_entry("Other", "online meaning")};
        };
        showthis::DictionaryLookup lookup(&network, nullptr, showthis::default_dictionary_providers(&store));
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("Haus", "de", {{{"kaikki", "ru"}}});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(network.requests.isEmpty());
        finished.clear();
        lookup.lookup("Other", "de", {{{"kaikki", "ru"}}});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(network.requests.size(), 2);
        store.set_mode("online_only");
        finished.clear();
        network.responder = [](const QNetworkRequest &) -> TestNetwork::Response { return {{}, 503}; };
        lookup.lookup("Haus", "de", {{{"kaikki", "ru"}}});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(showthis::dictionary_senses(finished[0][0].value<QJsonObject>(), "de").isEmpty());
        QCOMPARE(network.requests.size(), 3);
    }

    void failed_updates_keep_the_old_pack_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::newRow("bad-gzip") << QByteArray("not gzip") << 200;
        QTest::newRow("bad-json") << gzip_bytes("not JSON\n") << 200;
        QTest::newRow("empty") << gzip_bytes("") << 200;
        auto truncated = gzip_bytes(offline_dictionary_data());
        truncated.chop(8);
        QTest::newRow("truncated") << truncated << 200;
        QTest::newRow("http-failure") << QByteArray{} << 503;
        QTest::newRow("oversized-record") << gzip_bytes(QByteArray(8 * 1024 * 1024 + 1, 'x')) << 200;
    }
    void failed_updates_keep_the_old_pack() {
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QTemporaryDir directory;
        TestNetwork network;
        serve_download(network);
        showthis::KaikkiOfflineStore store(&network, directory.path());
        QVERIFY(install(store));
        const auto token = store.cache_token();
        network.responder = [body, status](const QNetworkRequest &) -> TestNetwork::Response { return {body, status}; };
        // The inspected offer remains valid; a refresh that later fails must
        // never delete the already installed database.
        store.download("ru");
        QTRY_VERIFY(!store.busy());
        QCOMPARE(store.cache_token(), token);
        QCOMPARE(store.packs().size(), 1);
        QVERIFY(!store.message().contains("dictionary installed"));
        QVERIFY((*store.lookup("ru", "de", "Haus"))().error.isEmpty());
    }

    void cancel_download_and_indexing() {
        QTemporaryDir directory;
        TestNetwork network;
        serve_download(network);
        showthis::KaikkiOfflineStore store(&network, directory.path());
        QVERIFY(install(store));
        const auto token = store.cache_token();
        network.automatic = false;
        store.download("ru");
        QVERIFY(store.busy());
        store.cancel();
        QVERIFY(!store.busy());
        QVERIFY(network.last_reply->isFinished());
        QCOMPARE(store.cache_token(), token);
        network.automatic = true;
        bool cancelled = false;
        connect(&store, &showthis::KaikkiOfflineStore::changed, &store, [&]() {
            if (!cancelled && store.message().startsWith("Indexing")) {
                cancelled = true;
                store.cancel();
            }
        });
        store.download("ru");
        QTRY_VERIFY(!store.busy());
        QVERIFY(cancelled);
        QCOMPARE(store.cache_token(), token);
        QVERIFY(store.message().contains("cancelled"));
    }

    void local_lookup_cancellation_and_pack_removal() {
        QTemporaryDir directory;
        TestNetwork network;
        serve_download(network);
        showthis::KaikkiOfflineStore store(&network, directory.path());
        QVERIFY(install(store));
        store.set_mode("local_only");
        showthis::DictionaryLookup lookup(&network, nullptr, showthis::default_dictionary_providers(&store));
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("Haus", "de", {{{"kaikki", "ru"}}});
        QVERIFY(lookup.busy());
        lookup.cancel();
        QTest::qWait(30);
        QVERIFY(finished.isEmpty());
        QVERIFY(!lookup.busy());
        const auto token = store.cache_token();
        QVERIFY(!store.remove("../../words"));
        QVERIFY(store.remove("ru"));
        QVERIFY(store.packs().isEmpty());
        QVERIFY(!store.lookup("ru", "de", "Haus"));
        QVERIFY(store.cache_token() != token);
    }

    void service_reopens_and_invalidates_cache_when_pack_changes() {
        QTemporaryDir directory;
        TestNetwork network;
        serve_download(network);
        const auto path = directory.filePath("words.json");
        showthis::LearningService service(nullptr, path, &network);
        auto *store = service.offline_dictionaries();
        QVERIFY(install(*store));
        store->set_mode("local_only");
        QVERIFY(service.configure_dictionaries({dictionary_selection("kaikki", {"ru"})}, true));
        service.set_source_language("de");
        network.requests.clear();
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(service.senses().size(), 1);
        QVERIFY(service.save_word({}, "Haus", "", QString::fromUtf8("дом"), "", "", 0));
        serve_download(network, kaikki_entry("Haus", "updated meaning"));
        QVERIFY(install(*store));
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(service.senses()[0].toMap().value("definition").toString(), QString("updated meaning"));
        showthis::LearningService reopened(nullptr, path, &network);
        QCOMPARE(reopened.offline_dictionaries()->mode(), QString("local_only"));
        QCOMPARE(reopened.offline_dictionaries()->packs().size(), 1);
        QVERIFY(reopened.offline_dictionaries()->remove("ru"));
        QCOMPARE(reopened.vocabulary().size(), 1);
        reopened.lookup("Haus");
        QVERIFY(reopened.senses().isEmpty());
        QVERIFY(reopened.message().contains("Download it in Settings"));
    }

    void multiple_downloaded_editions_search_in_parallel() {
        QTemporaryDir directory;
        TestNetwork network;
        serve_download(network);
        showthis::KaikkiOfflineStore store(&network, directory.path());
        QVERIFY(install(store, "ru"));
        QVERIFY(install(store, "de"));
        QCOMPARE(store.packs().size(), 2);
        store.set_mode("local_only");
        network.requests.clear();
        showthis::DictionaryLookup lookup(&network, nullptr, showthis::default_dictionary_providers(&store));
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("Haus", "de", {{{"kaikki", "ru"}, {"kaikki", "de"}}});
        QTRY_COMPARE(finished.size(), 1);
        const auto senses = showthis::dictionary_senses(finished[0][0].value<QJsonObject>(), "de");
        QCOMPARE(senses.size(), 2);
        QCOMPARE(senses[0].toMap().value("definition_language").toString(), QString("ru"));
        QCOMPARE(senses[1].toMap().value("definition_language").toString(), QString("de"));
        QVERIFY(network.requests.isEmpty());
    }

    void validation_size_limits_and_unrecognized_files() {
        QTemporaryDir directory;
        TestNetwork network;
        serve_download(network);
        showthis::KaikkiOfflineStore store(&network, directory.path());
        store.download("ru"); // Requires a checked offer and an explicit download action.
        store.inspect("../../secret");
        QVERIFY(network.requests.isEmpty());
        network.responder = [](const QNetworkRequest &) -> TestNetwork::Response {
            return {{}, 200, QNetworkReply::NoError, 33LL * 1024 * 1024 * 1024};
        };
        store.inspect("ru");
        QTRY_VERIFY(!store.busy());
        store.download("ru");
        QVERIFY(!store.busy());
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(store.message().contains("too large"));
        serve_download(network);
        store.inspect("ru");
        QTRY_VERIFY(!store.busy());
        QFile unknown(directory.filePath("ru.sqlite"));
        QVERIFY(unknown.open(QIODevice::WriteOnly));
        unknown.write("user file");
        unknown.close();
        const auto before = network.requests.size();
        store.download("ru");
        QVERIFY(!store.busy());
        QCOMPARE(network.requests.size(), before);
        QVERIFY(store.message().contains("unrecognized"));
        QVERIFY(!store.remove("ru"));
        QVERIFY(unknown.open(QIODevice::ReadOnly));
        QCOMPARE(unknown.readAll(), QByteArray("user file"));
    }

    void real_redirect_records_and_origin_language_filter() {
        QFile fixture(QFINDTESTDATA("fixtures/kaikki-raw-redirects.jsonl"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto raw = fixture.readAll();
        QTemporaryDir directory;
        TestNetwork network;
        serve_download(network, raw);
        showthis::KaikkiOfflineStore store(&network, directory.path());
        QVERIFY(install(store, "ru", "de"));
        QCOMPARE(store.packs().size(), 1);
        const auto pack = store.packs()[0].toMap();
        QCOMPARE(pack.value("word_language").toString(), QString("de"));
        QCOMPARE(pack.value("records").toInt(), 2);
        QCOMPARE(pack.value("redirects").toInt(), 4);
        QCOMPARE(pack.value("unsupported_records").toInt(), 1);
        QVERIFY(!store.lookup("ru", "en", "house"));
        const auto response = (*store.lookup("ru", "de", "Haus-redirect"))();
        QVERIFY2(response.error.isEmpty(), qPrintable(response.error));
        const auto senses = showthis::dictionary_senses(response.document, "de");
        QCOMPARE(senses.size(), 1);
        QCOMPARE(senses[0].toMap().value("headword").toString(), QString("Haus"));
        QCOMPARE(senses[0].toMap().value("article").toString(), QString("das"));
        QVERIFY((*store.lookup("ru", "de", "cycle-a"))().error.contains("cycle"));
        QVERIFY(install(store, "ru", "en"));
        QCOMPARE(store.packs().size(), 2);
        QCOMPARE(showthis::dictionary_senses((*store.lookup("ru", "en", "house"))().document, "en").size(), 1);
        QVERIFY(store.remove("ru", "de"));
        QVERIFY(!store.lookup("ru", "de", "Haus"));
        QVERIFY(store.lookup("ru", "en", "house"));
        showthis::KaikkiOfflineStore reopened(&network, directory.path());
        QCOMPARE(reopened.packs().size(), 1);
        QCOMPARE(reopened.packs()[0].toMap().value("word_language").toString(), QString("en"));
    }

    void missing_word_language_and_parse_error_have_specific_diagnostics() {
        QTemporaryDir directory;
        TestNetwork network;
        serve_download(network);
        showthis::KaikkiOfflineStore store(&network, directory.path());
        store.inspect("ru", "ja");
        QTRY_VERIFY(!store.busy());
        store.download("ru", "ja");
        QTRY_VERIFY(!store.busy());
        QVERIFY(store.packs().isEmpty());
        QVERIFY(store.message().contains("no entries for Japanese"));
        QVERIFY(!store.message().contains("Invalid JSONL"));
        QVERIFY(install(store, "ru", "de"));
        const auto revision = store.cache_token();
        serve_download(network, QByteArray("{\"title\":\"Alias\",\"redirect\":\"Haus\",\"pos\":\"hard-redirect\"}\nnot JSON\n"));
        store.download("ru", "de");
        QTRY_VERIFY(!store.busy());
        QVERIFY(store.message().contains("line 2"));
        QCOMPARE(store.cache_token(), revision);
        serve_download(network, QByteArray::fromHex("efbbbf") + offline_dictionary_data());
        QVERIFY(install(store, "ru", "de"));
        QVERIFY(store.cache_token() != revision);
    }
};

QTEST_GUILESS_MAIN(KaikkiOfflineStoreTest)
#include "KaikkiOfflineStoreTest.moc"
