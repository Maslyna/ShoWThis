#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "LearningService.h"
#include "LearningServiceTest.h"
#include "TestNetwork.h"
#include "DictionaryFixtures.h"

static QByteArray dictionary_response() {
    QFile file(QFINDTESTDATA("fixtures/dictionary-de-Haus.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

    void LearningServiceTest::initTestCase() {
        QVERIFY(m_settings_directory.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings_directory.path());
    }

    void LearningServiceTest::dictionary_cache_and_vocabulary() {
        QTemporaryDir directory;
        const auto path = directory.filePath("vocabulary.json");
        QString card_id;
        {
            TestNetwork network;
            network.response = dictionary_response();
            showthis::LearningService service(nullptr, path, &network);
            service.set_source_language("de");
            service.set_target_language("uk");
            service.lookup("Haus");
            QVERIFY(service.dictionary_busy());
            QTRY_COMPARE(service.senses().size(), 2);
            QVERIFY(!service.dictionary_busy());
            QCOMPARE(service.senses()[0].toMap().value("part_of_speech").toString(), QString("noun"));
            QCOMPARE(network.requests.size(), 1);
            QCOMPARE(network.requests[0].url().path(), QString("/api/v1/entries/de/Haus"));
            QVERIFY(network.payloads[0].isEmpty());
            QVERIFY(service.save_word({}, "Haus", "Das ist mein Haus.", "house", "будинок", "das Haus", 0));
            QCOMPARE(service.vocabulary().size(), 1);
            const auto card = service.vocabulary()[0].toMap();
            card_id = card.value("id").toString();
            QCOMPARE(card.value("context").toString(), QString("Das ist mein Haus."));
            QCOMPARE(card.value("source").toMap().value("url").toString(),
                     QString("https://en.wiktionary.org/wiki/Haus"));
            QCOMPARE(card.value("sense").toMap().value("definition").toString(), QString("house"));
            const auto permissions = QFile::permissions(path);
            QVERIFY(!(permissions & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
        }
        {
            TestNetwork network;
            showthis::LearningService service(nullptr, path, &network);
            QCOMPARE(service.vocabulary().size(), 1);
            service.lookup("Haus");
            QCOMPARE(service.senses().size(), 2);
            QCOMPARE(network.requests.size(), 0);  // A new session works offline from cache.
            QVERIFY(service.save_word(card_id, "Haus", "Das ist mein Haus.", "house", "дім", "updated", 0));
            QCOMPARE(service.vocabulary().size(), 1);
            QCOMPARE(service.vocabulary()[0].toMap().value("notes").toString(), QString("updated"));
            QVERIFY(!service.save_word({}, "", "", "", "", "", -1));
            QVERIFY(service.remove_word(card_id));
        }
        showthis::LearningService reopened(nullptr, path);
        QVERIFY(reopened.vocabulary().isEmpty());
    }

void LearningServiceTest::lookup_without_selection_preserves_saved_attribution() {
    QTemporaryDir directory;
    const auto path = directory.filePath("words.json");
    TestNetwork network;
    network.responder = [](const QNetworkRequest &request) -> TestNetwork::Response {
        if (request.url().host() == "freedictionaryapi.com")
            return {free_entry("Haus", "house")};
        if (request.url().path().endsWith(".json"))
            return {kaikki_index("Haus", "ru")};
        return {kaikki_entry("Haus", "new definition")};
    };
    showthis::LearningService service(nullptr, path, &network);
    service.set_source_language("de");
    service.lookup("Haus");
    QTRY_COMPARE(service.senses().size(), 1);
    QVERIFY(service.save_word({}, "Haus", "", "house", "", "", 0));
    const auto original = service.vocabulary()[0].toMap();
    const auto id = original.value("id").toString();
    QCOMPARE(original.value("source").toMap().value("provider").toString(), QString("freedictionary"));

    QVERIFY(service.configure_dictionaries({dictionary_selection("kaikki", {"ru"})}, true));
    service.lookup("Haus");
    QTRY_VERIFY(!service.dictionary_busy());
    QCOMPARE(service.senses().size(), 1);
    QCOMPARE(service.source().value("provider").toString(), QString("kaikki"));
    QVERIFY(service.save_word(id, "Haus", "", "house", "", "note-only edit", -1));
    const auto edited = service.vocabulary()[0].toMap();
    QCOMPARE(edited.value("source"), original.value("source"));
    QCOMPARE(edited.value("sense"), original.value("sense"));
    showthis::LearningService reopened(nullptr, path, &network);
    QCOMPARE(reopened.vocabulary()[0].toMap().value("source"), original.value("source"));
    QCOMPARE(QJsonObject::fromVariantMap(reopened.vocabulary()[0].toMap().value("sense").toMap()),
        QJsonObject::fromVariantMap(original.value("sense").toMap()));

    const auto export_path = directory.filePath("anki.txt");
    QVERIFY(service.export_anki(QUrl::fromLocalFile(export_path)));
    QFile exported(export_path);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    const auto bytes = exported.readAll();
    QVERIFY(bytes.contains("FreeDictionaryAPI.com"));
    QVERIFY(!bytes.contains("Kaikki.org"));

    // Only explicitly choosing a new sense replaces both attribution and sense.
    QVERIFY(service.save_word(id, "Haus", "", "new definition", "", "", 0));
    const auto replaced = service.vocabulary()[0].toMap();
    QCOMPARE(replaced.value("source"), service.senses()[0].toMap().value("source"));
    QCOMPARE(replaced.value("sense"), QVariant(service.senses()[0].toMap()));
    QVERIFY(service.save_word({}, "Haus", "", "my own meaning", "", "", -1));
    QVERIFY(service.vocabulary()[0].toMap().value("source").toMap().isEmpty());
    QVERIFY(service.vocabulary()[0].toMap().value("sense").toMap().isEmpty());
}

    void LearningServiceTest::contextual_translation() {
        QTemporaryDir directory;
        TestNetwork network;
        network.response = R"({"translations":[{"text":"будинок"}]})";
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.set_source_language("de");
        service.set_target_language("uk");
        auto *translator = service.translator();
        translator->set_provider_id("deepl");
        translator->set_option("api_key", "test-key");
        service.translate("Haus", "Das ist mein Haus.");
        QTRY_VERIFY(!translator->busy());
        QCOMPARE(translator->text(), QString::fromUtf8("будинок"));
        QCOMPARE(network.requests[0].url().host(), QString("api-free.deepl.com"));
        QCOMPARE(network.requests[0].rawHeader("Authorization"), QByteArray("DeepL-Auth-Key test-key"));
        const auto payload = QJsonDocument::fromJson(network.payloads[0]).object();
        QCOMPARE(payload.value("context").toString(), QString("Das ist mein Haus."));
        QCOMPARE(payload.value("source_lang").toString(), QString("DE"));
        QCOMPARE(payload.value("target_lang").toString(), QString("UK"));
        QCOMPARE(payload.value("text").toArray()[0].toString(), QString("Haus"));
        QVERIFY(service.save_word({}, "Haus", "Das ist mein Haus.", "house", translator->text(), "", -1));
        QFile file(directory.filePath("words.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("test-key"));

        service.set_target_language("ru");
        QVERIFY(translator->text().isEmpty());
        translator->set_option("plan", "pro");
        service.translate("Haus", "context");
        QTRY_VERIFY(!translator->busy());
        QCOMPARE(network.requests.last().url().host(), QString("api.deepl.com"));

        network.automatic = false;
        service.translate("Haus", "context");
        QVERIFY(translator->busy());
        auto pending = network.last_reply;
        service.set_target_language("fr");
        QVERIFY(!translator->busy());
        QVERIFY(pending->isFinished());
        service.translate("Haus", "context");
        pending = network.last_reply;
        service.set_source_language("en");
        QVERIFY(!translator->busy());
        QVERIFY(pending->isFinished());
        translator->forget_credentials();
        const auto requests = network.requests.size();
        service.translate("Haus", "context");
        QCOMPARE(network.requests.size(), requests);
    }

    void LearningServiceTest::failures_and_cancellation() {
        QTemporaryDir directory;
        TestNetwork network;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.set_source_language("de");
        network.status = 429;
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        QVERIFY(service.senses().isEmpty());
        QVERIFY(service.message().contains("rate limit"));

        network.status = 403;
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        QVERIFY(!service.message().contains("DeepL"));

        network.status = 200;
        network.response = "not JSON";
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        QVERIFY(service.message().contains("invalid response"));

        network.response = dictionary_response();
        network.automatic = false;
        service.lookup("Haus");
        auto previous = network.last_reply;
        QVERIFY(service.dictionary_busy());
        service.lookup("Häuser");
        QVERIFY(!previous || previous->isFinished());
        QVERIFY(service.senses().isEmpty());
        network.last_reply->complete();
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(service.senses().size(), 2);
        service.lookup("next");
        service.set_source_language("en");
        QVERIFY(!service.dictionary_busy());
        QVERIFY(service.senses().isEmpty());

        network.automatic = true;
        network.response = R"({"translations":[]})";
        service.translator()->set_provider_id("deepl");
        service.translator()->set_option("api_key", "test-key");
        service.translate("test", "context");
        QTRY_VERIFY(!service.translator()->busy());
        QVERIFY(service.translator()->text().isEmpty());
        QVERIFY(service.message().contains("no translation"));
    }

    void LearningServiceTest::corrupt_store_is_preserved() {
        QTemporaryDir directory;
        const auto path = directory.filePath("words.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("broken JSON");
        file.close();
        showthis::LearningService service(nullptr, path);
        QVERIFY(!service.storage_error().isEmpty());
        QVERIFY(!service.save_word({}, "Haus", "", "house", "", "", -1));
        service.set_history_limit(2);
        QCOMPARE(service.history_limit(), 1000); // A failed write must not commit a new limit.
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("broken JSON"));
    }

QTEST_GUILESS_MAIN(LearningServiceTest)

void LearningServiceTest::init() {
    QSettings{}.clear();
}

void LearningServiceTest::history_limit_and_recency() {
    QTemporaryDir directory;
    const auto path = directory.filePath("words.json");
    TestNetwork network;
    network.response = dictionary_response();
    showthis::LearningService service(nullptr, path, &network);
    service.set_source_language("de");
    QCOMPARE(service.history_limit(), 1000);
    service.set_history_limit(2);
    service.lookup("Haus", "First context");
    QTRY_VERIFY(!service.dictionary_busy());
    service.lookup("Baum", "Second context");
    QTRY_VERIFY(!service.dictionary_busy());
    QCOMPARE(service.dictionary_history().size(), 2);
    QCOMPARE(service.dictionary_history()[0].toMap().value("word").toString(), QString("Baum"));
    service.lookup("Haus", "Updated context");
    QCOMPARE(network.requests.size(), 2);
    QCOMPARE(service.dictionary_history().size(), 2);
    QCOMPARE(service.dictionary_history()[0].toMap().value("context").toString(), QString("Updated context"));
    QVERIFY(service.save_word({}, "Haus", "Updated context", "house", "", "", 0));
    service.lookup("Buch", "Third context");
    QTRY_VERIFY(!service.dictionary_busy());
    QCOMPARE(service.dictionary_history().size(), 2);
    QCOMPARE(service.dictionary_history()[1].toMap().value("word").toString(), QString("Haus"));
    QCOMPARE(service.vocabulary().size(), 1);

    QVERIFY(service.flush_cache());
    showthis::LearningService reopened(nullptr, path);
    QCOMPARE(reopened.history_limit(), 2);
    QCOMPARE(reopened.dictionary_history()[0].toMap().value("word").toString(), QString("Buch"));
    QFile file(service.cache_path());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto stored = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(stored.value("dictionary_cache").toObject().size(), 2);
    QVERIFY(!stored.value("dictionary_cache").toObject().contains(showthis::DictionaryLookup::cache_key("de", "Baum", {})));
    service.set_history_limit(1);
    QCOMPARE(service.dictionary_history().size(), 1);
    QCOMPARE(service.vocabulary().size(), 1);
}

void LearningServiceTest::default_history_capacity() {
    QTemporaryDir directory;
    const auto path = directory.filePath("words.json");
    const auto response = QJsonDocument::fromJson(dictionary_response()).object();
    QJsonObject cache;
    QJsonArray history;
    for (int i = 0; i < 1002; ++i) {
        const auto word = QString("word%1").arg(i);
        const auto key = "de:" + word;
        cache.insert(key, response);
        history.append(QJsonObject{{"key", key}, {"word", word}, {"language", "de"}});
    }
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"version", 1}, {"cards", QJsonArray{}},
        {"dictionary_cache", cache}, {"dictionary_history", history}}).toJson());
    file.close();
    showthis::LearningService service(nullptr, path);
    QCOMPARE(service.history_limit(), 1000);
    QCOMPARE(service.dictionary_history().size(), 1000);
    QCOMPARE(service.dictionary_history().first().toMap().value("word").toString(), QString("word0"));
    QCOMPARE(service.dictionary_history().last().toMap().value("word").toString(), QString("word999"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto stored = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(stored.value("dictionary_cache").toObject().size(), 1000);
    QCOMPARE(stored.value("dictionary_history").toArray().size(), 1000);
}

void LearningServiceTest::history_disabled_and_cleared() {
    QTemporaryDir directory;
    TestNetwork network;
    network.response = dictionary_response();
    showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
    service.set_source_language("de");
    service.lookup("Haus");
    QTRY_VERIFY(!service.dictionary_busy());
    QVERIFY(service.save_word({}, "Haus", "", "house", "", "", 0));
    network.automatic = false;
    service.lookup("next");
    QVERIFY(service.dictionary_busy());
    QVERIFY(service.clear_history());
    QVERIFY(!service.dictionary_busy());
    QVERIFY(service.dictionary_history().isEmpty());
    QCOMPARE(service.vocabulary().size(), 1);

    network.automatic = true;
    service.set_history_limit(0);
    const auto count = network.requests.size();
    service.lookup("Haus");
    QTRY_VERIFY(!service.dictionary_busy());
    service.lookup("Haus");
    QTRY_VERIFY(!service.dictionary_busy());
    QCOMPARE(network.requests.size(), count + 2);
    QVERIFY(service.dictionary_history().isEmpty());
    QCOMPARE(service.senses().size(), 2);
    service.set_history_limit(10001);
    QCOMPARE(service.history_limit(), 10000);
    service.set_history_limit(-1);
    QCOMPARE(service.history_limit(), 0);
}

void LearningServiceTest::legacy_history_migration() {
    QTemporaryDir directory;
    const auto path = directory.filePath("words.json");
    const auto response = QJsonDocument::fromJson(dictionary_response()).object();
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"version", 1}, {"cards", QJsonArray{}},
        {"dictionary_cache", QJsonObject{{showthis::DictionaryLookup::cache_key("de", "Haus", {}), response},
            {showthis::DictionaryLookup::cache_key("de", "Baum", {}), response}}}}).toJson());
    file.close();
    TestNetwork network;
    showthis::LearningService service(nullptr, path, &network);
    QCOMPARE(service.dictionary_history().size(), 2);
    service.set_source_language("de");
    service.lookup("Haus", "New context");
    QCOMPARE(network.requests.size(), 0);
    QCOMPARE(service.dictionary_history()[0].toMap().value("word").toString(), QString("Haus"));
    service.set_history_limit(1);
    showthis::LearningService reopened(nullptr, path);
    QCOMPARE(reopened.dictionary_history().size(), 1);
    QVERIFY(reopened.storage_error().isEmpty());
}

void LearningServiceTest::anki_export() {
    QTemporaryDir directory;
    const auto path = directory.filePath("words.json");
    TestNetwork network;
    network.response = dictionary_response();
    showthis::LearningService service(nullptr, path, &network);
    service.set_source_language("de");
    service.translator()->set_provider_id("deepl");
    service.translator()->set_option("api_key", "never-export-this-key");
    const auto empty_export = QUrl::fromLocalFile(directory.filePath("empty.txt"));
    QVERIFY(!service.export_anki(empty_export));
    QVERIFY(!QFile::exists(empty_export.toLocalFile()));
    service.lookup("Haus");
    QTRY_VERIFY(!service.dictionary_busy());
    const QString context = "<script>alert(1)</script>\t\"quoted\"\nSecond line";
    QVERIFY(service.save_word({}, "Haus", context, "house & home", QString::fromUtf8("будинок"), "note\r\nnext", 0, "das"));
    const auto first_id = service.vocabulary()[0].toMap().value("id").toString();
    QVERIFY(service.save_word({}, "Baum", "tree context", "tree", "", "", -1));
    const auto destination = QUrl::fromLocalFile(directory.filePath("anki.txt"));
    QVERIFY(service.export_anki(destination, {first_id}));
    QFile file(destination.toLocalFile());
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto bytes = file.readAll();
    file.close();
    QVERIFY(bytes.startsWith("#separator:Tab\n#html:true\n"));
    QVERIFY(bytes.contains("#tags column:3"));
    QVERIFY(bytes.contains("&lt;script&gt;"));
    QVERIFY(!bytes.contains("<script>"));
    QVERIFY(bytes.contains("&#9;"));
    QVERIFY(bytes.contains("<br>Second line"));
    QVERIFY(bytes.contains("&quot;quoted&quot;"));
    QVERIFY(bytes.contains(QString::fromUtf8("будинок").toUtf8()));
    QVERIFY(bytes.contains("https://en.wiktionary.org/wiki/Haus"));
    QVERIFY(bytes.contains("CC BY-SA 4.0"));
    QVERIFY(bytes.contains("<b>das Haus</b>"));
    QVERIFY(bytes.contains("FreeDictionaryAPI.com"));
    QVERIFY(!bytes.contains("never-export-this-key"));
    QCOMPARE(bytes.count('\n'), 5); // Four headers and one physical record.
    const auto record = bytes.split('\n')[4];
    QCOMPARE(record.count('\t'), 2);
    QVERIFY(!bytes.contains("tree context"));
    QVERIFY(service.export_anki(destination));
    QVERIFY(file.open(QIODevice::ReadOnly));
    bytes = file.readAll();
    file.close();
    QCOMPARE(bytes.count('\n'), 6);
    QVERIFY(bytes.contains("tree context"));
    QVERIFY(!service.export_anki(QUrl::fromLocalFile(path)));
    QVERIFY(!service.export_anki(QUrl("https://example.com/export.txt")));
    QVERIFY(!service.export_anki(destination, {"missing-id"}));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes); // Invalid selections do not clobber a previous export.
}

void LearningServiceTest::dictionary_grammar_data() {
    QTest::addColumn<QString>("language");
    QTest::addColumn<QString>("part_of_speech");
    QTest::addColumn<QStringList>("tags");
    QTest::addColumn<QStringList>("expected");
    QTest::newRow("masculine") << QString("de") << QString("noun") << QStringList{"masculine"} << QStringList{"der"};
    QTest::newRow("french-feminine") << QString("fr") << QString("noun") << QStringList{"feminine"} << QStringList{};
    QTest::newRow("neuter") << QString("de") << QString("noun") << QStringList{"neuter"} << QStringList{"das"};
    QTest::newRow("plural-only") << QString("de") << QString("noun") << QStringList{"plural-only"} << QStringList{"die"};
    QTest::newRow("spanish-multiple-genders") << QString("es") << QString("noun") << QStringList{"masculine", "feminine"} << QStringList{};
    QTest::newRow("unknown") << QString("de") << QString("noun") << QStringList{} << QStringList{};
    QTest::newRow("plural-form-unknown-gender") << QString("de") << QString("noun") << QStringList{"plural"} << QStringList{"die"};
    QTest::newRow("verb") << QString("de") << QString("verb") << QStringList{"masculine"} << QStringList{};
    QTest::newRow("proper-name") << QString("de") << QString("name") << QStringList{"neuter"} << QStringList{};
    QTest::newRow("not-german") << QString("en") << QString("noun") << QStringList{"neuter"} << QStringList{};
}

void LearningServiceTest::dictionary_grammar() {
    QFETCH(QString, language);
    QFETCH(QString, part_of_speech);
    QFETCH(QStringList, tags);
    QFETCH(QStringList, expected);
    QTemporaryDir directory;
    TestNetwork network;
    auto response = QJsonDocument::fromJson(dictionary_response()).object();
    auto entry = response.value("entries").toArray().first().toObject();
    entry.insert("language", QJsonObject{{"code", language}});
    entry.insert("partOfSpeech", part_of_speech);
    auto sense = entry.value("senses").toArray().first().toObject();
    sense.insert("tags", QJsonArray::fromStringList(tags));
    entry.insert("senses", QJsonArray{sense});
    response.insert("entries", QJsonArray{entry});
    network.response = QJsonDocument(response).toJson();
    showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
    service.set_source_language(language);
    service.lookup("Haus");
    QTRY_VERIFY(!service.dictionary_busy());
    QCOMPARE(service.senses().size(), 2);
    for (const auto &value : service.senses()) {
        const auto parsed = value.toMap();
        QCOMPARE(parsed.value("articles").toStringList(), expected);
        QCOMPARE(parsed.value("article").toString(), expected.size() == 1 ? expected.first() : QString{});
        QCOMPARE(parsed.value("tags").toStringList(), tags);
    }
    QVERIFY(service.save_word({}, "Haus", "", "house", "", "", 0));
    QCOMPARE(service.vocabulary()[0].toMap().value("article").toString(),
        expected.size() == 1 ? expected.first() : QString{});
}

void LearningServiceTest::article_persistence_and_legacy_cards() {
    QTemporaryDir directory;
    const auto path = directory.filePath("words.json");
    TestNetwork network;
    network.response = dictionary_response();
    showthis::LearningService service(nullptr, path, &network);
    service.set_source_language("de");
    service.lookup("Haus");
    QTRY_VERIFY(!service.dictionary_busy());
    // The German noun profile suggests the article; manual corrections win.
    QCOMPARE(service.senses()[1].toMap().value("tags").toStringList(), QStringList{"neuter"});
    QVERIFY(service.save_word({}, "Haus", "", "house", "", "", 1, "das"));
    auto card = service.vocabulary()[0].toMap();
    const auto id = card.value("id").toString();
    QCOMPARE(card.value("article").toString(), QString("das"));
    QVERIFY(service.save_word(id, "Haus", "", "house", "", "manual correction", -1, "die"));
    {
        showthis::LearningService reopened(nullptr, path);
        QCOMPARE(reopened.vocabulary()[0].toMap().value("article").toString(), QString("die"));
        QVERIFY(reopened.save_word(id, "Haus", "", "house", "", "note-only edit", -1));
        QCOMPARE(reopened.vocabulary()[0].toMap().value("article").toString(), QString("die"));
        QVERIFY(reopened.save_word(id, "Haus", "", "house", "", "explicitly no article", -1, ""));
    }
    {
        showthis::LearningService reopened(nullptr, path);
        QVERIFY(reopened.vocabulary()[0].toMap().value("article").toString().isEmpty());
    }
    QVERIFY(!service.save_word({}, "Haus", "", "house", "", "", -1, QString(81, 'x')));
    service.set_source_language("fr");
    QVERIFY(service.save_word({}, "maison", "", "house", "", "", -1, "la"));
    QCOMPARE(service.vocabulary()[0].toMap().value("article").toString(), QString("la"));
    QVERIFY(service.save_word({}, "ami", "", "friend", "", "", -1, "l'"));
    const auto french_export = QUrl::fromLocalFile(directory.filePath("french.txt"));
    QVERIFY(service.export_anki(french_export));
    QFile french(french_export.toLocalFile());
    QVERIFY(french.open(QIODevice::ReadOnly));
    const auto french_data = french.readAll();
    QVERIFY(french_data.contains("<b>la maison</b>"));
    QVERIFY(french_data.contains("<b>l'ami</b>") || french_data.contains("<b>l&#39;ami</b>"));

    // A legacy card without an article field is enriched from its stored sense.
    card.remove("article");
    auto old_sense = card.value("sense").toMap();
    old_sense.insert("article", "das");
    card.insert("sense", old_sense);
    QFile legacy(directory.filePath("legacy.json"));
    QVERIFY(legacy.open(QIODevice::WriteOnly));
    legacy.write(QJsonDocument(QJsonObject{{"version", 1}, {"cards", QJsonArray{QJsonObject::fromVariantMap(card)}},
        {"dictionary_cache", QJsonObject{}}}).toJson());
    legacy.close();
    showthis::LearningService migrated(nullptr, legacy.fileName());
    QCOMPARE(migrated.vocabulary()[0].toMap().value("article").toString(), QString("das"));
    const auto destination = QUrl::fromLocalFile(directory.filePath("legacy-anki.txt"));
    QVERIFY(migrated.export_anki(destination));
    QFile exported(destination.toLocalFile());
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QVERIFY(exported.readAll().contains("<b>das Haus</b>"));
}

void LearningServiceTest::flexible_languages() {
    QTemporaryDir directory;
    TestNetwork network;
    showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
    service.set_source_language(" FRA ");
    QCOMPARE(service.source_language(), QString("fr"));
    service.set_target_language("ES");
    QCOMPARE(service.target_language(), QString("es"));
    service.set_source_language("ja");
    QCOMPARE(service.source_language(), QString("ja"));
    service.set_target_language("pt_BR");
    QCOMPARE(service.target_language(), QString("pt-br"));
    service.set_source_language("../invalid");
    QCOMPARE(service.source_language(), QString("ja"));
    QVERIFY(!service.message().isEmpty());
    service.set_target_language("");
    QCOMPARE(service.target_language(), QString("pt-br"));
    QVERIFY(service.language_options().size() > 30);
    bool has_french = false;
    for (const auto &option : service.language_options())
        has_french = has_french || option.toMap().value("code").toString() == "fr";
    QVERIFY(has_french);
    QCOMPARE(network.requests.size(), 0); // Choosing languages never sends text.
    showthis::LearningService reopened(nullptr, directory.filePath("words.json"));
    QCOMPARE(reopened.source_language(), QString("ja"));
    QCOMPARE(reopened.target_language(), QString("pt-br"));
    service.set_source_language("fr-CA");
    QCOMPARE(service.source_language(), QString("fr"));
    service.set_target_language("en-US");
    QCOMPARE(service.target_language(), QString("en-us"));
    QSettings{}.setValue("learning/source_language", "de-DE");
    showthis::LearningService regional(nullptr, directory.filePath("regional.json"));
    QCOMPARE(regional.source_language(), QString("de"));
    QCOMPARE(regional.target_language(), QString("en-us"));
    QVERIFY(network.requests.isEmpty());
}

void LearningServiceTest::retired_metadata_is_preserved() {
    QTemporaryDir directory;
    const auto path = directory.filePath("words.json");
    const auto observed = QString::fromUtf8("Häusern");
    const auto context = QString::fromUtf8("In den Häusern wohnen Menschen.");
    const QVariantMap linguistics{{"provider", "spaCy"}, {"model", "test_model"},
        {"part_of_speech", "NOUN"}, {"features", QVariantMap{{"Case", "Dat"}, {"Number", "Plur"}}}};
    const QVariantMap card{{"id", "legacy"}, {"word", "Haus"}, {"language", "de"},
        {"context", context}, {"meaning", "house"}, {"article", "das"},
        {"original_word", observed}, {"lemma", "Haus"}, {"lemma_confirmed", true},
        {"linguistics", linguistics}, {"custom_field", "keep me"}, {"created", "2026-01-01"}};
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"version", 1}, {"cards", QJsonArray{QJsonObject::fromVariantMap(card)}},
        {"dictionary_cache", QJsonObject{}}, {"custom_root", "also keep me"}}).toJson());
    file.close();

    showthis::LearningService service(nullptr, path);
    service.set_source_language("de");
    QVERIFY(service.save_word("legacy", "Haus", context, "home", "", "updated note", -1));
    // Retired metadata is opaque: it must not prevent ordinary word editing.
    QVERIFY(service.save_word("legacy", "Häuschen", context, "small house", "", "updated", -1));
    showthis::LearningService reopened(nullptr, path);
    const auto updated = reopened.vocabulary()[0].toMap();
    for (const auto &key : {"original_word", "lemma", "lemma_confirmed", "linguistics", "custom_field", "created"})
        QCOMPARE(updated.value(key), card.value(key));
    QCOMPARE(updated.value("word").toString(), QString::fromUtf8("Häuschen"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("custom_root").toString(), QString("also keep me"));
    const auto destination = QUrl::fromLocalFile(directory.filePath("anki.txt"));
    QVERIFY(reopened.export_anki(destination));
    QFile exported(destination.toLocalFile());
    QVERIFY(exported.open(QIODevice::ReadOnly));
    const auto data = exported.readAll();
    QVERIFY(!data.contains("Dictionary form:"));
    QVERIFY(!data.contains("Observed form:"));
    QVERIFY(!data.contains("Case=Dat"));
    QVERIFY(!data.contains("test_model"));
    QVERIFY(service.metaObject()->indexOfProperty("lemmatizer") < 0);
}

void LearningServiceTest::failed_write_does_not_commit_changes() {
    QTemporaryDir directory;
    const auto path = directory.filePath("blocked/words.json");
    QFile blocker(directory.filePath("blocked"));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    showthis::LearningService service(nullptr, path);
    QVERIFY(!service.save_word({}, "word", "", "meaning", "", "", -1));
    QVERIFY(service.vocabulary().isEmpty());
    QVERIFY(!service.storage_error().isEmpty());
    service.set_history_limit(2);
    QCOMPARE(service.history_limit(), 1000);
    QVERIFY(blocker.remove());
    QVERIFY(service.save_word({}, "word", "", "meaning", "", "", -1));
    QCOMPARE(service.vocabulary().size(), 1);
    QVERIFY(service.storage_error().isEmpty());
}
