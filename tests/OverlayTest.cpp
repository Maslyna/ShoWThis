#include <QQmlEngine>
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
            {"dictionary_cache", QJsonObject{{"de:Haus", data}}}}).toJson());
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
        QCOMPARE(translation->property("text").toString(), translator_configured ? QString::fromUtf8("будинок") : QString{});
        QCOMPARE(translation->isVisible(), translator_configured);
        QCOMPARE(panel->property("word_prefix").toString(), QString{});
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

    void translator_configuration_controls_the_word_card() {
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
        QVERIFY(!translation->isVisible());
        QVERIFY(!target->isVisible());
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
        QVERIFY(!translation->isVisible());

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
        QTRY_VERIFY(!translation->isVisible());
        QVERIFY(!target->isVisible());
        QVERIFY(!translate->isVisible());
        QVERIFY(translation->property("text").toString().isEmpty());
        QVERIFY(!save->isEnabled());
        meaning->setProperty("text", QStringLiteral("house"));
        QVERIFY(save->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QCOMPARE(service.vocabulary().size(), 1);
        QVERIFY(service.vocabulary()[0].toMap().value("translation").toString().isEmpty());
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
