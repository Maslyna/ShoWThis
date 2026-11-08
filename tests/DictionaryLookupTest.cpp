#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <memory>

#include "DictionaryData.h"
#include "DictionaryFixtures.h"
#include "DictionaryLookup.h"
#include "LearningService.h"
#include "NotebookData.h"
#include "TestNetwork.h"

class DictionaryLookupTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;

    static QJsonObject result(const QSignalSpy &spy) { return spy[0][0].value<QJsonObject>(); }
    static QVariantList senses(const QSignalSpy &spy) { return showthis::dictionary_senses(result(spy), "de"); }

private slots:
    void initTestCase() {
        QVERIFY(m_settings.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void init() {
        QSettings{}.clear();
    }

    void free_dictionary_tagged_form_reference() {
        // This is the shape returned by the live API: form_of is absent, but
        // the explicit form-of tag and definition identify a dictionary link.
        const QJsonObject raw{{"word", QString::fromUtf8("Häuser")}, {"entries", QJsonArray{
            QJsonObject{{"language", QJsonObject{{"code", "de"}}}, {"partOfSpeech", "noun"},
                {"senses", QJsonArray{QJsonObject{{"definition", "nominative/accusative/genitive plural of Haus"},
                    {"tags", QJsonArray{"form of", "plural", "genitive"}}}}}}}}};
        const auto document = showthis::free_dictionary_document(raw, QString::fromUtf8("Häuser"), "de");
        QCOMPARE(showthis::dictionary_form_targets(document, "de"), QStringList{"Haus"});
        QVERIFY(!showthis::has_dictionary_meanings(document, "de"));
        auto ordinary = raw;
        auto entry = ordinary.value("entries").toArray()[0].toObject();
        entry.insert("senses", QJsonArray{QJsonObject{{"definition", "a part of something"}}});
        ordinary.insert("entries", QJsonArray{entry});
        QVERIFY(showthis::dictionary_form_targets(showthis::free_dictionary_document(ordinary, "part", "de"), "de").isEmpty());
    }

    void free_dictionary_follows_base_forms() {
        TestNetwork network;
        const auto observed = QString::fromUtf8("Häuser");
        network.responder = [&](const QNetworkRequest &request) -> TestNetwork::Response {
            return {request.url().path().endsWith(observed)
                ? free_entry(observed, "genitive plural of Haus", "Haus") : free_entry("Haus", "house")};
        };
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup(observed, "de", {});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(senses(finished).size(), 2);
        QCOMPARE(senses(finished)[1].toMap().value("headword").toString(), QString("Haus"));
        QCOMPARE(senses(finished)[1].toMap().value("observed_word").toString(), observed);
        QCOMPARE(senses(finished)[1].toMap().value("source").toMap().value("name").toString(), QString("FreeDictionaryAPI.com"));
        QVERIFY(finished[0][2].toBool());
    }

    void kaikki_native_definitions_and_word_language() {
        TestNetwork network;
        network.responder = [](const QNetworkRequest &request) -> TestNetwork::Response {
            if (request.url().path().endsWith(".json")) return {kaikki_index("Haus", "ru")};
            return {kaikki_entry("Haus", QString::fromUtf8("дом, здание"))
                + kaikki_entry("Haus", "wrong language", "fr")};
        };
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("Haus", "de", {{{"kaikki", "ru"}}});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(network.requests[0].url().path(), QString("/ruwiktionary/search/start/ha.json"));
        QCOMPARE(senses(finished).size(), 1);
        const auto sense = senses(finished)[0].toMap();
        QCOMPARE(sense.value("definition").toString(), QString::fromUtf8("дом, здание"));
        QCOMPARE(sense.value("definition_language").toString(), QString("ru"));
        QCOMPARE(sense.value("provider").toString(), QString("kaikki"));
        QCOMPARE(sense.value("source").toMap().value("url").toString(), QString("https://ru.wiktionary.org/wiki/Haus"));
    }

    void unicode_prefix_and_partitioned_indexes() {
        TestNetwork network;
        const auto word = QString::fromUtf8("Häuser");
        network.responder = [&](const QNetworkRequest &request) -> TestNetwork::Response {
            if (request.url().path().endsWith(QString::fromUtf8("hä.json"))) return {"[true, []]"};
            if (request.url().path().endsWith(".json")) return {kaikki_index(word, "en")};
            return {kaikki_entry(word, "plural of Haus", "de", "Haus")};
        };
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup(word, "de", {{{"kaikki", "en"}}, false});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(network.requests.size(), 3);
        QCOMPARE(network.requests[1].url().path(), QString::fromUtf8("/dictionary/search/start/häu.json"));
        QCOMPARE(showthis::dictionary_form_targets(result(finished), "de"), QStringList{"Haus"});
    }

    void multiple_word_and_definition_languages_data() {
        QTest::addColumn<QString>("word");
        QTest::addColumn<QString>("word_language");
        QTest::addColumn<QString>("definition_language");
        QTest::newRow("german-in-russian") << QString("Haus") << QString("de") << QString("ru");
        QTest::newRow("russian-in-german") << QString::fromUtf8("дом") << QString("ru") << QString("de");
        QTest::newRow("english-in-french") << QString("house") << QString("en") << QString("fr");
        QTest::newRow("spanish-in-english") << QString("casa") << QString("es") << QString("en");
    }
    void multiple_word_and_definition_languages() {
        QFETCH(QString, word);
        QFETCH(QString, word_language);
        QFETCH(QString, definition_language);
        QString error;
        const auto document = showthis::kaikki_dictionary_document(kaikki_entry(word, "native definition", word_language),
            word, word_language, definition_language, error);
        QVERIFY(error.isEmpty());
        const auto entries = showthis::dictionary_senses(document, word_language);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries[0].toMap().value("definition_language").toString(), definition_language);
        QCOMPARE(QUrl(entries[0].toMap().value("source").toMap().value("url").toString()).host(),
            definition_language + ".wiktionary.org");
    }

    void both_sources_start_together_and_finish_independently() {
        TestNetwork network;
        network.automatic = false;
        network.responder = [](const QNetworkRequest &request) -> TestNetwork::Response {
            if (request.url().host() == "freedictionaryapi.com") return {free_entry("Haus", "house")};
            if (request.url().path().endsWith(".json")) return {kaikki_index("Haus", "ru")};
            return {kaikki_entry("Haus", QString::fromUtf8("дом"))};
        };
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("Haus", "de", {{{"kaikki", "ru"}, {"freedictionary", "en"}}});
        QCOMPARE(network.requests.size(), 2);
        QCOMPARE(network.requests[0].url().host(), QString("kaikki.org"));
        QCOMPARE(network.requests[1].url().host(), QString("freedictionaryapi.com"));
        network.replies[1]->complete();
        QVERIFY(lookup.busy());
        QVERIFY(finished.isEmpty());
        network.replies[0]->complete();
        QCOMPARE(network.requests.size(), 3);
        network.replies[2]->complete();
        QCOMPARE(finished.size(), 1);
        QVERIFY(!lookup.busy());
        QCOMPARE(senses(finished).size(), 2);
        QCOMPARE(senses(finished)[0].toMap().value("provider").toString(), QString("kaikki"));
        QCOMPARE(senses(finished)[1].toMap().value("provider").toString(), QString("freedictionary"));
    }

    void fallback_can_find_native_headwords_from_other_source() {
        TestNetwork network;
        const auto word = QString::fromUtf8("Häuser");
        network.responder = [&](const QNetworkRequest &request) -> TestNetwork::Response {
            const auto path = request.url().path();
            if (request.url().host() == "freedictionaryapi.com")
                return {path.endsWith(word) ? free_entry(word, "plural of Haus", "Haus") : free_entry("Haus", "house")};
            if (path.endsWith(QString::fromUtf8("hä.json"))) return {{}, 404};
            if (path.endsWith(".json")) return {kaikki_index("Haus", "ru")};
            return {kaikki_entry("Haus", QString::fromUtf8("дом"))};
        };
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup(word, "de", {{{"kaikki", "ru"}, {"freedictionary", "en", true}}});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(network.requests[0].url().host(), QString("kaikki.org"));
        QCOMPARE(network.requests[1].url().host(), QString("freedictionaryapi.com"));
        const auto entries = senses(finished);
        QVERIFY(std::any_of(entries.begin(), entries.end(), [](const QVariant &value) {
            const auto sense = value.toMap();
            return sense.value("headword") == "Haus" && sense.value("definition_language") == "ru";
        }));
        QVERIFY(!lookup.busy());
        QVERIFY(finished[0][1].toString().isEmpty());
        QVERIFY(finished[0][2].toBool());
    }

    void fallback_replays_primary_base_forms_data() {
        QTest::addColumn<bool>("follow_forms");
        QTest::newRow("follow") << true;
        QTest::newRow("manual-only") << false;
    }

    void fallback_replays_primary_base_forms() {
        QFETCH(bool, follow_forms);
        TestNetwork network;
        const auto word = QString::fromUtf8("Häuser");
        network.responder = [word](const QNetworkRequest &request) -> TestNetwork::Response {
            const auto path = request.url().path();
            if (request.url().host() == "freedictionaryapi.com")
                return path.endsWith(word) ? TestNetwork::Response{free_entry(word, "plural of Haus", "Haus")}
                                           : TestNetwork::Response{{}, 404};
            if (path.endsWith(QString::fromUtf8("hä.json"))) return {{}, 404};
            if (path.endsWith("ha.json")) return {kaikki_index("Haus", "ru")};
            return {kaikki_entry("Haus", "house from fallback")};
        };
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup(word, "de", {{{"freedictionary", "en"}, {"kaikki", "ru", true}}, follow_forms});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!lookup.busy());
        const auto entries = senses(finished);
        const auto resolved = std::count_if(entries.begin(), entries.end(), [](const QVariant &value) {
            return value.toMap().value("definition").toString() == "house from fallback";
        });
        QCOMPARE(resolved, follow_forms ? 1 : 0);
        QCOMPARE(std::count_if(network.requests.begin(), network.requests.end(), [](const QNetworkRequest &request) {
            return request.url().host() == "kaikki.org" && request.url().path().endsWith("ha.json");
        }), follow_forms ? 1 : 0);
    }

    void regional_word_language_uses_dictionary_base_language() {
        TestNetwork network;
        network.response = free_entry("Haus", "house");
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("Haus", "de-DE", {});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(network.requests[0].url().path(), QString("/api/v1/entries/de/Haus"));
        QCOMPARE(senses(finished).size(), 1);
        QCOMPARE(showthis::DictionaryLookup::cache_key("de-DE", "Haus", {}),
            showthis::DictionaryLookup::cache_key("de", "Haus", {}));
    }

    void network_destruction_before_lookup_is_safe() {
        auto owner = std::make_unique<QObject>();
        auto *network = new TestNetwork;
        network->setParent(owner.get());
        network->automatic = false;
        auto *lookup = new showthis::DictionaryLookup(network, owner.get());
        lookup->lookup("Haus", "de", {{{"kaikki", "ru"}, {"freedictionary", "en"}}});
        QVERIFY(lookup->busy());
        owner.reset(); // The network and its replies are destroyed first.
    }

    void fallback_does_not_query_other_source_when_not_needed() {
        TestNetwork network;
        network.response = free_entry("Haus", "house");
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("Haus", "de", {{{"freedictionary", "en"}, {"kaikki", "ru", true}}});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(network.requests.size(), 1);
    }

    void partial_failure_keeps_successful_results() {
        TestNetwork network;
        network.responder = [](const QNetworkRequest &request) -> TestNetwork::Response {
            if (request.url().host() == "kaikki.org") return {{}, 503};
            return {free_entry("Haus", "house")};
        };
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("Haus", "de", {{{"kaikki", "ru"}, {"freedictionary", "en"}}});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(senses(finished).size(), 1);
        QVERIFY(finished[0][1].toString().contains("Kaikki"));
        QVERIFY(!finished[0][2].toBool()); // Retry the failed source on the next lookup.
    }

    void cycles_and_following_disabled() {
        TestNetwork network;
        network.responder = [](const QNetworkRequest &request) -> TestNetwork::Response {
            const auto word = request.url().path().section('/', -1);
            const auto other = word == "a" ? "b" : "a";
            return {free_entry(word, QString("form of %1").arg(other), other)};
        };
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("a", "de", {});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(network.requests.size(), 2);
        finished.clear();
        network.requests.clear();
        lookup.lookup("a", "de", {{{"freedictionary", "en"}}, false});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(network.requests.size(), 1);
        QCOMPARE(showthis::dictionary_form_targets(result(finished), "de"), QStringList{"b"});
    }

    void cancel_and_replacement_ignore_old_results() {
        TestNetwork network;
        network.automatic = false;
        network.responder = [](const QNetworkRequest &request) -> TestNetwork::Response {
            return {free_entry(request.url().path().section('/', -1), "meaning")};
        };
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("old", "de", {{{"kaikki", "ru"}, {"freedictionary", "en"}}});
        const auto old = network.replies;
        lookup.lookup("new", "de", {});
        for (const auto &reply : old)
            QVERIFY(!reply || reply->isFinished());
        network.last_reply->complete();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(result(finished).value("word").toString(), QString("new"));
        lookup.lookup("another", "de", {});
        lookup.cancel();
        QVERIFY(!lookup.busy());
        QCOMPARE(finished.size(), 1);
    }

    void invalid_data_and_untrusted_links_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("invalid-json") << QByteArray("bad JSON");
        QTest::newRow("invalid-schema") << QByteArray("{}");
        QTest::newRow("untrusted-link") << QByteArray("[false, [[\"Haus\", \"https://example.com/secret.html\"]]]");
        QTest::newRow("oversized") << QByteArray(8 * 1024 * 1024 + 1, 'x');
    }
    void invalid_data_and_untrusted_links() {
        QFETCH(QByteArray, body);
        TestNetwork network;
        network.response = body;
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("Haus", "de", {{{"kaikki", "ru"}}});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(senses(finished).isEmpty());
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(!finished[0][1].toString().isEmpty());
    }

    void cache_keys_and_preferences_are_source_and_language_specific() {
        QTemporaryDir directory;
        TestNetwork network;
        network.responder = [](const QNetworkRequest &request) -> TestNetwork::Response {
            if (request.url().host() == "freedictionaryapi.com") return {free_entry("Haus", "house")};
            const auto edition = request.url().path().startsWith("/ruwiktionary/") ? QString("ru") : QString("de");
            if (request.url().path().endsWith(".json")) return {kaikki_index("Haus", edition)};
            return {kaikki_entry("Haus", edition == "ru" ? QString::fromUtf8("дом") : QString("Wohngebäude"))};
        };
        const auto path = directory.filePath("words.json");
        showthis::LearningService service(nullptr, path, &network);
        service.set_source_language("de");
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(network.requests.size(), 1);
        service.lookup("Haus");
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(service.configure_dictionaries({dictionary_selection("kaikki", {"ru"})}, true));
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(network.requests.size(), 3);
        QCOMPARE(service.senses()[0].toMap().value("definition").toString(), QString::fromUtf8("дом"));
        QVERIFY(service.configure_dictionaries({dictionary_selection("kaikki", {"de"})}, true));
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(network.requests.size(), 5);
        QVERIFY(service.configure_dictionaries({dictionary_selection("kaikki", {"ru"})}, true));
        service.lookup("Haus");
        QCOMPARE(network.requests.size(), 5);
        QVERIFY(service.configure_dictionaries({dictionary_selection("kaikki", {"ru"}),
            dictionary_selection("freedictionary", {"en"})}, true));
        service.lookup("Haus");
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(network.requests.size(), 8);
        QCOMPARE(service.senses().size(), 2);
        QCOMPARE(service.dictionary_history().size(), 4);
        QCOMPARE(service.dictionary_history()[0].toMap().value("dictionary_selection").toList(), service.dictionary_selection());
        showthis::LearningService reopened(nullptr, path, &network);
        QCOMPARE(reopened.dictionary_selection(), service.dictionary_selection());
        reopened.lookup("Haus");
        QCOMPARE(network.requests.size(), 8);
        QCOMPARE(reopened.senses().size(), 2);
    }

    void save_resolved_headword_with_its_own_attribution() {
        QTemporaryDir directory;
        TestNetwork network;
        const auto word = QString::fromUtf8("Häuser");
        network.responder = [&](const QNetworkRequest &request) -> TestNetwork::Response {
            const auto current = request.url().path().contains(QString::fromUtf8("hä"))
                || request.url().path().endsWith(word + ".jsonl") ? word : QString("Haus");
            if (request.url().path().endsWith(".json")) return {kaikki_index(current, "ru")};
            return {kaikki_entry(current, current == word ? QString::fromUtf8("форма Haus") : QString::fromUtf8("дом"),
                "de", current == word ? QString("Haus") : QString{}, current == "Haus" ? house_forms() : QJsonArray{})};
        };
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.set_source_language("de");
        QVERIFY(service.configure_dictionaries({dictionary_selection("kaikki", {"ru"})}, true));
        service.lookup(word);
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(service.senses().size(), 2);
        QVERIFY(service.save_word({}, "Haus", "der Häuser", QString::fromUtf8("дом"), "", "", 1));
        const auto card = service.vocabulary()[0].toMap();
        QCOMPARE(card.value("word").toString(), QString("Haus"));
        QCOMPARE(card.value("base_form").toString(), QString("Haus"));
        QCOMPARE(card.value("base_article").toString(), QString("das"));
        QCOMPARE(card.value("article").toString(), QString("das"));
        QCOMPARE(card.value("plural_forms").toStringList(), QStringList{QString::fromUtf8("Häuser")});
        QCOMPARE(card.value("dictionary_selection").toList(), service.dictionary_selection());
        QCOMPARE(card.value("follow_forms").toBool(), service.follow_forms());
        QCOMPARE(card.value("source").toMap().value("name").toString(), QString("Kaikki.org"));
        QCOMPARE(card.value("source").toMap().value("url").toString(), QString("https://ru.wiktionary.org/wiki/Haus"));
        const auto destination = QUrl::fromLocalFile(directory.filePath("anki.txt"));
        QVERIFY(service.export_anki(destination));
        QFile exported(destination.toLocalFile());
        QVERIFY(exported.open(QIODevice::ReadOnly));
        const auto bytes = exported.readAll();
        QVERIFY(bytes.contains("Kaikki.org"));
        QVERIFY(bytes.contains("https://ru.wiktionary.org/wiki/Haus"));
        QVERIFY(!bytes.contains("via FreeDictionaryAPI.com"));
        QVERIFY(bytes.contains("Base form: das Haus"));
        QVERIFY(bytes.contains("Plural: H\xc3\xa4user"));
        showthis::LearningService reopened(nullptr, directory.filePath("words.json"), &network);
        QCOMPARE(reopened.vocabulary()[0].toMap().value("plural_forms").toStringList(), card.value("plural_forms").toStringList());
        const auto id = card.value("id").toString();
        QVERIFY(reopened.save_word(id, "Haus", "", "home", "", "note edit", -1));
        QCOMPARE(reopened.vocabulary()[0].toMap().value("base_article").toString(), QString("das"));
        const QVariantMap manual{{"base_form", "house"}, {"base_article", "the"}, {"plural_forms", QStringList{"houses"}}};
        QVERIFY(reopened.save_word(id, "Haus", "", "home", "", "", -1, "", manual));
        QCOMPARE(reopened.vocabulary()[0].toMap().value("base_form").toString(), QString("house"));
        const QVariantMap invalid{{"plural_forms", QStringList{"bad\nform"}}};
        QVERIFY(!reopened.save_word(id, "Haus", "", "home", "", "", -1, "", invalid));
        QCOMPARE(reopened.vocabulary()[0].toMap().value("base_form").toString(), QString("house"));
        const QVariantMap clear{{"base_form", ""}, {"base_article", ""}, {"plural_forms", QStringList{}}};
        QVERIFY(reopened.save_word(id, "Haus", "", "home", "", "", -1, "", clear));
        QVERIFY(reopened.vocabulary()[0].toMap().value("base_form").toString().isEmpty());
        QVERIFY(reopened.vocabulary()[0].toMap().value("plural_forms").toStringList().isEmpty());
    }

    void multiple_editions_and_injected_provider_run_concurrently() {
        TestNetwork network;
        network.automatic = false;
        network.responder = [](const QNetworkRequest &request) -> TestNetwork::Response {
            if (request.url().host() != "kaikki.org")
                return {free_entry("Haus", request.url().path().startsWith("/fr/") ? "maison" : "house")};
            const auto edition = request.url().path().startsWith("/ruwiktionary/") ? QString("ru") : QString("de");
            if (request.url().path().endsWith(".json")) return {kaikki_index("Haus", edition)};
            return {kaikki_entry("Haus", edition == "ru" ? QString::fromUtf8("дом") : QString("Wohngebäude"))};
        };
        showthis::DictionaryLookup lookup(&network, nullptr, sample_dictionary_providers());
        QCOMPARE(lookup.sources().size(), 3);
        showthis::DictionaryOptions options;
        QVariantList normalized;
        QString error;
        QVERIFY(lookup.configure({dictionary_selection("kaikki", {"ru", "de"}),
            dictionary_selection("freedictionary", {"en"}), dictionary_selection("sample", {"fr", "en"})},
            true, options, normalized, error));
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("Haus", "de", options);
        QCOMPARE(network.requests.size(), 5); // All source/language pairs start together.
        for (int i = 0; i < network.replies.size(); ++i)
            network.replies[i]->complete();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(senses(finished).size(), 5);
        QSet<QString> combinations;
        for (const auto &value : senses(finished)) {
            const auto sense = value.toMap();
            combinations.insert(sense.value("provider").toString() + ':' + sense.value("definition_language").toString());
            if (sense.value("provider") == "sample") {
                QCOMPARE(sense.value("source").toMap().value("name").toString(), QString("Sample Dictionary"));
                QCOMPARE(sense.value("source").toMap().value("provider_url").toString(), QString("https://dictionary.example/"));
            }
        }
        QCOMPARE(combinations, (QSet<QString>{"kaikki:ru", "kaikki:de", "freedictionary:en", "sample:fr", "sample:en"}));
        const auto key = showthis::DictionaryLookup::cache_key("de", "Haus", options);
        std::reverse(options.targets.begin(), options.targets.end());
        QCOMPARE(showthis::DictionaryLookup::cache_key("de", "Haus", options), key);
        options.follow_forms = false;
        QVERIFY(showthis::DictionaryLookup::cache_key("de", "Haus", options) != key);
        lookup.lookup("Haus", "de", options);
        QVERIFY(lookup.busy());
        lookup.cancel();
        for (const auto &reply : network.replies)
            QVERIFY(!reply || reply->isFinished());
        QCOMPARE(finished.size(), 1);
        QVERIFY(!lookup.busy());
    }

    void selections_are_validated_atomically_and_duplicates_merged() {
        QTemporaryDir directory;
        TestNetwork network;
        network.automatic = false;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        const auto original = service.dictionary_selection();
        service.lookup("Haus");
        QVERIFY(service.dictionary_busy());
        QSignalSpy preferences(&service, &showthis::LearningService::preferences_changed);
        QVERIFY(!service.configure_dictionaries({dictionary_selection("missing", {"en"})}, false));
        QVERIFY(!service.configure_dictionaries({dictionary_selection("freedictionary", {"ru"})}, false));
        QVERIFY(!service.configure_dictionaries({dictionary_selection("kaikki", {})}, false));
        QVERIFY(!service.configure_dictionaries({dictionary_selection("kaikki", {"ru"}),
            dictionary_selection("kaikki", {"de"}, true)}, false));
        QCOMPARE(service.dictionary_selection(), original);
        QVERIFY(service.dictionary_busy()); // Invalid edits do not cancel an active lookup.
        QCOMPARE(preferences.size(), 0);
        QVERIFY(service.configure_dictionaries({dictionary_selection("kaikki", {"ru", "ru"}),
            dictionary_selection("kaikki", {"de", "ru"})}, false));
        QCOMPARE(preferences.size(), 1);
        QVERIFY(!service.dictionary_busy());
        QVERIFY(network.last_reply->isFinished());
        QCOMPARE(service.dictionary_selection(), QVariantList{dictionary_selection("kaikki", {"de", "ru"})});
        QVERIFY(!service.follow_forms());
        QVERIFY(service.configure_dictionaries({}, true));
        service.lookup("Haus");
        QVERIFY(!service.dictionary_busy());
        QVERIFY(service.message().contains("Select a dictionary"));
    }

    void multiple_fallback_languages_are_deferred_then_started_together() {
        TestNetwork network;
        network.automatic = false;
        network.responder = [](const QNetworkRequest &request) -> TestNetwork::Response {
            if (request.url().host() == "freedictionaryapi.com") return {{}, 404};
            const auto edition = request.url().path().startsWith("/ruwiktionary/") ? QString("ru") : QString("de");
            if (request.url().path().endsWith(".json")) return {kaikki_index("Haus", edition)};
            return {kaikki_entry("Haus", edition)};
        };
        showthis::DictionaryLookup lookup(&network);
        QSignalSpy finished(&lookup, &showthis::DictionaryLookup::finished);
        lookup.lookup("Haus", "de", {{{"freedictionary", "en"}, {"kaikki", "ru", true}, {"kaikki", "de", true}}});
        QCOMPARE(network.requests.size(), 1);
        network.replies[0]->complete();
        QCOMPARE(network.requests.size(), 3);
        QVERIFY(finished.isEmpty());
        for (int i = 1; i < network.replies.size(); ++i)
            network.replies[i]->complete();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(senses(finished).size(), 2);
    }

    void source_language_pair_limit() {
        class ManyLanguagesProvider : public SampleDictionaryProvider {
        public:
            QVariantList definition_languages() const override {
                QVariantList result;
                for (int i = 0; i < 40; ++i)
                    result.append(QVariantMap{{"code", QString("lang%1").arg(i)}, {"name", QString("Language %1").arg(i)}});
                return result;
            }
        };
        showthis::DictionaryProviders providers;
        providers.push_back(std::make_unique<ManyLanguagesProvider>());
        TestNetwork network;
        showthis::DictionaryLookup lookup(&network, nullptr, std::move(providers));
        QStringList languages;
        for (int i = 0; i < 33; ++i)
            languages.append(QString("lang%1").arg(i));
        showthis::DictionaryOptions options;
        QVariantList normalized;
        QString error;
        QVERIFY(!lookup.configure({dictionary_selection("sample", languages)}, true, options, normalized, error));
        QVERIFY(error.contains("32"));
        languages.removeLast();
        QVERIFY(lookup.configure({dictionary_selection("sample", languages)}, true, options, normalized, error));
        QCOMPARE(options.targets.size(), 32);
    }
};

QTEST_GUILESS_MAIN(DictionaryLookupTest)
#include "DictionaryLookupTest.moc"
