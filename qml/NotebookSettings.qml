import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: settings_page
    required property var service
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

        NotebookSurface {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Contextual translation"); font.pixelSize: 17; font.bold: true; Layout.fillWidth: true }
                    Label {
                        text: settings_page.service.has_translation_key ? qsTr("● Ready") : qsTr("Optional")
                        color: settings_page.service.has_translation_key ? settings_page.palette.highlight : settings_page.palette.text
                        opacity: 0.7
                    }
                }
                Label {
                    text: qsTr("Use your own DeepL API key to translate words using the surrounding sentence. Dictionary lookup works without it.")
                    opacity: 0.65
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                ComboBox {
                    model: ["DeepL API Free", "DeepL API Pro"]
                    currentIndex: settings_page.service.deepl_pro ? 1 : 0
                    onActivated: settings_page.service.deepl_pro = currentIndex === 1
                    Layout.fillWidth: true
                    Accessible.name: qsTr("DeepL API plan")
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: api_key_input
                        echoMode: TextInput.Password
                        placeholderText: qsTr("API key · this session only")
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Accessible.name: qsTr("DeepL API key")
                        onAccepted: if (text.trim().length > 0) {
                            settings_page.service.set_translation_key(text)
                            clear()
                        }
                    }
                    NotebookButton {
                        text: qsTr("Use key")
                        enabled: api_key_input.text.trim().length > 0
                        onClicked: {
                            settings_page.service.set_translation_key(api_key_input.text)
                            api_key_input.clear()
                        }
                    }
                    NotebookButton {
                        text: qsTr("Forget")
                        quiet: true
                        visible: settings_page.service.has_translation_key
                        onClicked: settings_page.service.set_translation_key("")
                    }
                }
                Label {
                    text: qsTr("Only pressing Translate sends your word and context. Keys are never stored in your notebook or settings. SHOWTHIS_DEEPL_API_KEY is also supported.")
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
            }
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
