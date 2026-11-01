#include <QJsonArray>
#include <QtTest>

#include "AnkiExporter.h"
#include "NotebookData.h"

using namespace Qt::Literals::StringLiterals;

class NotebookDataTest : public QObject {
    Q_OBJECT

private slots:
    void word_prefix_data() {
        QTest::addColumn<QString>("word");
        QTest::addColumn<QString>("prefix");
        QTest::addColumn<QString>("expected");
        QTest::newRow("none") << u"Haus"_s << u""_s << u"Haus"_s;
        QTest::newRow("space") << u"Haus"_s << u"das"_s << u"das Haus"_s;
        QTest::newRow("existing") << u"Das Haus"_s << u"das"_s << u"Das Haus"_s;
        QTest::newRow("apostrophe") << u"ami"_s << u"l'"_s << u"l'ami"_s;
        QTest::newRow("curly-apostrophe") << u"ami"_s << u"l’"_s << u"l’ami"_s;
        QTest::newRow("hyphen") << u"word"_s << u"pre-"_s << u"pre-word"_s;
        QTest::newRow("existing-hyphen") << u"pre-word"_s << u"pre-"_s << u"pre-word"_s;
        QTest::newRow("trimmed") << u"Haus"_s << u" das "_s << u"das Haus"_s;
        QTest::newRow("whitespace") << u"Haus"_s << u"   "_s << u"Haus"_s;
    }
    void word_prefix() {
        QFETCH(QString, word);
        QFETCH(QString, prefix);
        QFETCH(QString, expected);
        QCOMPARE(showthis::word_with_article(word, prefix), expected);
    }
    void language_codes() {
        QCOMPARE(showthis::normalized_language_code(" FRA "), QString("fr"));
        QCOMPARE(showthis::normalized_language_code("pt_BR"), QString("pt-br"));
        QVERIFY(showthis::normalized_language_code("../invalid").isEmpty());
    }
    void pure_anki_formatting() {
        const QVariantMap card{{"word", "<word>"}, {"article", "the"}, {"language", "en"},
            {"context", "line\nnext\tcell"}, {"meaning", "meaning & more"}, {"notes", "\"quoted\""}};
        const auto record = showthis::AnkiExporter::format_card(card);
        QVERIFY(record.contains("<b>the &lt;word&gt;</b>"));
        QVERIFY(record.contains("line<br>next&#9;cell"));
        QVERIFY(record.contains("meaning &amp; more"));
        QVERIFY(record.contains("&quot;quoted&quot;"));
        QCOMPARE(record.count('\t'), 2);
        QCOMPARE(record.count('\n'), 1);
    }
    void sense_limit_and_inheritance() {
        QJsonArray children;
        for (int i = 0; i < 110; ++i)
            children.append(QJsonObject{{"definition", QString("meaning %1").arg(i)}});
        const QJsonObject sense{{"tags", QJsonArray{"neuter"}}, {"subsenses", children}};
        const QJsonObject entry{{"language", QJsonObject{{"code", "de"}}},
            {"partOfSpeech", "noun"}, {"senses", QJsonArray{sense}}};
        const QJsonObject response{{"entries", QJsonArray{entry}}};
        const auto senses = showthis::dictionary_senses(response, "de");
        QCOMPARE(senses.size(), 100);
        QCOMPARE(senses[0].toMap().value("tags").toStringList(), QStringList{"neuter"});
        QVERIFY(senses[0].toMap().value("article").toString().isEmpty());
        QVERIFY(showthis::dictionary_senses(response, "en").isEmpty());
    }
};

QTEST_GUILESS_MAIN(NotebookDataTest)
#include "NotebookDataTest.moc"
