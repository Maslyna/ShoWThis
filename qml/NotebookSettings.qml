import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: settings_page
    required property var service
    objectName: "notebook_settings_scroll"
    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: parent.width
        spacing: 16

        NotebookSurface {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Label { text: qsTr("Lookup history"); font.pixelSize: 17; font.bold: true }
                Label {
                    text: qsTr("Keep recent dictionary results for quick, offline access. Saved words are never removed by this limit.")
                    opacity: 0.65
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    SpinBox {
                        id: history_limit_input
                        objectName: "history_limit"
                        from: 0
                        to: 10000
                        stepSize: 100
                        editable: true
                        value: settings_page.service.history_limit
                        Accessible.name: qsTr("Maximum history entries")
                        Layout.fillWidth: true
                    }
                    NotebookButton {
                        objectName: "apply_history_limit"
                        text: qsTr("Apply limit")
                        onClicked: settings_page.service.history_limit = history_limit_input.value
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("0 disables history. Reducing the limit removes older entries.")
                        opacity: 0.65
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    NotebookButton {
                        text: qsTr("Clear history…")
                        quiet: true
                        enabled: settings_page.service.dictionary_history.length > 0
                        onClicked: history_confirmation.open()
                    }
                }
            }
        }

        NotebookTranslatorSettings {
            translator: settings_page.service.translator
            Layout.fillWidth: true
        }

        NotebookOfflineDictionaries {
            service: settings_page.service
        }

        NotebookSurface {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 10
                Label { text: qsTr("Your data stays here"); font.pixelSize: 17; font.bold: true }
                Label {
                    text: qsTr("Back up this file to keep your saved words, notes, and history. The file is local, but not encrypted.")
                    opacity: 0.65
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                TextArea {
                    text: settings_page.service.storage_path
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    Layout.fillWidth: true
                }
            }
        }
    }

    NotebookConfirm {
        id: history_confirmation
        palette: settings_page.palette
        title: qsTr("Clear lookup history?")
        description: qsTr("Recent lookups and cached definitions will be removed. Your saved word cards will be kept.")
        confirm_text: qsTr("Clear history")
        onAccepted: settings_page.service.clear_history()
    }
}
