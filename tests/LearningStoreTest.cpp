#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include "LearningStore.h"

class LearningStoreTest : public QObject {
    Q_OBJECT

private slots:
    void unreadable_store_is_never_overwritten_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("invalid-json") << QByteArray("broken JSON");
        QTest::newRow("non-object") << QByteArray("[]");
        QTest::newRow("unsupported-version") << QByteArray(R"({"version":2,"cards":[],"dictionary_cache":{}})");
        QTest::newRow("invalid-cards") << QByteArray(R"({"version":1,"cards":{},"dictionary_cache":{}})");
        QTest::newRow("invalid-cache") << QByteArray(R"({"version":1,"cards":[],"dictionary_cache":[]})");
        QTest::newRow("invalid-history") << QByteArray(R"({"version":1,"cards":[],"dictionary_cache":{},"dictionary_history":{}})");
        const QJsonObject valid{{"id", "saved"}, {"word", "Haus"}};
        const auto add_cards = [](const char *name, const QJsonArray &cards) {
            QTest::newRow(name) << QJsonDocument(QJsonObject{{"version", 1}, {"cards", cards},
                {"dictionary_cache", QJsonObject{}}}).toJson();
        };
        add_cards("non-object-card", {42});
        add_cards("missing-id", {QJsonObject{{"word", "Haus"}}});
        add_cards("blank-id", {QJsonObject{{"id", " "}, {"word", "Haus"}}});
        add_cards("missing-word", {QJsonObject{{"id", "saved"}}});
        add_cards("duplicate-ids", {valid, valid});
        for (const auto *field : {"word", "meaning", "translation", "language", "article", "plural_status"}) {
            auto card = valid;
            card.insert(field, 42);
            add_cards(qPrintable(QString("invalid-%1").arg(field)), {card});
        }
        add_cards("invalid-plural-list", {QJsonObject{{"id", "saved"}, {"word", "Haus"}, {"plural_forms", "Häuser"}}});
        add_cards("invalid-plural-item", {QJsonObject{{"id", "saved"}, {"word", "Haus"}, {"plural_forms", QJsonArray{42}}}});
        add_cards("invalid-sense", {QJsonObject{{"id", "saved"}, {"word", "Haus"}, {"sense", QJsonArray{}}}});
        add_cards("invalid-source", {QJsonObject{{"id", "saved"}, {"word", "Haus"}, {"source", "source"}}});
        add_cards("invalid-number-flag", {QJsonObject{{"id", "saved"}, {"word", "Haus"}, {"number_relevant", "yes"}}});
    }

    void unreadable_store_is_never_overwritten() {
        QFETCH(QByteArray, bytes);
        QTemporaryDir directory;
        QFile file(directory.filePath("words.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        showthis::LearningStore store(file.fileName());
        store.load(1000);
        QVERIFY(!store.error().isEmpty());
        QVERIFY(!store.save({}, {}, {}));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
    }

    void history_migration_retains_metadata() {
        QTemporaryDir directory;
        QFile file(directory.filePath("words.json"));
        const QJsonObject card{{"id", "legacy"}, {"word", "Haus"}, {"custom", "keep"},
            {"sense", QJsonObject{{"article", "das"}}}};
        const QJsonObject cache{{"de:Haus", QJsonObject{}}, {"de:Baum", QJsonObject{}}};
        const QJsonArray history{QJsonObject{{"key", "de:Haus"}, {"custom", "history metadata"}},
            QJsonObject{{"key", "de:Haus"}}, QJsonObject{{"key", "missing"}}};
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"cards", QJsonArray{card}},
            {"dictionary_cache", cache}, {"dictionary_history", history}, {"custom", "root metadata"}}).toJson());
        file.close();

        showthis::LearningStore store(file.fileName());
        const auto contents = store.load(1);
        QVERIFY(store.error().isEmpty());
        QCOMPARE(contents.history.size(), 1);
        QCOMPARE(contents.history[0].toMap().value("key").toString(), QString("de:Haus"));
        QCOMPARE(contents.history[0].toMap().value("custom").toString(), QString("history metadata"));
        QCOMPARE(contents.cache.size(), 1);
        QCOMPARE(contents.cards[0].toMap().value("article").toString(), QString("das"));
        QCOMPARE(contents.cards[0].toMap().value("custom").toString(), QString("keep"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto saved = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(saved.value("custom").toString(), QString("root metadata"));
        QCOMPARE(saved.value("dictionary_cache").toObject().size(), 1);
    }

    void invalid_save_preserves_previous_notebook() {
        QTemporaryDir directory;
        const auto path = directory.filePath("words.json");
        showthis::LearningStore store(path);
        const QVariantMap card{{"id", "saved"}, {"word", "Haus"}, {"custom", QVariantList{42, "keep"}}};
        QVERIFY(store.save({card}, {}, {}));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto original = file.readAll();
        file.close();
        QVERIFY(!store.save({card, card}, {}, {}));
        QVERIFY(!store.error().isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
        file.close();
        QVERIFY(store.save({card}, {}, {}));
        QVERIFY(store.error().isEmpty());
        const auto loaded = store.load(1000);
        QCOMPARE(loaded.cards[0].toMap().value("custom").toList(), card.value("custom").toList());
    }

    void legacy_null_grammar_profile_is_accepted() {
        QTemporaryDir directory;
        QFile file(directory.filePath("words.json"));
        const QJsonObject card{{"id", "legacy"}, {"word", "Haus"}, {"grammar_profile", QJsonValue::Null},
            {"custom", QJsonObject{{"preserved", true}}}};
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"cards", QJsonArray{card}},
            {"dictionary_cache", QJsonObject{}}}).toJson());
        file.close();
        showthis::LearningStore store(file.fileName());
        const auto contents = store.load(1000);
        QVERIFY(store.error().isEmpty());
        QCOMPARE(contents.cards.size(), 1);
        QVERIFY(store.save(contents.cards, contents.cache, contents.history));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto saved = QJsonDocument::fromJson(file.readAll()).object().value("cards").toArray()[0].toObject();
        QVERIFY(saved.value("grammar_profile").isNull());
        QCOMPARE(saved.value("custom"), card.value("custom"));
    }
};

QTEST_GUILESS_MAIN(LearningStoreTest)
#include "LearningStoreTest.moc"
