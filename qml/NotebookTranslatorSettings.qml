import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

NotebookSurface {
    id: settings
    required property var translator
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Translation"); font.pixelSize: 17; font.bold: true; Layout.fillWidth: true }
            Label {
                text: settings.translator.configured ? qsTr("● Ready") : qsTr("Optional")
                color: settings.translator.configured ? settings.palette.highlight : settings.palette.text
                opacity: 0.7
            }
        }
        Label {
            text: qsTr("Word-card translations can be written and edited without a translator. Choose and configure a translator to enable online translation.")
            opacity: 0.65
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        ComboBox {
            objectName: "translation_provider"
            model: settings.translator.providers
            textRole: "name"
            valueRole: "id"
            currentIndex: settings.translator.providers.findIndex(function(provider) {
                return provider.id === settings.translator.provider_id
            })
            onActivated: settings.translator.provider_id = currentValue
            Layout.fillWidth: true
            Accessible.name: qsTr("Translation provider")
        }
        Repeater {
            model: settings.translator.configuration_fields
            ColumnLayout {
                id: field
                required property var modelData
                readonly property var choices: modelData.choices || []
                Layout.fillWidth: true
                Label { text: field.modelData.label; font.bold: true }
                ComboBox {
                    objectName: "translator_option_" + field.modelData.key
                    visible: field.choices.length > 0
                    model: field.choices
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: field.choices.findIndex(function(choice) {
                        return choice.value === settings.translator.options[field.modelData.key]
                    })
                    onActivated: settings.translator.set_option(field.modelData.key, currentValue)
                    Layout.fillWidth: true
                    Accessible.name: field.modelData.label
                }
                RowLayout {
                    visible: field.choices.length === 0
                    Layout.fillWidth: true
                    TextField {
                        id: input
                        objectName: "translator_input_" + field.modelData.key
                        text: field.modelData.secret ? "" : settings.translator.options[field.modelData.key] || ""
                        echoMode: field.modelData.secret ? TextInput.Password : TextInput.Normal
                        placeholderText: field.modelData.secret ? qsTr("This session only") : ""
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Accessible.name: field.modelData.label
                        function apply() {
                            settings.translator.set_option(field.modelData.key, text)
                            if (field.modelData.secret) clear()
                        }
                        onAccepted: apply()
                    }
                    NotebookButton {
                        objectName: "apply_translator_" + field.modelData.key
                        text: qsTr("Apply")
                        enabled: input.text.trim().length > 0
                        onClicked: input.apply()
                    }
                }
            }
        }
        NotebookButton {
            objectName: "forget_translator_credentials"
            visible: settings.translator.configuration_fields.some(function(field) { return !!field.secret })
            text: qsTr("Forget credentials")
            quiet: true
            onClicked: settings.translator.forget_credentials()
        }
        Label {
            text: qsTr("Only pressing Translate contacts the selected translator. Credentials are kept in memory for this session, never in your notebook or settings.")
            wrapMode: Text.WordWrap
            opacity: 0.6
            font.pixelSize: 12
            Layout.fillWidth: true
        }
    }
}
