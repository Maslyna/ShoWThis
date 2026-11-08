import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

NotebookSurface {
    id: settings
    required property var service
    readonly property var manager: service.offline_dictionaries
    property string word_language: service.source_language
    objectName: "offline_dictionary_settings"
    Layout.fillWidth: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        Label { text: qsTr("Offline dictionaries"); font.pixelSize: 17; font.bold: true }
        Label {
            text: qsTr("Choose the language of the words and the language you want definitions in. Kaikki downloads are edition-wide, but only the selected word language is kept in your local index. Downloads can be several GB.")
            wrapMode: Text.WordWrap
            opacity: 0.7
            Layout.fillWidth: true
        }
        Label { text: qsTr("Word language — what are you looking up?"); font.pixelSize: 12 }
        NotebookLanguagePicker {
            objectName: "offline_dictionary_word_language"
            options: [{code: "", name: qsTr("All word languages")}].concat(Array.from(settings.service.language_options))
            language_code: settings.word_language
            enabled: !settings.manager.busy
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            onLanguage_selected: function(code) { settings.word_language = code }
        }
        Label { text: qsTr("Definitions in — which language do you read?"); font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: edition
                objectName: "offline_dictionary_edition"
                model: settings.manager.editions
                textRole: "name"
                valueRole: "code"
                enabled: !settings.manager.busy
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Accessible.name: qsTr("Downloaded definition language")
            }
            NotebookButton {
                objectName: "check_offline_dictionary_size"
                text: qsTr("Check size")
                enabled: !settings.manager.busy && edition.currentIndex >= 0
                onClicked: settings.manager.inspect(edition.currentValue, settings.word_language)
            }
        }
        Label {
            visible: settings.manager.download_info.edition === edition.currentValue
                && settings.manager.download_info.word_language === settings.word_language
            text: qsTr("Compressed download: %1. The indexed dictionary and temporary files need extra space.")
                .arg(settings.manager.download_info.size_text || "")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        NotebookButton {
            objectName: "download_offline_dictionary"
            text: qsTr("Download / update…")
            primary: true
            enabled: !settings.manager.busy && settings.manager.download_info.edition === edition.currentValue
                && settings.manager.download_info.word_language === settings.word_language
            onClicked: download_confirmation.open()
        }
        ProgressBar {
            objectName: "offline_dictionary_progress"
            visible: settings.manager.busy
            indeterminate: settings.manager.progress < 0
            value: Math.max(0, settings.manager.progress)
            Layout.fillWidth: true
        }
        Label {
            objectName: "offline_dictionary_message"
            text: settings.manager.message
            visible: text.length > 0
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        NotebookButton {
            objectName: "cancel_offline_dictionary"
            visible: settings.manager.busy
            text: qsTr("Cancel")
            onClicked: settings.manager.cancel()
        }
        Label { text: qsTr("When to use downloaded Kaikki data"); font.pixelSize: 12 }
        ComboBox {
            objectName: "offline_dictionary_mode"
            model: [
                {code: "fallback", name: qsTr("When online lookup cannot connect")},
                {code: "prefer_local", name: qsTr("Prefer downloaded data")},
                {code: "local_only", name: qsTr("Downloaded data only (no Kaikki requests)")},
                {code: "online_only", name: qsTr("Online only")}
            ]
            textRole: "name"
            valueRole: "code"
            currentIndex: model.findIndex(function(item) { return item.code === settings.manager.mode })
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            onActivated: settings.manager.mode = currentValue
        }
        Label {
            text: qsTr("For lookup, choose this word language in the word editor, and Kaikki with the matching definition language in Sources & languages. Other selected sources may still use the internet.")
            font.pixelSize: 11
            opacity: 0.65
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Repeater {
            model: settings.manager.packs
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Label {
                    text: qsTr("%1 → %2 definitions · %3 entries · %4 on disk").arg(parent.modelData.word_language_name)
                        .arg(parent.modelData.name).arg(parent.modelData.records).arg(parent.modelData.size_text)
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                NotebookButton {
                    objectName: "remove_offline_dictionary_" + (parent.modelData.word_language || "all") + "_" + parent.modelData.edition
                    text: qsTr("Remove…")
                    quiet: true
                    enabled: !settings.manager.busy
                    onClicked: {
                        remove_confirmation.edition = parent.modelData.edition
                        remove_confirmation.word_language = parent.modelData.word_language
                        remove_confirmation.open()
                    }
                }
            }
        }
        Label {
            text: settings.manager.storage_path
            font.pixelSize: 10
            opacity: 0.6
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
        }
    }

    NotebookConfirm {
        id: download_confirmation
        objectName: "offline_download_confirmation"
        title: qsTr("Download Kaikki edition?")
        description: qsTr("Download %1 → %2 definitions (%3 compressed)? The transfer contains the full definition-language edition; only the chosen word language is indexed locally. Installation needs extra disk space. Existing data stays usable until the new index is complete.")
            .arg(settings.manager.download_info.word_language_name || "").arg(settings.manager.download_info.name || "")
            .arg(settings.manager.download_info.size_text || "")
        confirm_text: qsTr("Download")
        onAccepted: settings.manager.download(settings.manager.download_info.edition, settings.manager.download_info.word_language)
    }
    NotebookConfirm {
        id: remove_confirmation
        property string edition: ""
        property string word_language: ""
        title: qsTr("Remove downloaded edition?")
        description: qsTr("Only the downloaded dictionary will be removed. Your saved word cards and lookup history will be kept.")
        confirm_text: qsTr("Remove")
        onAccepted: settings.manager.remove(edition, word_language)
    }
}
