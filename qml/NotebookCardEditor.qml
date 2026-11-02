import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

NotebookSurface {
    id: form
    required property var service
    required property string word
    required property string card_id
    required property bool dirty
    required property bool wide_layout
    readonly property bool busy: service.dictionary_busy || service.translator.busy
    property alias word_prefix: prefix.text
    property alias base_form: base_input.text
    property alias base_prefix: base_prefix_input.text
    property alias plural_form: plural_input.text
    property alias singular_form: singular_input.text
    property bool number_relevant: false
    property string singular_status: "unknown"
    property string plural_status: "unknown"
    property string article_origin: ""
    property string base_article_origin: ""
    property string grammar_profile: ""
    property alias meaning: meaning_input.text
    property alias translation: translation_input.text
    property alias notes: notes_input.text
    signal edited()
    signal translation_language_changed()
    signal translate_requested()
    objectName: "personal_card_pane"

    function load(card) {
        word_prefix = card.article || ""
        load_forms(card)
        meaning = card.meaning || ""
        translation = service.translator.configured ? card.translation || "" : ""
        notes = card.notes || ""
        notes_toggle.checked = notes.length > 0
        scroll.contentItem.contentY = 0
    }
    function load_forms(sense) {
        base_form = sense.base_form || ""
        base_prefix = sense.base_article || ""
        plural_form = (sense.plural_forms || []).join(" / ")
        singular_form = (sense.singular_forms || []).join(" / ")
        number_relevant = !!sense.number_relevant
        singular_status = sense.singular_status || "unknown"
        plural_status = sense.plural_status || "unknown"
        article_origin = sense.article_origin || ""
        base_article_origin = sense.base_article_origin || ""
        grammar_profile = sense.grammar_profile || ""
        forms_toggle.checked = !!(base_form || base_prefix || plural_form || number_relevant)
    }
    function word_forms() {
        return {base_form: base_form, base_article: base_prefix, number_relevant: number_relevant,
            singular_status: singular_status, plural_status: plural_status, grammar_profile: grammar_profile,
            article_origin: article_origin, base_article_origin: base_article_origin,
            singular_forms: singular_form.split(/\s*\/\s*/).map(function(word) { return word.trim() }).filter(function(word) { return !!word }),
            plural_forms: plural_form.split(/\s*\/\s*/).map(function(word) { return word.trim() }).filter(function(word) { return !!word })}
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Your word card"); font.pixelSize: 17; font.bold: true; Layout.fillWidth: true }
            Label {
                text: form.dirty ? qsTr("Unsaved") : form.card_id.length > 0 ? qsTr("Saved") : qsTr("New")
                font.pixelSize: 11
                opacity: 0.5
            }
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Article / prefix"); font.bold: true; font.pixelSize: 12 }
                    TextField {
                        id: prefix
                        objectName: "word_prefix"
                        placeholderText: qsTr("Optional")
                        maximumLength: 80
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Accessible.name: qsTr("Optional article or word prefix")
                        onTextEdited: { form.article_origin = "manual"; form.edited() }
                    }
                }
                Label {
                    objectName: "word_headword"
                    visible: form.word_prefix.length > 0
                    text: form.service.format_word(form.word, form.word_prefix)
                    textFormat: Text.PlainText
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    color: form.palette.highlight
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    objectName: "word_number_availability"
                    text: form.service.number_availability(form.word_forms())
                    visible: text.length > 0
                    font.pixelSize: 12
                    opacity: 0.7
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    visible: form.article_origin === "profile" || form.base_article_origin === "profile"
                    text: qsTr("Article suggested by grammar profile · editable")
                    font.pixelSize: 11
                    opacity: 0.6
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label { text: qsTr("Meaning"); font.bold: true; font.pixelSize: 12 }
                TextArea {
                    id: meaning_input
                    objectName: "word_meaning"
                    Layout.fillWidth: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    selectByMouse: true
                    placeholderText: form.wide_layout ? qsTr("Choose a meaning on the left, or write your own.") : qsTr("Your chosen meaning, in your own words…")
                    Accessible.name: qsTr("Chosen meaning")
                    onTextChanged: form.edited()
                }
                NotebookButton {
                    id: forms_toggle
                    objectName: "toggle_word_forms"
                    text: checked ? qsTr("Word forms −") : qsTr("+ Word forms")
                    quiet: true
                    checkable: true
                    align_left: true
                    Layout.fillWidth: true
                }
                GridLayout {
                    visible: forms_toggle.checked
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 6
                    Label { text: qsTr("Base form"); font.pixelSize: 12 }
                    TextField {
                        id: base_input
                        objectName: "word_base_form"
                        placeholderText: qsTr("If available")
                        maximumLength: 200
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Accessible.name: qsTr("Base form")
                        onTextEdited: form.edited()
                    }
                    Label { text: qsTr("Base prefix"); font.pixelSize: 12 }
                    TextField {
                        id: base_prefix_input
                        objectName: "word_base_prefix"
                        placeholderText: qsTr("Article / prefix, if found")
                        maximumLength: 80
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Accessible.name: qsTr("Base form article or prefix")
                        onTextEdited: { form.base_article_origin = "manual"; form.edited() }
                    }
                    Label { visible: form.number_relevant; text: qsTr("Singular"); font.pixelSize: 12 }
                    TextField {
                        id: singular_input
                        objectName: "word_singular_form"
                        visible: form.number_relevant
                        placeholderText: form.singular_status === "unavailable" ? qsTr("No singular") : qsTr("Not listed")
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Accessible.name: qsTr("Singular forms")
                        onTextEdited: {
                            form.singular_status = text.trim().length > 0 ? "available" : "unknown"
                            form.edited()
                        }
                    }
                    Label { text: qsTr("Plural"); font.pixelSize: 12 }
                    TextField {
                        id: plural_input
                        objectName: "word_plural_form"
                        placeholderText: form.plural_status === "unavailable" ? qsTr("No plural") : qsTr("Not listed · alternatives with /")
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Accessible.name: qsTr("Plural forms")
                        onTextEdited: {
                            form.number_relevant = true
                            form.plural_status = text.trim().length > 0 ? "available" : "unknown"
                            form.edited()
                        }
                    }
                }
                RowLayout {
                    visible: form.service.translator.configured
                    Layout.fillWidth: true
                    Label { text: qsTr("Translation"); font.bold: true; font.pixelSize: 12; Layout.fillWidth: true }
                    NotebookLanguagePicker {
                        objectName: "target_language"
                        options: form.service.language_options
                        language_code: form.service.target_language
                        Layout.preferredWidth: 125
                        enabled: !form.busy
                        Accessible.name: qsTr("Translation language")
                        onLanguage_selected: function(code) {
                            const previous = form.service.target_language
                            form.service.target_language = code
                            if (form.service.target_language !== previous) form.translation_language_changed()
                        }
                    }
                }
                TextArea {
                    id: translation_input
                    objectName: "word_translation"
                    visible: form.service.translator.configured
                    Layout.fillWidth: true
                    font.pixelSize: 19
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    selectByMouse: true
                    enabled: !form.service.translator.busy
                    placeholderText: qsTr("A translation in your language…")
                    Accessible.name: qsTr("Word translation")
                    onTextChanged: form.edited()
                }
                NotebookButton {
                    objectName: "translate_word"
                    visible: form.service.translator.configured
                    text: form.service.translator.busy ? qsTr("Translating…")
                        : qsTr("Translate with %1").arg(form.service.translator.provider_name)
                    quiet: true
                    Layout.fillWidth: true
                    enabled: !form.busy && form.word.trim().length > 0
                    onClicked: form.translate_requested()
                    ToolTip.visible: hovered
                    ToolTip.text: form.service.translator.supports_context
                        ? qsTr("Only clicking Translate sends your word and sentence to %1").arg(form.service.translator.provider_name)
                        : qsTr("Only clicking Translate sends your word to %1").arg(form.service.translator.provider_name)
                }
                NotebookButton {
                    id: notes_toggle
                    text: checked ? qsTr("Notes −") : qsTr("+ Add a note")
                    quiet: true
                    checkable: true
                    align_left: true
                    Layout.fillWidth: true
                }
                TextArea {
                    id: notes_input
                    visible: notes_toggle.checked
                    Layout.fillWidth: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    selectByMouse: true
                    placeholderText: qsTr("An association, grammar note, or example of your own…")
                    Accessible.name: qsTr("Personal notes")
                    onTextChanged: form.edited()
                }
            }
        }
    }
}
