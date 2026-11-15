#include <QApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "LearningService.h"
#include "ShoWSurface.h"
#include "TestNetwork.h"

namespace {
QQuickItem *find_item(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems()) {
        if (auto *item = find_item(child, name))
            return item;
    }
    return nullptr;
}

QQuickItem *settings_item(QQuickItem *dialog, const QString &name) {
    auto *popup = dialog->findChild<QObject *>("translator_settings_popup");
    auto *content = popup ? popup->property("contentItem").value<QQuickItem *>() : nullptr;
    return content ? find_item(content, name) : nullptr;
}

void click_item(QQuickItem *dialog, QQuickItem *item) {
    auto *window = dialog->window();
    // Bring controls into view through both the page and the nested editor scroll views.
    for (auto *parent = item->parentItem(); parent; parent = parent->parentItem()) {
        if (!parent->property("contentY").isValid() || !parent->property("contentHeight").isValid())
            continue;
        const auto rect = item->mapRectToItem(parent, QRectF(QPointF(), item->size()));
        const auto delta = rect.top() < 0 ? rect.top()
            : rect.bottom() > parent->height() ? rect.bottom() - parent->height() : 0.0;
        const auto maximum = qMax(0.0, parent->property("contentHeight").toReal() - parent->height());
        parent->setProperty("contentY", qBound(0.0, parent->property("contentY").toReal() + delta, maximum));
    }
    window->requestActivate();
    QTest::qWait(20);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
}

void enter_text(QQuickItem *dialog, QQuickItem *item, const QByteArray &text) {
    auto *window = dialog->window();
    click_item(dialog, item);
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    for (const auto character : text)
        QTest::keyClick(window, character);
}

void choose(QQuickItem *dialog, QQuickItem *item, const QByteArray &text) {
    enter_text(dialog, item, text);
    QTest::keyClick(dialog->window(), Qt::Key_Return);
}

void drag_handle(QQuickItem *dialog, const QString &name, const QPoint &delta) {
    auto *handle = find_item(dialog, name);
    Q_ASSERT(handle);
    auto *window = dialog->window();
    const auto start = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window, start + delta, 30);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + delta);
}

void show_card(QQuickItem *dialog) {
    auto *button = find_item(dialog, "compact_word_card");
    if (button && button->isVisible())
        click_item(dialog, button);
}

void configure(showthis::LearningService &service) {
    service.translator()->set_provider_id("libretranslate");
    service.translator()->set_option("base_url", "https://translator.invalid/test");
}
} // namespace

class TranslatorDialogTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings_directory;
    QPalette m_original_palette;

private slots:
    void initTestCase() {
        m_original_palette = QApplication::palette();
        QVERIFY(m_settings_directory.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings_directory.path());
    }

    void init() { QSettings{}.clear(); }
    void cleanup() { QApplication::setPalette(m_original_palette); }

    void standalone_explicit_request_and_reopen() {
        QTemporaryDir directory;
        TestNetwork network;
        network.automatic = false;
        network.response = R"({"translatedText":"bonjour"})";
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.set_source_language("de");
        service.set_target_language("en");
        configure(service);
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        const auto top_level_count = QApplication::topLevelWindows().size();
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        QApplication::clipboard()->setText("clipboard sentinel");
        surface.open_translator();
        auto *window = surface.translator_dialog();
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QVERIFY(surface.notebook_dialog());
        QVERIFY(!surface.notebook_dialog()->isVisible());
        QCOMPARE(window->objectName(), QString("translator_dialog"));
        QCOMPARE(window->window(), &surface);
        QCOMPARE(surface.notebook_dialog()->window(), &surface);
        QCOMPARE(QApplication::topLevelWindows().size(), top_level_count);
        auto *source = find_item(window, "translator_source_text");
        auto *result = find_item(window, "translator_result_text");
        auto *source_language = find_item(window, "translator_source_language");
        auto *target_language = find_item(window, "translator_target_language");
        auto *add = find_item(window, "translator_add_result");
        auto *add_source = find_item(window, "translator_add_source");
        auto *copy = find_item(window, "translator_copy");
        QVERIFY(source && result && source_language && target_language && add && add_source && copy);
        QVERIFY(network.requests.isEmpty());
        enter_text(window, source, "hello");
        choose(window, source_language, "en");
        choose(window, target_language, "fr");
        QCOMPARE(surface.translator_source_language(), QString("en"));
        QCOMPARE(surface.translator_target_language(), QString("fr"));
        QCOMPARE(service.source_language(), QString("de"));
        QCOMPARE(service.target_language(), QString("en"));
        QVERIFY(network.requests.isEmpty());
        QVERIFY(!add->isEnabled());
        QVERIFY(!add_source->isEnabled());
        click_item(window, source);
        QTest::keyClick(&surface, Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE(network.requests.size(), 1);
        QVERIFY(service.translator()->session_busy());
        QVERIFY(!service.translator()->busy());
        const auto payload = QJsonDocument::fromJson(network.payloads.last()).object();
        QCOMPARE(payload.value("q").toString(), QString("hello"));
        QCOMPARE(payload.value("source").toString(), QString("en"));
        QCOMPARE(payload.value("target").toString(), QString("fr"));
        QVERIFY(!payload.contains("context"));
        QCOMPARE(network.requests.last().url().toString(), QString("https://translator.invalid/test/translate"));
        network.last_reply->complete();
        QTRY_COMPARE(result->property("text").toString(), QString("bonjour"));
        QCOMPARE(QApplication::clipboard()->text(), QString("clipboard sentinel"));
        QVERIFY(!add->isEnabled());
        click_item(window, copy);
        QCOMPARE(QApplication::clipboard()->text(), QString("bonjour"));
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!window->isVisible());
        surface.open_translator();
        QCOMPARE(surface.translator_dialog(), window);
        QTRY_VERIFY(window->isVisible());
        QCOMPARE(source->property("text").toString(), QString("hello"));
        QCOMPARE(result->property("text").toString(), QString("bonjour"));
        QCOMPARE(surface.translator_target_language(), QString("fr"));
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(!surface.notebook_dialog()->isVisible());
        QCOMPARE(warnings.size(), 0);
    }

    void language_search_escape_does_not_close_translator() {
        QTemporaryDir directory;
        TestNetwork network;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.open_translator();
        auto *window = surface.translator_dialog();
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *language = find_item(window, "translator_source_language");
        QVERIFY(language);
        const auto original = surface.translator_source_language();
        enter_text(window, language, "Fre");
        auto *popup = language->property("popup").value<QObject *>();
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(window->isVisible());
        QCOMPARE(surface.translator_source_language(), original);
        QVERIFY(network.requests.isEmpty());
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!window->isVisible());
    }

    void overlay_dialogs_resize_and_visibility_data() {
        QTest::addColumn<QSizeF>("size");
        QTest::newRow("minimum") << QSizeF(360, 400);
        QTest::newRow("desktop") << QSizeF(900, 700);
    }

    void overlay_dialogs_resize_and_visibility() {
        QFETCH(QSizeF, size);
        QTemporaryDir directory;
        TestNetwork network;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.resize(2000, 1000);
        const auto top_level_count = QApplication::topLevelWindows().size();
        surface.open_notebook_word("Haus", "Original context");
        auto *notebook = surface.notebook_dialog();
        QVERIFY(notebook);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        surface.open_translator("Haus", "de", "translation");
        auto *window = surface.translator_dialog();
        QVERIFY(window);
        QTRY_VERIFY(window->isVisible());
        window->setPosition(QPointF(1100, 0));
        window->setSize(size);
        QTRY_COMPARE(window->size(), size);
        QCOMPARE(window->property("minimumWidth").toReal(), 360.0);
        QCOMPARE(window->property("minimumHeight").toReal(), 400.0);
        QCOMPARE(window->window(), &surface);
        QCOMPARE(notebook->window(), &surface);
        QCOMPARE(QApplication::topLevelWindows().size(), top_level_count);
        QVERIFY(notebook->isVisible());
        auto *source = find_item(window, "translator_source_text");
        auto *result = find_item(window, "translator_result_text");
        auto *clear = find_item(window, "translator_clear");
        QVERIFY(source && result && clear);
        QTRY_VERIFY(source->width() > 80 && source->height() > 40);
        QTRY_VERIFY(result->width() > 80 && result->height() > 40);
        if (size.width() < 640)
            QTRY_VERIFY(result->mapToScene(QPointF()).y() > source->mapToScene(QPointF()).y());
        else
            QTRY_VERIFY(result->mapToScene(QPointF()).x() > source->mapToScene(QPointF()).x());
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS")) {
            QTest::qWait(30);
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS")
                + (size.width() < 640 ? "/translator-compact.png" : "/translator-wide.png")));
        }
        click_item(window, clear);
        QCOMPARE(source->property("text").toString(), QString());
        surface.close_notebook();
        QTRY_VERIFY(!notebook->isVisible());
        QVERIFY(window->isVisible());
        surface.open_notebook_word("Baum", "");
        QTRY_VERIFY(notebook->isVisible());
        click_item(window, source);
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(notebook->isVisible());
        QVERIFY(network.requests.isEmpty());
    }

    void dialogs_move_resize_focus_and_remain_bounded() {
        QTemporaryDir directory;
        TestNetwork network;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.resize(2000, 1100);
        const auto top_level_count = QApplication::topLevelWindows().size();
        surface.open_notebook_word("Haus", "Original context");
        surface.open_translator("independent source", "en", "notes");
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *notebook = surface.notebook_dialog();
        auto *translator = surface.translator_dialog();
        QVERIFY(notebook && translator);
        notebook->setPosition(QPointF(20, 20));
        translator->setPosition(QPointF(1160, 20));
        const auto translator_position = translator->position();
        const auto translator_size = translator->size();
        const auto notebook_position = notebook->position();
        drag_handle(notebook, "dialog_drag_handle", QPoint(30, 50));
        QTRY_COMPARE(notebook->position(), notebook_position + QPointF(30, 50));
        QCOMPARE(translator->position(), translator_position);
        QVERIFY(notebook->hasActiveFocus());
        QVERIFY(notebook->z() > translator->z());
        const auto notebook_size = notebook->size();
        drag_handle(notebook, "dialog_resize_handle", QPoint(-120, -100));
        QTRY_COMPARE(notebook->size(), notebook_size - QSizeF(120, 100));
        QCOMPARE(translator->size(), translator_size);
        const auto moved_notebook = notebook->position();
        const auto resized_notebook = notebook->size();
        drag_handle(translator, "dialog_drag_handle", QPoint(-30, 40));
        QTRY_COMPARE(translator->position(), translator_position + QPointF(-30, 40));
        QVERIFY(translator->hasActiveFocus());
        QVERIFY(translator->z() > notebook->z());
        drag_handle(translator, "dialog_resize_handle", QPoint(-80, -60));
        QTRY_COMPARE(translator->size(), translator_size - QSizeF(80, 60));
        QCOMPARE(notebook->position(), moved_notebook);
        QCOMPARE(notebook->size(), resized_notebook);
        QVERIFY(notebook->property("user_geometry").toBool());
        QVERIFY(translator->property("user_geometry").toBool());
        auto *source = find_item(translator, "translator_source_text");
        QVERIFY(source);
        click_item(notebook, find_item(notebook, "dialog_close"));
        QVERIFY(!notebook->isVisible());
        QVERIFY(translator->isVisible());
        QCOMPARE(source->property("text").toString(), QString("independent source"));
        surface.open_notebook();
        QCOMPARE(notebook->position(), moved_notebook);
        QCOMPARE(notebook->size(), resized_notebook);
        QCOMPARE(QApplication::topLevelWindows().size(), top_level_count);
        surface.resize(300, 250);
        QTRY_COMPARE(notebook->size(), QSizeF(300, 250));
        QTRY_COMPARE(translator->size(), QSizeF(300, 250));
        QCOMPARE(notebook->position(), QPointF(0, 0));
        QCOMPARE(translator->position(), QPointF(0, 0));
    }

    void default_system_palette_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void default_system_palette() {
        QFETCH(bool, dark);
        auto palette = m_original_palette;
        palette.setColor(QPalette::Window, dark ? QColor("#252a31") : QColor("#f7f4ec"));
        palette.setColor(QPalette::Base, dark ? QColor("#1c2026") : QColor("#fffdf8"));
        palette.setColor(QPalette::Button, dark ? QColor("#303741") : QColor("#ebe6dc"));
        palette.setColor(QPalette::Text, dark ? QColor("#edf1f5") : QColor("#24211c"));
        palette.setColor(QPalette::WindowText, palette.text().color());
        palette.setColor(QPalette::ButtonText, palette.text().color());
        palette.setColor(QPalette::Highlight, dark ? QColor("#b88bc9") : QColor("#7d397e"));
        palette.setColor(QPalette::HighlightedText, dark ? QColor("#24152b") : QColor("#fff6fa"));
        QApplication::setPalette(palette);
        QTemporaryDir directory;
        TestNetwork network;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        configure(service);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.open_translator("hello", "en", "notes");
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *dialog = surface.translator_dialog();
        auto *source = find_item(dialog, "translator_source_text");
        auto *result = find_item(dialog, "translator_result_text");
        auto *translate = find_item(dialog, "translator_translate");
        auto *error = find_item(dialog, "translator_error");
        QVERIFY(source && result && translate && error);
        QTRY_COMPARE(source->property("color").value<QColor>(), palette.text().color());
        QCOMPARE(result->property("color").value<QColor>(), palette.text().color());
        QCOMPARE(source->property("selectionColor").value<QColor>(), palette.highlight().color());
        QCOMPARE(source->property("selectedTextColor").value<QColor>(), palette.highlightedText().color());
        QCOMPARE(error->property("color").value<QColor>(), palette.windowText().color());
        auto *background = translate->property("background").value<QQuickItem *>();
        QVERIFY(background);
        QCOMPARE(background->property("color").value<QColor>(), palette.highlight().color());
    }

    void explicit_append_keeps_draft_and_persists_same_card() {
        QTemporaryDir directory;
        const auto path = directory.filePath("words.json");
        TestNetwork network;
        network.response = R"({"translatedText":"maison"})";
        showthis::LearningService service(nullptr, path, &network);
        service.set_source_language("de");
        service.set_target_language("en");
        configure(service);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.resize(2000, 1000);
        surface.open_notebook_word("Haus", "Original context");
        auto *notebook = surface.notebook_dialog();
        QVERIFY(notebook);
        notebook->setSize(QSizeF(1100, 850));
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        show_card(notebook);
        auto *translation = find_item(notebook, "word_translation");
        auto *context = find_item(notebook, "word_context");
        auto *card_form = find_item(notebook, "personal_card_pane");
        auto *save = find_item(notebook, "save_word");
        auto *panel = find_item(notebook, "learning_panel");
        QVERIFY(translation && context && card_form && save && panel);
        enter_text(notebook, translation, "original");
        click_item(notebook, save);
        QTRY_COMPARE(service.vocabulary().size(), 1);
        const auto id = service.vocabulary()[0].toMap().value("id").toString();
        surface.open_translator("Haus", "de", "translation");
        auto *window = surface.translator_dialog();
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *result = find_item(window, "translator_result_text");
        auto *target_language = find_item(window, "translator_target_language");
        auto *translate = find_item(window, "translator_translate");
        auto *add = find_item(window, "translator_add_result");
        auto *add_source = find_item(window, "translator_add_source");
        auto *field = find_item(window, "translator_card_field");
        auto *feedback = find_item(window, "translator_feedback");
        QVERIFY(result && target_language && translate && add && add_source && field && feedback);
        choose(window, target_language, "fr");
        click_item(window, translate);
        QTRY_COMPARE(result->property("text").toString(), QString("maison"));
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS")) {
            window->setSize(QSizeF(760, 600));
            QTRY_COMPARE(window->width(), 760);
            QTRY_VERIFY(result->mapToScene(QPointF()).x() > find_item(window, "translator_source_text")->mapToScene(QPointF()).x());
            QTest::mouseMove(&surface, QPoint(4, 4));
            QTest::qWait(30);
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS") + "/translator-desktop.png"));
        }
        QCOMPARE(translation->property("text").toString(), QString("original"));
        QVERIFY(!panel->property("dirty").toBool());
        click_item(window, add);
        click_item(window, add);
        QCOMPARE(translation->property("text").toString(), QString("original\nmaison\nmaison"));
        QVERIFY(panel->property("dirty").toBool());
        QCOMPARE(service.target_language(), QString("fr"));
        QCOMPARE(surface.selected_card_id(), id);
        QVERIFY(feedback->property("text").toString().contains("draft"));
        QCOMPARE(service.vocabulary()[0].toMap().value("translation").toString(), QString("original"));
        choose(window, field, "Notes");
        enter_text(window, result, "private note");
        click_item(window, add);
        QCOMPARE(card_form->property("notes").toString(), QString("private note"));
        choose(window, target_language, "es");
        click_item(window, add_source);
        QCOMPARE(context->property("text").toString(), QString("Original context\nHaus"));
        QCOMPARE(service.target_language(), QString("fr"));
        QVERIFY(service.vocabulary()[0].toMap().value("notes").toString().isEmpty());
        click_item(window, result);
        QTest::keyClick(&surface, Qt::Key_S, Qt::ControlModifier);
        QVERIFY(panel->property("dirty").toBool());
        QVERIFY(service.vocabulary()[0].toMap().value("notes").toString().isEmpty());
        click_item(notebook, save);
        QTRY_VERIFY(!panel->property("dirty").toBool());
        QCOMPARE(service.vocabulary().size(), 1);
        const auto card = service.vocabulary()[0].toMap();
        QCOMPARE(card.value("id").toString(), id);
        QCOMPARE(card.value("translation").toString(), QString("original\nmaison\nmaison"));
        QCOMPARE(card.value("notes").toString(), QString("private note"));
        QCOMPARE(card.value("context").toString(), QString("Original context\nHaus"));
        QCOMPARE(card.value("target_language").toString(), QString("fr"));
        showthis::LearningService reopened(nullptr, path, &network);
        QCOMPARE(reopened.vocabulary().size(), 1);
        QCOMPARE(reopened.vocabulary()[0].toMap().value("id").toString(), id);
        QCOMPARE(reopened.vocabulary()[0].toMap().value("translation").toString(), QString("original\nmaison\nmaison"));
    }

    void card_changes_do_not_redirect_or_apply_session_automatically() {
        QTemporaryDir directory;
        TestNetwork network;
        network.automatic = false;
        network.response = R"({"translatedText":"tree"})";
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.set_source_language("de");
        service.set_target_language("en");
        configure(service);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.resize(2000, 1000);
        surface.open_notebook_word("Haus", "notebook-only private context");
        surface.open_translator("Baum", "de", "translation");
        auto *window = surface.translator_dialog();
        auto *notebook = surface.notebook_dialog();
        QVERIFY(window && notebook);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *translate = find_item(window, "translator_translate");
        auto *result = find_item(window, "translator_result_text");
        auto *target = find_item(window, "translator_selected_card");
        auto *add = find_item(window, "translator_add_result");
        QVERIFY(translate && result && target && add);
        click_item(window, translate);
        QTRY_VERIFY(service.translator()->session_busy());
        surface.open_notebook_word("Baum", "other private context");
        QTRY_COMPARE(surface.selected_card_word(), QString("Baum"));
        QVERIFY(target->property("text").toString().contains("Baum"));
        QVERIFY(service.translator()->session_busy());
        service.set_source_language("it");
        QCOMPARE(surface.translator_source_language(), QString("de"));
        QVERIFY(service.translator()->session_busy());
        QCOMPARE(network.requests.size(), 1);
        const auto payload = QJsonDocument::fromJson(network.payloads.last()).object();
        QCOMPARE(payload.value("q").toString(), QString("Baum"));
        QVERIFY(!payload.contains("context"));
        network.last_reply->complete();
        QTRY_COMPARE(result->property("text").toString(), QString("tree"));
        auto *translation = find_item(notebook, "word_translation");
        auto *panel = find_item(notebook, "learning_panel");
        QVERIFY(translation && panel);
        QCOMPARE(translation->property("text").toString(), QString());
        QVERIFY(service.vocabulary().isEmpty());
        surface.close_notebook();
        click_item(window, add);
        QCOMPARE(translation->property("text").toString(), QString("tree"));
        QVERIFY(panel->property("dirty").toBool());
        QVERIFY(service.vocabulary().isEmpty());
        QCOMPARE(surface.selected_card_word(), QString("Baum"));
    }

    void cancellation_edits_and_seed_invalidation() {
        QTemporaryDir directory;
        TestNetwork network;
        network.automatic = false;
        network.response = R"({"translatedText":"obsolete"})";
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        configure(service);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.open_translator("hello", "en", "notes");
        auto *window = surface.translator_dialog();
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *translate = find_item(window, "translator_translate");
        auto *cancel = find_item(window, "translator_cancel");
        auto *source = find_item(window, "translator_source_text");
        auto *result = find_item(window, "translator_result_text");
        auto *swap = find_item(window, "translator_swap");
        auto *field = find_item(window, "translator_card_field");
        QVERIFY(translate && cancel && source && result && swap && field);
        click_item(window, translate);
        QTRY_VERIFY(service.translator()->session_busy());
        const auto cancelled = network.last_reply;
        click_item(window, cancel);
        QVERIFY(!service.translator()->session_busy());
        if (cancelled)
            cancelled->complete();
        QCOMPARE(result->property("text").toString(), QString());
        click_item(window, translate);
        QTRY_VERIFY(service.translator()->session_busy());
        enter_text(window, result, "my revision");
        QVERIFY(!service.translator()->session_busy());
        QCOMPARE(result->property("text").toString(), QString("my revision"));
        const auto before = network.requests.size();
        surface.open_translator("new source", "de", "context_translation");
        QCOMPARE(network.requests.size(), before);
        QCOMPARE(source->property("text").toString(), QString("new source"));
        QCOMPARE(result->property("text").toString(), QString());
        QCOMPARE(field->property("currentValue").toString(), QString("context_translation"));
        const auto previous_source = surface.translator_source_language();
        const auto previous_target = surface.translator_target_language();
        click_item(window, translate);
        QTRY_VERIFY(service.translator()->session_busy());
        click_item(window, swap);
        QVERIFY(!service.translator()->session_busy());
        QCOMPARE(surface.translator_source_language(), previous_target);
        QCOMPARE(surface.translator_target_language(), previous_source);
        click_item(window, translate);
        QTRY_VERIFY(service.translator()->session_busy());
        enter_text(window, source, "edited");
        QVERIFY(!service.translator()->session_busy());
        click_item(window, translate);
        QTRY_VERIFY(service.translator()->session_busy());
        click_item(window, source);
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(!service.translator()->session_busy());
        surface.open_translator();
        QCOMPARE(source->property("text").toString(), QString("edited"));
        QCOMPARE(result->property("text").toString(), QString());
    }

    void invalid_provider_configuration_stays_local() {
        QTemporaryDir directory;
        TestNetwork network;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        configure(service);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.open_translator("private source", "en", "notes");
        auto *window = surface.translator_dialog();
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *setup = find_item(window, "translator_setup");
        QVERIFY(setup);
        click_item(window, setup);
        QQuickItem *url = nullptr;
        QQuickItem *apply = nullptr;
        QTRY_VERIFY((url = settings_item(window, "translator_input_base_url")));
        QTRY_VERIFY((apply = settings_item(window, "apply_translator_base_url")));
        enter_text(window, url, "not-a-url");
        click_item(window, apply);
        QTRY_VERIFY(!window->property("configuration_error").toString().isEmpty());
        QCOMPARE(service.translator()->options().value("base_url").toString(),
                 QString("https://translator.invalid/test"));
        QVERIFY(service.translator()->session_error().isEmpty());
        QVERIFY(network.requests.isEmpty());
        QVERIFY(!surface.notebook_dialog()->isVisible());
        QTest::keyClick(&surface, Qt::Key_Escape);
        QVERIFY(window->isVisible());
    }

    void provider_setup_credentials_and_errors() {
        QTemporaryDir directory;
        TestNetwork network;
        network.response = R"({"message":"denied"})";
        network.status = 403;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.open_translator("private source", "en", "notes");
        auto *window = surface.translator_dialog();
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *provider = find_item(window, "translator_provider");
        auto *translate = find_item(window, "translator_translate");
        auto *setup = find_item(window, "translator_setup");
        auto *error = find_item(window, "translator_error");
        QVERIFY(provider && translate && setup && error);
        QVERIFY(!translate->isEnabled());
        choose(window, provider, "deepl");
        QTRY_COMPARE(service.translator()->provider_id(), QString("deepl"));
        QVERIFY(!translate->isEnabled());
        QVERIFY(network.requests.isEmpty());
        click_item(window, setup);
        QQuickItem *key = nullptr;
        QQuickItem *apply = nullptr;
        QTRY_VERIFY((key = settings_item(window, "translator_input_api_key")));
        QTRY_VERIFY((apply = settings_item(window, "apply_translator_api_key")));
        enter_text(window, key, "session-secret");
        click_item(window, apply);
        QTRY_VERIFY(service.translator()->configured());
        QCOMPARE(key->property("text").toString(), QString());
        QVERIFY(!service.translator()->options().contains("api_key"));
        QVERIFY(!QSettings{}.contains("translation/deepl/api_key"));
        QVERIFY(network.requests.isEmpty());
        QTest::keyClick(&surface, Qt::Key_Escape);
        QVERIFY(window->isVisible());
        click_item(window, translate);
        QTRY_VERIFY(!service.translator()->session_error().isEmpty());
        QVERIFY(error->property("text").toString().contains("API key"));
        QCOMPARE(network.requests.size(), 1);
        QCOMPARE(network.requests[0].rawHeader("Authorization"), QByteArray("DeepL-Auth-Key session-secret"));
        const auto payload = QJsonDocument::fromJson(network.payloads[0]).object();
        QCOMPARE(payload.value("context").toString(), QString());
        choose(window, provider, "off");
        QTRY_COMPARE(service.translator()->provider_id(), QString());
        QVERIFY(!translate->isEnabled());
        QCOMPARE(error->property("text").toString(), QString());
        QCOMPARE(network.requests.size(), 1);
    }
};

QTEST_MAIN(TranslatorDialogTest)
#include "TranslatorDialogTest.moc"
