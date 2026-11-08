#include <QQmlEngine>
#include <QApplication>
#include <QFileDialog>
#include <QLineEdit>
#include <QPalette>
#include <QQuickItem>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>
#include <QSemaphore>
#include <atomic>
#include <stdexcept>

#include "AppSettings.h"
#include "ShoWSurface.h"
#include "LearningService.h"
#include "OverlayController.h"
#include "AScreenCaptureBackend.h"
#include "TestNetwork.h"
#include "DictionaryFixtures.h"
#include "OfflineDictionaryFixtures.h"

class TestCaptureBackend : public showthis::AScreenCaptureBackend {
public:
    int capture_count = 0;
    void capture() override { ++capture_count; }
    void complete(const QImage &image) { emit captured(image); }
    void fail(const QString &reason) { emit capture_failed(reason); }
};

static QQuickItem *find_item(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems()) {
        if (auto *item = find_item(child, name))
            return item;
    }
    return nullptr;
}

class OverlayTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings_directory;

private slots:
    void initTestCase() {
        QVERIFY(m_settings_directory.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings_directory.path());
    }

    void init() {
        QSettings{}.clear();
    }

    void word_card_context_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("translator_configured");
        QTest::newRow("desktop-dictionary-only") << QSize(1000, 700) << false;
        QTest::newRow("compact-dictionary-only") << QSize(640, 700) << false;
        QTest::newRow("desktop-with-translator") << QSize(1000, 700) << true;
        QTest::newRow("compact-with-translator") << QSize(640, 700) << true;
    }

    void word_card_context() {
        QFETCH(QSize, size);
        QFETCH(bool, translator_configured);
        QTemporaryDir directory;
        QFile fixture(QFINDTESTDATA("fixtures/dictionary-de-Haus.json"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto data = QJsonDocument::fromJson(fixture.readAll()).object();
        QFile store(directory.filePath(QStringLiteral("vocabulary.json")));
        QVERIFY(store.open(QIODevice::WriteOnly));
        store.write(QJsonDocument(QJsonObject{{"version", 1}, {"cards", QJsonArray{}},
            {"dictionary_cache", QJsonObject{{dictionary_cache_key("de", "Haus"), data}}}}).toJson());
        store.close();
        showthis::LearningService service(nullptr, store.fileName());
        service.set_source_language(QStringLiteral("de"));
        service.set_target_language(QStringLiteral("uk"));
        if (translator_configured) {
            service.translator()->set_provider_id(QStringLiteral("deepl"));
            service.translator()->set_option(QStringLiteral("api_key"), QStringLiteral("test-key"));
        }
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(size);
        QImage screenshot(size, QImage::Format_RGB32);
        screenshot.fill(Qt::white);
        surface.set_image(screenshot, {{QStringLiteral("Haus"), QRect(100, 200, 80, 30),
                                       QStringLiteral("Das ist mein Haus.")}});
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(140, 215));
        auto *panel = find_item(surface.contentItem(), QStringLiteral("learning_panel"));
        auto *word = find_item(surface.contentItem(), QStringLiteral("lookup_word"));
        auto *context = find_item(surface.contentItem(), QStringLiteral("word_context"));
        QVERIFY(panel);
        QVERIFY(word);
        QVERIFY(context);
        QTRY_VERIFY(panel->isVisible());
        QCOMPARE(word->property("text").toString(), QStringLiteral("Haus"));
        QCOMPARE(context->property("text").toString(), QStringLiteral("Das ist mein Haus."));
        auto *dictionary_button = find_item(surface.contentItem(), QStringLiteral("lookup_dictionary"));
        auto *senses = find_item(surface.contentItem(), QStringLiteral("dictionary_senses"));
        auto *save_button = find_item(surface.contentItem(), QStringLiteral("save_word"));
        auto *meaning = find_item(surface.contentItem(), QStringLiteral("word_meaning"));
        auto *translation = find_item(surface.contentItem(), QStringLiteral("word_translation"));
        QVERIFY(dictionary_button);
        QVERIFY(senses);
        QVERIFY(save_button);
        QVERIFY(meaning);
        QVERIFY(translation);
        QVERIFY(QMetaObject::invokeMethod(dictionary_button, "clicked"));
        QTRY_COMPARE(senses->property("count").toInt(), 2);
        QTest::qWait(30);
        auto *first_sense = find_item(surface.contentItem(), QStringLiteral("dictionary_sense_0"));
        QVERIFY(first_sense);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
            first_sense->mapToScene(QPointF(first_sense->width() / 2, first_sense->height() / 2)).toPoint());
        QTRY_COMPARE(meaning->property("text").toString(), QStringLiteral("house"));
        if (!panel->property("wide_layout").toBool())
            QCOMPARE(panel->property("compact_page").toInt(), 1);
        QCOMPARE(translation->property("text").toString(), QString::fromUtf8("будинок"));
        QVERIFY(translation->isVisible());
        QCOMPARE(panel->property("word_prefix").toString(), QString("das"));
        auto *article = find_item(surface.contentItem(), QStringLiteral("word_prefix"));
        auto *headword = find_item(surface.contentItem(), QStringLiteral("word_headword"));
        QVERIFY(article);
        QVERIFY(headword);
        article->setProperty("text", QStringLiteral("das"));
        QVERIFY(QMetaObject::invokeMethod(article, "textEdited"));
        QCOMPARE(headword->property("text").toString(), QStringLiteral("das Haus"));
        QVERIFY(QMetaObject::invokeMethod(save_button, "clicked"));
        QCOMPARE(service.vocabulary().size(), 1);
        QCOMPARE(service.vocabulary()[0].toMap().value("article").toString(), QStringLiteral("das"));
        QVERIFY(QMetaObject::invokeMethod(surface.rootObject(), "open_vocabulary"));
        QTest::qWait(30);
        auto *list = find_item(surface.contentItem(), QStringLiteral("vocabulary_list"));
        QVERIFY(list);
        QCOMPARE(list->property("count").toInt(), 1);
        QTRY_VERIFY(list->property("contentHeight").toReal() > 0);
        auto *export_button = find_item(surface.contentItem(), QStringLiteral("export_anki"));
        QVERIFY(export_button);
        QVERIFY(export_button->isEnabled());
        const auto card_id = service.vocabulary()[0].toMap().value("id").toString();
        auto *checkbox = find_item(surface.contentItem(), QStringLiteral("select_saved_") + card_id);
        QVERIFY(checkbox);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
            checkbox->mapToScene(QPointF(checkbox->width() / 2, checkbox->height() / 2)).toPoint());
        QVERIFY(export_button->property("text").toString().contains(QStringLiteral("selected")));
        QCOMPARE(service.dictionary_history().size(), 1);
        auto *tabs = find_item(surface.contentItem(), QStringLiteral("notebook_tabs"));
        QVERIFY(tabs);
        QCOMPARE(tabs->property("current_index").toInt(), 1);
        auto *actions = find_item(surface.contentItem(), QStringLiteral("row_actions_") + card_id);
        QVERIFY(actions);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
            actions->mapToScene(QPointF(actions->width() / 2, actions->height() / 2)).toPoint());
        auto *menu = actions->findChild<QObject *>(QStringLiteral("saved_actions_") + card_id);
        QVERIFY(menu);
        QTRY_VERIFY(menu->property("visible").toBool());
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!menu->property("visible").toBool());
        QVERIFY(panel->isVisible());
        tabs->setProperty("current_index", 2);
        auto *history = find_item(surface.contentItem(), QStringLiteral("dictionary_history"));
        QVERIFY(history);
        QTRY_COMPARE(history->property("count").toInt(), 1);
        auto *history_search = find_item(surface.contentItem(), QStringLiteral("history_search"));
        QVERIFY(history_search);
        history_search->setProperty("text", QStringLiteral("no matching word"));
        QTRY_COMPARE(history->property("count").toInt(), 0);
        history_search->setProperty("text", QStringLiteral("Haus"));
        QTRY_COMPARE(history->property("count").toInt(), 1);
        tabs->setProperty("current_index", 3);
        QTest::qWait(30);
        auto *limit = find_item(surface.contentItem(), QStringLiteral("history_limit"));
        auto *apply_limit = find_item(surface.contentItem(), QStringLiteral("apply_history_limit"));
        QVERIFY(limit);
        QVERIFY(apply_limit);
        limit->setProperty("value", 2);
        QVERIFY(QMetaObject::invokeMethod(apply_limit, "clicked"));
        QCOMPARE(service.history_limit(), 2);
        QCOMPARE(service.vocabulary().size(), 1);
        service.set_history_limit(1000);

        // Switching cards never silently discards an edited meaning.
        tabs->setProperty("current_index", 0);
        meaning->setProperty("text", QStringLiteral("An unsaved edit"));
        QVERIFY(panel->property("dirty").toBool());
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QStringLiteral("Baum")),
            Q_ARG(QString, QStringLiteral("Ein Baum."))));
        auto *discard_dialog = panel->findChild<QObject *>(QStringLiteral("discard_draft"));
        QVERIFY(discard_dialog);
        QTRY_VERIFY(discard_dialog->property("visible").toBool());
        QCOMPARE(word->property("text").toString(), QStringLiteral("Haus"));
        auto *dialog_footer = discard_dialog->property("footer").value<QQuickItem *>();
        QVERIFY(dialog_footer);
        auto *cancel_action = find_item(dialog_footer, QStringLiteral("cancel_action"));
        auto *confirm_action = find_item(dialog_footer, QStringLiteral("confirm_action"));
        QVERIFY(cancel_action);
        QVERIFY(confirm_action);
        QTest::qWait(30);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
            cancel_action->mapToScene(QPointF(cancel_action->width() / 2, cancel_action->height() / 2)).toPoint());
        QTRY_VERIFY(!discard_dialog->property("visible").toBool());
        QCOMPARE(meaning->property("text").toString(), QStringLiteral("An unsaved edit"));
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QStringLiteral("Baum")),
            Q_ARG(QString, QStringLiteral("Ein Baum."))));
        QTest::qWait(30);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
            confirm_action->mapToScene(QPointF(confirm_action->width() / 2, confirm_action->height() / 2)).toPoint());
        QTRY_COMPARE(word->property("text").toString(), QStringLiteral("Baum"));
        QVERIFY(!panel->property("dirty").toBool());
        QCOMPARE(warnings.size(), 0);
    }

    void responsive_notebook_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("dark");
        QTest::newRow("wide") << QSize(1440, 900) << false;
        QTest::newRow("desktop") << QSize(1000, 700) << false;
        QTest::newRow("compact") << QSize(640, 600) << false;
        QTest::newRow("small") << QSize(480, 640) << false;
        QTest::newRow("wide-dark") << QSize(1440, 900) << true;
        QTest::newRow("small-dark") << QSize(480, 640) << true;
    }

    void responsive_notebook() {
        QFETCH(QSize, size);
        QFETCH(bool, dark);
        const auto original_palette = QGuiApplication::palette();
        const auto restore_palette = qScopeGuard([&]() { QGuiApplication::setPalette(original_palette); });
        if (dark) {
            QPalette palette = original_palette;
            palette.setColor(QPalette::Window, QColor("#252a31"));
            palette.setColor(QPalette::Base, QColor("#1c2026"));
            palette.setColor(QPalette::Button, QColor("#303741"));
            palette.setColor(QPalette::WindowText, QColor("#edf1f5"));
            palette.setColor(QPalette::Text, QColor("#edf1f5"));
            palette.setColor(QPalette::ButtonText, QColor("#edf1f5"));
            QGuiApplication::setPalette(palette);
        }
        QTemporaryDir directory;
        showthis::LearningService service(nullptr, directory.filePath(QStringLiteral("words.json")));
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(size);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = find_item(surface.contentItem(), QStringLiteral("learning_panel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word",
            Q_ARG(QString, QStringLiteral("Verantwortungsbewusstsein")),
            Q_ARG(QString, QStringLiteral("Das zeigt ein hohes Verantwortungsbewusstsein."))));
        auto *meaning = find_item(surface.contentItem(), QStringLiteral("word_meaning"));
        auto *save = find_item(surface.contentItem(), QStringLiteral("save_word"));
        auto *columns = find_item(surface.contentItem(), QStringLiteral("word_columns"));
        auto *dictionary = find_item(surface.contentItem(), QStringLiteral("dictionary_pane"));
        auto *personal = find_item(surface.contentItem(), QStringLiteral("personal_card_pane"));
        auto *navigation = find_item(surface.contentItem(), QStringLiteral("notebook_tabs"));
        QVERIFY(meaning);
        QVERIFY(save);
        QVERIFY(columns);
        QVERIFY(dictionary);
        QVERIFY(personal);
        QVERIFY(navigation);
        if (dark) {
            auto *background = panel->property("background").value<QQuickItem *>();
            QVERIFY(background);
            QTRY_COMPARE(background->property("color").value<QColor>(), QColor("#252a31"));
        }
        meaning->setProperty("text", QStringLiteral("A sense of responsibility."));
        QTRY_VERIFY(save->isEnabled());
        QTRY_COMPARE(columns->property("columns").toInt(), size.width() - 32 >= 880 ? 2 : 1);
        QTest::qWait(30);
        const auto save_rect = save->mapRectToScene(QRectF(QPointF(), save->size()));
        QVERIFY(save_rect.left() >= 0);
        QVERIFY(save_rect.right() <= size.width());
        QVERIFY(save_rect.bottom() <= size.height());
        QVERIFY(save_rect.top() >= 0);
        for (auto *item : {dictionary, personal}) {
            if (!item->isVisible())
                continue;
            const auto rect = item->mapRectToScene(QRectF(QPointF(), item->size()));
            QVERIFY(rect.left() >= 0);
            QVERIFY(rect.right() <= size.width() + 1);
            QVERIFY(item->width() > 100);
        }
        if (panel->property("wide_layout").toBool())
            QVERIFY(personal->mapToScene(QPointF()).x() > dictionary->mapToScene(QPointF()).x());
        else {
            QVERIFY(dictionary->isVisible());
            QVERIFY(!personal->isVisible());
            panel->setProperty("compact_page", 1);
            QTRY_VERIFY(personal->isVisible());
            QVERIFY(!dictionary->isVisible());
        }

        panel->forceActiveFocus();
        QTest::keyClick(&surface, Qt::Key_S, Qt::ControlModifier);
        QTRY_COMPARE(service.vocabulary().size(), 1);
        QVERIFY(!panel->property("dirty").toBool());
        navigation->setProperty("current_index", 1);
        auto *export_button = find_item(surface.contentItem(), QStringLiteral("export_anki"));
        auto *search = find_item(surface.contentItem(), QStringLiteral("vocabulary_search"));
        QVERIFY(export_button);
        QVERIFY(search);
        QTest::qWait(30);
        const auto export_rect = export_button->mapRectToScene(QRectF(QPointF(), export_button->size()));
        QVERIFY(export_rect.left() >= 0);
        QVERIFY(export_rect.right() <= size.width());
        QTest::keyClick(&surface, Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(search->hasActiveFocus());
        navigation->setProperty("current_index", 3);
        QTest::qWait(30);
        QCOMPARE(warnings.size(), 0);
    }

    void ordinary_word_editing_without_analysis() {
        QTemporaryDir directory;
        showthis::LearningService service(nullptr, directory.filePath(QStringLiteral("words.json")));
        service.set_source_language(QStringLiteral("de"));
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(1100, 800);
        auto *panel = find_item(surface.contentItem(), QStringLiteral("learning_panel"));
        QVERIFY(panel);
        const auto observed = QString::fromUtf8("Häusern");
        const auto context = QString::fromUtf8("Wir wohnen in den Häusern.");
        QImage image(1100, 800, QImage::Format_RGB32);
        image.fill(Qt::white);
        surface.set_image(image, {{observed, QRect(100, 200, 100, 30), context}});
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(140, 215));
        auto *word = find_item(surface.contentItem(), QStringLiteral("lookup_word"));
        auto *meaning = find_item(surface.contentItem(), QStringLiteral("word_meaning"));
        auto *save = find_item(surface.contentItem(), QStringLiteral("save_word"));
        QVERIFY(word);
        QVERIFY(meaning);
        QVERIFY(save);
        QVERIFY(!find_item(surface.contentItem(), QStringLiteral("dictionary_form")));
        QVERIFY(!find_item(surface.contentItem(), QStringLiteral("suggest_lemma")));
        QCOMPARE(word->property("text").toString(), observed);
        word->setProperty("text", QStringLiteral("Haus"));
        QVERIFY(QMetaObject::invokeMethod(word, "textEdited"));
        meaning->setProperty("text", QStringLiteral("house"));
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QCOMPARE(service.vocabulary().size(), 1);
        const auto card = service.vocabulary()[0].toMap();
        QCOMPARE(card.value("word").toString(), QStringLiteral("Haus"));
        QCOMPARE(card.value("context").toString(), context);
        QVERIFY(!card.contains("lemma"));
        QVERIFY(!card.contains("linguistics"));
        QVERIFY(QMetaObject::invokeMethod(panel, "edit_card", Q_ARG(QVariant, QVariant(card))));
        QCOMPARE(word->property("text").toString(), QStringLiteral("Haus"));
        QVERIFY(!panel->property("dirty").toBool());
        QCOMPARE(warnings.size(), 0);
    }

    void dictionary_sources_and_native_headwords_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("dark");
        QTest::newRow("desktop") << QSize(1100, 800) << false;
        QTest::newRow("compact") << QSize(640, 700) << false;
        QTest::newRow("small") << QSize(480, 640) << false;
        QTest::newRow("small-dark") << QSize(480, 640) << true;
    }

    void dictionary_sources_and_native_headwords() {
        QFETCH(QSize, size);
        QFETCH(bool, dark);
        const auto original_palette = QGuiApplication::palette();
        const auto restore_palette = qScopeGuard([&]() { QGuiApplication::setPalette(original_palette); });
        if (dark) {
            QPalette palette = original_palette;
            palette.setColor(QPalette::Window, QColor("#252a31"));
            palette.setColor(QPalette::Base, QColor("#1c2026"));
            palette.setColor(QPalette::Button, QColor("#303741"));
            palette.setColor(QPalette::WindowText, QColor("#edf1f5"));
            palette.setColor(QPalette::Text, QColor("#edf1f5"));
            palette.setColor(QPalette::ButtonText, QColor("#edf1f5"));
            QGuiApplication::setPalette(palette);
        }
        QTemporaryDir directory;
        TestNetwork network;
        const auto observed = QString::fromUtf8("Häuser");
        network.responder = [&](const QNetworkRequest &request) -> TestNetwork::Response {
            if (request.url().host() == "freedictionaryapi.com") return {free_entry("Haus", "house")};
            if (request.url().host() == "dictionary.example") return {free_entry("Haus", "maison")};
            const auto word = request.url().path().contains(QString::fromUtf8("hä"))
                || request.url().path().endsWith(observed + ".jsonl") ? observed : QString("Haus");
            if (request.url().path().endsWith(".json")) return {kaikki_index(word, "ru")};
            return {kaikki_entry(word, word == observed ? QString::fromUtf8("форма Haus") : QString::fromUtf8("дом"),
                "de", word == observed ? QString("Haus") : QString{}, word == "Haus" ? house_forms() : QJsonArray{})};
        };
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network, sample_dictionary_providers());
        service.set_source_language("de");
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(size);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = find_item(surface.contentItem(), "learning_panel");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, observed), Q_ARG(QString, QString("der Häuser"))));
        auto *choose = find_item(surface.contentItem(), "choose_dictionary_sources");
        auto *lookup = find_item(surface.contentItem(), "lookup_dictionary");
        auto *word = find_item(surface.contentItem(), "lookup_word");
        auto *meaning = find_item(surface.contentItem(), "word_meaning");
        auto *save = find_item(surface.contentItem(), "save_word");
        QVERIFY(choose);
        QVERIFY(lookup);
        QVERIFY(word);
        QVERIFY(meaning);
        QVERIFY(save);
        const auto original = service.dictionary_selection();
        meaning->setProperty("text", "My typed meaning");
        QVERIFY(QMetaObject::invokeMethod(choose, "clicked"));
        QTRY_VERIFY(find_item(surface.contentItem(), "dictionary_source_kaikki"));
        auto *dialog = surface.rootObject()->findChild<QObject *>("dictionary_source_picker");
        QVERIFY(dialog);
        if (dark) {
            auto *background = dialog->property("background").value<QQuickItem *>();
            QVERIFY(background);
            QTRY_COMPARE(background->property("color").value<QColor>(), QColor("#252a31"));
            auto *card = find_item(surface.contentItem(), "dictionary_source_card_kaikki");
            QVERIFY(card);
            auto *card_background = card->property("background").value<QQuickItem *>();
            QVERIFY(card_background);
            QTRY_COMPARE(card_background->property("color").value<QColor>(), QColor("#1c2026"));
        }
        auto *kaikki = find_item(surface.contentItem(), "dictionary_source_kaikki");
        auto *free = find_item(surface.contentItem(), "dictionary_source_freedictionary");
        auto *russian = find_item(surface.contentItem(), "dictionary_language_kaikki_ru");
        auto *english = find_item(surface.contentItem(), "dictionary_language_kaikki_en");
        auto *apply = find_item(surface.contentItem(), "apply_dictionary_sources");
        auto *cancel = find_item(surface.contentItem(), "cancel_dictionary_sources");
        QVERIFY(kaikki && free && russian && english && apply && cancel);
        auto toggle = [](QQuickItem *item, bool checked) {
            item->setProperty("checked", checked);
            return QMetaObject::invokeMethod(item, "toggled");
        };
        QVERIFY(toggle(kaikki, true));
        QVERIFY(toggle(free, false));
        if (!russian->property("checked").toBool()) QVERIFY(QMetaObject::invokeMethod(russian, "clicked"));
        if (english->property("checked").toBool()) QVERIFY(QMetaObject::invokeMethod(english, "clicked"));
        QVERIFY(QMetaObject::invokeMethod(russian, "clicked"));
        QVERIFY(!apply->isEnabled()); // Selected sources must have a language.
        QVERIFY(QMetaObject::invokeMethod(russian, "clicked"));
        QVERIFY(apply->isEnabled());
        QCOMPARE(service.dictionary_selection(), original); // Changes are staged.
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QCOMPARE(service.dictionary_selection(), original);
        QVERIFY(QMetaObject::invokeMethod(choose, "clicked"));
        QTest::qWait(30);
        QVERIFY(!kaikki->property("checked").toBool());
        QVERIFY(toggle(kaikki, true));
        QVERIFY(toggle(free, false));
        if (!russian->property("checked").toBool()) QVERIFY(QMetaObject::invokeMethod(russian, "clicked"));
        if (english->property("checked").toBool()) QVERIFY(QMetaObject::invokeMethod(english, "clicked"));
        const auto apply_rect = apply->mapRectToScene(QRectF(QPointF(), apply->size()));
        QVERIFY(apply_rect.left() >= 0 && apply_rect.right() <= size.width());
        QVERIFY(apply_rect.top() >= 0 && apply_rect.bottom() <= size.height());
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS")) {
            QTest::qWait(80);
            const auto path = qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS")
                + "/dictionary-picker-" + QTest::currentDataTag() + ".png";
            QVERIFY(surface.grabWindow().save(path));
        }
        QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QCOMPARE(service.dictionary_selection(), QVariantList{dictionary_selection("kaikki", {"ru"})});
        QCOMPARE(meaning->property("text").toString(), QString("My typed meaning"));
        QCOMPARE(word->property("text").toString(), observed);
        QCOMPARE(service.source_language(), QString("de"));
        QVERIFY(network.requests.isEmpty());
        QVERIFY(QMetaObject::invokeMethod(lookup, "clicked"));
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(service.senses().size(), 2);
        QTRY_VERIFY(find_item(surface.contentItem(), "dictionary_sense_1"));
        auto *resolved = find_item(surface.contentItem(), "dictionary_sense_1");
        QVERIFY(QMetaObject::invokeMethod(resolved, "clicked"));
        QCOMPARE(word->property("text").toString(), QString("Haus"));
        QCOMPARE(meaning->property("text").toString(), QString::fromUtf8("дом"));
        auto *base_form = find_item(surface.contentItem(), "word_base_form");
        auto *base_prefix = find_item(surface.contentItem(), "word_base_prefix");
        auto *plural_form = find_item(surface.contentItem(), "word_plural_form");
        auto *prefix = find_item(surface.contentItem(), "word_prefix");
        QVERIFY(base_form && base_prefix && plural_form && prefix);
        QCOMPARE(base_form->property("text").toString(), QString("Haus"));
        QCOMPARE(base_prefix->property("text").toString(), QString("das"));
        QCOMPARE(prefix->property("text").toString(), QString("das"));
        QCOMPARE(plural_form->property("text").toString(), QString::fromUtf8("Häuser"));
        QTest::qWait(30);
        const auto prefix_rect = base_prefix->mapRectToScene(QRectF(QPointF(), base_prefix->size()));
        QVERIFY(prefix_rect.left() >= 0 && prefix_rect.right() <= size.width());
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS")) {
            const auto path = qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS")
                + "/word-forms-" + QTest::currentDataTag() + ".png";
            QVERIFY(surface.grabWindow().save(path));
        }
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QCOMPARE(service.vocabulary()[0].toMap().value("source").toMap().value("name").toString(), QString("Kaikki.org"));
        QCOMPARE(service.vocabulary()[0].toMap().value("base_article").toString(), QString("das"));
        QCOMPARE(service.vocabulary()[0].toMap().value("plural_forms").toStringList(), QStringList{QString::fromUtf8("Häuser")});
        QVERIFY(QMetaObject::invokeMethod(panel, "edit_card", Q_ARG(QVariant, service.vocabulary()[0])));
        QCOMPARE(base_form->property("text").toString(), QString("Haus"));
        QCOMPARE(plural_form->property("text").toString(), QString::fromUtf8("Häuser"));
        plural_form->setProperty("text", QString::fromUtf8("Häuser / Häuserchen"));
        QVERIFY(QMetaObject::invokeMethod(plural_form, "textEdited"));
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QCOMPARE(service.vocabulary()[0].toMap().value("plural_forms").toStringList(),
            (QStringList{QString::fromUtf8("Häuser"), QString::fromUtf8("Häuserchen")}));
        QVERIFY(QMetaObject::invokeMethod(choose, "clicked"));
        QVERIFY(toggle(free, true));
        auto *sample = find_item(surface.contentItem(), "dictionary_source_sample");
        auto *sample_en = find_item(surface.contentItem(), "dictionary_language_sample_en");
        auto *sample_fr = find_item(surface.contentItem(), "dictionary_language_sample_fr");
        QVERIFY(sample && sample_en && sample_fr);
        QVERIFY(toggle(sample, true));
        if (!sample_en->property("checked").toBool()) QVERIFY(QMetaObject::invokeMethod(sample_en, "clicked"));
        if (!sample_fr->property("checked").toBool()) QVERIFY(QMetaObject::invokeMethod(sample_fr, "clicked"));
        QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QVERIFY(QMetaObject::invokeMethod(lookup, "clicked"));
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(service.dictionary_selection().size(), 3);
        QCOMPARE(service.senses().size(), 4);
        QCOMPARE(service.senses()[0].toMap().value("definition_language").toString(), QString("ru"));
        QCOMPARE(service.senses()[1].toMap().value("definition_language").toString(), QString("en"));
        QCOMPARE(service.senses()[2].toMap().value("source").toMap().value("name").toString(), QString("Sample Dictionary"));
        QVERIFY(QMetaObject::invokeMethod(choose, "clicked"));
        QVERIFY(toggle(sample, false));
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(panel->isVisible());
        QCOMPARE(service.dictionary_selection().size(), 3); // Escape discards staged changes only.
        QCOMPARE(warnings.size(), 0);
    }

    void grammar_profile_cards_data() {
        QTest::addColumn<QString>("word");
        QTest::addColumn<QString>("language");
        QTest::addColumn<QStringList>("tags");
        QTest::addColumn<QString>("prefix");
        QTest::addColumn<QString>("status");
        QTest::newRow("german-plural-only") << QString("Eltern") << QString("de") << QStringList{"plural", "plural only"} << QString("die") << QString("No singular");
        QTest::newRow("german-gender") << QString("Haus") << QString("de") << QStringList{"neuter"} << QString("das") << QString("Plural not listed");
        QTest::newRow("english-mass") << QString("information") << QString("en") << QStringList{"uncountable"} << QString{} << QString("No plural");
        QTest::newRow("english-missing-plural") << QString("book") << QString("en") << QStringList{} << QString{} << QString("Plural not listed");
    }

    void grammar_profile_cards() {
        QFETCH(QString, word);
        QFETCH(QString, language);
        QFETCH(QStringList, tags);
        QFETCH(QString, prefix);
        QFETCH(QString, status);
        QTemporaryDir directory;
        TestNetwork network;
        const QJsonObject entry{{"language", QJsonObject{{"code", language}}}, {"partOfSpeech", "noun"},
            {"forms", QJsonArray{}}, {"senses", QJsonArray{QJsonObject{{"definition", "meaning"}, {"tags", QJsonArray::fromStringList(tags)}}}}};
        network.response = QJsonDocument(QJsonObject{{"word", word}, {"entries", QJsonArray{entry}}}).toJson();
        const auto path = directory.filePath("words.json");
        showthis::LearningService service(nullptr, path, &network);
        service.set_source_language(language);
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(640, 700);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = find_item(surface.contentItem(), "learning_panel");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, word), Q_ARG(QString, QString{})));
        auto *lookup = find_item(surface.contentItem(), "lookup_dictionary");
        QVERIFY(lookup);
        QVERIFY(QMetaObject::invokeMethod(lookup, "clicked"));
        QTRY_VERIFY(!service.dictionary_busy());
        QTRY_VERIFY(find_item(surface.contentItem(), "dictionary_sense_0"));
        auto *sense = find_item(surface.contentItem(), "dictionary_sense_0");
        QVERIFY(QMetaObject::invokeMethod(sense, "clicked"));
        auto *article = find_item(surface.contentItem(), "word_prefix");
        auto *availability = find_item(surface.contentItem(), "word_number_availability");
        auto *save = find_item(surface.contentItem(), "save_word");
        QVERIFY(article && availability && save);
        QCOMPARE(article->property("text").toString(), prefix);
        QCOMPARE(availability->property("text").toString(), status);
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS")) {
            QTest::qWait(30);
            const auto screenshot = qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS")
                + "/grammar-card-" + QTest::currentDataTag() + ".png";
            QVERIFY(surface.grabWindow().save(screenshot));
        }
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QCOMPARE(service.vocabulary().size(), 1);
        const auto card = service.vocabulary()[0].toMap();
        QCOMPARE(service.number_availability(card), status);
        showthis::LearningService reopened(nullptr, path, &network);
        QCOMPARE(reopened.number_availability(reopened.vocabulary()[0].toMap()), status);
        QVERIFY(QMetaObject::invokeMethod(panel, "edit_card", Q_ARG(QVariant, QVariant(card))));
        QCOMPARE(availability->property("text").toString(), status);
        if (!prefix.isEmpty()) {
            QCOMPARE(card.value("article_origin").toString(), QString("profile"));
            article->setProperty("text", "manual");
            QVERIFY(QMetaObject::invokeMethod(article, "textEdited"));
            QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
            QCOMPARE(service.vocabulary()[0].toMap().value("article_origin").toString(), QString("manual"));
        }
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QString("new")), Q_ARG(QString, QString{})));
        QCOMPARE(availability->property("text").toString(), QString{});
        QCOMPARE(warnings.size(), 0);
    }

    void anki_export_uses_standard_dialog_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("desktop") << QSize(1100, 800);
        QTest::newRow("compact") << QSize(480, 640);
    }

    void anki_export_uses_standard_dialog() {
        QFETCH(QSize, size);
        QTemporaryDir directory;
        const auto store_path = directory.filePath("words.json");
        showthis::LearningService service(nullptr, store_path);
        QVERIFY(service.save_word({}, "Haus", "", "house", "", "", -1));
        const auto selected_id = service.vocabulary()[0].toMap().value("id").toString();
        QVERIFY(service.save_word({}, "Baum", "", "tree", "", "", -1));
        showthis::ShoWSurface surface(nullptr, &service);
        TestCaptureBackend backend;
        showthis::OverlayController controller(surface, backend,
            [](const QImage &, const QString &) { return showthis::ScanResult{}; });
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(size);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = find_item(surface.contentItem(), "learning_panel");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QString("draft")), Q_ARG(QString, QString("context"))));
        auto *meaning = find_item(surface.contentItem(), "word_meaning");
        QVERIFY(meaning);
        meaning->setProperty("text", "Keep this unsaved draft");
        QVERIFY(panel->property("dirty").toBool());
        QVERIFY(QMetaObject::invokeMethod(panel, "open_vocabulary"));
        auto *selection = find_item(surface.contentItem(), "select_saved_" + selected_id);
        QTRY_VERIFY(selection || (selection = find_item(surface.contentItem(), "select_saved_" + selected_id)));
        selection->setProperty("checked", true);
        QVERIFY(QMetaObject::invokeMethod(selection, "clicked"));
        auto *export_button = find_item(surface.contentItem(), "export_anki");
        QVERIFY(export_button);
        QVERIFY(QMetaObject::invokeMethod(export_button, "clicked"));
        QVERIFY(!surface.isVisible());
        QVERIFY(surface.export_dialog_open());
        const auto find_dialog = []() -> QFileDialog * {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *dialog = qobject_cast<QFileDialog *>(widget);
                    dialog && dialog->objectName() == "anki_export_dialog" && dialog->isVisible())
                    return dialog;
            }
            return nullptr;
        };
        QPointer<QFileDialog> dialog;
        QTRY_VERIFY(dialog = find_dialog());
        QCOMPARE(dialog->parentWidget(), nullptr);
        QCOMPARE(dialog->acceptMode(), QFileDialog::AcceptSave);
        QVERIFY(!dialog->testOption(QFileDialog::DontUseNativeDialog));
        QCOMPARE(dialog->defaultSuffix(), QString("txt"));
        controller.toggle_overlay();
        controller.show_notebook();
        QVERIFY(!surface.isVisible());
        QCOMPARE(backend.capture_count, 0);
        const auto destination = directory.filePath("chosen export.tsv");
        dialog->setDirectory(directory.path());
        auto *filename = dialog->findChild<QLineEdit *>("fileNameEdit");
        QVERIFY(filename);
        filename->selectAll();
        QTest::keyClicks(filename, "chosen export.tsv");
        QCOMPARE(dialog->selectedUrls().first().toLocalFile(), destination);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY(!surface.export_dialog_open());
        QTRY_VERIFY(surface.isVisible());
        QFile file(destination);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(service.message()));
        const auto exported = file.readAll();
        QVERIFY(exported.contains("Haus"));
        QVERIFY(!exported.contains("Baum"));
        QCOMPARE(meaning->property("text").toString(), QString("Keep this unsaved draft"));
        QVERIFY(panel->property("dirty").toBool());
        QVERIFY(QMetaObject::invokeMethod(export_button, "clicked"));
        QTRY_VERIFY(dialog = find_dialog());
        QVERIFY(!surface.isVisible());
        const auto cancelled = directory.filePath("should-not-exist.txt");
        dialog->setDirectory(directory.path());
        filename = dialog->findChild<QLineEdit *>("fileNameEdit");
        QVERIFY(filename);
        filename->selectAll();
        QTest::keyClicks(filename, "should-not-exist.txt");
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(surface.isVisible());
        QVERIFY(panel->isVisible());
        QVERIFY(!QFileInfo::exists(cancelled));
        QVERIFY(QMetaObject::invokeMethod(export_button, "clicked"));
        QTRY_VERIFY(dialog = find_dialog());
        controller.hide_overlay(); // A later explicit hide must beat automatic restoration.
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!surface.export_dialog_open());
        QTest::qWait(20);
        QVERIFY(!surface.isVisible());
        QCOMPARE(warnings.size(), 0);
    }

    void offline_dictionary_settings_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("desktop") << QSize(1100, 850);
        QTest::newRow("compact") << QSize(480, 700);
    }

    void offline_dictionary_settings() {
        QFETCH(QSize, size);
        QTemporaryDir directory;
        TestNetwork network;
        const auto gzip = gzip_bytes(offline_dictionary_data());
        network.responder = [gzip](const QNetworkRequest &) -> TestNetwork::Response {
            return {gzip, 200, QNetworkReply::NoError, gzip.size()};
        };
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(size);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *navigation = find_item(surface.contentItem(), "notebook_tabs");
        auto *panel = find_item(surface.contentItem(), "learning_panel");
        QVERIFY(navigation && panel);
        // Open the notebook before navigating to its settings page.
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QString("Haus")), Q_ARG(QString, QString{})));
        navigation->setProperty("current_index", 3);
        auto *edition = find_item(surface.contentItem(), "offline_dictionary_edition");
        auto *word_language = find_item(surface.contentItem(), "offline_dictionary_word_language");
        auto *check = find_item(surface.contentItem(), "check_offline_dictionary_size");
        auto *download = find_item(surface.contentItem(), "download_offline_dictionary");
        auto *mode = find_item(surface.contentItem(), "offline_dictionary_mode");
        auto *cancel = find_item(surface.contentItem(), "cancel_offline_dictionary");
        auto *settings = find_item(surface.contentItem(), "offline_dictionary_settings");
        QVERIFY(edition && word_language && check && download && mode && cancel && settings);
        QVERIFY(network.requests.isEmpty());
        QVERIFY(!download->isEnabled());
        edition->setProperty("currentIndex", 1); // Russian definitions, not word language.
        QVERIFY(word_language->property("count").toInt() > 2);
        int german_index = 1;
        for (const auto &option : service.language_options()) {
            if (option.toMap().value("code").toString() == "de") break;
            ++german_index;
        }
        word_language->setProperty("currentIndex", german_index);
        QVERIFY(QMetaObject::invokeMethod(word_language, "activated", Q_ARG(int, german_index)));
        word_language->setProperty("editText", "de-DE");
        QVERIFY(QMetaObject::invokeMethod(word_language, "accepted"));
        QCOMPARE(settings->property("word_language").toString(), QString("de"));
        QVERIFY(QMetaObject::invokeMethod(check, "clicked"));
        QTRY_VERIFY(!service.offline_dictionaries()->busy());
        QTRY_VERIFY(download->isEnabled());
        QCOMPARE(network.requests.size(), 1); // Size inspection only; no data download.
        QVERIFY(QMetaObject::invokeMethod(download, "clicked"));
        auto *dialog = surface.rootObject()->findChild<QObject *>("offline_download_confirmation");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(service.offline_dictionaries()->packs().isEmpty());
        network.automatic = false;
        QVERIFY(QMetaObject::invokeMethod(download, "clicked"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QVERIFY(service.offline_dictionaries()->busy());
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QVERIFY(!service.offline_dictionaries()->busy());
        QVERIFY(service.offline_dictionaries()->packs().isEmpty());
        network.automatic = true;
        QVERIFY(QMetaObject::invokeMethod(download, "clicked"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY(!service.offline_dictionaries()->busy());
        QCOMPARE(service.offline_dictionaries()->packs().size(), 1);
        QCOMPARE(service.offline_dictionaries()->packs()[0].toMap().value("word_language").toString(), QString("de"));
        QCOMPARE(service.offline_dictionaries()->packs()[0].toMap().value("records").toInt(), 2);
        QTRY_VERIFY(find_item(surface.contentItem(), "remove_offline_dictionary_de_ru"));
        mode->setProperty("currentIndex", 2);
        QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 2)));
        QCOMPARE(service.offline_dictionaries()->mode(), QString("local_only"));
        QTest::qWait(30);
        QVERIFY(settings->width() > 200 && settings->width() <= size.width());
        auto *scroll = find_item(surface.contentItem(), "notebook_settings_scroll");
        QVERIFY(scroll);
        auto *flickable = scroll->property("contentItem").value<QQuickItem *>();
        QVERIFY(flickable);
        auto *content = flickable->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        flickable->setProperty("contentY", settings->mapToItem(content, QPointF()).y());
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS")) {
            QTest::qWait(50);
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS")
                + "/offline-settings-" + QTest::currentDataTag() + ".png"));
        }
        QVERIFY(service.configure_dictionaries({dictionary_selection("kaikki", {"ru"})}, true));
        service.set_source_language("de");
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QString("Haus")), Q_ARG(QString, QString{})));
        const auto requests = network.requests.size();
        auto *lookup = find_item(surface.contentItem(), "lookup_dictionary");
        QVERIFY(lookup);
        QVERIFY(QMetaObject::invokeMethod(lookup, "clicked"));
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(service.senses().size(), 1);
        QCOMPARE(network.requests.size(), requests);
        QVERIFY(service.senses()[0].toMap().value("source").toMap().value("offline").toBool());
        QCOMPARE(warnings.size(), 0);
    }

    void selecting_sense_cancels_pending_translation_data() {
        QTest::addColumn<int>("selected_index");
        QTest::newRow("same-headword") << 0;
        QTest::newRow("resolved-headword") << 1;
    }

    void selecting_sense_cancels_pending_translation() {
        QFETCH(int, selected_index);
        QTemporaryDir directory;
        TestNetwork network;
        const auto inflection = QString::fromUtf8("Häuser");
        network.responder = [inflection](const QNetworkRequest &request) -> TestNetwork::Response {
            return {request.url().path().endsWith(inflection)
                ? free_entry(inflection, "plural of Haus", "Haus") : free_entry("Haus", "house")};
        };
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.set_source_language("de");
        service.translator()->set_provider_id("deepl");
        service.translator()->set_option("api_key", "test-key");
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(1100, 800);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = find_item(surface.contentItem(), "learning_panel");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, inflection), Q_ARG(QString, QString{})));
        service.lookup(inflection);
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(service.senses().size(), 2);
        auto *sense = find_item(surface.contentItem(), "dictionary_sense_" + QString::number(selected_index));
        auto *translation = find_item(surface.contentItem(), "word_translation");
        auto *word = find_item(surface.contentItem(), "lookup_word");
        auto *translate = find_item(surface.contentItem(), "translate_word");
        QVERIFY(sense);
        QVERIFY(translation);
        QVERIFY(word);
        QVERIFY(translate);
        network.responder = {};
        network.response = R"({"translations":[{"text":"stale translation"}]})";
        network.automatic = false;
        QVERIFY(QMetaObject::invokeMethod(translate, "clicked"));
        QVERIFY(service.translator()->busy());
        const auto pending = network.last_reply;
        QSignalSpy translated(service.translator(), &showthis::TranslationService::translated);
        QVERIFY(QMetaObject::invokeMethod(sense, "clicked"));
        QVERIFY(!service.translator()->busy());
        QCOMPARE(word->property("text").toString(), selected_index == 0 ? inflection : QString("Haus"));
        const auto chosen_translation = translation->property("text").toString();
        if (pending)
            pending->complete();
        QCoreApplication::processEvents();
        QCOMPARE(translated.size(), 0);
        QCOMPARE(translation->property("text").toString(), chosen_translation);

        // A new explicit translation of the selected sense still works.
        network.response = R"({"translations":[{"text":"fresh translation"}]})";
        QVERIFY(QMetaObject::invokeMethod(translate, "clicked"));
        network.last_reply->complete();
        QCOMPARE(translation->property("text").toString(), QString("fresh translation"));
        QCOMPARE(warnings.size(), 0);
    }

    void saved_translation_is_independent_of_provider_data() {
        QTest::addColumn<QString>("change");
        for (const auto *change : {"restart-without-key", "no-provider", "forget", "disable", "key", "plan"})
            QTest::newRow(change) << QString::fromLatin1(change);
    }

    void saved_translation_is_independent_of_provider() {
        QFETCH(QString, change);
        QTemporaryDir directory;
        const auto path = directory.filePath("words.json");
        {
            showthis::LearningService original(nullptr, path);
            original.translator()->set_provider_id("deepl");
            original.translator()->set_option("api_key", "session-only");
            QVERIFY(original.save_word({}, "Haus", "", "", "saved translation", "original notes", -1));
        }
        TestNetwork network;
        showthis::LearningService service(nullptr, path, &network);
        QVERIFY(!service.translator()->configured());
        if (change == "no-provider")
            service.translator()->set_provider_id("");
        else if (change != "restart-without-key")
            service.translator()->set_option("api_key", "new-session-key");
        const auto card = service.vocabulary()[0];
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(1100, 800);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = find_item(surface.contentItem(), "learning_panel");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "edit_card", Q_ARG(QVariant, card)));
        auto *translation = find_item(surface.contentItem(), "word_translation");
        auto *form = find_item(surface.contentItem(), "personal_card_pane");
        auto *save = find_item(surface.contentItem(), "save_word");
        QVERIFY(translation);
        QVERIFY(form);
        QVERIFY(save);
        QCOMPARE(translation->property("text").toString(), QString("saved translation"));
        QVERIFY(translation->isVisible());
        QVERIFY(!save->isEnabled());
        if (change == "forget") service.translator()->forget_credentials();
        else if (change == "disable") service.translator()->set_provider_id("");
        else if (change == "key") service.translator()->set_option("api_key", "replacement-key");
        else if (change == "plan") service.translator()->set_option("plan", "pro");
        QCOMPARE(translation->property("text").toString(), QString("saved translation"));
        QVERIFY(!panel->property("dirty").toBool());
        QVERIFY(form->setProperty("notes", "edited notes"));
        QVERIFY(save->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QCOMPARE(service.vocabulary()[0].toMap().value("translation").toString(), QString("saved translation"));
        showthis::LearningService reopened(nullptr, path, &network);
        QCOMPARE(reopened.vocabulary()[0].toMap().value("translation").toString(), QString("saved translation"));
        QCOMPARE(reopened.vocabulary()[0].toMap().value("notes").toString(), QString("edited notes"));
        const auto export_path = directory.filePath("anki.txt");
        QVERIFY(service.export_anki(QUrl::fromLocalFile(export_path)));
        QFile exported(export_path);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        QVERIFY(exported.readAll().contains("saved translation"));

        // This remains ordinary editable card data, including explicit removal.
        translation->setProperty("text", "manual translation");
        QVERIFY(save->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QCOMPARE(service.vocabulary()[0].toMap().value("translation").toString(), QString("manual translation"));
        auto *meaning = find_item(surface.contentItem(), "word_meaning");
        QVERIFY(meaning);
        meaning->setProperty("text", "a meaning instead");
        translation->setProperty("text", "");
        QVERIFY(save->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QVERIFY(service.vocabulary()[0].toMap().value("translation").toString().isEmpty());
        QVERIFY(network.requests.isEmpty());
        QCOMPARE(warnings.size(), 0);
    }

    void translator_configuration_controls_online_actions_only() {
        QTemporaryDir directory;
        TestNetwork network;
        network.response = R"({"translations":[{"text":"дом"}]})";
        showthis::LearningService service(nullptr, directory.filePath(QStringLiteral("words.json")), &network);
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(1100, 800);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = find_item(surface.contentItem(), QStringLiteral("learning_panel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QStringLiteral("Haus")),
            Q_ARG(QString, QStringLiteral("Das ist ein Haus."))));
        auto *translation = find_item(surface.contentItem(), QStringLiteral("word_translation"));
        auto *target = find_item(surface.contentItem(), QStringLiteral("target_language"));
        auto *translate = find_item(surface.contentItem(), QStringLiteral("translate_word"));
        auto *save = find_item(surface.contentItem(), QStringLiteral("save_word"));
        auto *meaning = find_item(surface.contentItem(), QStringLiteral("word_meaning"));
        auto *navigation = find_item(surface.contentItem(), QStringLiteral("notebook_tabs"));
        QVERIFY(translation);
        QVERIFY(target);
        QVERIFY(translate);
        QVERIFY(save);
        QVERIFY(meaning);
        QVERIFY(navigation);
        QVERIFY(translation->isVisible());
        QVERIFY(target->isVisible());
        QVERIFY(!translate->isVisible());
        QVERIFY(!save->isEnabled());

        navigation->setProperty("current_index", 3);
        auto *provider = find_item(surface.contentItem(), QStringLiteral("translation_provider"));
        QVERIFY(provider);
        QCOMPARE(provider->property("currentIndex").toInt(), 0);
        provider->setProperty("currentIndex", 1);
        QVERIFY(QMetaObject::invokeMethod(provider, "activated", Q_ARG(int, 1)));
        QCOMPARE(service.translator()->provider_id(), QString("deepl"));
        QVERIFY(!service.translator()->configured());
        navigation->setProperty("current_index", 0);
        QVERIFY(translation->isVisible());

        navigation->setProperty("current_index", 3);
        QTRY_VERIFY(find_item(surface.contentItem(), QStringLiteral("translator_input_api_key")));
        auto *key = find_item(surface.contentItem(), QStringLiteral("translator_input_api_key"));
        auto *apply = find_item(surface.contentItem(), QStringLiteral("apply_translator_api_key"));
        QVERIFY(apply);
        key->setProperty("text", QStringLiteral("private-test-key"));
        QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QVERIFY(service.translator()->configured());
        QVERIFY(key->property("text").toString().isEmpty());
        navigation->setProperty("current_index", 0);
        QTRY_VERIFY(translation->isVisible());
        QVERIFY(target->isVisible());
        QVERIFY(translate->isVisible());
        QVERIFY(translate->property("text").toString().contains(QStringLiteral("DeepL")));
        QVERIFY(network.requests.isEmpty());
        QVERIFY(QMetaObject::invokeMethod(translate, "clicked"));
        QTRY_COMPARE(translation->property("text").toString(), QString::fromUtf8("дом"));
        QCOMPARE(network.requests.size(), 1);
        QTRY_VERIFY(save->isEnabled());

        service.translator()->forget_credentials();
        QTRY_VERIFY(translation->isVisible());
        QVERIFY(target->isVisible());
        QVERIFY(!translate->isVisible());
        QCOMPARE(translation->property("text").toString(), QString::fromUtf8("дом"));
        QVERIFY(save->isEnabled());
        meaning->setProperty("text", QStringLiteral("house"));
        QVERIFY(save->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QCOMPARE(service.vocabulary().size(), 1);
        QCOMPARE(service.vocabulary()[0].toMap().value("translation").toString(), QString::fromUtf8("дом"));
        QCOMPARE(warnings.size(), 0);
    }

    void flexible_language_controls() {
        QTemporaryDir directory;
        showthis::LearningService service(nullptr, directory.filePath(QStringLiteral("words.json")));
        service.translator()->set_provider_id(QStringLiteral("deepl"));
        service.translator()->set_option(QStringLiteral("api_key"), QStringLiteral("test-key"));
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(1100, 800);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = find_item(surface.contentItem(), QStringLiteral("learning_panel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QStringLiteral("maison")),
            Q_ARG(QString, QStringLiteral("Voici ma maison."))));
        auto *source = find_item(surface.contentItem(), QStringLiteral("lookup_language"));
        auto *target = find_item(surface.contentItem(), QStringLiteral("target_language"));
        auto *prefix = find_item(surface.contentItem(), QStringLiteral("word_prefix"));
        auto *meaning = find_item(surface.contentItem(), QStringLiteral("word_meaning"));
        auto *save = find_item(surface.contentItem(), QStringLiteral("save_word"));
        QVERIFY(source);
        QVERIFY(target);
        QVERIFY(prefix);
        QVERIFY(meaning);
        QVERIFY(save);
        source->setProperty("editText", QStringLiteral("fr"));
        QVERIFY(QMetaObject::invokeMethod(source, "accepted"));
        QCOMPARE(service.source_language(), QStringLiteral("fr"));
        target->setProperty("editText", QStringLiteral("es"));
        QVERIFY(QMetaObject::invokeMethod(target, "accepted"));
        QCOMPARE(service.target_language(), QStringLiteral("es"));
        prefix->setProperty("text", QStringLiteral("la"));
        QVERIFY(QMetaObject::invokeMethod(prefix, "textEdited"));
        meaning->setProperty("text", QStringLiteral("house"));
        source->setProperty("editText", QStringLiteral("../invalid"));
        QVERIFY(QMetaObject::invokeMethod(source, "accepted"));
        QCOMPARE(service.source_language(), QStringLiteral("fr"));
        QCOMPARE(meaning->property("text").toString(), QStringLiteral("house"));
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QCOMPARE(service.vocabulary().size(), 1);
        const auto card = service.vocabulary()[0].toMap();
        QCOMPARE(card.value("language").toString(), QStringLiteral("fr"));
        QCOMPARE(card.value("target_language").toString(), QStringLiteral("es"));
        QCOMPARE(card.value("article").toString(), QStringLiteral("la"));
        QCOMPARE(warnings.size(), 0);
    }

    void language_settings() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto file = directory.filePath(QStringLiteral("settings.ini"));
        const QStringList languages{QStringLiteral("eng"), QStringLiteral("deu"), QStringLiteral("ukr")};
        {
            showthis::AppSettings settings(file);
            QCOMPARE(settings.ocr_language(languages), QStringLiteral("eng+deu+ukr"));
            settings.set_ocr_language(QStringLiteral("deu"));
        }
        {
            showthis::AppSettings settings(file);
            QCOMPARE(settings.ocr_language(languages), QStringLiteral("deu"));
            // A removed language must not leave the selector or engine invalid.
            QCOMPARE(settings.ocr_language({QStringLiteral("eng")}), QStringLiteral("eng"));
            QCOMPARE(settings.ocr_language({}), QString{});
            settings.set_ocr_language(QStringLiteral("eng+ukr"));
        }
        showthis::AppSettings settings(file);
        QCOMPARE(settings.ocr_language(languages), QStringLiteral("eng+ukr"));
        QCOMPARE(settings.ocr_language({QStringLiteral("eng"), QStringLiteral("deu")}), QStringLiteral("eng"));
    }

    void system_palette() {
        const auto original = QGuiApplication::palette();
        const auto restore = qScopeGuard([&]() { QGuiApplication::setPalette(original); });
        showthis::ShoWSurface surface;
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        auto *root = surface.rootObject();
        auto *toolbar = root->findChild<QObject *>(QStringLiteral("toolbar"));
        auto *popup = root->findChild<QObject *>(QStringLiteral("wordPopup"));
        auto *text = root->findChild<QObject *>(QStringLiteral("popupText"));
        auto *card = find_item(surface.contentItem(), QStringLiteral("personal_card_pane"));
        QVERIFY(toolbar);
        QVERIFY(popup);
        QVERIFY(text);
        QVERIFY(card);
        auto *card_background = card->property("background").value<QQuickItem *>();
        QVERIFY(card_background);

        for (const bool dark : {true, false}) {
            QPalette palette = original;
            const QColor background(dark ? "#202020" : "#fafafa");
            const QColor foreground(dark ? "#eeeeee" : "#111111");
            palette.setColor(QPalette::Window, background);
            palette.setColor(QPalette::WindowText, foreground);
            palette.setColor(QPalette::Base, background);
            QGuiApplication::setPalette(palette);
            QTRY_COMPARE(toolbar->property("color").value<QColor>(), background);
            QTRY_COMPARE(popup->property("color").value<QColor>(), background);
            QTRY_COMPARE(text->property("color").value<QColor>(), foreground);
            QTRY_COMPARE(card_background->property("color").value<QColor>(), background);
        }
        QCOMPARE(warnings.size(), 0);
    }

    void hover_and_updates() {
        showthis::ShoWSurface surface;
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(800, 600);
        QImage screenshot(1600, 1200, QImage::Format_RGB32);
        screenshot.fill(Qt::white);
        surface.set_image(screenshot, {{QStringLiteral("hello"), QRect(200, 400, 200, 60)}});
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QCOMPARE(surface.image_size(), screenshot.size());
        QCOMPARE(surface.words().size(), 1);

        auto *root = surface.rootObject();
        auto *popup = root->findChild<QQuickItem *>(QStringLiteral("wordPopup"));
        QVERIFY(popup);
        QVERIFY(!popup->isVisible());
        QTest::mouseMove(&surface, QPoint(150, 215));
        QTRY_COMPARE(root->property("hoveredWord").toString(), QStringLiteral("hello"));
        QVERIFY(popup->isVisible());

        QTest::mouseMove(&surface, QPoint(50, 400));
        QTRY_VERIFY(!popup->isVisible());

        surface.set_words({{QStringLiteral("edge"), QRect(1540, 1140, 60, 60)}});
        QTest::mouseMove(&surface, QPoint(780, 580));
        QTRY_COMPARE(root->property("hoveredWord").toString(), QStringLiteral("edge"));
        QVERIFY(popup->x() >= 0);
        QVERIFY(popup->y() >= 0);
        QVERIFY(popup->x() + popup->width() <= surface.width());
        QVERIFY(popup->y() + popup->height() <= surface.height());

        surface.set_words({});
        QTRY_VERIFY(!popup->isVisible());
        const auto previousSource = surface.image_source();
        surface.set_image(screenshot);
        QVERIFY(surface.image_source() != previousSource);
        QCOMPARE(warnings.size(), 0);
    }

    void language_selector() {
        showthis::ShoWSurface surface;
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.set_languages({QStringLiteral("eng"), QStringLiteral("deu"), QStringLiteral("ukr")});
        QCOMPARE(surface.language(), QStringLiteral("eng+deu+ukr"));
        surface.resize(800, 600);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QSignalSpy changes(&surface, &showthis::ShoWSurface::selectedLanguageChanged);

        auto *root = surface.rootObject();
        auto *selector = root->findChild<QQuickItem *>(QStringLiteral("languageSelector"));
        QVERIFY(selector);
        const auto click = [&](QQuickItem *item) {
            // Let the popup's scroll view finish its layout before hit-testing.
            QTest::qWait(30);
            QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
                             item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        click(selector);
        auto *dropdown = root->findChild<QObject *>(QStringLiteral("languagePopup"));
        QVERIFY(dropdown);
        QTRY_VERIFY(dropdown->property("opened").toBool());
        auto *german = find_item(surface.contentItem(), QStringLiteral("languageChoice_deu"));
        auto *apply = find_item(surface.contentItem(), QStringLiteral("applyLanguages"));
        auto *cancel = find_item(surface.contentItem(), QStringLiteral("cancelLanguages"));
        QVERIFY(german);
        QVERIFY(apply);
        QVERIFY(cancel);
        click(german);
        QVERIFY(!german->property("checked").toBool());
        QCOMPARE(surface.language(), QStringLiteral("eng+deu+ukr"));
        QCOMPARE(changes.size(), 0);
        click(apply);
        QTRY_COMPARE(surface.language(), QStringLiteral("eng+ukr"));
        QCOMPARE(changes.size(), 1);

        click(selector);
        QTRY_VERIFY(dropdown->property("opened").toBool());
        click(german);
        click(cancel);
        QCOMPARE(surface.language(), QStringLiteral("eng+ukr"));
        click(selector);
        QTRY_VERIFY(dropdown->property("opened").toBool());
        QVERIFY(!german->property("checked").toBool());
        click(cancel);

        surface.set_language(QStringLiteral("missing"));
        surface.set_language(QString{});
        surface.set_language(QStringLiteral("eng+missing"));
        QCOMPARE(surface.language(), QStringLiteral("eng+ukr"));
        surface.set_language(QStringLiteral("ukr+eng+eng"));
        QCOMPARE(surface.language(), QStringLiteral("eng+ukr"));
        QCOMPARE(changes.size(), 1);
        surface.set_scanning(true);
        QVERIFY(!selector->property("enabled").toBool());
        surface.set_scanning(false);
        QVERIFY(selector->property("enabled").toBool());
        surface.set_language(QStringLiteral("eng"));
        click(selector);
        QTRY_VERIFY(dropdown->property("opened").toBool());
        auto *english = find_item(surface.contentItem(), QStringLiteral("languageChoice_eng"));
        QVERIFY(english);
        QVERIFY(english->isEnabled());
        click(cancel);
        surface.set_languages({QStringLiteral("eng")});
        QCOMPARE(surface.language(), QStringLiteral("eng"));
        QCOMPARE(warnings.size(), 0);
    }

    void ocr_selection_persistence() {
        QTemporaryDir directory;
        const auto settings_file = directory.filePath(QStringLiteral("settings.ini"));
        const QStringList languages{QStringLiteral("eng"), QStringLiteral("deu"), QStringLiteral("ukr")};
        showthis::AppSettings settings(settings_file);
        settings.set_ocr_language(QStringLiteral("eng"));
        showthis::ShoWSurface surface;
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.set_languages(languages);
        surface.set_language(settings.ocr_language(languages));
        QObject::connect(&surface, &showthis::ShoWSurface::selectedLanguageChanged,
            &surface, [&]() { settings.set_ocr_language(surface.language()); });
        surface.resize(800, 600);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        const auto click = [&](QQuickItem *item) {
            QTest::qWait(30);
            QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        auto *selector = find_item(surface.contentItem(), QStringLiteral("languageSelector"));
        QVERIFY(selector);
        click(selector);
        auto *popup = surface.rootObject()->findChild<QObject *>(QStringLiteral("languagePopup"));
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        auto *english = find_item(surface.contentItem(), QStringLiteral("languageChoice_eng"));
        auto *german = find_item(surface.contentItem(), QStringLiteral("languageChoice_deu"));
        auto *ukrainian = find_item(surface.contentItem(), QStringLiteral("languageChoice_ukr"));
        auto *apply = find_item(surface.contentItem(), QStringLiteral("applyLanguages"));
        auto *cancel = find_item(surface.contentItem(), QStringLiteral("cancelLanguages"));
        QVERIFY(english);
        QVERIFY(german);
        QVERIFY(ukrainian);
        QVERIFY(apply);
        QVERIFY(cancel);
        QVERIFY(english->isEnabled());

        // Regression: checking a second language used to disable it before
        // onClicked could update the draft, leaving only English saved.
        click(german);
        QVERIFY(german->isEnabled());
        QVERIFY(german->property("checked").toBool());
        click(apply);
        QCOMPARE(surface.language(), QStringLiteral("eng+deu"));
        {
            showthis::AppSettings reopened(settings_file);
            QCOMPARE(reopened.ocr_language(languages), QStringLiteral("eng+deu"));
            showthis::ShoWSurface restarted;
            restarted.set_languages(languages);
            restarted.set_language(reopened.ocr_language(languages));
            QCOMPARE(restarted.language(), QStringLiteral("eng+deu"));
        }

        click(selector);
        QTRY_VERIFY(popup->property("opened").toBool());
        click(english);
        click(apply);
        QCOMPARE(surface.language(), QStringLiteral("deu"));

        click(selector);
        QTRY_VERIFY(popup->property("opened").toBool());
        click(german);
        QVERIFY(!apply->isEnabled());
        QVERIFY(english->isEnabled());
        QVERIFY(german->isEnabled());
        QVERIFY(ukrainian->isEnabled());
        QCOMPARE(surface.language(), QStringLiteral("deu"));
        click(cancel);
        QCOMPARE(surface.language(), QStringLiteral("deu"));

        click(selector);
        QTRY_VERIFY(popup->property("opened").toBool());
        QVERIFY(german->property("checked").toBool());
        click(ukrainian);
        click(apply);
        QCOMPARE(surface.language(), QStringLiteral("deu+ukr"));
        showthis::AppSettings reopened(settings_file);
        QCOMPARE(reopened.ocr_language(languages), QStringLiteral("deu+ukr"));
        QCOMPARE(warnings.size(), 0);
    }

    void escape_closes_notebook_before_hiding_overlay_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("desktop") << QSize(1000, 700);
        QTest::newRow("compact") << QSize(640, 700);
    }

    void escape_closes_notebook_before_hiding_overlay() {
        QFETCH(QSize, size);
        showthis::ShoWSurface surface;
        QCOMPARE(surface.status(), QQuickView::Ready);
        surface.resize(size);
        QImage screenshot(size, QImage::Format_RGB32);
        screenshot.fill(Qt::white);
        surface.set_image(screenshot);
        const auto image_source = surface.image_source();
        TestCaptureBackend backend;
        showthis::OverlayController controller(surface, backend,
            [](const QImage &, const QString &) { return showthis::ScanResult{}; });
        QSignalSpy hide_requests(&surface, &showthis::ShoWSurface::hide_requested);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        controller.show_notebook();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = find_item(surface.contentItem(), QStringLiteral("learning_panel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QStringLiteral("word")),
            Q_ARG(QString, QStringLiteral("Original sentence"))));
        auto *meaning = find_item(surface.contentItem(), QStringLiteral("word_meaning"));
        QVERIFY(meaning);
        meaning->setProperty("text", QStringLiteral("Keep this unsaved meaning"));
        panel->setProperty("compact_page", 1);
        meaning->forceActiveFocus();
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!panel->isVisible());
        QVERIFY(surface.isVisible());
        QCOMPARE(hide_requests.size(), 0);
        QCOMPARE(surface.image_source(), image_source);
        QTRY_VERIFY(surface.rootObject()->hasActiveFocus());
        QVERIFY(panel->property("dirty").toBool());
        QCOMPARE(meaning->property("text").toString(), QStringLiteral("Keep this unsaved meaning"));

        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!surface.isVisible());
        QCOMPARE(hide_requests.size(), 1);
        QVERIFY(panel->property("dirty").toBool());
        QCOMPARE(meaning->property("text").toString(), QStringLiteral("Keep this unsaved meaning"));

        controller.show_notebook();
        QTRY_VERIFY(surface.isVisible());
        QVERIFY(panel->isVisible());
        QCOMPARE(meaning->property("text").toString(), QStringLiteral("Keep this unsaved meaning"));
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!panel->isVisible());
        QVERIFY(surface.isVisible());
        QCOMPARE(hide_requests.size(), 1);
        auto *hide_button = find_item(surface.contentItem(), QStringLiteral("hide_overlay"));
        QVERIFY(hide_button);
        QTest::qWait(30);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
            hide_button->mapToScene(QPointF(hide_button->width() / 2, hide_button->height() / 2)).toPoint());
        QTRY_VERIFY(!surface.isVisible());
        QCOMPARE(hide_requests.size(), 2);
        QCOMPARE(backend.capture_count, 0);
        QCOMPARE(warnings.size(), 0);
    }

    void capture_respects_latest_visibility() {
        showthis::ShoWSurface surface;
        surface.set_languages({QStringLiteral("eng")});
        TestCaptureBackend backend;
        std::atomic<int> scans{0};
        showthis::OverlayController controller(surface, backend,
            [&](const QImage &, const QString &) {
                ++scans;
                return showthis::ScanResult{};
            });
        QImage image(400, 300, QImage::Format_RGB32);
        image.fill(Qt::white);
        controller.toggle_overlay();
        QCOMPARE(backend.capture_count, 1);
        QVERIFY(!surface.isVisible());
        controller.toggle_overlay(); // Hide while the first capture is pending.
        backend.complete(image);
        QVERIFY(!surface.isVisible());
        QCOMPARE(scans.load(), 0);

        controller.toggle_overlay();
        controller.toggle_overlay();
        controller.toggle_overlay(); // Show wins; the same pending capture is reused.
        QCOMPARE(backend.capture_count, 2);
        backend.complete(image);
        QVERIFY(surface.isVisible());
        controller.hide_overlay();
        QTRY_VERIFY(!surface.scanning());
        QVERIFY(!surface.isVisible());
        QCOMPARE(scans.load(), 1);
    }

    void toggling_during_ocr_is_immediate() {
        showthis::ShoWSurface surface;
        surface.set_languages({QStringLiteral("eng")});
        TestCaptureBackend backend;
        QSemaphore started;
        QSemaphore release;
        std::atomic<int> scans{0};
        showthis::OverlayController controller(surface, backend,
            [&](const QImage &, const QString &) {
                ++scans;
                started.release();
                release.acquire();
                return showthis::ScanResult{};
            });
        const auto unblock_on_failure = qScopeGuard([&]() { release.release(10); });
        QImage image(400, 300, QImage::Format_RGB32);
        image.fill(Qt::white);
        controller.toggle_overlay();
        backend.complete(image);
        QVERIFY(started.tryAcquire(1, 1000));
        QVERIFY(surface.scanning());
        for (int i = 0; i < 10; ++i) {
            controller.toggle_overlay();
            QVERIFY(!surface.isVisible());
            controller.toggle_overlay();
            QVERIFY(surface.isVisible());
        }
        QCOMPARE(backend.capture_count, 1);
        QCOMPARE(scans.load(), 1);
        controller.hide_overlay();
        release.release();
        QTRY_VERIFY(!surface.scanning());
        QVERIFY(!surface.isVisible()); // Finishing OCR never reopens a hidden overlay.
    }

    void ocr_coalesces_language_changes() {
        showthis::ShoWSurface surface;
        surface.set_languages({QStringLiteral("eng"), QStringLiteral("deu"), QStringLiteral("ukr")});
        surface.set_language(QStringLiteral("eng"));
        TestCaptureBackend backend;
        QSemaphore started;
        QSemaphore release;
        std::atomic<int> scans{0};
        std::atomic<int> active{0};
        std::atomic<bool> overlapped{false};
        showthis::OverlayController controller(surface, backend,
            [&](const QImage &, const QString &language) {
                if (active.fetch_add(1) > 0) overlapped = true;
                ++scans;
                started.release();
                release.acquire();
                --active;
                return showthis::ScanResult{{{language, QRect(1, 1, 10, 10), {}}}, {}};
            });
        const auto unblock_on_failure = qScopeGuard([&]() { release.release(10); });
        QImage image(400, 300, QImage::Format_RGB32);
        image.fill(Qt::white);
        controller.toggle_overlay();
        backend.complete(image);
        QVERIFY(started.tryAcquire(1, 1000));
        surface.set_language(QStringLiteral("deu"));
        surface.set_language(QStringLiteral("ukr"));
        release.release();
        QTRY_COMPARE(scans.load(), 2);
        QVERIFY(started.tryAcquire(1, 1000));
        QVERIFY(surface.words().isEmpty()); // The obsolete English result was not published.
        release.release();
        QTRY_VERIFY(!surface.scanning());
        QCOMPARE(surface.words().size(), 1);
        QCOMPARE(surface.words()[0].toMap().value("text").toString(), QStringLiteral("ukr"));
        QVERIFY(!overlapped.load());
        QCOMPARE(backend.capture_count, 1);
        controller.hide_overlay();
    }

    void capture_failure_is_recoverable() {
        showthis::ShoWSurface surface;
        surface.set_languages({QStringLiteral("eng")});
        TestCaptureBackend backend;
        showthis::OverlayController controller(surface, backend,
            [](const QImage &, const QString &) { return showthis::ScanResult{}; });
        controller.toggle_overlay();
        backend.fail(QStringLiteral("Temporary capture failure"));
        QCOMPARE(surface.error(), QStringLiteral("Temporary capture failure"));
        QVERIFY(surface.isVisible());
        controller.toggle_overlay();
        controller.toggle_overlay();
        QCOMPARE(backend.capture_count, 2);
        QImage image(400, 300, QImage::Format_RGB32);
        image.fill(Qt::white);
        backend.complete(image);
        QTRY_VERIFY(!surface.scanning());
        QVERIFY(surface.error().isEmpty());
        controller.hide_overlay();
    }

    void ocr_failure_is_recoverable() {
        showthis::ShoWSurface surface;
        surface.set_languages({QStringLiteral("eng")});
        TestCaptureBackend backend;
        std::atomic<int> scans{0};
        showthis::OverlayController controller(surface, backend,
            [&](const QImage &, const QString &) {
                if (++scans == 1)
                    throw std::runtime_error("Simulated OCR failure");
                return showthis::ScanResult{};
            });
        QImage image(400, 300, QImage::Format_RGB32);
        image.fill(Qt::white);
        controller.toggle_overlay();
        backend.complete(image);
        QTRY_VERIFY(!surface.scanning());
        QCOMPARE(surface.error(), QStringLiteral("Simulated OCR failure"));
        QVERIFY(surface.isVisible());
        surface.hide(); // A native hide must not consume the next show request.
        controller.toggle_overlay();
        QCOMPARE(backend.capture_count, 2);
        backend.complete(image);
        QTRY_VERIFY(!surface.scanning());
        QVERIFY(surface.isVisible());
        QVERIFY(surface.error().isEmpty());
        QCOMPARE(scans.load(), 2);
        controller.hide_overlay();
    }

    void word_outlines_checkbox() {
        showthis::ShoWSurface surface;
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(1000, 700);
        QImage image(1000, 700, QImage::Format_RGB32);
        image.fill(Qt::white);
        surface.set_image(image, {{QStringLiteral("Haus"), QRect(100, 200, 100, 30)}});
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *radio = find_item(surface.contentItem(), QStringLiteral("word_outlines"));
        auto *word = find_item(surface.contentItem(), QStringLiteral("word_outline_0"));
        QVERIFY(radio);
        QVERIFY(word);
        QVERIFY(radio->property("checked").toBool());
        QVERIFY(QString::fromLatin1(radio->metaObject()->className()).contains(QStringLiteral("CheckBox")));
        auto *border = word->property("border").value<QObject *>();
        QVERIFY(border);
        QCOMPARE(border->property("width").toInt(), 1);
        const auto point = radio->mapToScene(QPointF(radio->width() / 2, radio->height() / 2)).toPoint();
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, point);
        QTRY_VERIFY(!radio->property("checked").toBool());
        QTRY_COMPARE(border->property("width").toInt(), 0);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, point);
        QTRY_VERIFY(radio->property("checked").toBool());
        QTRY_COMPARE(border->property("width").toInt(), 1);
        QCOMPARE(warnings.size(), 0);
    }

    void movable_toolbar() {
        showthis::ShoWSurface surface;
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(1000, 700);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *root = surface.rootObject();
        auto *toolbar = root->findChild<QQuickItem *>(QStringLiteral("toolbar"));
        auto *handle = root->findChild<QQuickItem *>(QStringLiteral("toolbarDragHandle"));
        QVERIFY(toolbar);
        QVERIFY(handle);
        QCOMPARE(toolbar->x(), (surface.width() - toolbar->width()) / 2);
        QVERIFY(toolbar->y() > surface.height() / 2);
        const auto before = toolbar->position();
        const auto start = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
        QTest::mousePress(&surface, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&surface, start + QPoint(40, -200));
        QTest::mouseRelease(&surface, Qt::LeftButton, Qt::NoModifier, start + QPoint(40, -200));
        QTRY_VERIFY(toolbar->y() < before.y() - 100);
        QVERIFY(toolbar->property("moved").toBool());

        surface.resize(500, 400);
        QTRY_VERIFY(toolbar->x() + toolbar->width() <= surface.width());
        QTRY_VERIFY(toolbar->y() + toolbar->height() <= surface.height());
        const auto reset = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
        QTest::mouseDClick(&surface, Qt::LeftButton, Qt::NoModifier, reset);
        QTRY_VERIFY(!toolbar->property("moved").toBool());
        QCOMPARE(warnings.size(), 0);
    }
};

QTEST_MAIN(OverlayTest)
#include "OverlayTest.moc"
