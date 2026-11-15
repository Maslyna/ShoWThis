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
        width: Math.min(settings_page.availableWidth, 1040)
        x: (settings_page.availableWidth - width) / 2
        spacing: 16

        NotebookTranslatorSettings {
            translator: settings_page.service.translator
            Layout.fillWidth: true
        }

        Repeater {
            model: settings_page.service.dictionary_sources
            Loader {
                required property var modelData
                Layout.fillWidth: true
                active: !!modelData.settings && !!modelData.settings_component
                Component.onCompleted: {
                    if (active) setSource(Qt.resolvedUrl(modelData.settings_component),
                        {service: settings_page.service, manager: modelData.settings})
                }
            }
        }

        NotebookSurface {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Label { text: qsTr("Lookup history & cache"); font.pixelSize: 17; font.bold: true }
                Label {
                    text: qsTr("Recent dictionary results are cached for offline access. This limit never removes saved word cards.")
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
                        Layout.minimumWidth: 0
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
                        text: qsTr("0 disables history. Lowering the limit removes older lookups.")
                        opacity: 0.65
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    NotebookButton {
                        objectName: "clear_history"
                        text: qsTr("Clear history…")
                        quiet: true
                        onClicked: history_confirmation.open()
                    }
                }
            }
        }

        NotebookSurface {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 10
                Label { text: qsTr("Local notebook files"); font.pixelSize: 17; font.bold: true }
                Label {
                    text: qsTr("Back up this vocabulary file to keep your word cards, translations and notes. Local files are not encrypted.")
                    opacity: 0.65
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label { text: qsTr("Vocabulary file"); font.pixelSize: 12; font.bold: true }
                TextArea {
                    text: settings_page.service.storage_path
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Disposable lookup cache: %1").arg(settings_page.service.cache_path)
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    font.pixelSize: 10
                    Layout.fillWidth: true
                }
            }
        }
    }

    NotebookConfirm {
        id: history_confirmation
        objectName: "clear_history_confirmation"
        palette: settings_page.palette
        title: qsTr("Clear lookup history?")
        description: qsTr("Recent lookups and cached definitions will be removed. Your saved word cards will be kept.")
        confirm_text: qsTr("Clear history")
        onAccepted: settings_page.service.clear_history()
    }
}
