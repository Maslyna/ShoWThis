import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property var service
    required property bool wide_layout
    required property string status_text
    property bool context_selection_available: false
    signal context_selection_requested()
    signal settings_requested()
    signal translator_requested(string text, string field)
    property string card_id: ""
    property int selected_sense: -1
    property var draft_sense: ({})
    property var saved_source: ({})
    property bool dirty: false
    property bool loading: false
    property int compact_page: 0
    property bool lookup_attempted: false
    property int pending_sense: -1
    property alias word_prefix: card_form.word_prefix
    readonly property string current_word: word_input.text
    readonly property bool busy: service.dictionary_busy
    readonly property var active_source: draft_sense.source && draft_sense.source.url ? draft_sense.source
        : saved_source.url ? saved_source : service.source
    readonly property var current_sense: selected_sense >= 0 && selected_sense < service.senses.length
        ? service.senses[selected_sense] : ({})
    readonly property bool can_save: !busy && !sense_confirmation.visible && current_word.trim().length > 0
        && (card_form.meaning.trim().length > 0 || card_form.translation.trim().length > 0
            || card_form.context_translation.trim().length > 0)
        && (card_id.length === 0 || dirty)
    spacing: height < 360 ? 8 : 12

    function cancel_sense_replacement() {
        pending_sense = -1
        sense_confirmation.close()
    }
    function mark_dirty() {
        if (!loading) {
            cancel_sense_replacement()
            dirty = true
        }
    }
    function focus_search() {
        workspace_scroll.contentItem.contentY = 0
        word_input.forceActiveFocus()
        word_input.selectAll()
    }
    function apply_context(text: string) {
        workspace_scroll.contentItem.contentY = 0
        context_input.text = text
        context_input.forceActiveFocus()
    }
    function load(action) {
        cancel_sense_replacement()
        workspace_scroll.contentItem.contentY = 0
        loading = true
        const card = action.card || ({})
        const configuration = {}
        if (card.language || action.language) configuration.language = card.language || action.language
        if (card.target_language) configuration.target_language = card.target_language
        const selection = card.dictionary_selection || action.dictionary_selection
        const forms = card.follow_forms !== undefined ? card.follow_forms : action.follow_forms
        if (selection !== undefined) configuration.dictionary_selection = selection
        if (forms !== undefined) configuration.follow_forms = forms
        service.prepare_draft(configuration, !card.id && !action.from_history)
        service.clear()
        card_id = card.id || ""
        saved_source = card.source || ({})
        draft_sense = card.sense || ({})
        selected_sense = -1
        word_input.text = card.id ? card.word : (action.word || "").trim()
            .replace(/^[\s.,!?;:"“”„()]+|[\s.,!?;:"“”„()]+$/g, "")
        context_input.text = card.context || action.context || ""
        card_form.load(card)
        compact_page = card_id.length > 0 ? 1 : 0
        lookup_attempted = false
        loading = false
        dirty = false
        if (action.from_history) {
            lookup_attempted = true
            service.lookup_history(action.history_key)
        }
    }
    function reset_lookup() {
        service.clear()
        selected_sense = -1
        lookup_attempted = false
        mark_dirty()
    }
    function lookup(refresh = false) {
        compact_page = 0
        lookup_attempted = true
        service.lookup(current_word, context_input.text, refresh)
    }
    function native_translation(sense): string {
        return (sense.translations || []).filter(function(item) {
            return item.language.code === editor.service.target_language
        }).map(function(item) { return item.word }).join(", ")
    }
    function select_sense(index: int) {
        if (busy || index < 0 || index >= service.senses.length) return
        if (card_form.has_sense_content) {
            pending_sense = index
            sense_confirmation.open()
        } else apply_sense(index)
    }
    function apply_sense(index: int) {
        if (busy || index < 0 || index >= service.senses.length) return
        service.translator.clear_word()
        selected_sense = index
        draft_sense = current_sense
        if (current_sense.headword) word_input.text = current_sense.headword
        card_form.meaning = current_sense.definition || ""
        card_form.translation = native_translation(current_sense)
        card_form.word_prefix = current_sense.article || ""
        card_form.load_forms(current_sense)
        mark_dirty()
        if (!wide_layout) compact_page = 1
    }
    function search_headword(word: string) {
        word_input.text = word
        reset_lookup()
        lookup()
    }
    function save_card() {
        if (!can_save) return
        if (service.save_word(card_id, current_word, context_input.text,
                card_form.context_translation, card_form.meaning, card_form.translation,
                card_form.notes, draft_sense, card_form.word_prefix, card_form.word_forms())) {
            if (card_id.length === 0) card_id = service.vocabulary[0].id
            dirty = false
        }
    }
    function appended(current: string, text: string): string {
        return current.trim().length === 0 ? text : current + (current.endsWith("\n") ? "" : "\n") + text
    }
    function add_to_card(field: string, text: string, target_language: string): bool {
        const addition = text.trim()
        if (current_word.trim().length === 0 || addition.length === 0
                || ["translation", "context_translation", "meaning", "notes", "context"].indexOf(field) < 0)
            return false
        if ((field === "translation" || field === "context_translation") && target_language.length > 0)
            service.target_language = target_language
        switch (field) {
        case "translation": card_form.translation = appended(card_form.translation, addition); break
        case "context_translation": card_form.context_translation = appended(card_form.context_translation, addition); break
        case "meaning": card_form.meaning = appended(card_form.meaning, addition); break
        case "notes": card_form.notes = appended(card_form.notes, addition); break
        case "context": context_input.text = appended(context_input.text, addition); break
        }
        compact_page = 1
        mark_dirty()
        return true
    }

    Connections {
        target: editor.service
        function onLookup_changed() {
            editor.cancel_sense_replacement()
            editor.selected_sense = -1
        }
        function onPreferences_changed() { editor.mark_dirty() }
    }

    ScrollView {
        id: workspace_scroll
        objectName: "word_workspace_scroll"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 0
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: workspace_scroll.availableWidth
            height: Math.max(implicitHeight, workspace_scroll.availableHeight)
            spacing: editor.height < 360 ? 8 : 12
            NotebookSurface {
                Layout.fillWidth: true
                padding: 10
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TextField {
                            id: word_input
                            objectName: "lookup_word"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            font.pixelSize: 24
                            placeholderText: qsTr("Word")
                            background: null
                            selectByMouse: true
                            enabled: !editor.busy
                            Accessible.name: qsTr("Word to look up")
                            onAccepted: if (lookup_button.enabled) editor.lookup()
                            onTextEdited: editor.reset_lookup()
                        }
                        NotebookLanguagePicker {
                            objectName: "lookup_language"
                            motion_enabled: overlay.motion_enabled
                            options: editor.service.language_options
                            language_code: editor.service.source_language
                            compact: !editor.wide_layout
                            Layout.preferredWidth: editor.wide_layout ? 120 : 64
                            enabled: !editor.busy
                            Accessible.name: qsTr("Dictionary language")
                            onLanguage_selected: function(code) {
                                const previous = editor.service.source_language
                                editor.service.source_language = code
                                if (editor.service.source_language !== previous) editor.reset_lookup()
                            }
                        }
                        NotebookButton {
                            id: lookup_button
                            objectName: "lookup_dictionary"
                            text: editor.busy ? qsTr("Looking…") : qsTr("Look up")
                            primary: true
                            enabled: !editor.busy && editor.current_word.trim().length > 0
                            onClicked: editor.lookup()
                        }
                        NotebookButton {
                            objectName: "translate_word"
                            text: qsTr("Translate")
                            quiet: true
                            enabled: editor.current_word.trim().length > 0
                            Accessible.name: qsTr("Translate word in separate window")
                            onClicked: editor.translator_requested(editor.current_word, "translation")
                        }
                        NotebookButton {
                            objectName: "refresh_dictionary"
                            text: "↻"
                            quiet: true
                            enabled: !editor.busy && editor.current_word.trim().length > 0
                            Accessible.name: qsTr("Refresh dictionary lookup")
                            onClicked: editor.lookup(true)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredHeight: 56
                            clip: true
                            contentWidth: availableWidth
                            TextArea {
                                id: context_input
                                objectName: "word_context"
                                placeholderText: qsTr("Context — type a sentence or select it from the screen")
                                font.pixelSize: 13
                                textFormat: TextEdit.PlainText
                                wrapMode: TextEdit.Wrap
                                selectByMouse: true
                                Accessible.name: qsTr("Context")
                                onTextChanged: editor.mark_dirty()
                            }
                        }
                        NotebookButton {
                            objectName: "select_context"
                            text: qsTr("Select")
                            quiet: true
                            enabled: editor.context_selection_available && !editor.busy
                            Accessible.name: qsTr("Select context from screen")
                            onClicked: editor.context_selection_requested()
                        }
                        NotebookButton {
                            objectName: "translate_context"
                            text: qsTr("Translate")
                            quiet: true
                            enabled: context_input.text.trim().length > 0
                            Accessible.name: qsTr("Translate context in separate window")
                            onClicked: editor.translator_requested(context_input.text, "context_translation")
                        }
                    }
                }
            }
            RowLayout {
                visible: !editor.wide_layout
                Layout.fillWidth: true
                spacing: 4
                NotebookButton {
                    objectName: "compact_dictionary"
                    text: qsTr("Dictionary (%1)").arg(editor.service.senses.length)
                    checked: editor.compact_page === 0
                    quiet: true
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    onClicked: editor.compact_page = 0
                }
                NotebookButton {
                    objectName: "compact_word_card"
                    text: qsTr("My word card")
                    checked: editor.compact_page === 1
                    quiet: true
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    onClicked: editor.compact_page = 1
                }
            }
            SplitView {
                id: columns
                objectName: "word_columns"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: Math.max(220, dictionary_pane.implicitHeight)
                orientation: Qt.Horizontal
                handle: Rectangle {
                    id: divider
                    objectName: "workspace_divider"
                    implicitWidth: 12
                    color: "transparent"
                    Rectangle {
                        anchors.centerIn: parent
                        width: divider.SplitHandle.hovered || divider.SplitHandle.pressed ? 4 : 2
                        height: Math.min(56, parent.height)
                        radius: width / 2
                        color: divider.SplitHandle.pressed ? columns.palette.highlight
                            : Qt.alpha(columns.palette.windowText, divider.SplitHandle.hovered ? 0.45 : 0.2)
                    }
                }
                NotebookDictionary {
                    id: dictionary_pane
                    service: editor.service
                    word: word_input.text
                    selected_sense: editor.selected_sense
                    current_sense: editor.current_sense
                    active_source: editor.active_source
                    lookup_attempted: editor.lookup_attempted
                    wide_layout: editor.wide_layout
                    visible: editor.wide_layout || editor.compact_page === 0
                    SplitView.minimumWidth: editor.wide_layout ? 220 : 0
                    SplitView.preferredWidth: columns.width * 0.48
                    onSense_selected: function(index) { editor.select_sense(index) }
                    onManual_meaning_requested: editor.compact_page = 1
                    onHeadword_requested: function(word) { editor.search_headword(word) }
                    onSources_changed: editor.reset_lookup()
                }
                NotebookCardEditor {
                    id: card_form
                    service: editor.service
                    word: word_input.text
                    card_id: editor.card_id
                    dirty: editor.dirty
                    wide_layout: editor.wide_layout
                    visible: editor.wide_layout || editor.compact_page === 1
                    SplitView.minimumWidth: editor.wide_layout ? 260 : 0
                    SplitView.fillWidth: true
                    onEdited: editor.mark_dirty()
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 4
            Label {
                objectName: "word_save_status"
                text: editor.current_word.trim().length === 0 ? qsTr("Enter a word to save.")
                    : !card_form.meaning.trim().length && !card_form.translation.trim().length
                        && !card_form.context_translation.trim().length ? qsTr("Add a meaning or translation to save.")
                    : editor.busy ? qsTr("Wait for the lookup to finish.")
                    : editor.card_id.length > 0 && !editor.dirty ? qsTr("Saved")
                    : editor.dirty ? qsTr("Unsaved changes") : qsTr("New card")
                opacity: 0.65
                font.pixelSize: 12
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                wrapMode: Text.WordWrap
            }
            Label {
                text: editor.status_text
                textFormat: Text.PlainText
                font.pixelSize: editor.service.message.length > 0 ? 12 : 10
                opacity: editor.service.message.length > 0 ? 0.8 : 0.55
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                ToolTip.visible: feedback_hover.hovered
                ToolTip.text: text
                HoverHandler { id: feedback_hover }
            }
        }
        NotebookButton {
            objectName: "save_word"
            text: editor.card_id.length > 0 ? qsTr("Save changes") : qsTr("Save word")
            primary: true
            enabled: editor.can_save
            onClicked: editor.save_card()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Save card · Ctrl+S")
        }
    }
    NotebookConfirm {
        id: sense_confirmation
        objectName: "replace_card_sense"
        title: qsTr("Replace card fields?")
        description: qsTr("This meaning will replace your meaning, word translation, article and word forms. Your notes, context and context translation will stay.")
        confirm_text: qsTr("Replace fields")
        onAccepted: {
            const index = editor.pending_sense
            editor.pending_sense = -1
            editor.apply_sense(index)
        }
        onRejected: editor.pending_sense = -1
    }
}
