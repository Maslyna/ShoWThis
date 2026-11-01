import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property var service
    required property bool wide_layout
    property string card_id: ""
    property int selected_sense: -1
    property var saved_source: ({})
    property bool dirty: false
    property bool loading: false
    property int compact_page: 0
    property bool context_expanded: false
    property bool lookup_attempted: false
    property alias word_prefix: card_form.word_prefix
    readonly property bool busy: service.dictionary_busy || service.translator.busy
    readonly property var active_source: current_sense.source && current_sense.source.url ? current_sense.source
        : service.source.url ? service.source : saved_source
    readonly property var current_sense: selected_sense >= 0 && selected_sense < service.senses.length
        ? service.senses[selected_sense] : ({})
    readonly property bool can_save: !busy && word_input.text.trim().length > 0
        && (card_form.meaning.trim().length > 0 || (service.translator.configured && card_form.translation.trim().length > 0))
        && (card_id.length === 0 || dirty)
    spacing: 10

    function mark_dirty() { if (!loading) dirty = true }
    function focus_search() {
        word_input.forceActiveFocus()
        word_input.selectAll()
    }
    function load(action) {
        loading = true
        const card = action.card || ({})
        if (card.language || action.language) service.source_language = card.language || action.language
        if (card.target_language) service.target_language = card.target_language
        if (card.dictionary_mode || action.dictionary_mode)
            service.dictionary_mode = card.dictionary_mode || action.dictionary_mode
        if (card.definition_language || action.definition_language)
            service.definition_language = card.definition_language || action.definition_language
        if (action.dictionary_fallback !== undefined) service.dictionary_fallback = action.dictionary_fallback
        if (action.follow_forms !== undefined) service.follow_forms = action.follow_forms
        service.clear()
        card_id = card.id || ""
        saved_source = card.source || ({})
        selected_sense = -1
        // Existing cards are never normalized merely by opening them.
        word_input.text = card.id ? card.word : (action.word || "").trim()
            .replace(/^[\s.,!?;:"“”„()]+|[\s.,!?;:"“”„()]+$/g, "")
        context_input.text = card.context || action.context || ""
        card_form.load(card)
        compact_page = card_id.length > 0 ? 1 : 0
        context_expanded = false
        lookup_attempted = false
        scroll.contentItem.contentY = 0
        loading = false
        dirty = false
        if (action.from_history) lookup()
    }
    function reset_lookup() {
        service.clear()
        selected_sense = -1
        saved_source = ({})
        lookup_attempted = false
        card_form.word_prefix = ""
        card_form.meaning = ""
        card_form.translation = ""
        mark_dirty()
    }
    function lookup() {
        compact_page = 0
        lookup_attempted = true
        service.lookup(word_input.text, context_input.text)
    }
    function native_translation(sense): string {
        if (!service.translator.configured) return ""
        return (sense.translations || []).filter(function(item) {
            return item.language.code === editor.service.target_language
        }).map(function(item) { return item.word }).join(", ")
    }
    function select_sense(index: int) {
        selected_sense = index
        if (current_sense.headword) word_input.text = current_sense.headword
        card_form.meaning = current_sense.definition || ""
        card_form.translation = native_translation(current_sense)
        card_form.word_prefix = current_sense.article || ""
        mark_dirty()
        if (!wide_layout) compact_page = 1
    }
    function search_headword(word: string) {
        word_input.text = word
        reset_lookup()
        lookup()
    }
    function search_both() {
        service.dictionary_mode = "both"
        lookup()
    }
    function save_card() {
        if (!can_save) return
        if (service.save_word(card_id, word_input.text, context_input.text,
                card_form.meaning, service.translator.configured ? card_form.translation : "",
                card_form.notes, selected_sense, card_form.word_prefix)) {
            if (card_id.length === 0) card_id = service.vocabulary[0].id
            dirty = false
        }
    }

    Connections {
        target: editor.service
        function onLookup_changed() { editor.selected_sense = -1 }
    }
    Connections {
        target: editor.service.translator
        function onTranslated(text) {
            card_form.translation = text
            editor.mark_dirty()
        }
        function onConfiguration_changed() {
            // Never save hidden translations or allow old provider results to
            // survive a configuration change.
            card_form.translation = ""
        }
    }

    ScrollView {
        id: scroll
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: availableWidth
        ScrollBar.vertical.policy: editor.wide_layout ? ScrollBar.AsNeeded : ScrollBar.AlwaysOn
        ColumnLayout {
            width: parent.width
            spacing: 12
            NotebookSurface {
                id: search_surface
                Layout.fillWidth: true
                padding: 12
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        TextField {
                            id: word_input
                            objectName: "lookup_word"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            font.pixelSize: 26
                            placeholderText: qsTr("Type a word…")
                            background: null
                            selectByMouse: true
                            enabled: !editor.busy
                            Accessible.name: qsTr("Word to look up")
                            onAccepted: if (lookup_button.enabled) editor.lookup()
                            onTextEdited: editor.reset_lookup()
                        }
                        NotebookLanguagePicker {
                            objectName: "lookup_language"
                            options: editor.service.language_options
                            language_code: editor.service.source_language
                            compact: !editor.wide_layout
                            Layout.preferredWidth: editor.wide_layout ? 120 : 64
                            enabled: !editor.busy
                            Accessible.name: qsTr("Word language, separate from OCR")
                            onLanguage_selected: function(code) {
                                const previous = editor.service.source_language
                                editor.service.source_language = code
                                if (editor.service.source_language !== previous) editor.reset_lookup()
                            }
                        }
                        NotebookButton {
                            id: lookup_button
                            objectName: "lookup_dictionary"
                            text: editor.service.dictionary_busy ? qsTr("Looking…") : qsTr("Look up")
                            primary: true
                            enabled: !editor.busy && word_input.text.trim().length > 0
                            onClicked: editor.lookup()
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Send only this word to the selected dictionaries · Enter")
                        }
                    }
                    NotebookDictionaryOptions {
                        service: editor.service
                        onChanged: editor.reset_lookup()
                    }
                }
            }
            RowLayout {
                id: context_row
                Layout.fillWidth: true
                spacing: 12
                Label { text: editor.wide_layout ? qsTr("IN CONTEXT") : qsTr("Context"); font.pixelSize: 10; font.bold: true; opacity: 0.5 }
                Label {
                    visible: !editor.wide_layout && !editor.context_expanded
                    text: context_input.text || qsTr("Add the original sentence")
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    opacity: 0.65
                    Layout.fillWidth: true
                }
                ScrollView {
                    visible: editor.wide_layout || editor.context_expanded
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    clip: true
                    contentWidth: availableWidth
                    TextArea {
                        id: context_input
                        objectName: "word_context"
                        placeholderText: qsTr("Your sentence, or the line captured by OCR…")
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        enabled: !editor.service.translator.busy
                        Accessible.name: qsTr("Original sentence")
                        onTextChanged: editor.mark_dirty()
                    }
                }
                NotebookButton {
                    visible: !editor.wide_layout
                    text: editor.context_expanded ? qsTr("Done") : qsTr("Edit")
                    quiet: true
                    onClicked: editor.context_expanded = !editor.context_expanded
                }
            }
            RowLayout {
                visible: !editor.wide_layout
                Layout.fillWidth: true
                NotebookButton {
                    objectName: "compact_dictionary"
                    text: qsTr("Dictionary (%1)").arg(editor.service.senses.length)
                    checked: editor.compact_page === 0
                    quiet: true
                    Layout.fillWidth: true
                    onClicked: editor.compact_page = 0
                }
                NotebookButton {
                    objectName: "compact_word_card"
                    text: qsTr("My word card")
                    checked: editor.compact_page === 1
                    quiet: true
                    Layout.fillWidth: true
                    onClicked: editor.compact_page = 1
                }
            }
            GridLayout {
                objectName: "word_columns"
                columns: editor.wide_layout ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12
                Layout.fillWidth: true
                readonly property real card_height: editor.wide_layout
                    ? Math.max(240, scroll.availableHeight - search_surface.height - context_row.height - 36)
                    : Math.max(190, scroll.availableHeight - search_surface.height - context_row.height - 86)
                NotebookDictionary {
                    service: editor.service
                    word: word_input.text
                    selected_sense: editor.selected_sense
                    current_sense: editor.current_sense
                    active_source: editor.active_source
                    lookup_attempted: editor.lookup_attempted
                    wide_layout: editor.wide_layout
                    visible: editor.wide_layout || editor.compact_page === 0
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: parent.card_height
                    onSense_selected: function(index) { editor.select_sense(index) }
                    onManual_meaning_requested: editor.compact_page = 1
                    onHeadword_requested: function(word) { editor.search_headword(word) }
                    onBoth_requested: editor.search_both()
                }
                NotebookCardEditor {
                    id: card_form
                    service: editor.service
                    word: word_input.text
                    card_id: editor.card_id
                    dirty: editor.dirty
                    wide_layout: editor.wide_layout
                    visible: editor.wide_layout || editor.compact_page === 1
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: parent.card_height
                    onEdited: editor.mark_dirty()
                    onTranslation_language_changed: {
                        translation = editor.native_translation(editor.current_sense)
                        editor.mark_dirty()
                    }
                    onTranslate_requested: editor.service.translate(word_input.text, context_input.text)
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: word_input.text.trim().length === 0 ? qsTr("Start with a word above.")
                : card_form.meaning.trim().length === 0 && card_form.translation.trim().length === 0
                    ? (editor.service.translator.configured ? qsTr("Choose a meaning or add a translation to save.")
                        : qsTr("Choose a meaning to save."))
                : editor.card_id.length > 0 && !editor.dirty ? qsTr("✓ In your collection")
                : qsTr("Ready to save · Ctrl+S")
            opacity: 0.6
            font.pixelSize: 12
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        NotebookButton {
            objectName: "save_word"
            text: editor.card_id.length > 0 ? (editor.dirty ? qsTr("Save changes") : qsTr("Saved ✓")) : qsTr("Save word")
            primary: true
            enabled: editor.can_save
            onClicked: editor.save_card()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Save to your vocabulary · Ctrl+S")
        }
    }
}
