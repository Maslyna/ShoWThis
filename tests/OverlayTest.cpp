#include <QQmlEngine>
#include <QApplication>
#include <QAccessible>
#include <QFileDialog>
#include <QLineEdit>
#include <QPalette>
#include <QPainter>
#include <QQuickItem>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>
#include <QSemaphore>
#include <QElapsedTimer>
#include <QThread>
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

static QList<showthis::ocr::WordBox> manual_context_words() {
    return {
        {QStringLiteral("Haus"), QRect(120, 200, 120, 40), QStringLiteral("Original OCR context.")},
        {QStringLiteral("steht"), QRect(300, 200, 140, 40)},
        {QStringLiteral("hier."), QRect(500, 200, 120, 40)},
        {QStringLiteral("Das"), QRect(120, 300, 100, 40)},
        {QStringLiteral("Haus"), QRect(300, 300, 120, 40)},
        {QStringLiteral("bleibt."), QRect(500, 300, 140, 40)},
        {QStringLiteral("Rand"), QRect(700, 200, 160, 40)}
    };
}

static QImage manual_context_image(const QList<showthis::ocr::WordBox> &words) {
    QImage image(1600, 1000, QImage::Format_RGB32);
    image.fill(QColor("#faf8f3"));
    QPainter painter(&image);
    painter.setPen(QColor("#242424"));
    QFont font = painter.font();
    font.setPixelSize(26);
    painter.setFont(font);
    for (const auto &word : words)
        painter.drawText(word.rect, Qt::AlignLeft | Qt::AlignVCenter, word.text);
    return image;
}

static QPoint image_point(const showthis::ShoWSurface &surface, const QPointF &point) {
    return QPoint(qRound(point.x() * surface.width() / surface.image_size().width()),
                  qRound(point.y() * surface.height() / surface.image_size().height()));
}

static void click_item(showthis::ShoWSurface &surface, QQuickItem *item) {
    for (auto *dialog = item; dialog; dialog = dialog->parentItem()) {
        if (dialog->objectName() == "notebook_dialog" || dialog->objectName() == "translator_dialog") {
            dialog->forceActiveFocus();
            QVERIFY(QMetaObject::invokeMethod(dialog, "activated"));
            break;
        }
    }
    QTest::qWait(30);
    for (auto *parent = item->parentItem(); parent; parent = parent->parentItem()) {
        if (!parent->property("contentY").isValid() || !parent->property("contentHeight").isValid())
            continue;
        const auto rect = item->mapRectToItem(parent, QRectF(QPointF(), item->size()));
        const auto delta = rect.top() < 0 ? rect.top()
            : rect.bottom() > parent->height() ? rect.bottom() - parent->height() : 0.0;
        if (delta != 0) {
            const auto maximum = qMax(0.0, parent->property("contentHeight").toReal() - parent->height());
            parent->setProperty("contentY", qBound(0.0, parent->property("contentY").toReal() + delta, maximum));
        }
    }
    QCOMPARE(item->window(), &surface);
    surface.requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(&surface));
    auto visible_rect = item->mapRectToScene(QRectF(QPointF(), item->size()));
    for (auto *parent = item->parentItem(); parent; parent = parent->parentItem()) {
        if (parent->clip())
            visible_rect = visible_rect.intersected(parent->mapRectToScene(QRectF(QPointF(), parent->size())));
    }
    QVERIFY(!visible_rect.isEmpty());
    QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
        visible_rect.center().toPoint());
}

static void notebook_key(showthis::ShoWSurface &surface, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QVERIFY(surface.notebook_dialog());
    surface.requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(&surface));
    QTest::keyClick(&surface, key, modifiers);
}

static void choose_language(showthis::ShoWSurface &surface, QQuickItem *control, const QByteArray &text) {
    click_item(surface, control);
    notebook_key(surface, Qt::Key_A, Qt::ControlModifier);
    for (const auto character : text)
        QTest::keyClick(&surface, character);
    notebook_key(surface, Qt::Key_Return);
}

static QQuickItem *notebook_item(showthis::ShoWSurface &surface, const QString &name) {
    auto *dialog = surface.notebook_dialog();
    auto *item = dialog ? find_item(dialog, name) : nullptr;
    return item ? item : find_item(surface.contentItem(), name);
}

static bool activate_card(showthis::ShoWSurface &surface) {
    auto *panel = notebook_item(surface, "learning_panel");
    if (!panel)
        return false;
    if (!panel->property("wide_layout").toBool()) {
        auto *button = notebook_item(surface, "compact_word_card");
        if (!button || !button->isVisible())
            return false;
        click_item(surface, button);
    }
    return true;
}

static bool activate_dictionary(showthis::ShoWSurface &surface) {
    auto *panel = notebook_item(surface, "learning_panel");
    if (!panel)
        return false;
    if (!panel->property("wide_layout").toBool()) {
        auto *button = notebook_item(surface, "compact_dictionary");
        if (!button || !button->isVisible())
            return false;
        click_item(surface, button);
    }
    return true;
}

static void drag_context(showthis::ShoWSurface &surface, const QPointF &from, const QPointF &to) {
    const auto start = image_point(surface, from);
    const auto end = image_point(surface, to);
    QTest::mousePress(&surface, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(&surface, (start + end) / 2, 10);
    QTest::mouseMove(&surface, end, 10);
    QTest::mouseRelease(&surface, Qt::LeftButton, Qt::NoModifier, end);
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

    void keyboard_capture_and_context_selection() {
        QTemporaryDir directory;
        TestNetwork network;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        const bool accessibility_was_active = QAccessible::isActive();
        const auto restore_accessibility = qScopeGuard([&]() { QAccessible::setActive(accessibility_was_active); });
        QAccessible::setActive(true);
        surface.resize(800, 600);
        const auto words = manual_context_words();
        surface.set_image(manual_context_image(words), words);
        surface.show();
        surface.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&surface));
        auto *capture_picker = surface.rootObject()->findChild<QObject *>("capture_word_picker");
        auto *context_picker = surface.rootObject()->findChild<QObject *>("context_word_picker");
        QVERIFY(capture_picker && context_picker);
        QTest::keyClick(&surface, Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(capture_picker->property("visible").toBool());
        auto *list = find_item(surface.contentItem(), "recognized_word_list");
        QVERIFY(list);
        QTest::keyClick(&surface, Qt::Key_Down);
        QTRY_COMPARE(list->property("currentIndex").toInt(), 1);
        auto *focused_word = QAccessible::queryAccessibleInterface(surface.activeFocusItem());
        QVERIFY(focused_word);
        QCOMPARE(focused_word->text(QAccessible::Name), QString("steht"));
        QCOMPARE(focused_word->role(), QAccessible::Button);
        QTest::keyClick(&surface, Qt::Key_Return);
        QTRY_VERIFY(!capture_picker->property("visible").toBool());
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
        QCOMPARE(notebook_item(surface, "lookup_word")->property("text").toString(), QString("steht"));
        auto *context = notebook_item(surface, "word_context");
        auto *select = notebook_item(surface, "select_context");
        QVERIFY(context && select);
        context->setProperty("text", "Keep this context until applied.");
        select->forceActiveFocus();
        QTest::keyClick(&surface, Qt::Key_Space);
        auto *selection = find_item(surface.contentItem(), "context_selection");
        QVERIFY(selection);
        QTRY_VERIFY(selection->property("active").toBool());
        QTest::keyClick(&surface, Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(context_picker->property("visible").toBool());
        QTest::keyClick(&surface, Qt::Key_Space);
        QTest::keyClick(&surface, Qt::Key_Down);
        QTest::keyClick(&surface, Qt::Key_Space);
        QTRY_COMPARE(selection->property("preview_text").toString(), QString("Haus\nsteht"));
        QTest::keyClick(&surface, Qt::Key_Home);
        QTest::keyClick(&surface, Qt::Key_Space);
        QTRY_COMPARE(selection->property("preview_text").toString(), QString("steht"));
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS"))
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS") + "/keyboard-context-picker.png"));
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!context_picker->property("visible").toBool());
        QVERIFY(selection->property("active").toBool());
        QCOMPARE(context->property("text").toString(), QString("Keep this context until applied."));
        auto *apply = find_item(surface.contentItem(), "context_selection_apply");
        QVERIFY(apply && apply->isEnabled());
        apply->forceActiveFocus();
        QTest::keyClick(&surface, Qt::Key_Space);
        QTRY_VERIFY(!selection->property("active").toBool());
        QCOMPARE(context->property("text").toString(), QString("steht"));
        QVERIFY(service.vocabulary().isEmpty());
        select->forceActiveFocus();
        QTest::keyClick(&surface, Qt::Key_Space);
        QTRY_VERIFY(selection->property("active").toBool());
        QTest::keyClick(&surface, Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(context_picker->property("visible").toBool());
        surface.set_words({});
        QTRY_VERIFY(!context_picker->property("visible").toBool());
        QVERIFY(!selection->property("active").toBool());
        QCOMPARE(context->property("text").toString(), QString("steht"));
        QVERIFY(network.requests.isEmpty());
        QCOMPARE(warnings.size(), 0);
    }

    void hover_feedback_tracks_resized_word() {
        QTemporaryDir directory;
        TestNetwork network;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.resize(1000, 700);
        QImage image(1000, 700, QImage::Format_RGB32);
        image.fill(Qt::white);
        const QRect box(100, 100, 400, 250);
        const QRect duplicate_box(600, 100, 300, 250);
        surface.set_image(image, {{"Haus", box}, {"Haus", duplicate_box}});
        surface.show();
        surface.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&surface));
        QTest::mouseMove(&surface, box.center());
        auto *feedback = find_item(surface.contentItem(), "ocr_hover_feedback");
        auto *outline = find_item(surface.contentItem(), "word_outline_0");
        QVERIFY(feedback && outline);
        QTRY_COMPARE(surface.rootObject()->property("hoveredWord").toString(), QString("Haus"));
        auto *duplicate_outline = find_item(surface.contentItem(), "word_outline_1");
        QVERIFY(duplicate_outline);
        QTest::mouseMove(&surface, duplicate_box.center());
        QTRY_COMPARE(surface.rootObject()->property("hoveredWord").toString(), QString("Haus"));
        QTRY_COMPARE(feedback->mapRectToScene(QRectF(QPointF(), feedback->size())),
            duplicate_outline->mapRectToScene(QRectF(QPointF(), duplicate_outline->size())));
        QTest::mouseMove(&surface, box.center());
        QTRY_COMPARE(surface.rootObject()->property("hoveredWord").toString(), QString("Haus"));
        QTRY_COMPARE(feedback->mapRectToScene(QRectF(QPointF(), feedback->size())),
            outline->mapRectToScene(QRectF(QPointF(), outline->size())));
        surface.resize(1200, 840);
        QTRY_COMPARE(outline->width(), 480.0);
        QCOMPARE(surface.rootObject()->property("hoveredWord").toString(), QString("Haus"));
        QTRY_COMPARE(feedback->mapRectToScene(QRectF(QPointF(), feedback->size())),
            outline->mapRectToScene(QRectF(QPointF(), outline->size())));
        surface.set_words({});
        QTRY_COMPARE(feedback->width(), 0.0);
        QVERIFY(network.requests.isEmpty());
    }

    void compact_capture_retains_actionable_error() {
        QTemporaryDir directory;
        TestNetwork network;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.resize(480, 400);
        surface.set_languages({"eng", "deu"});
        surface.set_error("No OCR language data installed. Install a Tesseract language pack to enable screen recognition.");
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *status = find_item(surface.contentItem(), "recognition_status");
        auto *toolbar = find_item(surface.contentItem(), "toolbar");
        QVERIFY(status && toolbar);
        QTRY_VERIFY(status->isVisible());
        QTRY_VERIFY(status->height() >= status->property("implicitHeight").toReal());
        const auto status_rect = status->mapRectToScene(QRectF(QPointF(), status->size()));
        QVERIFY(QRectF(QPointF(), surface.size()).contains(status_rect));
        QVERIFY(toolbar->mapRectToScene(QRectF(QPointF(), toolbar->size())).contains(status_rect));
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS"))
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS") + "/compact-ocr-error.png"));
    }

    void minimum_empty_state_and_long_confirmation_remain_operable() {
        QTemporaryDir directory;
        TestNetwork network;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.resize(460, 360);
        surface.open_notebook();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        surface.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&surface));
        auto *list = notebook_item(surface, "vocabulary_list");
        QVERIFY(list);
        auto *action = find_item(list, "empty_state_action");
        QVERIFY(action && action->isVisible());
        action->forceActiveFocus();
        QTRY_VERIFY(action->hasActiveFocus());
        QTRY_VERIFY(QRectF(QPointF(), list->size()).contains(
            action->mapRectToItem(list, QRectF(QPointF(), action->size()))));
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS"))
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS") + "/minimum-empty-words.png"));
        QTest::keyClick(&surface, Qt::Key_Space);
        auto *navigation = notebook_item(surface, "notebook_tabs");
        QVERIFY(navigation);
        QTRY_COMPARE(navigation->property("current_index").toInt(), 0);
        auto *word = notebook_item(surface, "lookup_word");
        auto *meaning = notebook_item(surface, "word_meaning");
        QVERIFY(word && meaning);
        word->setProperty("text", "Haus");
        meaning->setProperty("text", "Keep my draft");
        click_item(surface, notebook_item(surface, "new_word"));
        auto *confirmation = surface.notebook_dialog()->findChild<QObject *>("discard_draft");
        QVERIFY(confirmation);
        QTRY_VERIFY(confirmation->property("visible").toBool());
        confirmation->setProperty("description", QString("Opening another word discards the current meaning, translation and notes. ").repeated(24));
        auto *footer = confirmation->property("footer").value<QQuickItem *>();
        QVERIFY(footer);
        auto *cancel = find_item(footer, "cancel_action");
        auto *confirm = find_item(footer, "confirm_action");
        QVERIFY(cancel && confirm);
        QTRY_VERIFY(confirmation->property("height").toReal() <= surface.height() - 24);
        for (auto *button : {cancel, confirm})
            QVERIFY(QRectF(QPointF(), surface.size()).contains(
                button->mapRectToScene(QRectF(QPointF(), button->size()))));
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS"))
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS") + "/minimum-long-confirmation.png"));
        click_item(surface, cancel);
        QTRY_VERIFY(!confirmation->property("visible").toBool());
        QCOMPARE(meaning->property("text").toString(), QString("Keep my draft"));
        QVERIFY(service.vocabulary().isEmpty());
        QVERIFY(network.requests.isEmpty());
    }

    void dialogs_share_overlay_and_preserve_user_geometry() {
        QTemporaryDir directory;
        TestNetwork network;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.resize(2200, 1200);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        const auto windows = QGuiApplication::topLevelWindows().size();
        surface.open_notebook_word("Haus", "Original context");
        auto *notebook = surface.notebook_dialog();
        QVERIFY(notebook && notebook->isVisible());
        QCOMPARE(QGuiApplication::topLevelWindows().size(), windows);
        surface.open_translator("Independent source", "de", "notes");
        auto *translator = surface.translator_dialog();
        QVERIFY(translator && translator->isVisible());
        QCOMPARE(QGuiApplication::topLevelWindows().size(), windows);
        auto *word = notebook_item(surface, "lookup_word");
        auto *meaning = notebook_item(surface, "word_meaning");
        auto *source = find_item(translator, "translator_source_text");
        QVERIFY(word && meaning && source);
        const auto click_visible = [&](QQuickItem *item) {
            QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        meaning->setProperty("text", "Unsaved meaning");
        const auto drag = [&](QQuickItem *handle, const QPoint &offset) {
            const auto start = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
            QTest::mousePress(&surface, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(&surface, start + offset / 2, 10);
            QTest::mouseMove(&surface, start + offset, 10);
            QTest::mouseRelease(&surface, Qt::LeftButton, Qt::NoModifier, start + offset);
        };
        for (auto *dialog : {notebook, translator}) {
            QCOMPARE(dialog->window(), &surface);
            auto *move = find_item(dialog, "dialog_drag_handle");
            auto *resize = find_item(dialog, "dialog_resize_handle");
            QVERIFY(move && resize);
            QTRY_VERIFY(move->width() > 0 && resize->height() > 0);
            const auto old_size = dialog->size();
            drag(resize, QPoint(-100, -80));
            QTRY_VERIFY(dialog->width() < old_size.width() - 50);
            QTRY_VERIFY(dialog->height() < old_size.height() - 40);
            const auto old_position = dialog->position();
            drag(move, QPoint(40, 30));
            QTRY_VERIFY(dialog->x() > old_position.x() + 20);
            QTRY_VERIFY(dialog->y() > old_position.y() + 15);
            QVERIFY(dialog->property("user_geometry").toBool());
            QVERIFY(dialog->x() >= 0 && dialog->y() >= 0);
            QVERIFY(dialog->x() + dialog->width() <= surface.width());
            QVERIFY(dialog->y() + dialog->height() <= surface.height());
            QVERIFY(notebook->isVisible() && translator->isVisible());
            QVERIFY(dialog->hasActiveFocus());
        }
        const auto notebook_position = notebook->position();
        const auto notebook_size = notebook->size();
        const auto translator_position = translator->position();
        const auto translator_size = translator->size();
        click_visible(source);
        QVERIFY(translator->hasActiveFocus());
        click_visible(word);
        QVERIFY(notebook->hasActiveFocus());
        QVERIFY(notebook->z() > translator->z());
        click_visible(source);
        QVERIFY(translator->hasActiveFocus());
        QVERIFY(translator->z() > notebook->z());
        auto *close = find_item(translator, "dialog_close");
        QVERIFY(close);
        click_item(surface, close);
        QTRY_VERIFY(!translator->isVisible());
        QVERIFY(notebook->isVisible() && surface.isVisible());
        QCOMPARE(meaning->property("text").toString(), QString("Unsaved meaning"));
        surface.open_translator();
        QCOMPARE(source->property("text").toString(), QString("Independent source"));
        QCOMPARE(translator->position(), translator_position);
        QCOMPARE(translator->size(), translator_size);
        surface.close_notebook();
        QVERIFY(!notebook->isVisible() && translator->isVisible() && surface.isVisible());
        surface.open_notebook();
        QCOMPARE(notebook->position(), notebook_position);
        QCOMPARE(notebook->size(), notebook_size);
        QCOMPARE(meaning->property("text").toString(), QString("Unsaved meaning"));
        QCOMPARE(QGuiApplication::topLevelWindows().size(), windows);
        QVERIFY(network.requests.isEmpty());
        QCOMPARE(warnings.size(), 0);
    }

    void optional_word_and_context_translation_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("desktop") << QSize(1100, 900);
        QTest::newRow("compact") << QSize(640, 750);
        QTest::newRow("small") << QSize(480, 640);
    }

    void optional_word_and_context_translation() {
        QFETCH(QSize, size);
        QTemporaryDir directory;
        const auto path = directory.filePath("words.json");
        const QString source_context = "Das ist mein Haus. Es steht neben dem Park.";
        TestNetwork network;
        showthis::LearningService service(nullptr, path, &network);
        service.set_source_language("de");
        service.set_target_language("en");
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.open_notebook_word("Haus", source_context);
        auto *notebook = surface.notebook_dialog();
        QVERIFY(notebook);
        surface.resize(size);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QVERIFY(surface.isVisible());
        QCOMPARE(notebook->window(), &surface);
        auto *panel = notebook_item(surface, "learning_panel");
        auto *context = notebook_item(surface, "word_context");
        auto *word_result = notebook_item(surface, "word_translation");
        auto *context_result = notebook_item(surface, "context_translation");
        auto *translate_word = notebook_item(surface, "translate_word");
        auto *translate_context = notebook_item(surface, "translate_context");
        QVERIFY(panel && context && word_result && context_result && translate_word && translate_context);
        QVERIFY(context->isVisible());
        QVERIFY(translate_word->isVisible() && translate_context->isVisible());
        QVERIFY(!notebook_item(surface, "target_language"));
        QVERIFY(!notebook_item(surface, "context_target_language"));
        word_result->setProperty("text", "my own word translation");
        click_item(surface, translate_word);
        auto *translator = surface.translator_dialog();
        QVERIFY(translator);
        QTRY_VERIFY(translator->isVisible());
        QCOMPARE(translator->window(), &surface);
        QVERIFY(notebook->isVisible());
        auto *source = find_item(translator, "translator_source_text");
        QVERIFY(source);
        QCOMPARE(source->property("text").toString(), QString("Haus"));
        QVERIFY(network.requests.isEmpty());
        QCOMPARE(word_result->property("text").toString(), QString("my own word translation"));
        click_item(surface, translate_context);
        QCOMPARE(surface.translator_dialog(), translator);
        QTRY_COMPARE(source->property("text").toString(), source_context);
        QVERIFY(network.requests.isEmpty());
        QVERIFY(activate_card(surface));
        auto *add_context = notebook_item(surface, "add_context_translation");
        if (!context_result->isVisible()) {
            QVERIFY(add_context);
            click_item(surface, add_context);
        }
        QVERIFY(!context_result->property("readOnly").toBool());
        context_result->setProperty("text", "My manual sentence.");
        QVERIFY(surface.add_to_selected_card("context_translation", "Explicit addition.", "en"));
        QCOMPARE(context_result->property("text").toString(), QString("My manual sentence.\nExplicit addition."));
        QCOMPARE(service.vocabulary().size(), 0);
        QVERIFY(panel->property("dirty").toBool());
        panel->forceActiveFocus();
        notebook_key(surface, Qt::Key_S, Qt::ControlModifier);
        QTRY_COMPARE(service.vocabulary().size(), 1);
        const auto saved = service.vocabulary()[0].toMap();
        QCOMPARE(saved.value("translation").toString(), QString("my own word translation"));
        QCOMPARE(saved.value("context").toString(), source_context);
        QCOMPARE(saved.value("context_translation").toString(), QString("My manual sentence.\nExplicit addition."));
        showthis::LearningService reopened(nullptr, path, &network);
        QCOMPARE(reopened.vocabulary()[0].toMap().value("context_translation"), saved.value("context_translation"));
        QVERIFY(QMetaObject::invokeMethod(panel, "edit_card", Q_ARG(QVariant, QVariant(saved))));
        QCOMPARE(context_result->property("text").toString(), saved.value("context_translation").toString());
        QVERIFY(!panel->property("dirty").toBool());
        QVERIFY(network.requests.isEmpty());
        QCOMPARE(warnings.size(), 0);
    }

    void manual_context_translation_without_provider() {
        QTemporaryDir directory;
        const auto path = directory.filePath("words.json");
        TestNetwork network;
        showthis::LearningService service(nullptr, path, &network);
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        surface.resize(1100, 900);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = notebook_item(surface, "learning_panel");
        auto *navigation = notebook_item(surface, "notebook_tabs");
        QVERIFY(panel && navigation);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QString("Haus")),
            Q_ARG(QString, QString("Das ist mein Haus."))));
        auto *add = notebook_item(surface, "add_context_translation");
        auto *context_result = notebook_item(surface, "context_translation");
        auto *word_result = notebook_item(surface, "word_translation");
        auto *meaning = notebook_item(surface, "word_meaning");
        auto *save = notebook_item(surface, "save_word");
        QVERIFY(add && context_result && word_result && meaning && save);
        QVERIFY(!service.translator()->configured());
        QVERIFY(!save->isEnabled());
        QVERIFY(activate_card(surface));
        click_item(surface, add);
        QTRY_VERIFY(context_result->isVisible());
        context_result->setProperty("text", "A manually translated sentence.");
        QVERIFY(word_result->property("text").toString().isEmpty());
        QVERIFY(meaning->property("text").toString().isEmpty());
        QTRY_VERIFY(save->isEnabled());
        const auto original_id = panel->property("card_id").toString();
        surface.open_translator();
        QVERIFY(surface.translator_dialog());
        QVERIFY(surface.notebook_dialog()->isVisible());
        QCOMPARE(panel->property("card_id").toString(), original_id);
        QVERIFY(panel->property("dirty").toBool());
        QCOMPARE(context_result->property("text").toString(), QString("A manually translated sentence."));
        QVERIFY(network.requests.isEmpty());
        service.translator()->set_provider_id("libretranslate");
        navigation->setProperty("current_index", 0);
        QCOMPARE(context_result->property("text").toString(), QString("A manually translated sentence."));
        service.translator()->set_provider_id("");
        panel->forceActiveFocus();
        notebook_key(surface, Qt::Key_S, Qt::ControlModifier);
        QTRY_COMPARE(service.vocabulary().size(), 1);
        QVERIFY(!panel->property("dirty").toBool());
        const auto card = service.vocabulary()[0].toMap();
        QCOMPARE(card.value("context_translation").toString(), QString("A manually translated sentence."));
        QVERIFY(card.value("translation").toString().isEmpty());
        QVERIFY(card.value("meaning").toString().isEmpty());
        navigation->setProperty("current_index", 1);
        auto *search = notebook_item(surface, "vocabulary_search");
        auto *list = notebook_item(surface, "vocabulary_list");
        QVERIFY(search && list);
        search->setProperty("text", "manually translated");
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(panel, "edit_card", Q_ARG(QVariant, QVariant(card))));
        QCOMPARE(context_result->property("text").toString(), QString("A manually translated sentence."));
        QVERIFY(activate_card(surface));
        QVERIFY(context_result->isVisible());
        QVERIFY(!panel->property("dirty").toBool());
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS")) {
            QTest::qWait(30);
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS") + "/notebook-manual-translation.png"));
        }
        QVERIFY(network.requests.isEmpty());
        QCOMPARE(warnings.size(), 0);
    }

    void manual_context_selection_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("desktop") << QSize(1100, 800);
        QTest::newRow("compact") << QSize(640, 750);
        QTest::newRow("small") << QSize(480, 640);
    }

    void manual_context_selection() {
        QFETCH(QSize, size);
        QTemporaryDir directory;
        const auto path = directory.filePath("words.json");
        TestNetwork network;
        showthis::LearningService service(nullptr, path, &network);
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        TestCaptureBackend backend;
        std::atomic_int scans{0};
        showthis::OverlayController controller(surface, backend, [&](const QImage &, const QString &) {
            ++scans;
            return showthis::ScanResult{};
        });
        surface.resize(size);
        const auto words = manual_context_words();
        surface.set_image(manual_context_image(words), words);
        const auto original_source = surface.image_source();
        QSignalSpy image_changes(&surface, &showthis::ShoWSurface::imageChanged);
        QSignalSpy word_changes(&surface, &showthis::ShoWSurface::wordsChanged);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, image_point(surface, QPoint(180, 220)));
        QVERIFY(surface.notebook_dialog());
        auto *panel = notebook_item(surface, "learning_panel");
        auto *word = notebook_item(surface, "lookup_word");
        auto *context = notebook_item(surface, "word_context");
        auto *meaning = notebook_item(surface, "word_meaning");
        auto *select = notebook_item(surface, "select_context");
        auto *picker = find_item(surface.contentItem(), "context_selection");
        auto *preview = find_item(surface.contentItem(), "context_selection_preview");
        auto *apply = find_item(surface.contentItem(), "context_selection_apply");
        auto *clear = find_item(surface.contentItem(), "context_selection_clear");
        auto *cancel = find_item(surface.contentItem(), "context_selection_cancel");
        auto *popup = find_item(surface.contentItem(), "wordPopup");
        QVERIFY(panel && word && context && meaning && select && picker && preview && apply && clear && cancel && popup);
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
        QVERIFY(surface.isVisible());
        QCOMPARE(context->property("text").toString(), QString("Original OCR context."));
        meaning->setProperty("text", "A house");
        notebook_key(surface, Qt::Key_S, Qt::ControlModifier);
        QTRY_COMPARE(service.vocabulary().size(), 1);
        QVERIFY(!panel->property("dirty").toBool());
        const auto card_id = panel->property("card_id").toString();
        const auto compact_page = panel->property("compact_page").toInt();
        click_item(surface, select);
        QTRY_VERIFY(picker->property("active").toBool());
        QVERIFY(!surface.notebook_dialog()->isVisible());
        QVERIFY(surface.isVisible());
        QCOMPARE(picker->size(), surface.rootObject()->size());
        QVERIFY(preview->property("readOnly").toBool());
        QCOMPARE(preview->property("text").toString(), QString{});
        QVERIFY(!apply->isEnabled());
        QTest::mouseMove(&surface, image_point(surface, QPoint(180, 220)));
        QVERIFY(!popup->isVisible());

        // Merely touching a box edge does not select its word: its center must be inside.
        drag_context(surface, QPoint(110, 190), QPoint(160, 250));
        QCOMPARE(preview->property("text").toString(), QString{});
        QVERIFY(!apply->isEnabled());

        // Reverse-direction selection spans two OCR lines, not geometric column order.
        drag_context(surface, QPoint(650, 350), QPoint(110, 190));
        QTRY_COMPARE(preview->property("text").toString(), QString("Haus steht hier. Das Haus bleibt."));
        QVERIFY(apply->isEnabled());
        click_item(surface, clear);
        QCOMPARE(preview->property("text").toString(), QString{});
        QVERIFY(!apply->isEnabled());

        // A drag beginning on a word selects instead of opening a different notebook card.
        drag_context(surface, QPoint(130, 205), QPoint(450, 350));
        QTRY_COMPARE(preview->property("text").toString(), QString("Haus steht Das Haus"));
        QVERIFY(!surface.notebook_dialog()->isVisible());
        QCOMPARE(word->property("text").toString(), QString("Haus"));
        drag_context(surface, QPoint(280, 190), QPoint(650, 350));
        const auto selected_context = QString("Haus steht Das Haus\nhier. bleibt.");
        auto *selected_word = find_item(surface.contentItem(), "context_selection_word_0");
        auto *second_haus = find_item(surface.contentItem(), "context_selection_word_4");
        auto *outside_word = find_item(surface.contentItem(), "context_selection_word_6");
        auto *outline = find_item(surface.contentItem(), "word_outline_0");
        auto *marquee = find_item(surface.contentItem(), "context_selection_rectangle");
        QVERIFY(selected_word && second_haus && outside_word && outline && marquee);
        QTRY_COMPARE(preview->property("text").toString(), selected_context);
        QVERIFY(selected_word->isVisible());
        QVERIFY(second_haus->isVisible());
        QVERIFY(!outside_word->isVisible());
        QVERIFY(!marquee->isVisible());
        QCOMPARE(selected_word->mapToScene(QPointF(0, 0)), outline->mapToScene(QPointF(0, 0)));
        QCOMPARE(selected_word->size(), outline->size());
        QVERIFY(selected_word->property("color").value<QColor>().alpha() > 0);

        // Repeated rectangles deduplicate indices, but equal text in distinct boxes survives.
        drag_context(surface, QPoint(650, 350), QPoint(110, 190));
        QCOMPARE(preview->property("text").toString(), selected_context);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, image_point(surface, QPoint(370, 220)));
        QTRY_COMPARE(preview->property("text").toString(), QString("Haus Das Haus\nhier. bleibt."));
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, image_point(surface, QPoint(370, 220)));
        QTRY_COMPARE(preview->property("text").toString(), selected_context);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, image_point(surface, QPoint(360, 320)));
        QTRY_COMPARE(preview->property("text").toString(), QString("Haus steht Das\nhier. bleibt."));
        QVERIFY(!second_haus->isVisible());
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, image_point(surface, QPoint(360, 320)));
        QTRY_COMPARE(preview->property("text").toString(), selected_context);
        QVERIFY(second_haus->isVisible());
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS"))
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS")
                + "/manual-context-" + QTest::currentDataTag() + ".png"));
        click_item(surface, apply);
        QTRY_VERIFY(!picker->property("active").toBool());
        QVERIFY(surface.notebook_dialog()->isVisible());
        QVERIFY(surface.isVisible());
        QCOMPARE(panel->property("card_id").toString(), card_id);
        QCOMPARE(panel->property("compact_page").toInt(), compact_page);
        QVERIFY(context->isVisible());
        QCOMPARE(context->property("text").toString(), selected_context);
        QCOMPARE(meaning->property("text").toString(), QString("A house"));
        QVERIFY(panel->property("dirty").toBool());
        notebook_key(surface, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(!panel->property("dirty").toBool());
        QCOMPARE(service.vocabulary().size(), 1);
        QCOMPARE(service.vocabulary()[0].toMap().value("context").toString(), selected_context);
        showthis::LearningService reopened(nullptr, path, &network);
        QCOMPARE(reopened.vocabulary()[0].toMap().value("context").toString(), selected_context);
        QCOMPARE(reopened.vocabulary()[0].toMap().value("id").toString(), card_id);

        click_item(surface, select);
        QTRY_VERIFY(picker->property("active").toBool());
        QCOMPARE(preview->property("text").toString(), QString{});
        QVERIFY(!apply->isEnabled());
        click_item(surface, cancel);
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
        QCOMPARE(context->property("text").toString(), selected_context);
        QVERIFY(!panel->property("dirty").toBool());
        QVERIFY(context->isVisible());

        // Reapplying identical context is not a new data edit.
        click_item(surface, select);
        QTRY_VERIFY(picker->property("active").toBool());
        drag_context(surface, QPoint(130, 205), QPoint(450, 350));
        drag_context(surface, QPoint(280, 190), QPoint(650, 350));
        QTRY_COMPARE(preview->property("text").toString(), selected_context);
        click_item(surface, apply);
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
        QCOMPARE(context->property("text").toString(), selected_context);
        QVERIFY(!panel->property("dirty").toBool());

        // Closing the notebook leaves the captured overlay available.
        notebook_key(surface, Qt::Key_Escape);
        QTRY_VERIFY(!surface.notebook_dialog()->isVisible());
        QVERIFY(surface.isVisible());
        QTest::mouseMove(&surface, image_point(surface, QPoint(900, 400)));
        QTest::mouseMove(&surface, image_point(surface, QPoint(780, 220)));
        QTRY_VERIFY(popup->isVisible());
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, image_point(surface, QPoint(780, 220)));
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
        QCOMPARE(word->property("text").toString(), QString("Rand"));
        QCOMPARE(surface.image_source(), original_source);
        QCOMPARE(image_changes.size(), 0);
        QCOMPARE(word_changes.size(), 0);
        QCOMPARE(backend.capture_count, 0);
        QCOMPARE(scans.load(), 0);
        QCOMPARE(network.requests.size(), 0);
        QCOMPARE(warnings.size(), 0);
    }

    void manual_context_cancel_preserves_draft_data() {
        QTest::addColumn<bool>("dirty");
        QTest::addColumn<bool>("escape");
        QTest::newRow("clean-cancel") << false << false;
        QTest::newRow("dirty-cancel") << true << false;
        QTest::newRow("clean-escape") << false << true;
        QTest::newRow("dirty-escape") << true << true;
    }

    void manual_context_cancel_preserves_draft() {
        QFETCH(bool, dirty);
        QFETCH(bool, escape);
        QTemporaryDir directory;
        TestNetwork network;
        network.response = free_entry("Haus", "chosen dictionary meaning");
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.set_source_language("de");
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        surface.resize(1100, 800);
        const auto words = manual_context_words();
        surface.set_image(manual_context_image(words), words);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QSignalSpy hide_requests(&surface, &showthis::ShoWSurface::hide_requested);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, image_point(surface, QPoint(180, 220)));
        QVERIFY(surface.notebook_dialog());
        auto *panel = notebook_item(surface, "learning_panel");
        auto *lookup = notebook_item(surface, "lookup_dictionary");
        auto *meaning = notebook_item(surface, "word_meaning");
        auto *translation = notebook_item(surface, "word_translation");
        auto *notes = notebook_item(surface, "personal_card_pane");
        auto *context = notebook_item(surface, "word_context");
        auto *word = notebook_item(surface, "lookup_word");
        auto *select = notebook_item(surface, "select_context");
        auto *picker = find_item(surface.contentItem(), "context_selection");
        auto *cancel = find_item(surface.contentItem(), "context_selection_cancel");
        auto *preview = find_item(surface.contentItem(), "context_selection_preview");
        QVERIFY(panel && lookup && meaning && translation && notes && context && word && select && picker && cancel && preview);
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
        click_item(surface, lookup);
        QTRY_COMPARE(service.senses().size(), 1);
        QTRY_VERIFY(!service.dictionary_busy());
        QTRY_VERIFY(notebook_item(surface, "dictionary_sense_0"));
        auto *sense = notebook_item(surface, "dictionary_sense_0");
        click_item(surface, sense);
        QCOMPARE(meaning->property("text").toString(), QString("chosen dictionary meaning"));
        const auto selected_sense = service.senses()[0].toMap();
        translation->setProperty("text", "my translation");
        notes->setProperty("notes", "keep my notes");
        notebook_key(surface, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(!panel->property("dirty").toBool());
        if (dirty) {
            meaning->setProperty("text", "unsaved chosen meaning");
            context->setProperty("text", "Unsaved original context.");
        }
        const auto original_meaning = meaning->property("text").toString();
        const auto original_context = context->property("text").toString();
        const auto card_id = panel->property("card_id").toString();
        const auto page = panel->property("compact_page").toInt();
        const auto senses = service.senses();
        const auto source = service.source();
        const auto requests = network.requests.size();
        QCOMPARE(panel->property("dirty").toBool(), dirty);
        click_item(surface, select);
        QTRY_VERIFY(picker->property("active").toBool());
        QVERIFY(!surface.notebook_dialog()->isVisible());
        QVERIFY(surface.isVisible());
        drag_context(surface, QPoint(110, 190), QPoint(650, 350));
        QTRY_COMPARE(preview->property("text").toString(), QString("Haus steht hier. Das Haus bleibt."));
        if (escape)
            QTest::keyClick(&surface, Qt::Key_Escape);
        else
            click_item(surface, cancel);
        QTRY_VERIFY(!picker->property("active").toBool());
        QVERIFY(surface.notebook_dialog()->isVisible());
        QVERIFY(surface.isVisible());
        QCOMPARE(hide_requests.size(), 0);
        QCOMPARE(panel->property("card_id").toString(), card_id);
        QCOMPARE(panel->property("compact_page").toInt(), page);
        QCOMPARE(panel->property("dirty").toBool(), dirty);
        QCOMPARE(word->property("text").toString(), QString("Haus"));
        QVERIFY(context->isVisible());
        QCOMPARE(context->property("text").toString(), original_context);
        QCOMPARE(meaning->property("text").toString(), original_meaning);
        QCOMPARE(translation->property("text").toString(), QString("my translation"));
        QCOMPARE(notes->property("notes").toString(), QString("keep my notes"));
        QCOMPARE(service.senses(), senses);
        QCOMPARE(service.source(), source);
        if (!dirty)
            notes->setProperty("notes", "another note");
        notebook_key(surface, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(!panel->property("dirty").toBool());
        const auto saved = service.vocabulary()[0].toMap();
        QCOMPARE(saved.value("sense").toMap(), selected_sense);
        QCOMPARE(saved.value("context").toString(), original_context);
        QCOMPARE(saved.value("meaning").toString(), original_meaning);
        QCOMPARE(network.requests.size(), requests);
    }

    void manual_context_unavailable_without_screenshot_or_words() {
        QTemporaryDir directory;
        TestNetwork network;
        network.automatic = false;
        network.response = free_entry("Haus", "house");
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.set_source_language("de");
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        surface.resize(1100, 800);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = notebook_item(surface, "learning_panel");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QString("Haus")),
            Q_ARG(QString, QString("Keep this context."))));
        auto *select = notebook_item(surface, "select_context");
        auto *picker = find_item(surface.contentItem(), "context_selection");
        auto *context = notebook_item(surface, "word_context");
        auto *lookup = notebook_item(surface, "lookup_dictionary");
        QVERIFY(select && picker && context && lookup);
        QTRY_VERIFY(select->isVisible());
        QVERIFY(!select->isEnabled());
        click_item(surface, select);
        QVERIFY(!picker->property("active").toBool());
        const auto words = manual_context_words();
        surface.set_words(words);
        QVERIFY(!select->isEnabled());
        surface.set_image(manual_context_image(words));
        QVERIFY(!select->isEnabled());
        surface.set_words(words);
        QTRY_VERIFY(select->isEnabled());
        surface.set_scanning(true);
        QVERIFY(select->isVisible());
        QVERIFY(!select->isEnabled());
        surface.set_scanning(false);
        QTRY_VERIFY(select->isEnabled());
        QVERIFY(activate_card(surface));
        click_item(surface, lookup);
        QTRY_VERIFY(service.dictionary_busy());
        QVERIFY(select->isVisible());
        QVERIFY(!select->isEnabled());
        click_item(surface, select);
        QVERIFY(!picker->property("active").toBool());
        QVERIFY(network.last_reply);
        network.last_reply->complete();
        QTRY_VERIFY(!service.dictionary_busy());
        QTRY_VERIFY(select->isEnabled());
        QCOMPARE(context->property("text").toString(), QString("Keep this context."));
        service.translator()->set_provider_id("deepl");
        service.translator()->set_option("api_key", "test-key");
        service.set_target_language("uk");
        network.response = R"({"translations":[{"text":"будинок"}]})";
        surface.open_translator("Independent source", "de", "notes");
        auto *translator = surface.translator_dialog();
        QVERIFY(translator && translator->isVisible());
        auto *session_source = find_item(translator, "translator_source_text");
        QVERIFY(session_source);
        service.translator()->translate_session("Haus", "de", "uk");
        QTRY_VERIFY(service.translator()->session_busy());
        const auto pending = network.last_reply;
        QVERIFY(pending);
        QVERIFY(select->isVisible() && select->isEnabled());
        click_item(surface, select);
        QTRY_VERIFY(picker->property("active").toBool());
        QVERIFY(service.translator()->session_busy());
        QVERIFY(!surface.notebook_dialog()->isVisible());
        QVERIFY(!translator->isVisible());
        QVERIFY(surface.isVisible());
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
        QVERIFY(service.translator()->session_busy());
        QVERIFY(translator->isVisible());
        QVERIFY(surface.isVisible());
        QCOMPARE(session_source->property("text").toString(), QString("Independent source"));
        pending->complete();
        QTRY_VERIFY(!service.translator()->session_busy());
        QCOMPARE(context->property("text").toString(), QString("Keep this context."));
        click_item(surface, select);
        QTRY_VERIFY(picker->property("active").toBool());
        QVERIFY(!surface.notebook_dialog()->isVisible() && !translator->isVisible());
        QVERIFY(surface.isVisible());
        drag_context(surface, QPoint(110, 190), QPoint(650, 350));
        auto *apply = find_item(surface.contentItem(), "context_selection_apply");
        QVERIFY(apply);
        QTRY_VERIFY(apply->isEnabled());
        click_item(surface, apply);
        QTRY_VERIFY(!picker->property("active").toBool());
        QVERIFY(surface.notebook_dialog()->isVisible() && translator->isVisible());
        QVERIFY(surface.isVisible());
        QCOMPARE(context->property("text").toString(), QString("Haus steht hier. Das Haus bleibt."));
        QCOMPARE(session_source->property("text").toString(), QString("Independent source"));
        QCOMPARE(service.translator()->session_text(), QString::fromUtf8("будинок"));
        QVERIFY(service.vocabulary().isEmpty());
    }

    void manual_context_invalidation_during_gesture_data() {
        QTest::addColumn<QString>("invalidation");
        for (const auto *reason : {"image", "words", "scanning", "hidden"})
            QTest::newRow(reason) << QString::fromLatin1(reason);
    }

    void manual_context_invalidation_during_gesture() {
        QFETCH(QString, invalidation);
        QTemporaryDir directory;
        TestNetwork network;
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        surface.resize(1100, 800);
        const auto words = manual_context_words();
        const auto image = manual_context_image(words);
        surface.set_image(image, words);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, image_point(surface, QPoint(180, 220)));
        QVERIFY(surface.notebook_dialog());
        auto *panel = notebook_item(surface, "learning_panel");
        auto *context = notebook_item(surface, "word_context");
        auto *meaning = notebook_item(surface, "word_meaning");
        auto *select = notebook_item(surface, "select_context");
        auto *picker = find_item(surface.contentItem(), "context_selection");
        auto *preview = find_item(surface.contentItem(), "context_selection_preview");
        auto *apply = find_item(surface.contentItem(), "context_selection_apply");
        QVERIFY(panel && context && meaning && select && picker && preview && apply);
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
        meaning->setProperty("text", "unsaved meaning");
        click_item(surface, select);
        QTRY_VERIFY(picker->property("active").toBool());
        drag_context(surface, QPoint(110, 190), QPoint(650, 250));
        QTRY_COMPARE(preview->property("text").toString(), QString("Haus steht hier."));
        const auto start = image_point(surface, QPoint(110, 290));
        const auto end = image_point(surface, QPoint(650, 350));
        QTest::mousePress(&surface, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&surface, (start + end) / 2, 10);
        if (invalidation == "image")
            surface.set_image(image, words);
        else if (invalidation == "words")
            surface.set_words({{QString("Replacement"), QRect(120, 200, 120, 40)}});
        else if (invalidation == "scanning")
            surface.set_scanning(true);
        else
            surface.hide();
        QTRY_VERIFY(!picker->property("active").toBool());
        QTest::mouseRelease(&surface, Qt::LeftButton, Qt::NoModifier, end);
        surface.set_scanning(false);
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
        QCOMPARE(surface.isVisible(), invalidation != "hidden");
        if (invalidation == "hidden") {
            surface.show();
            QVERIFY(QTest::qWaitForWindowExposed(&surface));
        }
        QCOMPARE(context->property("text").toString(), QString("Original OCR context."));
        QCOMPARE(meaning->property("text").toString(), QString("unsaved meaning"));
        QVERIFY(panel->property("dirty").toBool());
        click_item(surface, select);
        QTRY_VERIFY(picker->property("active").toBool());
        QCOMPARE(preview->property("text").toString(), QString{});
        QVERIFY(!apply->isEnabled());
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, image_point(surface, QPoint(180, 220)));
        QTRY_COMPARE(preview->property("text").toString(),
            invalidation == "words" ? QString("Replacement") : QString("Haus"));
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
        QVERIFY(surface.isVisible());
        QCOMPARE(context->property("text").toString(), QString("Original OCR context."));
        QCOMPARE(network.requests.size(), 0);
    }

    void manual_context_resize_and_movable_controls() {
        QTemporaryDir directory;
        showthis::LearningService service(nullptr, directory.filePath("words.json"));
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        surface.resize(1100, 800);
        auto words = manual_context_words();
        surface.set_image(manual_context_image(words), words);
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, image_point(surface, QPoint(180, 220)));
        QVERIFY(surface.notebook_dialog());
        auto *panel = notebook_item(surface, "learning_panel");
        auto *select = notebook_item(surface, "select_context");
        auto *picker = find_item(surface.contentItem(), "context_selection");
        auto *preview = find_item(surface.contentItem(), "context_selection_preview");
        auto *clear = find_item(surface.contentItem(), "context_selection_clear");
        auto *controls = find_item(surface.contentItem(), "context_selection_controls");
        auto *handle = find_item(surface.contentItem(), "context_selection_drag_handle");
        QVERIFY(panel && select && picker && preview && clear && controls && handle);
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
        click_item(surface, select);
        QTRY_VERIFY(picker->property("active").toBool());
        QTRY_VERIFY(controls->width() > 0 && controls->height() > 0);
        const auto covered = controls->mapToScene(QPointF(controls->width() / 2, controls->height() / 2));
        const QPointF covered_image(covered.x() * surface.image_size().width() / surface.width(),
                                    covered.y() * surface.image_size().height() / surface.height());
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
        words.append({QString("Behind"), QRect(qRound(covered_image.x()) - 40,
            qRound(covered_image.y()) - 15, 80, 30)});
        surface.set_image(manual_context_image(words), words);
        click_item(surface, select);
        QTRY_VERIFY(picker->property("active").toBool());
        QVERIFY(controls->contains(controls->mapFromScene(covered)));
        const auto before = controls->mapToScene(QPointF(0, 0));
        const auto start = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
        const QPoint offset(0, covered.y() > surface.height() / 2 ? -300 : 300);
        QTest::mousePress(&surface, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&surface, start + offset / 2, 10);
        QTest::mouseMove(&surface, start + offset, 10);
        QTest::mouseRelease(&surface, Qt::LeftButton, Qt::NoModifier, start + offset);
        QTRY_VERIFY(qAbs(controls->mapToScene(QPointF(0, 0)).y() - before.y()) > 100);
        QCOMPARE(preview->property("text").toString(), QString{});
        QVERIFY(!controls->contains(controls->mapFromScene(covered)));
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, covered.toPoint());
        QTRY_COMPARE(preview->property("text").toString(), QString("Behind"));
        click_item(surface, clear);
        surface.resize(640, 750);
        QTRY_COMPARE(picker->size(), QSizeF(640, 750));
        const auto controls_corner = controls->mapToScene(QPointF(controls->width(), controls->height()));
        QVERIFY(controls_corner.x() <= surface.width());
        QVERIFY(controls_corner.y() <= surface.height());
        // Hide controls at the bottom if their moved position now covers the sample text.
        const auto resized_start = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
        const auto resized_end = QPoint(resized_start.x(), surface.height() - qRound(controls->height()) + 15);
        QTest::mousePress(&surface, Qt::LeftButton, Qt::NoModifier, resized_start);
        QTest::mouseMove(&surface, resized_end, 10);
        QTest::mouseRelease(&surface, Qt::LeftButton, Qt::NoModifier, resized_end);
        drag_context(surface, QPoint(650, 350), QPoint(110, 190));
        QTRY_COMPARE(preview->property("text").toString(), QString("Haus steht hier. Das Haus bleibt."));
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(surface.notebook_dialog()->isVisible());
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
        QVERIFY(surface.notebook_dialog());
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QVERIFY(surface.isVisible());
        auto *panel = notebook_item(surface, QStringLiteral("learning_panel"));
        auto *word = notebook_item(surface, QStringLiteral("lookup_word"));
        auto *context = notebook_item(surface, QStringLiteral("word_context"));
        QVERIFY(panel);
        QVERIFY(word);
        QVERIFY(context);
        QTRY_VERIFY(panel->isVisible());
        QCOMPARE(word->property("text").toString(), QStringLiteral("Haus"));
        QCOMPARE(context->property("text").toString(), QStringLiteral("Das ist mein Haus."));
        auto *dictionary_button = notebook_item(surface, QStringLiteral("lookup_dictionary"));
        auto *senses = notebook_item(surface, QStringLiteral("dictionary_senses"));
        auto *save_button = notebook_item(surface, QStringLiteral("save_word"));
        auto *meaning = notebook_item(surface, QStringLiteral("word_meaning"));
        auto *translation = notebook_item(surface, QStringLiteral("word_translation"));
        QVERIFY(dictionary_button);
        QVERIFY(senses);
        QVERIFY(save_button);
        QVERIFY(meaning);
        QVERIFY(translation);
        QVERIFY(QMetaObject::invokeMethod(dictionary_button, "clicked"));
        QTRY_COMPARE(senses->property("count").toInt(), 2);
        QTest::qWait(30);
        auto *first_sense = notebook_item(surface, QStringLiteral("dictionary_sense_0"));
        QVERIFY(first_sense);
        click_item(surface, first_sense);
        QTRY_COMPARE(meaning->property("text").toString(), QStringLiteral("house"));
        if (!panel->property("wide_layout").toBool())
            QCOMPARE(panel->property("compact_page").toInt(), 1);
        QCOMPARE(translation->property("text").toString(), QString::fromUtf8("будинок"));
        QVERIFY(translation->isVisible());
        QCOMPARE(panel->property("word_prefix").toString(), QString("das"));
        auto *article = notebook_item(surface, QStringLiteral("word_prefix"));
        auto *headword = notebook_item(surface, QStringLiteral("word_headword"));
        QVERIFY(article);
        QVERIFY(headword);
        article->setProperty("text", QStringLiteral("das"));
        QVERIFY(QMetaObject::invokeMethod(article, "textEdited"));
        QCOMPARE(headword->property("text").toString(), QStringLiteral("das Haus"));
        QVERIFY(QMetaObject::invokeMethod(save_button, "clicked"));
        QCOMPARE(service.vocabulary().size(), 1);
        QCOMPARE(service.vocabulary()[0].toMap().value("article").toString(), QStringLiteral("das"));
        QVERIFY(QMetaObject::invokeMethod(surface.notebook_dialog(), "open_vocabulary"));
        QTest::qWait(30);
        auto *list = notebook_item(surface, QStringLiteral("vocabulary_list"));
        QVERIFY(list);
        QCOMPARE(list->property("count").toInt(), 1);
        QTRY_VERIFY(list->property("contentHeight").toReal() > 0);
        auto *export_button = notebook_item(surface, QStringLiteral("export_anki"));
        QVERIFY(export_button);
        QVERIFY(export_button->isEnabled());
        const auto card_id = service.vocabulary()[0].toMap().value("id").toString();
        auto *checkbox = notebook_item(surface, QStringLiteral("select_saved_") + card_id);
        QVERIFY(checkbox);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
            checkbox->mapToScene(QPointF(checkbox->width() / 2, checkbox->height() / 2)).toPoint());
        QVERIFY(export_button->property("text").toString().contains(QStringLiteral("selected")));
        QCOMPARE(service.dictionary_history().size(), 1);
        auto *tabs = notebook_item(surface, QStringLiteral("notebook_tabs"));
        QVERIFY(tabs);
        QCOMPARE(tabs->property("current_index").toInt(), 1);
        auto *actions = notebook_item(surface, QStringLiteral("row_actions_") + card_id);
        QVERIFY(actions);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
            actions->mapToScene(QPointF(actions->width() / 2, actions->height() / 2)).toPoint());
        auto *menu = actions->findChild<QObject *>(QStringLiteral("saved_actions_") + card_id);
        QVERIFY(menu);
        QTRY_VERIFY(menu->property("visible").toBool());
        notebook_key(surface, Qt::Key_Escape);
        QTRY_VERIFY(!menu->property("visible").toBool());
        QVERIFY(panel->isVisible());
        tabs->setProperty("current_index", 2);
        auto *history = notebook_item(surface, QStringLiteral("dictionary_history"));
        QVERIFY(history);
        QTRY_COMPARE(history->property("count").toInt(), 1);
        auto *history_search = notebook_item(surface, QStringLiteral("history_search"));
        QVERIFY(history_search);
        history_search->setProperty("text", QStringLiteral("no matching word"));
        QTRY_COMPARE(history->property("count").toInt(), 0);
        history_search->setProperty("text", QStringLiteral("Haus"));
        QTRY_COMPARE(history->property("count").toInt(), 1);
        tabs->setProperty("current_index", 3);
        QTest::qWait(30);
        auto *limit = notebook_item(surface, QStringLiteral("history_limit"));
        auto *apply_limit = notebook_item(surface, QStringLiteral("apply_history_limit"));
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

    void corrupt_cache_can_be_reset_from_settings_data() {
        QTest::addColumn<bool>("legacy_cache");
        QTest::newRow("sidecar") << false;
        QTest::newRow("legacy-migration") << true;
    }

    void corrupt_cache_can_be_reset_from_settings() {
        QFETCH(bool, legacy_cache);
        QTemporaryDir directory;
        const auto path = directory.filePath("words.json");
        const QJsonObject card{{"id", "kept"}, {"word", "kept"}, {"language", "de"}, {"meaning", "keep me"}};
        QJsonObject notebook{{"version", 1}, {"cards", QJsonArray{card}}, {"dictionary_cache", QJsonObject{}}};
        if (legacy_cache) {
            const auto lookup = QJsonDocument::fromJson(free_entry("Haus", "house")).object();
            notebook.insert("dictionary_cache", QJsonObject{{dictionary_cache_key("de", "Haus"), lookup}});
        }
        QFile vocabulary(path);
        QVERIFY(vocabulary.open(QIODevice::WriteOnly));
        const auto original = QJsonDocument(notebook).toJson();
        QCOMPARE(vocabulary.write(original), original.size());
        vocabulary.close();
        QFile cache(path + ".lookups.json");
        QVERIFY(cache.open(QIODevice::WriteOnly));
        cache.write("corrupt cache");
        cache.close();
        showthis::LearningService service(nullptr, path);
        QCOMPARE(service.vocabulary().size(), 1);
        QVERIFY(!service.storage_error().isEmpty());
        QVERIFY(service.dictionary_history().isEmpty());
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        surface.resize(1100, 800);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = notebook_item(surface, "learning_panel");
        auto *navigation = notebook_item(surface, "notebook_tabs");
        QVERIFY(panel && navigation);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_vocabulary"));
        navigation->setProperty("current_index", 3);
        auto *clear = notebook_item(surface, "clear_history");
        QVERIFY(clear);
        QTRY_VERIFY(clear->isVisible() && clear->isEnabled());
        click_item(surface, clear);
        auto *dialog = surface.notebook_dialog()->findChild<QObject *>("clear_history_confirmation");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS"))
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS")
                + "/cache-recovery-" + QTest::currentDataTag() + ".png"));
        auto *footer = dialog->property("footer").value<QQuickItem *>();
        QVERIFY(footer);
        auto *confirm = find_item(footer, "confirm_action");
        QVERIFY(confirm);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
            confirm->mapToScene(QPointF(confirm->width() / 2, confirm->height() / 2)).toPoint());
        QTRY_VERIFY(service.storage_error().isEmpty());
        QVERIFY(cache.open(QIODevice::ReadOnly));
        const auto reset = QJsonDocument::fromJson(cache.readAll()).object();
        QCOMPARE(reset.value("format").toString(), QString("showthis-lookup-cache"));
        QVERIFY(reset.value("dictionary_cache").toObject().isEmpty());
        cache.close();
        QCOMPARE(service.vocabulary()[0].toMap().value("id").toString(), QString("kept"));
        QVERIFY(vocabulary.open(QIODevice::ReadOnly));
        QCOMPARE(vocabulary.readAll(), original);
        vocabulary.close();
        QVERIFY(service.save_word("kept", "kept", "", "", "keep me", "", "saved after reset", {}));
        showthis::LearningService reopened(nullptr, path);
        QVERIFY(reopened.storage_error().isEmpty());
        QCOMPARE(reopened.vocabulary()[0].toMap().value("notes").toString(), QString("saved after reset"));
    }

    void sense_replacement_preserves_draft_until_confirmed() {
        QTemporaryDir directory;
        TestNetwork network;
        network.response = free_entry("Haus", "original meaning");
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.set_source_language("de");
        showthis::ShoWSurface surface(nullptr, &service);
        surface.resize(1100, 800);
        surface.open_notebook_word("Haus", "Original context");
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *lookup = notebook_item(surface, "lookup_dictionary");
        auto *refresh = notebook_item(surface, "refresh_dictionary");
        auto *form = notebook_item(surface, "personal_card_pane");
        auto *panel = notebook_item(surface, "learning_panel");
        auto *meaning = notebook_item(surface, "word_meaning");
        QVERIFY(lookup && refresh && form && panel && meaning);
        click_item(surface, lookup);
        QTRY_COMPARE(service.senses().size(), 1);
        QTRY_VERIFY(!service.dictionary_busy());
        auto *sense = notebook_item(surface, "dictionary_sense_0");
        QVERIFY(sense);
        click_item(surface, sense);
        QCOMPARE(meaning->property("text").toString(), QString("original meaning"));
        form->setProperty("meaning", "My personal definition");
        form->setProperty("translation", "My translation");
        form->setProperty("notes", "My notes");
        form->setProperty("context_translation", "My context translation");
        network.response = free_entry("Haus", "replacement meaning");
        click_item(surface, refresh);
        QTRY_VERIFY(!service.dictionary_busy());
        sense = notebook_item(surface, "dictionary_sense_0");
        QVERIFY(sense);
        auto *confirmation = panel->findChild<QObject *>("replace_card_sense");
        QVERIFY(confirmation);
        click_item(surface, sense);
        QTRY_VERIFY(confirmation->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(confirmation, "reject"));
        QCOMPARE(meaning->property("text").toString(), QString("My personal definition"));
        QCOMPARE(form->property("translation").toString(), QString("My translation"));
        click_item(surface, sense);
        QTRY_VERIFY(confirmation->property("visible").toBool());
        service.clear();
        QTRY_VERIFY(!confirmation->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(confirmation, "accept"));
        QCOMPARE(meaning->property("text").toString(), QString("My personal definition"));
        click_item(surface, refresh);
        QTRY_VERIFY(!service.dictionary_busy());
        sense = notebook_item(surface, "dictionary_sense_0");
        QVERIFY(sense);
        click_item(surface, sense);
        QTRY_VERIFY(confirmation->property("visible").toBool());
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS"))
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS") + "/replace-draft-fields.png"));
        QVERIFY(QMetaObject::invokeMethod(confirmation, "accept"));
        QCOMPARE(meaning->property("text").toString(), QString("replacement meaning"));
        QCOMPARE(form->property("translation").toString(), QString());
        QCOMPARE(form->property("notes").toString(), QString("My notes"));
        QCOMPARE(form->property("context_translation").toString(), QString("My context translation"));
        QCOMPARE(notebook_item(surface, "word_context")->property("text").toString(), QString("Original context"));
        QVERIFY(service.vocabulary().isEmpty());
        click_item(surface, notebook_item(surface, "save_word"));
        QCOMPARE(service.vocabulary().size(), 1);
        const auto saved = service.vocabulary()[0].toMap();
        QCOMPARE(saved.value("sense").toMap(), service.senses()[0].toMap());
        QCOMPARE(saved.value("notes").toString(), QString("My notes"));
    }

    void chosen_meaning_survives_lookup_changes_data() {
        QTest::addColumn<QString>("change");
        for (const auto *change : {"refresh", "failed-refresh", "cached-lookup", "sources", "word", "language"})
            QTest::newRow(change) << QString::fromLatin1(change);
    }

    void chosen_meaning_survives_lookup_changes() {
        QFETCH(QString, change);
        QTemporaryDir directory;
        TestNetwork network;
        network.response = free_entry("Haus", "original meaning");
        const auto path = directory.filePath("words.json");
        showthis::LearningService service(nullptr, path, &network);
        service.set_source_language("de");
        showthis::ShoWSurface surface(nullptr, &service);
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        surface.resize(1100, 800);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = notebook_item(surface, "learning_panel");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QString("Haus")), Q_ARG(QString, QString{})));
        auto *lookup = notebook_item(surface, "lookup_dictionary");
        auto *refresh = notebook_item(surface, "refresh_dictionary");
        auto *meaning = notebook_item(surface, "word_meaning");
        auto *word = notebook_item(surface, "lookup_word");
        auto *save = notebook_item(surface, "save_word");
        QVERIFY(lookup && refresh && meaning && word && save);
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
            lookup->mapToScene(QPointF(lookup->width() / 2, lookup->height() / 2)).toPoint());
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(service.senses().size(), 1);
        const auto selected = service.senses()[0].toMap();
        QTRY_VERIFY(notebook_item(surface, "dictionary_sense_0"));
        auto *sense = notebook_item(surface, "dictionary_sense_0");
        QTRY_VERIFY(sense->isVisible());
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
            sense->mapToScene(QPointF(sense->width() / 2, sense->height() / 2)).toPoint());
        QCOMPARE(meaning->property("text").toString(), QString("original meaning"));
        auto *form = notebook_item(surface, "personal_card_pane");
        QVERIFY(form);
        form->setProperty("translation", "my translation");
        form->setProperty("word_prefix", "das");
        form->setProperty("base_form", "Haus");
        form->setProperty("plural_form", "Häuser");
        form->setProperty("notes", "my association");
        form->setProperty("context_translation", "my sentence translation");
        const bool word_changed = change == "word";
        if (change == "sources") {
            QVERIFY(service.configure_dictionaries({dictionary_selection("kaikki", {"ru"})}, true));
        } else if (change == "word") {
            word->forceActiveFocus();
            notebook_key(surface, Qt::Key_A, Qt::ControlModifier);
            for (const auto key : {Qt::Key_B, Qt::Key_A, Qt::Key_U, Qt::Key_M})
                notebook_key(surface, key);
        } else if (change == "language") {
            auto *language = notebook_item(surface, "lookup_language");
            QVERIFY(language);
            choose_language(surface, language, "fr");
        } else {
            network.response = free_entry("Haus", "replacement meaning");
            if (change == "failed-refresh") network.status = 503;
            auto *button = change == "cached-lookup" ? lookup : refresh;
            QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
                button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
            QTRY_VERIFY(!service.dictionary_busy());
        }
        QCOMPARE(meaning->property("text").toString(), QString("original meaning"));
        QCOMPARE(form->property("translation").toString(), QString("my translation"));
        QCOMPARE(form->property("word_prefix").toString(), QString("das"));
        QCOMPARE(form->property("base_form").toString(), QString("Haus"));
        QCOMPARE(form->property("plural_form").toString(), QString("Häuser"));
        QCOMPARE(form->property("notes").toString(), QString("my association"));
        QCOMPARE(form->property("context_translation").toString(), QString("my sentence translation"));
        QTRY_VERIFY(save->isEnabled());
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier,
            save->mapToScene(QPointF(save->width() / 2, save->height() / 2)).toPoint());
        QCOMPARE(service.vocabulary().size(), 1);
        const auto card = service.vocabulary()[0].toMap();
        if (word_changed) {
            QVERIFY(card.value("source").toMap().isEmpty());
            QVERIFY(card.value("sense").toMap().isEmpty());
        } else {
            QCOMPARE(card.value("source"), selected.value("source"));
            QCOMPARE(card.value("sense").toMap(), selected);
            showthis::LearningService reopened(nullptr, path, &network);
            QCOMPARE(reopened.vocabulary()[0].toMap().value("source"), selected.value("source"));
            const auto destination = directory.filePath("anki.txt");
            QVERIFY(service.export_anki(QUrl::fromLocalFile(destination)));
            QFile exported(destination);
            QVERIFY(exported.open(QIODevice::ReadOnly));
            const auto bytes = exported.readAll();
            QVERIFY(bytes.contains("original meaning"));
            QVERIFY(!bytes.contains("replacement meaning"));
            QVERIFY(bytes.contains("FreeDictionaryAPI.com"));
            QVERIFY(bytes.contains("CC BY-SA 4.0"));
            QVERIFY(bytes.contains("https://en.wiktionary.org/wiki/Haus"));
        }
        QCOMPARE(warnings.size(), 0);
    }

    void workspace_navigation_and_geometry_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("desktop-tall") << QSize(1100, 900);
        QTest::newRow("desktop") << QSize(1000, 700);
        QTest::newRow("compact") << QSize(640, 750);
        QTest::newRow("small") << QSize(480, 640);
    }

    void workspace_navigation_and_geometry() {
        QFETCH(QSize, size);
        QTemporaryDir directory;
        TestNetwork network;
        network.response = free_entry("Haus", "A building used as a home.");
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.set_source_language("de");
        showthis::ShoWSurface surface(nullptr, &service);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.open_notebook_word("Haus", "Das Haus steht neben dem Park.");
        auto *notebook = surface.notebook_dialog();
        QVERIFY(notebook);
        surface.resize(size);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = notebook_item(surface, "learning_panel");
        auto *word = notebook_item(surface, "lookup_word");
        auto *context = notebook_item(surface, "word_context");
        auto *dictionary = notebook_item(surface, "dictionary_pane");
        auto *card = notebook_item(surface, "personal_card_pane");
        auto *columns = notebook_item(surface, "word_columns");
        auto *lookup = notebook_item(surface, "lookup_dictionary");
        auto *meaning = notebook_item(surface, "word_meaning");
        auto *translation = notebook_item(surface, "word_translation");
        auto *result = notebook_item(surface, "context_translation");
        auto *new_word = notebook_item(surface, "new_word");
        auto *translator = notebook_item(surface, "open_translator");
        QVERIFY(panel && word && context && dictionary && card && columns && lookup);
        QVERIFY(meaning && translation && result && new_word && translator);
        QVERIFY(!notebook_item(surface, "context_drawer"));
        QVERIFY(!notebook_item(surface, "compact_context"));
        QVERIFY(!notebook_item(surface, "editor_hide_notebook"));
        QVERIFY(!notebook_item(surface, "editor_new_word"));
        const auto scene_rect = [](QQuickItem *item) {
            return item->mapRectToScene(QRectF(QPointF(), item->size()));
        };
        QTRY_VERIFY(context->isVisible() && context->height() > 0);
        QVERIFY(scene_rect(context).top() >= scene_rect(word).bottom());
        QVERIFY(scene_rect(context).bottom() <= scene_rect(columns).top());
        QVERIFY(new_word->isVisible() && translator->isVisible());
        QVERIFY(scene_rect(new_word).bottom() <= scene_rect(columns).top());
        const bool desktop = panel->property("wide_layout").toBool();
        QTRY_VERIFY(dictionary->isVisible() && dictionary->height() > 0);
        QTRY_COMPARE(dictionary->height(), columns->height());
        const auto dictionary_workspace = scene_rect(dictionary);
        click_item(surface, lookup);
        QTRY_COMPARE(service.senses().size(), 1);
        QTRY_VERIFY(notebook_item(surface, "dictionary_sense_0"));
        auto *senses = notebook_item(surface, "dictionary_senses");
        auto *first_sense = notebook_item(surface, "dictionary_sense_0");
        QVERIFY(senses && first_sense);
        QTRY_VERIFY(first_sense->height() > 0 && senses->height() >= first_sense->height());
        click_item(surface, notebook_item(surface, "dictionary_sense_0"));
        QTRY_COMPARE(meaning->property("text").toString(), QString("A building used as a home."));
        QVERIFY(activate_card(surface));
        QTRY_VERIFY(card->isVisible());
        translation->setProperty("text", "house");
        card->setProperty("notes", "My personal notes.");
        if (desktop) {
            QTRY_VERIFY(qAbs(card->height() - columns->height()) <= 1);
            auto *divider = notebook_item(surface, "workspace_divider");
            QVERIFY(divider && divider->isVisible());
            const auto old_width = dictionary->width();
            const auto start = divider->mapToScene(QPointF(divider->width() / 2, divider->height() / 2)).toPoint();
            QTest::mousePress(&surface, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(&surface, start + QPoint(35, 0), 10);
            QTest::mouseMove(&surface, start + QPoint(70, 0), 10);
            QTest::mouseRelease(&surface, Qt::LeftButton, Qt::NoModifier, start + QPoint(70, 0));
            QTRY_VERIFY(qAbs(dictionary->width() - old_width) > 20);
            QVERIFY(dictionary->width() > 100 && card->width() > 100);
            QVERIFY(scene_rect(dictionary).right() <= scene_rect(card).left());
        } else {
            // Result metadata may grow the workspace after lookup; either page still fills it.
            QTRY_COMPARE(scene_rect(card), scene_rect(columns));
            QCOMPARE(scene_rect(card).topLeft(), dictionary_workspace.topLeft());
            QCOMPARE(scene_rect(card).width(), dictionary_workspace.width());
            QVERIFY(scene_rect(card).height() >= dictionary_workspace.height());
            QVERIFY(!dictionary->isVisible());
            QCOMPARE(panel->property("compact_page").toInt(), 1);
        }
        // Context is inline for both dictionary and card pages, never a third workspace.
        QVERIFY(context->isVisible());
        auto *add_context = notebook_item(surface, "add_context_translation");
        if (!result->isVisible()) {
            QVERIFY(add_context);
            click_item(surface, add_context);
        }
        result->setProperty("text", "The house is next to the park.");
        surface.resize(desktop ? QSize(640, 750) : QSize(1100, 900));
        QTRY_COMPARE(panel->property("wide_layout").toBool(), !desktop);
        QVERIFY(activate_card(surface));
        QTRY_VERIFY(context->isVisible() && result->isVisible());
        QCOMPARE(translation->property("text").toString(), QString("house"));
        QCOMPARE(result->property("text").toString(), QString("The house is next to the park."));
        QCOMPARE(network.requests.size(), 1);
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS")) {
            QTest::qWait(30);
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS")
                + "/workspace-" + QTest::currentDataTag() + ".png"));
        }
        click_item(surface, new_word);
        auto *discard = panel->findChild<QObject *>("discard_draft");
        QVERIFY(discard);
        QTRY_VERIFY(discard->property("visible").toBool());
        auto *footer = discard->property("footer").value<QQuickItem *>();
        QVERIFY(footer);
        auto *cancel = find_item(footer, "cancel_action");
        auto *confirm = find_item(footer, "confirm_action");
        QVERIFY(cancel && confirm);
        click_item(surface, cancel);
        QTRY_VERIFY(!discard->property("visible").toBool());
        QCOMPARE(result->property("text").toString(), QString("The house is next to the park."));
        click_item(surface, new_word);
        QTRY_VERIFY(discard->property("visible").toBool());
        click_item(surface, confirm);
        QTRY_VERIFY(!panel->property("dirty").toBool());
        QVERIFY(context->property("text").toString().isEmpty());
        QVERIFY(result->property("text").toString().isEmpty());
        QCOMPARE(panel->property("compact_page").toInt(), 0);
        QVERIFY(translator->isVisible() && translator->isEnabled());
        click_item(surface, translator);
        QVERIFY(surface.translator_dialog());
        QVERIFY(network.requests.size() == 1);
        QCOMPARE(warnings.size(), 0);
    }

    void responsive_notebook_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("dark");
        QTest::newRow("wide") << QSize(1440, 900) << false;
        QTest::newRow("desktop-tall") << QSize(1100, 900) << false;
        QTest::newRow("desktop") << QSize(1000, 700) << false;
        QTest::newRow("compact") << QSize(640, 600) << false;
        QTest::newRow("small") << QSize(480, 640) << false;
        QTest::newRow("short") << QSize(480, 400) << false;
        QTest::newRow("minimum") << QSize(460, 360) << false;
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
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        surface.resize(size);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = notebook_item(surface, QStringLiteral("learning_panel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word",
            Q_ARG(QString, QStringLiteral("Verantwortungsbewusstsein")),
            Q_ARG(QString, QStringLiteral("Das zeigt ein hohes Verantwortungsbewusstsein."))));
        auto *meaning = notebook_item(surface, QStringLiteral("word_meaning"));
        auto *save = notebook_item(surface, QStringLiteral("save_word"));
        auto *columns = notebook_item(surface, QStringLiteral("word_columns"));
        auto *dictionary = notebook_item(surface, QStringLiteral("dictionary_pane"));
        auto *personal = notebook_item(surface, QStringLiteral("personal_card_pane"));
        auto *navigation = notebook_item(surface, QStringLiteral("notebook_tabs"));
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
        if (panel->property("wide_layout").toBool()) {
            QTRY_VERIFY(columns->height() >= panel->height() / 2);
            QTRY_VERIFY(qAbs(dictionary->height() - columns->height()) <= 1);
            QTRY_VERIFY(qAbs(personal->height() - columns->height()) <= 1);
        }
        if (panel->property("wide_layout").toBool())
            QVERIFY(personal->mapToScene(QPointF()).x() > dictionary->mapToScene(QPointF()).x());
        else {
            QVERIFY(dictionary->isVisible());
            QVERIFY(!personal->isVisible());
            QVERIFY(activate_card(surface));
            QTRY_VERIFY(personal->isVisible());
            QVERIFY(!dictionary->isVisible());
        }
        if (size.height() < 450) {
            click_item(surface, meaning);
            meaning->forceActiveFocus();
            notebook_key(surface, Qt::Key_A, Qt::ControlModifier);
            for (const auto character : QByteArray("Edited in a short window"))
                QTest::keyClick(&surface, character);
            QTRY_COMPARE(meaning->property("text").toString(), QString("Edited in a short window"));
            const auto scroll = notebook_item(surface, "word_workspace_scroll");
            QVERIFY(scroll && scroll->height() > 0);
            QVERIFY(meaning->mapRectToScene(QRectF(QPointF(), meaning->size())).intersects(
                scroll->mapRectToScene(QRectF(QPointF(), scroll->size()))));
            click_item(surface, save);
            QCOMPARE(service.vocabulary().size(), 1);
            QCOMPARE(service.vocabulary()[0].toMap().value("meaning").toString(), QString("Edited in a short window"));
            if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS"))
                QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS")
                    + "/notebook-" + QTest::currentDataTag() + ".png"));
        }

        panel->forceActiveFocus();
        notebook_key(surface, Qt::Key_S, Qt::ControlModifier);
        QTRY_COMPARE(service.vocabulary().size(), 1);
        QVERIFY(!panel->property("dirty").toBool());
        navigation->setProperty("current_index", 1);
        auto *export_button = notebook_item(surface, QStringLiteral("export_anki"));
        auto *search = notebook_item(surface, QStringLiteral("vocabulary_search"));
        QVERIFY(export_button);
        QVERIFY(search);
        QTest::qWait(30);
        const auto export_rect = export_button->mapRectToScene(QRectF(QPointF(), export_button->size()));
        QVERIFY(export_rect.left() >= 0);
        QVERIFY(export_rect.right() <= size.width());
        notebook_key(surface, Qt::Key_F, Qt::ControlModifier);
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
        const auto observed = QString::fromUtf8("Häusern");
        const auto context = QString::fromUtf8("Wir wohnen in den Häusern.");
        QImage image(1100, 800, QImage::Format_RGB32);
        image.fill(Qt::white);
        surface.set_image(image, {{observed, QRect(100, 200, 100, 30), context}});
        surface.show();
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QTest::mouseClick(&surface, Qt::LeftButton, Qt::NoModifier, QPoint(140, 215));
        QVERIFY(surface.notebook_dialog());
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QVERIFY(surface.isVisible());
        auto *panel = notebook_item(surface, "learning_panel");
        QVERIFY(panel);
        auto *word = notebook_item(surface, QStringLiteral("lookup_word"));
        auto *meaning = notebook_item(surface, QStringLiteral("word_meaning"));
        auto *save = notebook_item(surface, QStringLiteral("save_word"));
        QVERIFY(word);
        QVERIFY(meaning);
        QVERIFY(save);
        QVERIFY(!notebook_item(surface, QStringLiteral("dictionary_form")));
        QVERIFY(!notebook_item(surface, QStringLiteral("suggest_lemma")));
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
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        surface.resize(size);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = notebook_item(surface, "learning_panel");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, observed), Q_ARG(QString, QString("der Häuser"))));
        auto *choose = notebook_item(surface, "choose_dictionary_sources");
        auto *lookup = notebook_item(surface, "lookup_dictionary");
        auto *word = notebook_item(surface, "lookup_word");
        auto *meaning = notebook_item(surface, "word_meaning");
        auto *save = notebook_item(surface, "save_word");
        QVERIFY(choose);
        QVERIFY(lookup);
        QVERIFY(word);
        QVERIFY(meaning);
        QVERIFY(save);
        const auto original = service.dictionary_selection();
        meaning->setProperty("text", "My typed meaning");
        QVERIFY(activate_dictionary(surface));
        QTRY_VERIFY(choose->isVisible());
        click_item(surface, choose);
        QTRY_VERIFY(notebook_item(surface, "dictionary_source_kaikki"));
        auto *dialog = surface.notebook_dialog()->findChild<QObject *>("dictionary_source_picker");
        QVERIFY(dialog);
        if (dark) {
            auto *background = dialog->property("background").value<QQuickItem *>();
            QVERIFY(background);
            QTRY_COMPARE(background->property("color").value<QColor>(), QColor("#252a31"));
            auto *card = notebook_item(surface, "dictionary_source_card_kaikki");
            QVERIFY(card);
            auto *card_background = card->property("background").value<QQuickItem *>();
            QVERIFY(card_background);
            QTRY_COMPARE(card_background->property("color").value<QColor>(), QColor("#1c2026"));
        }
        auto *kaikki = notebook_item(surface, "dictionary_source_kaikki");
        auto *free = notebook_item(surface, "dictionary_source_freedictionary");
        auto *russian = notebook_item(surface, "dictionary_language_kaikki_ru");
        auto *english = notebook_item(surface, "dictionary_language_kaikki_en");
        auto *apply = notebook_item(surface, "apply_dictionary_sources");
        auto *cancel = notebook_item(surface, "cancel_dictionary_sources");
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
        QVERIFY(activate_dictionary(surface));
        QTRY_VERIFY(choose->isVisible());
        click_item(surface, choose);
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
        QTRY_VERIFY(notebook_item(surface, "dictionary_sense_1"));
        auto *resolved = notebook_item(surface, "dictionary_sense_1");
        QVERIFY(QMetaObject::invokeMethod(resolved, "clicked"));
        auto *replacement = panel->findChild<QObject *>("replace_card_sense");
        QVERIFY(replacement);
        QTRY_VERIFY(replacement->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(replacement, "accept"));
        QCOMPARE(word->property("text").toString(), QString("Haus"));
        QCOMPARE(meaning->property("text").toString(), QString::fromUtf8("дом"));
        auto *base_form = notebook_item(surface, "word_base_form");
        auto *base_prefix = notebook_item(surface, "word_base_prefix");
        auto *plural_form = notebook_item(surface, "word_plural_form");
        auto *prefix = notebook_item(surface, "word_prefix");
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
        QVERIFY(activate_dictionary(surface));
        QTRY_VERIFY(choose->isVisible());
        click_item(surface, choose);
        QVERIFY(toggle(free, true));
        auto *sample = notebook_item(surface, "dictionary_source_sample");
        auto *sample_en = notebook_item(surface, "dictionary_language_sample_en");
        auto *sample_fr = notebook_item(surface, "dictionary_language_sample_fr");
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
        QVERIFY(activate_dictionary(surface));
        QTRY_VERIFY(choose->isVisible());
        click_item(surface, choose);
        QVERIFY(toggle(sample, false));
        notebook_key(surface, Qt::Key_Escape);
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
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        surface.resize(640, 700);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = notebook_item(surface, "learning_panel");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, word), Q_ARG(QString, QString{})));
        auto *lookup = notebook_item(surface, "lookup_dictionary");
        QVERIFY(lookup);
        QVERIFY(QMetaObject::invokeMethod(lookup, "clicked"));
        QTRY_VERIFY(!service.dictionary_busy());
        QTRY_VERIFY(notebook_item(surface, "dictionary_sense_0"));
        auto *sense = notebook_item(surface, "dictionary_sense_0");
        QVERIFY(QMetaObject::invokeMethod(sense, "clicked"));
        auto *article = notebook_item(surface, "word_prefix");
        auto *availability = notebook_item(surface, "word_number_availability");
        auto *save = notebook_item(surface, "save_word");
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
        QVERIFY(service.save_word({}, "Haus", "", "", "house", "", "", {}));
        const auto selected_id = service.vocabulary()[0].toMap().value("id").toString();
        QVERIFY(service.save_word({}, "Baum", "", "", "tree", "", "", {}));
        showthis::ShoWSurface surface(nullptr, &service);
        TestCaptureBackend backend;
        showthis::OverlayController controller(surface, backend,
            [](const QImage &, const QString &) { return showthis::ScanResult{}; });
        QCOMPARE(surface.status(), QQuickView::Ready);
        QSignalSpy warnings(surface.engine(), &QQmlEngine::warnings);
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        surface.resize(size);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = notebook_item(surface, "learning_panel");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QString("draft")), Q_ARG(QString, QString("context"))));
        auto *meaning = notebook_item(surface, "word_meaning");
        QVERIFY(meaning);
        meaning->setProperty("text", "Keep this unsaved draft");
        QVERIFY(panel->property("dirty").toBool());
        QVERIFY(QMetaObject::invokeMethod(panel, "open_vocabulary"));
        auto *selection = notebook_item(surface, "select_saved_" + selected_id);
        QTRY_VERIFY(selection || (selection = notebook_item(surface, "select_saved_" + selected_id)));
        selection->setProperty("checked", true);
        QVERIFY(QMetaObject::invokeMethod(selection, "clicked"));
        auto *export_button = notebook_item(surface, "export_anki");
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
        QVERIFY(surface.notebook_dialog()->isVisible());
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
        QVERIFY(surface.notebook_dialog()->isVisible());
        QVERIFY(panel->isVisible());
        QVERIFY(!QFileInfo::exists(cancelled));
        QVERIFY(QMetaObject::invokeMethod(export_button, "clicked"));
        QTRY_VERIFY(dialog = find_dialog());
        surface.close_notebook(); // Closing one dialog does not hide the overlay.
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!surface.export_dialog_open());
        QTest::qWait(20);
        QVERIFY(surface.isVisible());
        QVERIFY(!surface.notebook_dialog()->isVisible());
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
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        surface.resize(size);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *navigation = notebook_item(surface, "notebook_tabs");
        auto *panel = notebook_item(surface, "learning_panel");
        QVERIFY(navigation && panel);
        // Open the notebook before navigating to its settings page.
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QString("Haus")), Q_ARG(QString, QString{})));
        navigation->setProperty("current_index", 3);
        auto *edition = notebook_item(surface, "offline_dictionary_edition");
        auto *word_language = notebook_item(surface, "offline_dictionary_word_language");
        auto *check = notebook_item(surface, "check_offline_dictionary_size");
        auto *download = notebook_item(surface, "download_offline_dictionary");
        auto *mode = notebook_item(surface, "offline_dictionary_mode");
        auto *cancel = notebook_item(surface, "cancel_offline_dictionary");
        auto *settings = notebook_item(surface, "offline_dictionary_settings");
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
        choose_language(surface, word_language, "de-DE");
        QCOMPARE(settings->property("word_language").toString(), QString("de"));
        QVERIFY(QMetaObject::invokeMethod(check, "clicked"));
        QTRY_VERIFY(!service.offline_dictionaries()->busy());
        QTRY_VERIFY(download->isEnabled());
        QCOMPARE(network.requests.size(), 1); // Size inspection only; no data download.
        QVERIFY(QMetaObject::invokeMethod(download, "clicked"));
        auto *dialog = settings->findChild<QObject *>("offline_download_confirmation");
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
        const auto selection_before_activation = service.dictionary_selection();
        auto *activate = notebook_item(surface, "activate_offline_dictionary_de_ru");
        QVERIFY(activate && activate->isEnabled());
        QVERIFY(!selection_before_activation.isEmpty());
        const auto requests_before_activation = network.requests.size();
        service.offline_dictionaries()->set_mode("online_only");
        click_item(surface, activate);
        QCOMPARE(service.source_language(), QString("de"));
        QCOMPARE(service.offline_dictionaries()->mode(), QString("prefer_local"));
        QVERIFY(service.dictionary_selection().contains(dictionary_selection("kaikki", {"ru"})));
        for (const auto &selected : selection_before_activation)
            if (selected.toMap().value("provider").toString() != "kaikki")
                QVERIFY(service.dictionary_selection().contains(selected));
        QCOMPARE(network.requests.size(), requests_before_activation);
        QTRY_VERIFY(activate->property("checked").toBool());
        if (qEnvironmentVariableIsSet("SHOWTHIS_TEST_SCREENSHOTS"))
            QVERIFY(surface.grabWindow().save(qEnvironmentVariable("SHOWTHIS_TEST_SCREENSHOTS")
                + "/offline-activated-" + QTest::currentDataTag() + ".png"));
        QTRY_VERIFY(notebook_item(surface, "remove_offline_dictionary_de_ru"));
        mode->setProperty("currentIndex", 2);
        QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 2)));
        QCOMPARE(service.offline_dictionaries()->mode(), QString("local_only"));
        QTest::qWait(30);
        QVERIFY(settings->width() > 200 && settings->width() <= size.width());
        auto *scroll = notebook_item(surface, "notebook_settings_scroll");
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
        auto *lookup = notebook_item(surface, "lookup_dictionary");
        QVERIFY(lookup);
        QVERIFY(QMetaObject::invokeMethod(lookup, "clicked"));
        QTRY_VERIFY(!service.dictionary_busy());
        QCOMPARE(service.senses().size(), 1);
        QCOMPARE(network.requests.size(), requests);
        QVERIFY(service.senses()[0].toMap().value("source").toMap().value("offline").toBool());
        QCOMPARE(warnings.size(), 0);
    }

    void card_edit_preserves_unknown_metadata() {
        QTemporaryDir directory;
        const auto path = directory.filePath("words.json");
        const QJsonObject metadata{{"future", QJsonArray{1, "retained"}}};
        const QJsonObject card{{"id", "future-card"}, {"word", "Haus"}, {"language", "de"},
            {"target_language", "uk"}, {"meaning", "house"}, {"translation", "my translation"},
            {"context", "Das Haus."}, {"context_translation", "The house."},
            {"future_metadata", metadata}, {"linguistics", QJsonObject{{"legacy", true}}}};
        QFile store(path);
        QVERIFY(store.open(QIODevice::WriteOnly));
        const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"cards", QJsonArray{card}},
            {"dictionary_cache", QJsonObject{}}}).toJson();
        QCOMPARE(store.write(data), data.size());
        store.close();
        TestNetwork network;
        showthis::LearningService service(nullptr, path, &network);
        QCOMPARE(service.vocabulary().size(), 1);
        showthis::ShoWSurface surface(nullptr, &service);
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = notebook_item(surface, "learning_panel");
        auto *form = notebook_item(surface, "personal_card_pane");
        auto *save = notebook_item(surface, "save_word");
        QVERIFY(panel && form && save);
        QVERIFY(QMetaObject::invokeMethod(panel, "edit_card", Q_ARG(QVariant, service.vocabulary()[0])));
        QVERIFY(activate_card(surface));
        form->setProperty("notes", "An explicit notebook edit");
        surface.set_translator_source_language("fr");
        surface.set_translator_target_language("es");
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QCOMPARE(service.vocabulary().size(), 1);
        const auto saved = service.vocabulary()[0].toMap();
        QCOMPARE(saved.value("id").toString(), QString("future-card"));
        QCOMPARE(saved.value("target_language").toString(), QString("uk"));
        QCOMPARE(saved.value("future_metadata").toMap(), metadata.toVariantMap());
        QCOMPARE(saved.value("linguistics").toMap(), card.value("linguistics").toObject().toVariantMap());
        QCOMPARE(saved.value("notes").toString(), QString("An explicit notebook edit"));
        showthis::LearningService reopened(nullptr, path, &network);
        QCOMPARE(reopened.vocabulary()[0].toMap().value("future_metadata"), saved.value("future_metadata"));
        QVERIFY(network.requests.isEmpty());
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
            QVERIFY(original.save_word({}, "Haus", "", "", "", "saved translation", "original notes", {}));
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
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        surface.resize(1100, 800);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = notebook_item(surface, "learning_panel");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "edit_card", Q_ARG(QVariant, card)));
        auto *translation = notebook_item(surface, "word_translation");
        auto *form = notebook_item(surface, "personal_card_pane");
        auto *save = notebook_item(surface, "save_word");
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
        auto *meaning = notebook_item(surface, "word_meaning");
        QVERIFY(meaning);
        meaning->setProperty("text", "a meaning instead");
        translation->setProperty("text", "");
        QVERIFY(save->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QVERIFY(service.vocabulary()[0].toMap().value("translation").toString().isEmpty());
        QVERIFY(network.requests.isEmpty());
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
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        surface.resize(1100, 800);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        auto *panel = notebook_item(surface, QStringLiteral("learning_panel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open_word", Q_ARG(QString, QStringLiteral("maison")),
            Q_ARG(QString, QStringLiteral("Voici ma maison."))));
        auto *source = notebook_item(surface, QStringLiteral("lookup_language"));
        auto *prefix = notebook_item(surface, QStringLiteral("word_prefix"));
        auto *meaning = notebook_item(surface, QStringLiteral("word_meaning"));
        auto *save = notebook_item(surface, QStringLiteral("save_word"));
        QVERIFY(source);
        QVERIFY(prefix);
        QVERIFY(meaning);
        QVERIFY(save);
        choose_language(surface, source, "fr");
        QCOMPARE(service.source_language(), QStringLiteral("fr"));
        prefix->setProperty("text", QStringLiteral("la"));
        QVERIFY(QMetaObject::invokeMethod(prefix, "textEdited"));
        meaning->setProperty("text", QStringLiteral("house"));
        choose_language(surface, source, "../invalid");
        notebook_key(surface, Qt::Key_Escape);
        QCOMPARE(service.source_language(), QStringLiteral("fr"));
        QCOMPARE(meaning->property("text").toString(), QStringLiteral("house"));
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QCOMPARE(service.vocabulary().size(), 1);
        const auto card = service.vocabulary()[0].toMap();
        QCOMPARE(card.value("language").toString(), QStringLiteral("fr"));
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
        surface.open_notebook();
        QVERIFY(surface.notebook_dialog());
        auto *card = notebook_item(surface, QStringLiteral("personal_card_pane"));
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

    void escape_closes_notebook_dialog_preserving_draft_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("desktop") << QSize(1000, 700);
        QTest::newRow("compact") << QSize(640, 700);
    }

    void escape_closes_notebook_dialog_preserving_draft() {
        QFETCH(QSize, size);
        QTemporaryDir directory;
        showthis::LearningService service(nullptr, directory.filePath("words.json"));
        showthis::ShoWSurface surface(nullptr, &service);
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
        auto *notebook = surface.notebook_dialog();
        QVERIFY(notebook);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QVERIFY(surface.isVisible());
        surface.open_notebook_word("word", "Original sentence");
        auto *panel = notebook_item(surface, "learning_panel");
        auto *meaning = notebook_item(surface, "word_meaning");
        QVERIFY(panel && meaning);
        meaning->setProperty("text", "Keep this unsaved meaning");
        QVERIFY(activate_card(surface));
        meaning->forceActiveFocus();
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!notebook->isVisible());
        QVERIFY(surface.isVisible());
        QCOMPARE(hide_requests.size(), 0);
        QCOMPARE(surface.image_source(), image_source);
        QVERIFY(panel->property("dirty").toBool());
        QCOMPARE(meaning->property("text").toString(), QString("Keep this unsaved meaning"));
        controller.show_notebook();
        QTRY_VERIFY(notebook->isVisible());
        QVERIFY(surface.isVisible());
        QCOMPARE(meaning->property("text").toString(), QString("Keep this unsaved meaning"));
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!notebook->isVisible());
        QTest::keyClick(&surface, Qt::Key_Escape);
        QTRY_VERIFY(!surface.isVisible());
        QCOMPARE(hide_requests.size(), 1);
        QCOMPARE(backend.capture_count, 0);
        QCOMPARE(warnings.size(), 0);
    }

    void overlay_visibility_preserves_dialogs_and_translation_session() {
        QTemporaryDir directory;
        TestNetwork network;
        network.automatic = false;
        network.response = R"({"translatedText":"house"})";
        showthis::LearningService service(nullptr, directory.filePath("words.json"), &network);
        service.translator()->set_provider_id("libretranslate");
        showthis::ShoWSurface surface(nullptr, &service);
        surface.set_languages({"eng"});
        TestCaptureBackend backend;
        showthis::OverlayController controller(surface, backend,
            [](const QImage &, const QString &) { return showthis::ScanResult{}; });
        surface.open_notebook_word("Haus", "Original context");
        surface.open_translator("Haus", "de", "translation");
        auto *notebook = surface.notebook_dialog();
        auto *translator = surface.translator_dialog();
        QVERIFY(notebook && translator);
        QVERIFY(QTest::qWaitForWindowExposed(&surface));
        QCOMPARE(notebook->window(), &surface);
        QCOMPARE(translator->window(), &surface);
        service.translator()->translate_session("Haus", "de", "en");
        const auto pending = network.last_reply;
        QVERIFY(pending && service.translator()->session_busy());
        controller.toggle_overlay();
        QVERIFY(!surface.isVisible());
        QCOMPARE(backend.capture_count, 0);
        QVERIFY(notebook->isVisible() && translator->isVisible());
        QVERIFY(service.translator()->session_busy());
        controller.show_translator();
        QTRY_VERIFY(surface.isVisible());
        QVERIFY(notebook->isVisible() && translator->isVisible());
        QVERIFY(service.translator()->session_busy());
        pending->complete();
        QTRY_COMPARE(service.translator()->session_text(), QString("house"));
        QVERIFY(service.vocabulary().isEmpty()); // Showing or hiding never applies a translation.
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
        QTRY_COMPARE(backend.capture_count, 1);
        QVERIFY(!surface.isVisible());
        controller.toggle_overlay(); // Hide while the first capture is pending.
        backend.complete(image);
        QVERIFY(!surface.isVisible());
        QCOMPARE(scans.load(), 0);

        controller.toggle_overlay();
        controller.toggle_overlay();
        controller.toggle_overlay(); // Show wins; the same pending capture is reused.
        QTRY_COMPARE(backend.capture_count, 2);
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
        QTRY_COMPARE(backend.capture_count, 1);
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
        QTRY_COMPARE(backend.capture_count, 1);
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
        QTRY_COMPARE(backend.capture_count, 1);
        backend.fail(QStringLiteral("Temporary capture failure"));
        QCOMPARE(surface.error(), QStringLiteral("Temporary capture failure"));
        QVERIFY(surface.isVisible());
        controller.toggle_overlay();
        controller.toggle_overlay();
        QTRY_COMPARE(backend.capture_count, 2);
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
        QTRY_COMPARE(backend.capture_count, 1);
        backend.complete(image);
        QTRY_VERIFY(!surface.scanning());
        QCOMPARE(surface.error(), QStringLiteral("Simulated OCR failure"));
        QVERIFY(surface.isVisible());
        surface.hide(); // A native hide must not consume the next show request.
        controller.toggle_overlay();
        QTRY_COMPARE(backend.capture_count, 2);
        backend.complete(image);
        QTRY_VERIFY(!surface.scanning());
        QVERIFY(surface.isVisible());
        QVERIFY(surface.error().isEmpty());
        QCOMPARE(scans.load(), 2);
        controller.hide_overlay();
    }

    void shutdown_requests_cooperative_scan_cancellation() {
        QTemporaryDir directory;
        showthis::LearningService service(nullptr, directory.filePath("words.json"));
        showthis::ShoWSurface surface(nullptr, &service);
        TestCaptureBackend backend;
        std::atomic_bool cancelled{false};
        QSemaphore started;
        showthis::OverlayController controller(surface, backend, [&](const QImage &, const QString &) {
            started.release();
            QElapsedTimer timer;
            timer.start();
            while (!cancelled.load() && timer.elapsed() < 5000) QThread::msleep(1);
            return showthis::ScanResult{};
        });
        controller.set_cancel_handler([&]() { cancelled.store(true); });
        controller.toggle_overlay();
        QTRY_COMPARE(backend.capture_count, 1);
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::white);
        backend.complete(image);
        QTRY_VERIFY(started.available() > 0);
        controller.wait_for_finished();
        QVERIFY(cancelled.load());
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
