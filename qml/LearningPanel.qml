import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: panel
    required property var service
    signal export_requested(var ids)
    property var export_ids: []
    property var pending_action: ({})
    property alias card_id: word_editor.card_id
    property alias dirty: word_editor.dirty
    property alias compact_page: word_editor.compact_page
    property alias word_prefix: word_editor.word_prefix
    readonly property bool wide_layout: width >= 880
    readonly property var page_names: [qsTr("Learn a word"), qsTr("Saved words"), qsTr("Recent lookups"), qsTr("Settings")]
    padding: 0
    visible: false
    focus: visible
    objectName: "learning_panel"

    // Use the desktop palette explicitly, independent of the Controls style.
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

    function open_word(word: string, context: string) { request_card({word: word, context: context}) }
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
        } else apply_action(action)
    }
    function apply_action(action) {
        word_editor.load(action)
        navigation.current_index = 0
    }
    function begin_export(ids) {
        export_ids = ids.slice()
        export_requested(export_ids)
    }

    Shortcut {
        sequence: "Ctrl+S"
        enabled: panel.visible && navigation.current_index === 0 && word_editor.can_save && !discard_confirmation.visible
        onActivated: word_editor.save_card()
    }
    Shortcut {
        sequence: "Ctrl+F"
        enabled: panel.visible && !discard_confirmation.visible
        onActivated: {
            const page = navigation.current_index === 1 ? vocabulary_page
                : navigation.current_index === 2 ? history_page : word_editor
            page.focus_search()
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
                    model: panel.page_names.slice(0, 3)
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
                    ToolTip.text: qsTr("Back to screen · Esc\nYour draft stays open")
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
                NotebookWordEditor {
                    id: word_editor
                    service: panel.service
                    wide_layout: panel.wide_layout
                }
                NotebookVocabulary {
                    id: vocabulary_page
                    service: panel.service
                    onOpen_requested: function(card) { panel.edit_card(card) }
                    onExport_requested: function(ids) { panel.begin_export(ids) }
                    onDelete_requested: function(card) {
                        delete_confirmation.card_id = card.id
                        delete_confirmation.description = qsTr("“%1” and its saved notes will be removed from your collection.").arg(card.word)
                        delete_confirmation.open()
                    }
                }
                NotebookHistory {
                    id: history_page
                    service: panel.service
                    onOpen_requested: function(entry) {
                        panel.request_card({word: entry.word, language: entry.language, context: entry.context,
                            dictionary_selection: entry.dictionary_selection,
                            follow_forms: entry.follow_forms, from_history: true})
                    }
                    onSettings_requested: navigation.current_index = 3
                }
                NotebookSettings { service: panel.service }
            }
            Label {
                text: panel.service.message || qsTr("Lookup sends only the word to the selected dictionaries. Your notebook is stored locally.")
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
}
