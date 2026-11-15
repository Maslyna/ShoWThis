import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

NotebookSurface {
    id: settings
    required property var translator
    property bool show_help: false
    readonly property bool provider_selected: translator.provider_id.length > 0
    readonly property bool has_help: translator.configuration_fields.some(function(field) {
        return !!field.description
    })
    readonly property bool has_secrets: translator.configuration_fields.some(function(field) {
        return !!field.secret
    })
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Translation · optional"); font.pixelSize: 17; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Label {
                objectName: "translator_configuration_status"
                text: !settings.provider_selected ? qsTr("Off")
                    : settings.translator.configured ? qsTr("Configured · not checked") : qsTr("Setup needed")
                color: settings.provider_selected && settings.translator.configured
                    ? settings.palette.highlight : settings.palette.text
            }
        }
        Label {
            text: qsTr("You can always enter word and context translations manually.")
            opacity: 0.7
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label { text: qsTr("Translation provider"); font.bold: true }
        SearchableComboBox {
            objectName: "translation_provider"
            model: settings.translator.providers
            textRole: "name"
            valueRole: "id"
            currentIndex: settings.translator.providers.findIndex(function(provider) {
                return provider.id === settings.translator.provider_id
            })
            onActivated: {
                settings.show_help = false
                settings.translator.provider_id = currentValue
            }
            Layout.fillWidth: true
            Accessible.name: qsTr("Translation provider")
        }
        Label {
            text: !settings.provider_selected
                ? qsTr("No translation service is used. Dictionary lookup and saved words work as usual.")
                : settings.translator.configured
                    ? qsTr("Settings are complete. Server availability and supported languages have not been checked.")
                    : qsTr("Complete the required settings below to enable translation requests.")
            opacity: 0.7
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        GridLayout {
            columns: settings.width >= 760 ? 2 : 1
            columnSpacing: 24
            rowSpacing: 12
            visible: settings.provider_selected
            Layout.fillWidth: true
            Repeater {
                model: settings.translator.configuration_fields
                ColumnLayout {
                    id: field
                    required property var modelData
                    readonly property var choices: modelData.choices || []
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignTop
                    Label {
                        text: field.modelData.required ? qsTr("%1 (required)").arg(field.modelData.label) : field.modelData.label
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: field.modelData.description || ""
                        visible: settings.show_help && text.length > 0
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        opacity: 0.65
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                    SearchableComboBox {
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
                        Layout.minimumWidth: 0
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
                            placeholderText: field.modelData.secret ? qsTr("Session-only credential")
                                : field.modelData.default_value || ""
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
        }
        NotebookButton {
            objectName: "translator_setup_help"
            visible: settings.provider_selected && settings.has_help
            text: settings.show_help ? qsTr("Hide setup help") : qsTr("Show setup help")
            quiet: true
            onClicked: settings.show_help = !settings.show_help
        }
        Label {
            objectName: "settings_word_translation_error"
            text: qsTr("Last word translation: %1").arg(settings.translator.word_error)
            visible: settings.translator.word_error.length > 0
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            objectName: "settings_context_translation_error"
            text: qsTr("Last context translation: %1").arg(settings.translator.context_error)
            visible: settings.translator.context_error.length > 0
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            objectName: "settings_session_translation_error"
            text: qsTr("Last translator request: %1").arg(settings.translator.session_error || "")
            visible: (settings.translator.session_error || "").length > 0
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        NotebookButton {
            objectName: "forget_translator_credentials"
            visible: settings.has_secrets
            text: qsTr("Forget credentials")
            quiet: true
            onClicked: settings.translator.forget_credentials()
        }
        Label {
            text: qsTr("No server requests until you choose Translate in the translator window.")
            visible: settings.provider_selected
            wrapMode: Text.WordWrap
            opacity: 0.6
            font.pixelSize: 12
            Layout.fillWidth: true
        }
        Label {
            text: qsTr("Credentials are session-only and never stored. Apply clears the password field.")
            visible: settings.has_secrets
            wrapMode: Text.WordWrap
            opacity: 0.6
            font.pixelSize: 12
            Layout.fillWidth: true
        }
    }
}
