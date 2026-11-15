#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QtTest>

namespace {

bool load(QQuickView &view, const QByteArray &contents)
{
    const QUrl url(QStringLiteral("qrc:/qml/LanguagePickerTest.qml"));
    auto *component = new QQmlComponent(view.engine(), &view);
    component->setData("import QtQuick\nimport QtQuick.Controls\nItem { width: 480; height: 420\n"
                       + contents + "\n}", url);
    auto *root = component->create();
    if (!root) {
        qWarning() << component->errors();
        return false;
    }
    view.setContent(url, component, root);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(480, 420);
    view.show();
    view.requestActivate();
    return view.status() == QQuickView::Ready && QTest::qWaitForWindowExposed(&view);
}

QQuickItem *picker(QQuickView &view)
{
    return view.rootObject()->findChild<QQuickItem *>(QStringLiteral("picker"));
}

QObject *popup(QQuickItem *control)
{
    return control->property("popup").value<QObject *>();
}

QObject *results(QQuickItem *control)
{
    return popup(control)->property("contentItem").value<QObject *>();
}

QQuickItem *editor(QQuickItem *control)
{
    auto *content = control->property("contentItem").value<QQuickItem *>();
    if (!content)
        return nullptr;
    for (auto *child : content->childItems()) {
        if (child->property("selectByMouse").isValid())
            return child;
    }
    return nullptr;
}

void type(QQuickView &view, QQuickItem *control, const QByteArray &text)
{
    control->forceActiveFocus();
    QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier);
    for (char character : text)
        QTest::keyClick(&view, character);
}

QQuickItem *find_text(QQuickItem *root, const QString &text)
{
    if (root->property("text").toString() == text)
        return root;
    for (auto *child : root->childItems()) {
        if (auto *found = find_text(child, text))
            return found;
    }
    return nullptr;
}

const QByteArray languages = R"(
    NotebookLanguagePicker {
        objectName: "picker"
        x: 20; y: 20; width: 300
        options: [
            {code: "en", name: "English"},
            {code: "fr", name: "French"},
            {code: "de", name: "German"},
            {code: "ka", name: "Georgian"},
            {code: "ja", name: "Japanese"}
        ]
        language_code: "en"
        onLanguage_selected: function(code) { language_code = code }
    }
)";

}

class LanguagePickerTest : public QObject {
    Q_OBJECT
private slots:
    void language_filter_navigation_commit_and_cancel()
    {
        QQuickView view;
        QVERIFY(load(view, languages));
        auto *control = picker(view);
        QVERIFY(control);
        QVERIFY(editor(control));
        QSignalSpy activated(control, SIGNAL(activated(int)));
        QSignalSpy selected(control, SIGNAL(language_selected(QString)));
        QSignalSpy accepted(control, SIGNAL(accepted()));
        QVERIFY(activated.isValid());
        QVERIFY(selected.isValid());
        QVERIFY(accepted.isValid());

        type(view, control, "gE");
        QTRY_VERIFY(popup(control)->property("visible").toBool());
        QTRY_COMPARE(results(control)->property("count").toInt(), 2);
        QCOMPARE(control->property("count").toInt(), 5);
        QCOMPARE(control->property("currentIndex").toInt(), 0);
        QCOMPARE(selected.size(), 0);
        QTest::keyClick(&view, Qt::Key_Down);
        QCOMPARE(control->property("currentIndex").toInt(), 0);
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_COMPARE(control->property("language_code").toString(), QStringLiteral("ka"));
        QCOMPARE(control->property("currentIndex").toInt(), 3);
        QCOMPARE(control->property("currentValue").toString(), QStringLiteral("ka"));
        QCOMPARE(activated.size(), 1);
        QCOMPARE(activated.first().first().toInt(), 3);
        QCOMPARE(selected.size(), 1);
        QCOMPARE(accepted.size(), 1);
        QCOMPARE(editor(control)->property("text").toString(), QStringLiteral("Georgian"));

        type(view, control, "FR");
        QTRY_COMPARE(results(control)->property("count").toInt(), 1);
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!popup(control)->property("visible").toBool());
        QCOMPARE(editor(control)->property("text").toString(), QStringLiteral("Georgian"));
        QCOMPARE(control->property("currentIndex").toInt(), 3);
        QCOMPARE(activated.size(), 1);
        QCOMPARE(selected.size(), 1);
        QCOMPARE(accepted.size(), 1);

        // Committing must not remove the caller's selection binding.
        control->setProperty("language_code", QStringLiteral("en"));
        QTRY_COMPARE(control->property("currentIndex").toInt(), 0);
        QCOMPARE(editor(control)->property("text").toString(), QStringLiteral("English"));
    }

    void partial_language_name_commits_filtered_match()
    {
        QQuickView view;
        QVERIFY(load(view, languages));
        auto *control = picker(view);
        QVERIFY(control);
        QSignalSpy selected(control, SIGNAL(language_selected(QString)));
        type(view, control, "jap");
        QTRY_COMPARE(results(control)->property("count").toInt(), 1);
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_COMPARE(control->property("language_code").toString(), QStringLiteral("ja"));
        QCOMPARE(selected.size(), 1);
    }

    void language_code_search_and_regional_input()
    {
        QQuickView view;
        QVERIFY(load(view, languages));
        auto *control = picker(view);
        QVERIFY(control);
        QVERIFY(editor(control));
        QSignalSpy activated(control, SIGNAL(activated(int)));
        QSignalSpy selected(control, SIGNAL(language_selected(QString)));
        QSignalSpy accepted(control, SIGNAL(accepted()));

        type(view, control, "JA");
        QTRY_COMPARE(results(control)->property("count").toInt(), 1);
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(control->property("language_code").toString(), QStringLiteral("ja"));
        QCOMPARE(activated.first().first().toInt(), 4);

        type(view, control, "pt-BR");
        QTRY_COMPARE(results(control)->property("count").toInt(), 0);
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_COMPARE(control->property("language_code").toString(), QStringLiteral("pt-BR"));
        QCOMPARE(control->property("currentIndex").toInt(), -1);
        QCOMPARE(selected.size(), 2);
        QCOMPARE(selected.last().first().toString(), QStringLiteral("pt-BR"));
        QCOMPARE(activated.size(), 1);
        QCOMPARE(accepted.size(), 2);
        QCOMPARE(editor(control)->property("text").toString(), QStringLiteral("pt-BR"));
        control->setProperty("compact", true);
        QTRY_COMPARE(editor(control)->property("text").toString(), QStringLiteral("PT-BR"));

        type(view, control, "not-a-language-code");
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(control->property("language_code").toString(), QStringLiteral("pt-BR"));
        QCOMPARE(selected.size(), 2);
        QCOMPARE(accepted.size(), 2);
        QTest::keyClick(&view, Qt::Key_Escape);
        QCOMPARE(editor(control)->property("text").toString(), QStringLiteral("PT-BR"));
    }

    void mouse_commit_uses_original_model_index()
    {
        QQuickView view;
        QVERIFY(load(view, languages));
        auto *control = picker(view);
        QVERIFY(control);
        QSignalSpy activated(control, SIGNAL(activated(int)));
        QSignalSpy selected(control, SIGNAL(language_selected(QString)));
        type(view, control, "ge");
        QTRY_COMPARE(results(control)->property("count").toInt(), 2);
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = find_text(view.contentItem(), QStringLiteral("Georgian"))));
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                         row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint());
        QTRY_COMPARE(control->property("currentIndex").toInt(), 3);
        QCOMPARE(activated.size(), 1);
        QCOMPARE(activated.first().first().toInt(), 3);
        QCOMPARE(selected.size(), 1);
        QCOMPARE(control->property("language_code").toString(), QStringLiteral("ka"));
    }

    void generic_choices_reject_unknown_text_and_preserve_binding()
    {
        QQuickView view;
        QVERIFY(load(view, R"(
            property int selected_index: 0
            SearchableComboBox {
                objectName: "picker"
                x: 20; y: 20; width: 300
                model: [{id: "off", name: "Off"}, {id: "first", name: "First provider"},
                        {id: "second", name: "Second provider"}]
                textRole: "name"
                valueRole: "id"
                currentIndex: parent.selected_index
                onActivated: function(index) { parent.selected_index = index }
            }
        )"));
        auto *control = picker(view);
        QVERIFY(control);
        QVERIFY(editor(control));
        QSignalSpy activated(control, SIGNAL(activated(int)));
        QSignalSpy accepted(control, SIGNAL(accepted()));
        type(view, control, "unknown value");
        QTRY_COMPARE(results(control)->property("count").toInt(), 0);
        QTest::keyClick(&view, Qt::Key_Down);
        QTest::keyClick(&view, Qt::Key_Up);
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(control->property("currentIndex").toInt(), 0);
        QCOMPARE(control->property("currentValue").toString(), QStringLiteral("off"));
        QCOMPARE(activated.size(), 0);
        QCOMPARE(accepted.size(), 0);
        QTest::keyClick(&view, Qt::Key_Escape);
        QCOMPARE(editor(control)->property("text").toString(), QStringLiteral("Off"));

        type(view, control, "SECOND");
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_COMPARE(control->property("currentIndex").toInt(), 2);
        QCOMPARE(control->property("currentValue").toString(), QStringLiteral("second"));
        QCOMPARE(activated.size(), 1);
        QCOMPARE(activated.first().first().toInt(), 2);
        QCOMPARE(accepted.size(), 1);
        view.rootObject()->setProperty("selected_index", 1);
        QTRY_COMPARE(control->property("currentIndex").toInt(), 1);
        QCOMPARE(editor(control)->property("text").toString(), QStringLiteral("First provider"));
    }

    void arrows_only_preview_until_enter()
    {
        QQuickView view;
        QVERIFY(load(view, R"(
            SearchableComboBox {
                objectName: "picker"
                x: 20; y: 20; width: 300
                model: ["One", "Two", "Three"]
            }
        )"));
        auto *control = picker(view);
        QVERIFY(control);
        QSignalSpy activated(control, SIGNAL(activated(int)));
        control->forceActiveFocus();
        QTest::keyClick(&view, Qt::Key_Down);
        QCOMPARE(control->property("currentIndex").toInt(), 0);
        QCOMPARE(activated.size(), 0);
        QTest::keyClick(&view, Qt::Key_Up);
        QTest::keyClick(&view, Qt::Key_Down);
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(control->property("currentIndex").toInt(), 1);
        QCOMPARE(activated.size(), 1);
        type(view, control, "TH");
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(control->property("currentIndex").toInt(), 2);
        QCOMPARE(control->property("currentValue").toString(), QStringLiteral("Three"));
        QCOMPARE(activated.last().first().toInt(), 2);
    }

    void empty_models_and_model_replacement_are_safe()
    {
        QQuickView view;
        QVERIFY(load(view, R"(
            SearchableComboBox {
                objectName: "picker"
                x: 20; y: 20; width: 300
                model: []
            }
        )"));
        auto *control = picker(view);
        QVERIFY(control);
        QSignalSpy activated(control, SIGNAL(activated(int)));
        QSignalSpy accepted(control, SIGNAL(accepted()));
        type(view, control, "missing");
        QTest::keyClick(&view, Qt::Key_Down);
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(control->property("currentIndex").toInt(), -1);
        QCOMPARE(activated.size(), 0);
        QCOMPARE(accepted.size(), 0);
        QTest::keyClick(&view, Qt::Key_Escape);
        control->setProperty("model", QStringList{QStringLiteral("Only")});
        type(view, control, "Only");
        QTRY_COMPARE(results(control)->property("count").toInt(), 1);
        control->setProperty("model", QVariantList{});
        QTRY_COMPARE(results(control)->property("count").toInt(), 0);
        QTest::keyClick(&view, Qt::Key_Return);
        QTest::keyClick(&view, Qt::Key_Escape);
        QCOMPARE(control->property("currentIndex").toInt(), -1);
        QCOMPARE(activated.size(), 0);
        QCOMPARE(accepted.size(), 0);
    }

    void language_popup_is_virtualized()
    {
        QQuickView view;
        QVERIFY(load(view, R"(
            NotebookLanguagePicker {
                objectName: "picker"
                x: 20; y: 20; width: 300
                language_code: "en"
                options: {
                    const entries = []
                    for (let index = 0; index < 1200; ++index)
                        entries.push({code: "code" + index, name: "Language " + index})
                    return entries
                }
            }
        )"));
        auto *control = picker(view);
        QVERIFY(control);
        type(view, control, "Language");
        QTRY_COMPARE(results(control)->property("count").toInt(), 1200);
        auto *content = results(control)->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        QTRY_VERIFY(!content->childItems().isEmpty());
        QVERIFY2(content->childItems().size() < 100, "The language popup must instantiate only visible/cached rows");
        QTest::keyClick(&view, Qt::Key_Escape);
    }
};

int main(int argc, char **argv)
{
    Q_INIT_RESOURCE(overlay);
    QGuiApplication application(argc, argv);
    LanguagePickerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "LanguagePickerTest.moc"
