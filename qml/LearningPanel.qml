import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs

Pane {
    id: panel
    required property var service
    property string card_id: ""
    property int selected_sense: -1
    property var saved_source: ({})
    property var selected_cards: []
    property var export_ids: []
    property var pending_action: ({})
    property bool dirty: false
    property bool loading_card: false
    property int compact_page: 0
    property bool context_expanded: false
    property bool lookup_attempted: false
    property string word_prefix: ""
    readonly property bool wide_layout: width >= 880
    readonly property var active_source: service.source.url ? service.source : saved_source
    readonly property var current_sense: selected_sense >= 0 && selected_sense < service.senses.length
        ? service.senses[selected_sense] : ({})
    readonly property bool busy: service.dictionary_busy || service.translation_busy
    readonly property var page_names: [qsTr("Learn a word"), qsTr("Saved words"), qsTr("Recent lookups"), qsTr("Settings")]

    padding: 0
    visible: false
    focus: visible
    objectName: "learning_panel"

    // Use the desktop palette explicitly: a Controls style may otherwise choose
    // its own initial light palette when a new QML engine is created.
    SystemPalette { id: system_palette; colorGroup: SystemPalette.Active }
    SystemPalette { id: disabled_palette; colorGroup: SystemPalette.Disabled }
    palette.window: system_palette.window
    palette.windowText: system_palette.windowText
    palette.base: system_palette.base
    palette.alternateBase: system_palette.alternateBase
    palette.text: system_palette.text
    palette.button: system_palette.button
    palette.buttonText: system_palette.buttonText
    palette.highlight: system_palette.highlight
    palette.highlightedText: system_palette.highlightedText
    palette.mid: system_palette.mid
    palette.dark: system_palette.dark
    palette.light: system_palette.light
    palette.shadow: system_palette.shadow
    palette.placeholderText: Qt.alpha(system_palette.text, 0.5)
    palette.disabled.text: disabled_palette.text
    palette.disabled.buttonText: disabled_palette.buttonText

    function open_word(word: string, context: string) {
        request_card({word: word, context: context})
    }
    function open_vocabulary() {
        navigation.current_index = 1
        visible = true
    }
    function edit_card(card) { request_card({card: card}) }
    function request_card(action) {
        visible = true
        if (dirty) {
            pending_action = action
            discard_confirmation.open()
        } else {
            apply_action(action)
        }
    }
    function apply_action(action) {
        loading_card = true
        const card = action.card || ({})
        if (card.language || action.language)
            service.source_language = card.language || action.language
        if (card.target_language)
            service.target_language = card.target_language
        service.clear()
        card_id = card.id || ""
        saved_source = card.source || ({})
        selected_sense = -1
        word_input.text = (card.word || action.word || "").trim()
            .replace(/^[\s.,!?;:"“”„()]+|[\s.,!?;:"“”„()]+$/g, "")
        context_input.text = card.context || action.context || ""
        meaning_input.text = card.meaning || ""
        translation_input.text = card.translation || ""
        notes_input.text = card.notes || ""
        word_prefix = card.article || ""
        notes_toggle.checked = notes_input.text.length > 0
        compact_page = card_id.length > 0 ? 1 : 0
        context_expanded = false
        lookup_attempted = false
        word_scroll.contentItem.contentY = 0
        editor_scroll.contentItem.contentY = 0
        navigation.current_index = 0
        loading_card = false
        dirty = false
        if (action.from_history) {
            lookup_attempted = true
            service.lookup(word_input.text, context_input.text)
        }
    }
    function mark_dirty() { if (!loading_card) dirty = true }
    function native_translation(sense): string {
        let words = []
        for (const item of (sense.translations || [])) {
            if (item.language.code === service.target_language)
                words.push(item.word)
        }
        return words.join(", ")
    }
    function select_sense(index: int) {
        selected_sense = index
        meaning_input.text = current_sense.definition || ""
        translation_input.text = native_translation(current_sense)
        word_prefix = current_sense.article || ""
        mark_dirty()
        if (!wide_layout) compact_page = 1
    }
    function toggle_selection(card_id: string, checked: bool) {
        let ids = selected_cards.slice()
        const index = ids.indexOf(card_id)
        if (checked && index < 0) ids.push(card_id)
        else if (!checked && index >= 0) ids.splice(index, 1)
        selected_cards = ids
    }
    function save_card() {
        if (!save_button.enabled)
            return
        if (service.save_word(card_id, word_input.text, context_input.text,
                meaning_input.text, translation_input.text, notes_input.text, selected_sense, word_prefix)) {
            if (card_id.length === 0) card_id = service.vocabulary[0].id
            dirty = false
        }
    }
    function begin_export(ids) {
        export_ids = ids.slice()
        export_dialog.open()
    }

    Connections {
        target: panel.service
        function onLookup_changed() { panel.selected_sense = -1 }
        function onTranslation_changed() {
            if (panel.service.translation.length > 0) {
                translation_input.text = panel.service.translation
                panel.mark_dirty()
            }
        }
        function onVocabulary_changed() {
            panel.selected_cards = panel.selected_cards.filter(function(id) {
                return panel.service.vocabulary.some(function(card) { return card.id === id })
            })
        }
    }
    Shortcut {
        sequence: "Ctrl+S"
        enabled: panel.visible && navigation.current_index === 0 && save_button.enabled && !discard_confirmation.visible
        onActivated: panel.save_card()
    }
    Shortcut {
        sequence: "Ctrl+F"
        enabled: panel.visible && !discard_confirmation.visible
        onActivated: {
            const field = navigation.current_index === 1 ? vocabulary_search
                : navigation.current_index === 2 ? history_search : word_input
            field.forceActiveFocus()
            field.selectAll()
        }
    }

    background: Rectangle {
        radius: 12
        color: panel.palette.window
        border.color: Qt.alpha(panel.palette.windowText, 0.2)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // A persistent navigation rail replaces the old form's crowded tabs.
        Rectangle {
            visible: panel.wide_layout
            Layout.preferredWidth: 168
            Layout.fillHeight: true
            color: Qt.alpha(panel.palette.windowText, 0.035)
            radius: 11
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8
                Label { text: "ShoWThis"; font.pixelSize: 21; font.weight: Font.DemiBold; Layout.topMargin: 8 }
                Label { text: qsTr("LANGUAGE NOTEBOOK"); font.pixelSize: 9; font.letterSpacing: 1; opacity: 0.5; Layout.bottomMargin: 24 }
                Repeater {
                    model: [qsTr("Learn a word"), qsTr("Saved words"), qsTr("Recent lookups")]
                    NotebookButton {
                        required property int index
                        required property string modelData
                        objectName: "nav_page_" + index
                        text: modelData
                        align_left: true
                        quiet: true
                        checked: navigation.current_index === index
                        Layout.fillWidth: true
                        onClicked: navigation.current_index = index
                    }
                }
                Label {
                    text: qsTr("%1 words collected").arg(panel.service.vocabulary.length)
                    opacity: 0.5
                    font.pixelSize: 11
                    Layout.leftMargin: 10
                    Layout.topMargin: 8
                }
                Item { Layout.fillHeight: true }
                NotebookButton {
                    text: qsTr("Settings")
                    objectName: "nav_page_3"
                    align_left: true
                    quiet: true
                    checked: navigation.current_index === 3
                    Layout.fillWidth: true
                    onClicked: navigation.current_index = 3
                }
                Label { text: qsTr("Stored on this computer"); font.pixelSize: 10; opacity: 0.5; Layout.leftMargin: 10 }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: panel.wide_layout ? 22 : 14
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Label {
                        text: panel.page_names[navigation.current_index]
                        font.pixelSize: panel.wide_layout ? 25 : 21
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        visible: panel.wide_layout
                        text: [qsTr("Understand it. Make it yours."), qsTr("Your words, with the context that made them memorable."),
                            qsTr("Pick up where you left off."), qsTr("Make the notebook work for you.")][navigation.current_index]
                        opacity: 0.6
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                NotebookButton {
                    text: qsTr("+ New word")
                    objectName: "new_word"
                    visible: navigation.current_index !== 3
                    onClicked: panel.open_word("", "")
                }
                NotebookButton {
                    text: "✕"
                    quiet: true
                    objectName: "hide_notebook"
                    Accessible.name: qsTr("Back to screen")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Back to screen\nYour draft stays open")
                    onClicked: panel.visible = false
                }
            }

            Item {
                id: navigation
                objectName: "notebook_tabs"
                property int current_index: 0
                Layout.fillWidth: true
                Layout.preferredHeight: panel.wide_layout ? 0 : 38
                visible: !panel.wide_layout
                RowLayout {
                    anchors.fill: parent
                    spacing: 4
                    Repeater {
                        model: [qsTr("Word"), qsTr("Saved"), qsTr("History"), qsTr("Settings")]
                        NotebookButton {
                            required property int index
                            required property string modelData
                            text: modelData
                            quiet: true
                            checked: navigation.current_index === index
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            onClicked: navigation.current_index = index
                        }
                    }
                }
            }

            Label {
                visible: panel.service.storage_error.length > 0
                text: panel.service.storage_error
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }

            StackLayout {
                currentIndex: navigation.current_index
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    spacing: 10
                    ScrollView {
                        id: word_scroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.vertical.policy: panel.wide_layout ? ScrollBar.AsNeeded : ScrollBar.AlwaysOn
                        ColumnLayout {
                            width: parent.width
                            spacing: 12
                            NotebookSurface {
                                id: search_surface
                                Layout.fillWidth: true
                                padding: 12
                                RowLayout {
                                    anchors.fill: parent
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
                                        enabled: !panel.busy
                                        Accessible.name: qsTr("Word to look up")
                                        onAccepted: if (lookup_button.enabled) lookup_button.clicked()
                                        onTextEdited: {
                                            panel.lookup_attempted = false
                                            panel.service.clear()
                                            panel.saved_source = ({})
                                            panel.word_prefix = ""
                                            meaning_input.clear()
                                            translation_input.clear()
                                            panel.mark_dirty()
                                        }
                                    }
                                    NotebookLanguagePicker {
                                        objectName: "lookup_language"
                                        options: panel.service.language_options
                                        language_code: panel.service.source_language
                                        compact: !panel.wide_layout
                                        Layout.preferredWidth: panel.wide_layout ? 120 : 64
                                        enabled: !panel.busy
                                        Accessible.name: qsTr("Word language, separate from OCR")
                                        onLanguage_selected: function(code) {
                                            const previous = panel.service.source_language
                                            panel.service.source_language = code
                                            if (panel.service.source_language === previous) return
                                            panel.lookup_attempted = false
                                            panel.saved_source = ({})
                                            panel.word_prefix = ""
                                            meaning_input.clear()
                                            translation_input.clear()
                                            panel.mark_dirty()
                                        }
                                    }
                                    NotebookButton {
                                        id: lookup_button
                                        objectName: "lookup_dictionary"
                                        text: panel.service.dictionary_busy ? qsTr("Looking…") : qsTr("Look up")
                                        primary: true
                                        enabled: !panel.busy && word_input.text.trim().length > 0
                                        onClicked: {
                                            panel.compact_page = 0
                                            panel.lookup_attempted = true
                                            panel.service.lookup(word_input.text, context_input.text)
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Send only this word to the dictionary · Enter")
                                    }
                                }
                            }

                            RowLayout {
                                id: context_row
                                Layout.fillWidth: true
                                spacing: 12
                                Label { text: panel.wide_layout ? qsTr("IN CONTEXT") : qsTr("Context"); font.pixelSize: 10; font.bold: true; opacity: 0.5 }
                                Label {
                                    visible: !panel.wide_layout && !panel.context_expanded
                                    text: context_input.text || qsTr("Add the original sentence")
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    opacity: 0.65
                                    Layout.fillWidth: true
                                }
                                ScrollView {
                                    visible: panel.wide_layout || panel.context_expanded
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
                                        enabled: !panel.service.translation_busy
                                        Accessible.name: qsTr("Original sentence")
                                        onTextChanged: panel.mark_dirty()
                                    }
                                }
                                NotebookButton {
                                    visible: !panel.wide_layout
                                    text: panel.context_expanded ? qsTr("Done") : qsTr("Edit")
                                    quiet: true
                                    onClicked: panel.context_expanded = !panel.context_expanded
                                }
                            }

                            RowLayout {
                                visible: !panel.wide_layout
                                Layout.fillWidth: true
                                NotebookButton {
                                    objectName: "compact_dictionary"
                                    text: qsTr("Dictionary (%1)").arg(panel.service.senses.length)
                                    checked: panel.compact_page === 0
                                    quiet: true
                                    Layout.fillWidth: true
                                    onClicked: panel.compact_page = 0
                                }
                                NotebookButton {
                                    objectName: "compact_word_card"
                                    text: qsTr("My word card")
                                    checked: panel.compact_page === 1
                                    quiet: true
                                    Layout.fillWidth: true
                                    onClicked: panel.compact_page = 1
                                }
                            }

                            GridLayout {
                                id: word_columns
                                objectName: "word_columns"
                                columns: panel.wide_layout ? 2 : 1
                                columnSpacing: 12
                                rowSpacing: 12
                                Layout.fillWidth: true
                                readonly property real card_height: panel.wide_layout
                                    ? Math.max(240, word_scroll.availableHeight - search_surface.height - context_row.height - 24)
                                    : Math.max(190, word_scroll.availableHeight - search_surface.height - context_row.height - 74)

                                NotebookSurface {
                                    objectName: "dictionary_pane"
                                    visible: panel.wide_layout || panel.compact_page === 0
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    Layout.preferredHeight: word_columns.card_height
                                    ColumnLayout {
                                        anchors.fill: parent
                                        spacing: 10
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Label { text: qsTr("Dictionary"); font.pixelSize: 17; font.bold: true; Layout.fillWidth: true }
                                            Label { text: qsTr("English definitions"); font.pixelSize: 10; opacity: 0.5 }
                                        }
                                        Item {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            NotebookEmpty {
                                                anchors.fill: parent
                                                visible: !panel.service.dictionary_busy && sense_list.count === 0
                                                heading: panel.lookup_attempted ? qsTr("No meaning found yet") : qsTr("What does it mean?")
                                                description: panel.lookup_attempted && panel.service.message.length > 0
                                                    ? panel.service.message : qsTr("Look up a word, then choose the meaning that fits your sentence.")
                                                action_text: panel.wide_layout ? "" : qsTr("Write my own meaning")
                                                onAction_requested: panel.compact_page = 1
                                            }
                                            BusyIndicator {
                                                anchors.centerIn: parent
                                                running: panel.service.dictionary_busy
                                                visible: running
                                            }
                                            ListView {
                                                id: sense_list
                                                objectName: "dictionary_senses"
                                                anchors.fill: parent
                                                visible: count > 0
                                                clip: true
                                                spacing: 6
                                                model: panel.service.senses
                                                ScrollBar.vertical: ScrollBar {}
                                                delegate: ItemDelegate {
                                                    id: sense_row
                                                    required property var modelData
                                                    required property int index
                                                    objectName: "dictionary_sense_" + index
                                                    width: sense_list.width
                                                    padding: 12
                                                    checked: panel.selected_sense === index
                                                    background: Rectangle {
                                                        radius: 7
                                                        color: sense_row.checked ? Qt.alpha(panel.palette.highlight, 0.12)
                                                            : sense_row.hovered ? Qt.alpha(panel.palette.windowText, 0.05) : "transparent"
                                                        border.color: sense_row.checked ? panel.palette.highlight : Qt.alpha(panel.palette.windowText, 0.09)
                                                    }
                                                    contentItem: ColumnLayout {
                                                        spacing: 6
                                                        Label {
                                                            text: (sense_row.index + 1) + ".  " + sense_row.modelData.definition
                                                            textFormat: Text.PlainText
                                                            wrapMode: Text.WordWrap
                                                            Layout.fillWidth: true
                                                        }
                                                        Label {
                                                            visible: (sense_row.modelData.articles || []).length > 0
                                                            text: (sense_row.modelData.articles || []).join(" / ") + " " + word_input.text
                                                            color: panel.palette.highlight
                                                            font.weight: Font.DemiBold
                                                            textFormat: Text.PlainText
                                                            wrapMode: Text.WordWrap
                                                            Layout.fillWidth: true
                                                        }
                                                        Label {
                                                            text: [sense_row.modelData.part_of_speech, (sense_row.modelData.tags || []).join(", ")]
                                                                .filter(function(value) { return value.length > 0 }).join(" · ")
                                                            opacity: 0.55
                                                            font.pixelSize: 11
                                                            textFormat: Text.PlainText
                                                            wrapMode: Text.WordWrap
                                                            Layout.fillWidth: true
                                                        }
                                                    }
                                                    onClicked: panel.select_sense(index)
                                                }
                                            }
                                        }
                                        Label {
                                            visible: panel.selected_sense >= 0
                                            text: (panel.current_sense.pronunciation || "")
                                                + ((panel.current_sense.examples || []).length > 0 ? "\n" + panel.current_sense.examples[0] : "")
                                            textFormat: Text.PlainText
                                            wrapMode: Text.WordWrap
                                            maximumLineCount: 3
                                            elide: Text.ElideRight
                                            opacity: 0.65
                                            Layout.fillWidth: true
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            NotebookButton {
                                                text: "FreeDictionaryAPI ↗"
                                                quiet: true
                                                font.pixelSize: 10
                                                onClicked: Qt.openUrlExternally("https://freedictionaryapi.com/")
                                            }
                                            Item { Layout.fillWidth: true }
                                            NotebookButton {
                                                visible: !!panel.active_source.url
                                                text: "Wiktionary · CC BY-SA ↗"
                                                quiet: true
                                                font.pixelSize: 10
                                                onClicked: {
                                                    const url = panel.active_source.url || ""
                                                    if (url.startsWith("https://en.wiktionary.org/")) Qt.openUrlExternally(url)
                                                }
                                            }
                                        }
                                    }
                                }

                                NotebookSurface {
                                    objectName: "personal_card_pane"
                                    visible: panel.wide_layout || panel.compact_page === 1
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    Layout.preferredHeight: word_columns.card_height
                                    ColumnLayout {
                                        anchors.fill: parent
                                        spacing: 10
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Label { text: qsTr("Your word card"); font.pixelSize: 17; font.bold: true; Layout.fillWidth: true }
                                            Label {
                                                text: panel.dirty ? qsTr("Unsaved") : panel.card_id.length > 0 ? qsTr("Saved") : qsTr("New")
                                                font.pixelSize: 11
                                                opacity: 0.5
                                            }
                                        }
                                        ScrollView {
                                            id: editor_scroll
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
                                                        objectName: "word_prefix"
                                                        text: panel.word_prefix
                                                        placeholderText: qsTr("Optional")
                                                        maximumLength: 80
                                                        Layout.fillWidth: true
                                                        Layout.minimumWidth: 0
                                                        Accessible.name: qsTr("Optional article or word prefix")
                                                        onTextEdited: {
                                                            panel.word_prefix = text
                                                            panel.mark_dirty()
                                                        }
                                                    }
                                                }
                                                Label {
                                                    objectName: "word_headword"
                                                    visible: panel.word_prefix.length > 0
                                                    text: {
                                                        const prefix = panel.word_prefix.trim()
                                                        const joined = /['’\-]$/.test(prefix) ? prefix : prefix + " "
                                                        return word_input.text.toLowerCase().startsWith(joined.toLowerCase())
                                                            ? word_input.text : joined + word_input.text
                                                    }
                                                    textFormat: Text.PlainText
                                                    font.pixelSize: 22
                                                    font.weight: Font.DemiBold
                                                    color: panel.palette.highlight
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
                                                    placeholderText: panel.wide_layout ? qsTr("Choose a meaning on the left, or write your own.") : qsTr("Your chosen meaning, in your own words…")
                                                    Accessible.name: qsTr("Chosen meaning")
                                                    onTextChanged: panel.mark_dirty()
                                                }
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    Label { text: qsTr("Translation"); font.bold: true; font.pixelSize: 12; Layout.fillWidth: true }
                                                    NotebookLanguagePicker {
                                                        objectName: "target_language"
                                                        options: panel.service.language_options
                                                        language_code: panel.service.target_language
                                                        Layout.preferredWidth: 125
                                                        enabled: !panel.busy
                                                        Accessible.name: qsTr("Translation language")
                                                        onLanguage_selected: function(code) {
                                                            const previous = panel.service.target_language
                                                            panel.service.target_language = code
                                                            if (panel.service.target_language === previous) return
                                                            translation_input.text = panel.native_translation(panel.current_sense)
                                                            panel.mark_dirty()
                                                        }
                                                    }
                                                }
                                                TextArea {
                                                    id: translation_input
                                                    objectName: "word_translation"
                                                    Layout.fillWidth: true
                                                    font.pixelSize: 19
                                                    wrapMode: TextEdit.Wrap
                                                    textFormat: TextEdit.PlainText
                                                    selectByMouse: true
                                                    enabled: !panel.service.translation_busy
                                                    placeholderText: qsTr("A translation in your language…")
                                                    Accessible.name: qsTr("Word translation")
                                                    onTextChanged: panel.mark_dirty()
                                                }
                                                NotebookButton {
                                                    text: panel.service.has_translation_key
                                                        ? (panel.service.translation_busy ? qsTr("Translating…") : qsTr("Translate with context")) : qsTr("Set up DeepL translation ↗")
                                                    quiet: true
                                                    Layout.fillWidth: true
                                                    enabled: !panel.busy && (word_input.text.trim().length > 0 || !panel.service.has_translation_key)
                                                    onClicked: {
                                                        if (!panel.service.has_translation_key) navigation.current_index = 3
                                                        else panel.service.translate(word_input.text, context_input.text)
                                                    }
                                                    ToolTip.visible: hovered
                                                    ToolTip.text: qsTr("Only clicking Translate sends your word and original sentence to DeepL")
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
                                                    onTextChanged: panel.mark_dirty()
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: word_input.text.trim().length === 0 ? qsTr("Start with a word above.")
                                : meaning_input.text.trim().length === 0 && translation_input.text.trim().length === 0
                                    ? qsTr("Choose a meaning or add a translation to save.")
                                : panel.card_id.length > 0 && !panel.dirty ? qsTr("✓ In your collection")
                                : qsTr("Ready to save · Ctrl+S")
                            opacity: 0.6
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        NotebookButton {
                            id: save_button
                            objectName: "save_word"
                            text: panel.card_id.length > 0 ? (panel.dirty ? qsTr("Save changes") : qsTr("Saved ✓")) : qsTr("Save word")
                            primary: true
                            enabled: !panel.busy && word_input.text.trim().length > 0
                                && (meaning_input.text.trim().length > 0 || translation_input.text.trim().length > 0)
                                && (panel.card_id.length === 0 || panel.dirty)
                            onClicked: panel.save_card()
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Save to your vocabulary · Ctrl+S")
                        }
                    }
                }

                ColumnLayout {
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: vocabulary_search
                            objectName: "vocabulary_search"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            placeholderText: qsTr("Search words, meanings, or sentences…")
                            selectByMouse: true
                            Accessible.name: qsTr("Search saved vocabulary")
                        }
                        NotebookButton {
                            objectName: "export_anki"
                            text: panel.selected_cards.length > 0 ? qsTr("Export selected (%1)…").arg(panel.selected_cards.length) : qsTr("Export to Anki…")
                            enabled: panel.service.vocabulary.length > 0
                            onClicked: panel.begin_export(panel.selected_cards)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: panel.selected_cards.length > 0 ? qsTr("%1 selected for export").arg(panel.selected_cards.length)
                                : qsTr("%1 saved words").arg(vocabulary_list.count)
                            opacity: 0.6
                            Layout.fillWidth: true
                        }
                        NotebookButton {
                            quiet: true
                            text: panel.selected_cards.length > 0 ? qsTr("Clear selection") : qsTr("Select shown")
                            enabled: vocabulary_list.count > 0 || panel.selected_cards.length > 0
                            onClicked: panel.selected_cards = panel.selected_cards.length > 0 ? []
                                : vocabulary_list.model.map(function(card) { return card.id })
                        }
                    }
                    NotebookSurface {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        padding: 6
                        ListView {
                            id: vocabulary_list
                            objectName: "vocabulary_list"
                            anchors.fill: parent
                            clip: true
                            spacing: 2
                            model: panel.service.vocabulary.filter(function(card) {
                                return [card.word, card.meaning, card.translation, card.context, card.notes]
                                    .join(" ").toLowerCase().includes(vocabulary_search.text.toLowerCase())
                            })
                            ScrollBar.vertical: ScrollBar {}
                            NotebookEmpty {
                                anchors.fill: parent
                                visible: vocabulary_list.count === 0
                                heading: panel.service.vocabulary.length > 0 ? qsTr("No matching words") : qsTr("Start your collection")
                                description: panel.service.vocabulary.length > 0 ? qsTr("Try a different word or part of a sentence.")
                                    : qsTr("Save words you discover on screen, with a meaning and a sentence to remember them by.")
                                action_text: panel.service.vocabulary.length > 0 ? qsTr("Clear search") : qsTr("Add your first word")
                                onAction_requested: {
                                    if (panel.service.vocabulary.length > 0) vocabulary_search.clear()
                                    else panel.open_word("", "")
                                }
                            }
                            delegate: NotebookWordRow {
                                required property var modelData
                                width: vocabulary_list.width
                                entry: modelData
                                selected: panel.selected_cards.indexOf(entry.id) >= 0
                                onOpen_requested: panel.edit_card(entry)
                                onSelection_toggled: function(checked) { panel.toggle_selection(entry.id, checked) }
                                onExport_requested: panel.begin_export([entry.id])
                                onDelete_requested: {
                                    delete_confirmation.card_id = entry.id
                                    delete_confirmation.description = qsTr("“%1” and its saved notes will be removed from your collection.").arg(entry.word)
                                    delete_confirmation.open()
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: history_search
                            objectName: "history_search"
                            placeholderText: qsTr("Find a word you looked up…")
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            selectByMouse: true
                            Accessible.name: qsTr("Search recent lookups")
                        }
                        NotebookButton { text: qsTr("History settings"); onClicked: navigation.current_index = 3 }
                    }
                    Label {
                        text: panel.service.history_limit === 0 ? qsTr("History is paused. You can enable it in Settings.")
                            : qsTr("%1 of %2 recent words · available offline · newest first")
                                .arg(panel.service.dictionary_history.length).arg(panel.service.history_limit)
                        opacity: 0.6
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    NotebookSurface {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        padding: 6
                        ListView {
                            id: history_list
                            objectName: "dictionary_history"
                            anchors.fill: parent
                            clip: true
                            spacing: 2
                            model: panel.service.dictionary_history.filter(function(item) {
                                return [item.word, item.context, item.summary || ""].join(" ")
                                    .toLowerCase().includes(history_search.text.toLowerCase())
                            })
                            ScrollBar.vertical: ScrollBar {}
                            NotebookEmpty {
                                anchors.fill: parent
                                visible: history_list.count === 0
                                heading: history_search.text.length > 0 ? qsTr("No matching lookups") : qsTr("Your discoveries will appear here")
                                description: qsTr("Successful dictionary lookups are kept automatically. Save a word card when you want to keep your own translation and notes.")
                            }
                            delegate: NotebookWordRow {
                                required property var modelData
                                width: history_list.width
                                entry: modelData
                                is_history: true
                                onOpen_requested: panel.request_card({word: entry.word, language: entry.language,
                                    context: entry.context, from_history: true})
                            }
                        }
                    }
                }

                NotebookSettings { service: panel.service }
            }

            Label {
                text: panel.service.message || qsTr("Dictionary lookup sends only the word. Your notebook is stored locally.")
                textFormat: Text.PlainText
                font.pixelSize: panel.service.message.length > 0 ? 12 : 10
                opacity: panel.service.message.length > 0 ? 0.8 : 0.55
                elide: Text.ElideRight
                Layout.fillWidth: true
                ToolTip.visible: status_hover.hovered
                ToolTip.text: text
                HoverHandler { id: status_hover }
            }
        }
    }

    NotebookConfirm {
        id: discard_confirmation
        palette: panel.palette
        objectName: "discard_draft"
        title: qsTr("You have unsaved changes")
        description: qsTr("Opening another word will discard this draft. Cancel to keep editing, or save your word first.")
        confirm_text: qsTr("Discard changes")
        onAccepted: panel.apply_action(panel.pending_action)
    }
    NotebookConfirm {
        id: delete_confirmation
        palette: panel.palette
        property string card_id: ""
        title: qsTr("Delete saved word?")
        confirm_text: qsTr("Delete word")
        onAccepted: {
            if (panel.service.remove_word(card_id) && panel.card_id === card_id) {
                panel.dirty = false
                panel.open_word("", "")
            }
        }
    }
    NativeDialogs.FileDialog {
        id: export_dialog
        title: qsTr("Export vocabulary for Anki")
        fileMode: NativeDialogs.FileDialog.SaveFile
        nameFilters: [qsTr("Anki text files (*.txt *.tsv)")]
        defaultSuffix: "txt"
        onAccepted: panel.service.export_anki(selectedFile, panel.export_ids)
    }
}
