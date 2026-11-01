import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

NotebookSurface {
    id: card
    required property var source
    required property bool source_enabled
    required property var languages
    required property bool fallback
    objectName: "dictionary_source_card_" + source.id
    signal enabled_requested(bool value)
    signal languages_requested(var value)
    signal fallback_requested(bool value)
    Layout.fillWidth: true
    padding: 14

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        CheckBox {
            objectName: "dictionary_source_" + card.source.id
            text: card.source.name
            font.bold: true
            font.pixelSize: 15
            checked: card.source_enabled
            Layout.fillWidth: true
            onToggled: card.enabled_requested(checked)
        }
        Label {
            text: card.source.description
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            opacity: 0.65
            font.pixelSize: 12
            Layout.fillWidth: true
        }
        ColumnLayout {
            visible: card.source_enabled
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Definition languages"); font.pixelSize: 11; font.bold: true; opacity: 0.7 }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: card.source.languages
                    NotebookButton {
                        required property var modelData
                        objectName: "dictionary_language_" + card.source.id + "_" + modelData.code
                        text: (checked ? "✓  " : "") + modelData.name
                        font.pixelSize: 12
                        implicitHeight: 32
                        checked: card.languages.indexOf(modelData.code) >= 0
                        Accessible.name: modelData.name
                        Accessible.checkable: true
                        Accessible.checked: checked
                        onClicked: {
                            const next = Array.from(card.languages)
                            const position = next.indexOf(modelData.code)
                            if (position < 0) next.push(modelData.code)
                            else next.splice(position, 1)
                            card.languages_requested(next)
                        }
                    }
                }
            }
            Label {
                visible: card.languages.length === 0
                text: qsTr("Select at least one definition language.")
                color: card.palette.highlight
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            CheckBox {
                objectName: "dictionary_fallback_" + card.source.id
                text: qsTr("Use only as fallback")
                checked: card.fallback
                onToggled: card.fallback_requested(checked)
            }
        }
    }
}
