#include <QJsonArray>
#include <QtTest>

#include "AnkiExporter.h"
#include "NotebookData.h"
#include "DictionaryData.h"
#include "DictionaryFixtures.h"
#include "GrammarProfile.h"

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
        QCOMPARE(showthis::dictionary_language_code("pt_BR"), QString("pt"));
        QCOMPARE(showthis::dictionary_language_code(" DEU-DE "), QString("de"));
        QVERIFY(showthis::dictionary_language_code("../invalid").isEmpty());
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

    void base_plural_and_explicit_article_from_kaikki() {
        QString error;
        const auto document = showthis::kaikki_dictionary_document(
            kaikki_entry("Haus", "house", "de", {}, house_forms()), "Haus", "de", "de", error);
        QVERIFY(error.isEmpty());
        const auto sense = showthis::dictionary_senses(document, "de")[0].toMap();
        QCOMPARE(sense.value("base_form").toString(), QString("Haus"));
        QCOMPARE(sense.value("article").toString(), QString("das"));
        QCOMPARE(sense.value("base_article").toString(), QString("das"));
        QCOMPARE(sense.value("plural_forms").toStringList(), QStringList{QString::fromUtf8("Häuser")});
    }

    void free_dictionary_plural_and_missing_article() {
        auto raw = QJsonDocument::fromJson(free_entry("Haus", "house")).object();
        auto entry = raw.value("entries").toArray()[0].toObject();
        entry.insert("forms", QJsonArray{
            QJsonObject{{"word", "Hauses"}, {"tags", QJsonArray{"genitive"}}},
            QJsonObject{{"word", "Häuser"}, {"tags", QJsonArray{"plural"}}},
            QJsonObject{{"word", "Häuser"}, {"tags", QJsonArray{"nominative", "plural"}}},
            QJsonObject{{"word", "Häusern"}, {"tags", QJsonArray{"dative", "plural"}}},
            QJsonObject{{"word", "Häuschen"}, {"tags", QJsonArray{"plural", "diminutive"}}}});
        raw.insert("entries", QJsonArray{entry});
        const auto document = showthis::free_dictionary_document(raw, "Haus", "de");
        const auto sense = showthis::dictionary_senses(document, "de")[0].toMap();
        QCOMPARE(sense.value("plural_forms").toStringList(), QStringList{QString::fromUtf8("Häuser")});
        QVERIFY(sense.value("base_article").toString().isEmpty());
        QVERIFY(sense.value("article").toString().isEmpty());
    }

    void free_dictionary_literal_form_targets_data() {
        QTest::addColumn<QString>("word");
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("definition");
        QTest::addColumn<QStringList>("targets");
        // Published FreeDictionary senses, without structured form_of links.
        QTest::newRow("russian-genitive") << u"дома"_s << u"ru"_s
            << u"genitive singular of дом (dom)"_s << QStringList{u"дом"_s};
        QTest::newRow("russian-plural") << u"дома"_s << u"ru"_s
            << u"nominative/accusative plural of дом (dom)"_s << QStringList{u"дом"_s};
        QTest::newRow("better-multiple-targets") << u"better"_s << u"en"_s
            << u"comparative degree of good and well"_s << QStringList{};
        QTest::newRow("better-explanation") << u"better"_s << u"en"_s
            << u"comparative form of well: more well"_s << QStringList{u"well"_s};
        QTest::newRow("literal-multiword") << u"attorneys general"_s << u"en"_s
            << u"plural of attorney general"_s << QStringList{u"attorney general"_s};
        QTest::newRow("literal-hyphenated") << u"mothers-in-law"_s << u"en"_s
            << u"plural of mother-in-law."_s << QStringList{u"mother-in-law"_s};
        QTest::newRow("literal-multiword-with-of") << u"Secretaries of State"_s << u"en"_s
            << u"plural of Secretary of State"_s << QStringList{u"Secretary of State"_s};
        QTest::newRow("non-latin-multiword") << u"белого дома"_s << u"ru"_s
            << u"genitive singular of белый дом (belyj dom)"_s << QStringList{u"белый дом"_s};
        QTest::newRow("ambiguous-conjunction") << u"forms"_s << u"en"_s
            << u"plural of rock and roll"_s << QStringList{};
        QTest::newRow("ambiguous-disjunction") << u"forms"_s << u"en"_s
            << u"plural of house or home"_s << QStringList{};
        QTest::newRow("ambiguous-comma") << u"forms"_s << u"en"_s
            << u"plural of house, home"_s << QStringList{};
        QTest::newRow("ambiguous-slash") << u"forms"_s << u"en"_s
            << u"plural of house/home"_s << QStringList{};
        QTest::newRow("ambiguous-colon") << u"forms"_s << u"en"_s
            << u"plural of house:home"_s << QStringList{};
        QTest::newRow("ambiguous-nested-colon") << u"forms"_s << u"en"_s
            << u"plural of house: home: building"_s << QStringList{};
        QTest::newRow("empty-colon-explanation") << u"forms"_s << u"en"_s
            << u"plural of house: "_s << QStringList{};
        QTest::newRow("latin-parenthetical") << u"forms"_s << u"en"_s
            << u"plural of house (building)"_s << QStringList{};
        QTest::newRow("mixed-script-parenthetical") << u"forms"_s << u"ru"_s
            << u"genitive singular of дом (dom дом)"_s << QStringList{};
        QTest::newRow("parenthetical-commentary") << u"forms"_s << u"ru"_s
            << u"genitive singular of дом (house, building)"_s << QStringList{};
        QTest::newRow("non-latin-parenthetical") << u"forms"_s << u"ru"_s
            << u"genitive singular of дом (дом)"_s << QStringList{};
    }

    void free_dictionary_literal_form_targets() {
        QFETCH(QString, word);
        QFETCH(QString, language);
        QFETCH(QString, definition);
        QFETCH(QStringList, targets);
        const QJsonObject raw{{"word", word}, {"entries", QJsonArray{
            QJsonObject{{"language", QJsonObject{{"code", language}}}, {"partOfSpeech", "noun"},
                {"senses", QJsonArray{QJsonObject{{"definition", definition},
                    {"tags", QJsonArray{"form of"}}}}}}}}};
        const auto document = showthis::free_dictionary_document(raw, word, language);
        QCOMPARE(showthis::dictionary_form_targets(document, language), targets);
        const auto senses = showthis::dictionary_senses(document, language);
        QCOMPARE(senses.size(), 1);
        const auto sense = senses[0].toMap();
        QCOMPARE(sense.value("definition").toString(), definition);
        QCOMPARE(sense.value("base_form").toString(), targets.isEmpty() ? QString{} : targets.first());
        QVERIFY(!showthis::has_dictionary_meanings(document, language));
        auto ordinary = raw;
        auto entry = ordinary.value("entries").toArray()[0].toObject();
        entry.insert("senses", QJsonArray{QJsonObject{{"definition", definition}}});
        ordinary.insert("entries", QJsonArray{entry});
        QVERIFY(showthis::dictionary_form_targets(
            showthis::free_dictionary_document(ordinary, word, language), language).isEmpty());
    }

    void free_dictionary_structured_form_targets_are_authoritative() {
        const QStringList targets{u"rock and roll"_s, u"AC/DC"_s, u"дом (dom)"_s, u"well: more well"_s};
        QJsonArray links;
        for (const auto &target : targets)
            links.append(QJsonObject{{"word", target}});
        for (const auto &key : {"form_of", "formOf"}) {
            const QJsonObject sense{{"definition", "comparative degree of good and well"},
                {"tags", QJsonArray{"form of"}}, {key, links}};
            const QJsonObject raw{{"word", "observed"}, {"entries", QJsonArray{
                QJsonObject{{"language", QJsonObject{{"code", "en"}}}, {"partOfSpeech", "noun"},
                    {"senses", QJsonArray{sense}}}}}};
            const auto document = showthis::free_dictionary_document(raw, "observed", "en");
            QCOMPARE(showthis::dictionary_form_targets(document, "en"), targets);
            const auto normalized = showthis::dictionary_senses(document, "en")[0].toMap();
            QCOMPARE(normalized.value("definition").toString(), sense.value("definition").toString());
            QVERIFY(normalized.value("base_form").toString().isEmpty());
        }
    }

    void finnish_missing_case_forms_preserve_numbers() {
        // Reduced FreeDictionary talo table: these singular cases have no
        // form, but the nominative singular and plural are both supplied.
        const QJsonArray forms{
            QJsonObject{{"word", "talo"}, {"tags", QJsonArray{"nominative", "singular"}}},
            QJsonObject{{"word", "talot"}, {"tags", QJsonArray{"nominative", "plural"}}},
            QJsonObject{{"word", "-"}, {"tags", QJsonArray{"instructive", "singular"}}},
            QJsonObject{{"word", "-"}, {"tags", QJsonArray{"comitative", "singular"}}}};
        const QJsonObject entry{{"language", QJsonObject{{"code", "fi"}}}, {"partOfSpeech", "noun"},
            {"forms", forms}, {"senses", QJsonArray{QJsonObject{{"definition", "house"}}}}};
        const QJsonObject raw{{"word", "talo"}, {"entries", QJsonArray{entry}}};
        const auto document = showthis::free_dictionary_document(raw, "talo", "fi");
        const auto sense = showthis::dictionary_senses(document, "fi")[0].toMap();
        QCOMPARE(sense.value("singular_status").toString(), QString("available"));
        QCOMPARE(sense.value("plural_status").toString(), QString("available"));
        QCOMPARE(sense.value("singular_forms").toStringList(), QStringList{"talo"});
        QCOMPARE(sense.value("plural_forms").toStringList(), QStringList{"talot"});
        QVERIFY(sense.value("unavailable_numbers").toStringList().isEmpty());
        QCOMPARE(sense.value("forms").toList(), forms.toVariantList());
        QVERIFY(showthis::number_availability_text(sense).isEmpty());
        QVERIFY(!showthis::AnkiExporter::format_card(sense).contains("No singular"));
    }

    void missing_number_markers_require_unqualified_evidence_data() {
        QTest::addColumn<QStringList>("missing_tags");
        QTest::addColumn<QString>("marker");
        QTest::addColumn<QString>("unavailable");
        QTest::newRow("instructive") << QStringList{"instructive", "singular"} << QString("-") << QString{};
        QTest::newRow("comitative") << QStringList{"comitative", "singular"} << QString("-") << QString{};
        QTest::newRow("nominative") << QStringList{"nominative", "singular"} << QString("-") << QString{};
        QTest::newRow("unlisted-case") << QStringList{"adessive", "plural"} << QString("-") << QString{};
        QTest::newRow("possessive") << QStringList{"singular", "possessive"} << QString("-") << QString{};
        QTest::newRow("qualified-frequency") << QStringList{"plural", "usually"} << QString("-") << QString{};
        QTest::newRow("unqualified-singular") << QStringList{"singular"} << QString("-") << QString("singular");
        QTest::newRow("unqualified-plural") << QStringList{"plural"} << QString("-") << QString("plural");
        QTest::newRow("unqualified-em-dash") << QStringList{"plural"} << u"—"_s << QString("plural");
        QTest::newRow("unqualified-en-dash") << QStringList{"plural"} << u"–"_s << QString("plural");
        QTest::newRow("empty-not-evidence") << QStringList{"plural"} << QString{} << QString{};
        QTest::newRow("untagged-not-evidence") << QStringList{} << QString("-") << QString{};
    }

    void missing_number_markers_require_unqualified_evidence() {
        QFETCH(QStringList, missing_tags);
        QFETCH(QString, marker);
        QFETCH(QString, unavailable);
        const auto headword = unavailable == "singular" ? QString("talot") : QString("talo");
        const QJsonArray forms{
            QJsonObject{{"word", "talo"}, {"tags", QJsonArray{"nominative", "singular"}}},
            QJsonObject{{"word", "talot"}, {"tags", QJsonArray{"nominative", "plural"}}},
            QJsonObject{{"word", marker}, {"tags", QJsonArray::fromStringList(missing_tags)}}};
        for (const auto &provider : {"freedictionary", "kaikki"}) {
            QJsonObject document;
            if (QString::fromLatin1(provider) == "freedictionary") {
                const QJsonObject entry{{"language", QJsonObject{{"code", "fi"}}}, {"partOfSpeech", "noun"},
                    {"forms", forms}, {"senses", QJsonArray{QJsonObject{{"definition", "house"}}}}};
                const QJsonObject raw{{"word", headword}, {"entries", QJsonArray{entry}}};
                document = showthis::free_dictionary_document(raw, headword, "fi");
            } else {
                QJsonArray kaikki_forms;
                for (const auto &value : forms) {
                    auto form = value.toObject();
                    form.insert("form", form.take("word"));
                    kaikki_forms.append(form);
                }
                QString error;
                document = showthis::kaikki_dictionary_document(
                    kaikki_entry(headword, "house", "fi", {}, kaikki_forms), headword, "fi", "en", error);
                QVERIFY(error.isEmpty());
            }
            const auto sense = showthis::dictionary_senses(document, "fi")[0].toMap();
            QCOMPARE(sense.value("unavailable_numbers").toStringList(),
                unavailable.isEmpty() ? QStringList{} : QStringList{unavailable});
            for (const auto &number : {"singular", "plural"}) {
                const auto prefix = QString::fromLatin1(number);
                const bool restricted = unavailable == prefix;
                QCOMPARE(sense.value(prefix + "_status").toString(),
                    restricted ? QString("unavailable") : QString("available"));
                QCOMPARE(sense.value(prefix + "_forms").toStringList(),
                    restricted ? QStringList{} : QStringList{prefix == "singular" ? "talo" : "talot"});
            }
        }
    }

    void inflection_uses_linked_base_metadata_not_oblique_article() {
        QString error;
        auto base = showthis::kaikki_dictionary_document(kaikki_entry("Haus", "house", "de", {}, house_forms()),
            "Haus", "de", "de", error);
        const auto inflection = showthis::kaikki_dictionary_document(kaikki_entry("Häusern", "dative plural", "de", "Haus"),
            "Häusern", "de", "de", error);
        auto entries = inflection.value("entries").toArray();
        entries.append(base.value("entries").toArray()[0]);
        const auto senses = showthis::dictionary_senses(QJsonObject{{"entries", entries}}, "de");
        const auto sense = senses[0].toMap();
        QCOMPARE(sense.value("base_form").toString(), QString("Haus"));
        QCOMPARE(sense.value("base_article").toString(), QString("das"));
        QCOMPARE(sense.value("plural_forms").toStringList(), QStringList{QString::fromUtf8("Häuser")});
        QVERIFY(sense.value("article").toString().isEmpty()); // Do not prefix Häusern with "das".
        auto other = entries[1].toObject();
        other.insert("definition_language", "en");
        entries[1] = other;
        const auto isolated = showthis::dictionary_senses(QJsonObject{{"entries", entries}}, "de")[0].toMap();
        QCOMPARE(isolated.value("base_article").toString(), QString("das"));
        QCOMPARE(isolated.value("base_article_origin").toString(), QString("profile"));
    }

    void inflection_number_restrictions_survive_enrichment_data() {
        QTest::addColumn<QString>("number");
        QTest::addColumn<QStringList>("tags");
        QTest::addColumn<bool>("explicit_missing_form");
        QTest::addColumn<bool>("restricted");
        QTest::newRow("no-plural") << QString("plural") << QStringList{"uncountable"} << false << true;
        QTest::newRow("no-singular") << QString("singular") << QStringList{"plural-only"} << false << true;
        QTest::newRow("missing-plural") << QString("plural") << QStringList{} << true << true;
        QTest::newRow("missing-singular") << QString("singular") << QStringList{} << true << true;
        QTest::newRow("qualified-restriction") << QString("plural") << QStringList{"uncountable", "usually"} << false << false;
        QTest::newRow("unknown-is-enriched") << QString("singular") << QStringList{} << false << false;
    }

    void inflection_number_restrictions_survive_enrichment() {
        QFETCH(QString, number);
        QFETCH(QStringList, tags);
        QFETCH(bool, explicit_missing_form);
        QFETCH(bool, restricted);
        tags.append("form-of");
        QJsonObject inflection{{"headword", "variant"}, {"language", QJsonObject{{"code", "en"}}},
            {"provider", "sample"}, {"definition_language", "en"}, {"partOfSpeech", "noun"},
            {"senses", QJsonArray{QJsonObject{{"definition", "variant of lemma"},
                {"tags", QJsonArray::fromStringList(tags)},
                {"form_of", QJsonArray{QJsonObject{{"word", "lemma"}}}}}}}};
        if (explicit_missing_form)
            inflection.insert("forms", QJsonArray{QJsonObject{{"word", "—"}, {"tags", QJsonArray{number}}}});
        const QJsonObject base{{"headword", "lemma"}, {"language", QJsonObject{{"code", "en"}}},
            {"provider", "sample"}, {"definition_language", "en"}, {"partOfSpeech", "noun"},
            {"forms", QJsonArray{
                QJsonObject{{"word", "lemma"}, {"tags", QJsonArray{"singular"}}},
                QJsonObject{{"word", "lemmas"}, {"tags", QJsonArray{"plural"}}}}},
            {"senses", QJsonArray{QJsonObject{{"definition", "a countable sense"}}}}};
        const auto senses = showthis::dictionary_senses(QJsonObject{{"entries", QJsonArray{inflection, base}}}, "en");
        QCOMPARE(senses.size(), 2);
        const auto sense = senses[0].toMap();
        QCOMPARE(sense.value(number + "_status").toString(), restricted ? QString("unavailable") : QString("available"));
        QCOMPARE(sense.value(number + "_forms").toStringList().isEmpty(), restricted);
        const auto other = number == "plural" ? QString("singular") : QString("plural");
        QCOMPARE(sense.value(other + "_status").toString(), QString("available"));
        QVERIFY(!sense.value(other + "_forms").toStringList().isEmpty());
        if (restricted)
            QVERIFY(showthis::AnkiExporter::format_card(sense).contains(number == "plural" ? "No plural" : "No singular"));
    }

    void ambiguous_articles_and_missing_forms_are_not_guessed() {
        const QJsonObject entry{{"headword", "word"}, {"language", QJsonObject{{"code", "de"}}},
            {"article", "a"}, {"senses", QJsonArray{
                QJsonObject{{"definition", "first"}, {"articles", QJsonArray{"one", "two"}}},
                QJsonObject{{"definition", "second"}, {"prefix", "pre-"}}}}};
        const auto senses = showthis::dictionary_senses(QJsonObject{{"entries", QJsonArray{entry}}}, "de");
        QVERIFY(senses[0].toMap().value("article").toString().isEmpty());
        QVERIFY(senses[0].toMap().value("base_article").toString().isEmpty());
        QVERIFY(senses[0].toMap().value("plural_forms").toStringList().isEmpty());
        QCOMPARE(senses[1].toMap().value("base_article").toString(), QString("pre-"));
        const QVariantMap card{{"word", "word"}, {"language", "en"}, {"base_form", "<base>"}, {"base_article", "the"},
            {"plural_forms", QStringList{"<plural>", "plural & two"}}};
        const auto exported = showthis::AnkiExporter::format_card(card);
        QVERIFY(exported.contains("Base form: the &lt;base&gt;"));
        QVERIFY(exported.contains("Plural: &lt;plural&gt; / plural &amp; two"));
    }

    void language_profiles_and_number_availability_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QStringList>("tags");
        QTest::addColumn<QString>("article");
        QTest::addColumn<QString>("singular_status");
        QTest::addColumn<QString>("plural_status");
        QTest::newRow("german-masculine") << QString("de") << QStringList{"masculine"} << QString("der") << QString("available") << QString("unknown");
        QTest::newRow("german-feminine") << QString("de") << QStringList{"feminine"} << QString("die") << QString("available") << QString("unknown");
        QTest::newRow("german-neuter") << QString("de") << QStringList{"neuter"} << QString("das") << QString("available") << QString("unknown");
        QTest::newRow("german-ambiguous") << QString("de") << QStringList{"masculine", "feminine"} << QString{} << QString("available") << QString("unknown");
        QTest::newRow("german-unknown") << QString("de") << QStringList{} << QString{} << QString("available") << QString("unknown");
        QTest::newRow("german-plural-only") << QString("de") << QStringList{"plural", "plural only"} << QString("die") << QString("unavailable") << QString("available");
        QTest::newRow("german-singular-only") << QString("de") << QStringList{"neuter", "singular-only"} << QString("das") << QString("available") << QString("unavailable");
        QTest::newRow("english-mass") << QString("en") << QStringList{"uncountable"} << QString{} << QString("available") << QString("unavailable");
        QTest::newRow("english-count-and-mass") << QString("en") << QStringList{"countable", "uncountable"} << QString{} << QString("available") << QString("unknown");
        QTest::newRow("english-plural-only") << QString("en") << QStringList{"plural-only"} << QString{} << QString("unavailable") << QString("available");
        QTest::newRow("english-usually-plural") << QString("en") << QStringList{"plural", "plural only", "usually"} << QString{} << QString("unknown") << QString("available");
        QTest::newRow("english-no-gender-article") << QString("en") << QStringList{"neuter"} << QString{} << QString("available") << QString("unknown");
        QTest::newRow("generic-no-language-guess") << QString("fr") << QStringList{"feminine"} << QString{} << QString("unknown") << QString("unknown");
    }

    void language_profiles_and_number_availability() {
        QFETCH(QString, language);
        QFETCH(QStringList, tags);
        QFETCH(QString, article);
        QFETCH(QString, singular_status);
        QFETCH(QString, plural_status);
        // Reader/definition languages never select the grammatical profile.
        for (const auto &edition : {"ru", "uk", "en"}) {
            const QJsonObject entry{{"headword", "word"}, {"language", QJsonObject{{"code", language}}},
                {"definition_language", edition}, {"partOfSpeech", "noun"},
                {"senses", QJsonArray{QJsonObject{{"definition", "meaning"}, {"tags", QJsonArray::fromStringList(tags)}}}}};
            const auto sense = showthis::dictionary_senses(QJsonObject{{"entries", QJsonArray{entry}}}, language)[0].toMap();
            QCOMPARE(sense.value("article").toString(), article);
            QCOMPARE(sense.value("singular_status").toString(), singular_status);
            QCOMPARE(sense.value("plural_status").toString(), plural_status);
            if (!article.isEmpty())
                QCOMPARE(sense.value("article_origin").toString(), QString("profile"));
        }
    }

    void profile_extension_and_explicit_article_priority() {
        auto profiles = showthis::default_grammar_profiles();
        auto languages = profiles.value("languages").toObject();
        languages.insert("xx", QJsonObject{{"noun", QJsonObject{{"lemma_is_singular", true},
            {"articles", QJsonObject{{"singular", QJsonObject{{"common", "ze"}}}}}}}});
        profiles.insert("languages", languages);
        QVariantMap sense{{"headword", "word"}, {"base_form", "word"}, {"part_of_speech", "noun"}, {"tags", QStringList{"common"}}};
        showthis::apply_grammar_profile(sense, "xx", profiles);
        QCOMPARE(sense.value("article").toString(), QString("ze"));
        QCOMPARE(sense.value("grammar_profile").toString(), QString("xx"));
        sense.insert("article", "explicit");
        sense.insert("articles", QStringList{"explicit"});
        sense.insert("base_article", "explicit");
        sense.insert("article_origin", "dictionary");
        sense.insert("base_article_origin", "dictionary");
        showthis::apply_grammar_profile(sense, "xx", profiles);
        QCOMPARE(sense.value("article").toString(), QString("explicit"));
        QCOMPARE(sense.value("base_article_origin").toString(), QString("dictionary"));
    }

    void explicitly_missing_number_forms_and_export() {
        const QJsonObject entry{{"headword", "word"}, {"language", QJsonObject{{"code", "en"}}}, {"partOfSpeech", "noun"},
            {"forms", QJsonArray{QJsonObject{{"word", "—"}, {"tags", QJsonArray{"plural"}}}}},
            {"senses", QJsonArray{QJsonObject{{"definition", "meaning"}}}}};
        auto sense = showthis::dictionary_senses(QJsonObject{{"entries", QJsonArray{entry}}}, "en")[0].toMap();
        QVERIFY(sense.value("plural_forms").toStringList().isEmpty());
        QCOMPARE(sense.value("plural_status").toString(), QString("unavailable"));
        QCOMPARE(showthis::number_availability_text(sense), QString("No plural"));
        sense.insert("word", "word");
        sense.insert("language", "en");
        QVERIFY(showthis::AnkiExporter::format_card(sense).contains("No plural"));
        sense.insert("plural_status", "unknown");
        QCOMPARE(showthis::number_availability_text(sense), QString("Plural not listed"));
        sense.insert("singular_status", "unavailable");
        QVERIFY(showthis::AnkiExporter::format_card(sense).contains("No singular"));
    }

    void number_evidence_and_part_of_speech() {
        QVariantMap noun{{"headword", "word"}, {"base_form", "word"}, {"part_of_speech", "noun"},
            {"tags", QStringList{"uncountable"}}, {"plural_forms", QStringList{"words"}}};
        showthis::apply_grammar_profile(noun, "en");
        QCOMPARE(noun.value("plural_status").toString(), QString("unavailable"));
        QVERIFY(noun.value("plural_forms").toStringList().isEmpty());
        QCOMPARE(showthis::number_availability_text(noun), QString("No plural"));
        noun.insert("tags", QStringList{"countable", "uncountable"});
        noun.insert("plural_forms", QStringList{"words"});
        showthis::apply_grammar_profile(noun, "en");
        QCOMPARE(noun.value("plural_status").toString(), QString("available"));
        QVariantMap verb{{"headword", "run"}, {"base_form", "run"}, {"part_of_speech", "verb"},
            {"tags", QStringList{"plural"}}, {"singular_forms", QStringList{"runs"}}, {"plural_forms", QStringList{"run"}}};
        showthis::apply_grammar_profile(verb, "en");
        QVERIFY(!verb.value("number_relevant").toBool());
        QVERIFY(verb.value("singular_forms").toStringList().isEmpty());
        QCOMPARE(verb.value("plural_status").toString(), QString("not_applicable"));
        QVERIFY(showthis::number_availability_text(verb).isEmpty());
    }
};

QTEST_GUILESS_MAIN(NotebookDataTest)
#include "NotebookDataTest.moc"
