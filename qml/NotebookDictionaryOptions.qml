import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: options
    required property var service
    signal changed()
    Layout.fillWidth: true
    spacing: 6
    property var draft: []
    property bool draft_forms: true
    SystemPalette { id: system_palette }

    function summary(): string {
        return service.dictionary_selection.map(function(selection) {
            const source = service.dictionary_sources.find(function(item) { return item.id === selection.provider })
            if (!source) return ""
            return source.name + " · " + selection.languages.map(function(code) { return code.toUpperCase() }).join(" / ")
                + (selection.fallback ? qsTr(" (fallback)") : "")
        }).join("   ·   ") || qsTr("No dictionaries selected")
    }
    function update(index, field, value) {
        const next = JSON.parse(JSON.stringify(draft))
        next[index][field] = value
        draft = next
    }
    function selection() {
        return draft.filter(function(item) { return item.enabled }).map(function(item) {
            return {provider: item.provider, languages: item.languages, fallback: item.fallback}
        })
    }
    readonly property bool valid: draft.some(function(item) { return item.enabled })
        && draft.every(function(item) { return !item.enabled || item.languages.length > 0 })

    RowLayout {
        Layout.fillWidth: true
        Label {
            objectName: "dictionary_source_summary"
            text: options.summary()
            textFormat: Text.PlainText
            font.pixelSize: 11
            opacity: 0.7
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            ToolTip.visible: summary_hover.hovered
            ToolTip.text: text
            HoverHandler { id: summary_hover }
        }
        NotebookButton {
            objectName: "choose_dictionary_sources"
            text: qsTr("Sources & languages…")
            quiet: true
            font.pixelSize: 12
            onClicked: picker.open()
        }
    }

    Dialog {
        id: picker
        objectName: "dictionary_source_picker"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, parent ? parent.width - 32 : 620)
        height: Math.min(680, parent ? parent.height - 32 : 680)
        modal: true
        focus: true
        padding: 20
        palette.window: system_palette.window
        palette.base: system_palette.base
        palette.windowText: system_palette.windowText
        palette.text: system_palette.text
        palette.button: system_palette.button
        palette.buttonText: system_palette.buttonText
        palette.highlight: system_palette.highlight
        palette.highlightedText: system_palette.highlightedText
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: picker.palette.window
            radius: 12
            border.color: Qt.alpha(picker.palette.windowText, 0.18)
        }
        onAboutToShow: {
            options.draft = options.service.dictionary_sources.map(function(source) {
                const saved = options.service.dictionary_selection.find(function(item) { return item.provider === source.id })
                return {provider: source.id, enabled: !!saved, fallback: saved ? saved.fallback : false,
                    languages: saved ? Array.from(saved.languages) : [source.default_language]}
            })
            options.draft_forms = options.service.follow_forms
        }
        header: ColumnLayout {
            spacing: 6
            Label {
                text: qsTr("Dictionary sources")
                font.pixelSize: 22
                font.bold: true
                Layout.leftMargin: 20
                Layout.topMargin: 20
            }
            Label {
                text: qsTr("Choose sources and the languages you want to read definitions in.")
                wrapMode: Text.WordWrap
                opacity: 0.7
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
            }
        }
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 12
                Repeater {
                    model: options.service.dictionary_sources
                    NotebookDictionarySource {
                        required property var modelData
                        required property int index
                        source: modelData
                        source_enabled: options.draft[index] ? options.draft[index].enabled : false
                        languages: options.draft[index] ? options.draft[index].languages : []
                        fallback: options.draft[index] ? options.draft[index].fallback : false
                        onEnabled_requested: function(value) { options.update(index, "enabled", value) }
                        onLanguages_requested: function(value) { options.update(index, "languages", value) }
                        onFallback_requested: function(value) { options.update(index, "fallback", value) }
                    }
                }
                CheckBox {
                    objectName: "follow_dictionary_forms"
                    text: qsTr("Look up base forms too")
                    checked: options.draft_forms
                    onToggled: options.draft_forms = checked
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Selected sources search together. Fallback sources run only when no full meanings are found.\nDefinition languages do not change the word language or translator target.")
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    opacity: 0.65
                    Layout.fillWidth: true
                }
            }
        }
        footer: DialogButtonBox {
            padding: 16
            spacing: 8
            NotebookButton {
                objectName: "cancel_dictionary_sources"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: picker.reject()
            }
            NotebookButton {
                objectName: "apply_dictionary_sources"
                text: qsTr("Apply")
                primary: true
                enabled: options.valid
                // Close only after the backend has validated the entire selection.
                onClicked: {
                    if (options.service.configure_dictionaries(options.selection(), options.draft_forms)) {
                        options.changed()
                        picker.close()
                    }
                }
            }
        }
    }
}
