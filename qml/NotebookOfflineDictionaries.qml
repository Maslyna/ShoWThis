import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

NotebookSurface {
    id: settings
    required property var service
    required property var manager
    property string word_language: service.source_language
    objectName: "offline_dictionary_settings"
    Layout.fillWidth: true

    function lookup_language(pack): string {
        return manager.normalize_word_language(pack.word_language || service.source_language)
    }
    function is_selected(pack): bool {
        const selected = service.dictionary_selection.find(function(item) { return item.provider === "kaikki" })
        return service.source_language === lookup_language(pack) && manager.mode !== "online_only"
            && !!selected && !selected.fallback && selected.languages.indexOf(pack.edition) >= 0
    }
    function activate(pack) {
        const language = lookup_language(pack)
        if (manager.busy || !language || !manager.packs.some(function(item) {
            return item.edition === pack.edition && item.word_language === pack.word_language
        })) return
        const selection = service.dictionary_selection.filter(function(item) { return item.provider !== "kaikki" })
            .map(function(item) {
                return {provider: item.provider, languages: Array.from(item.languages), fallback: item.fallback}
            })
        selection.push({provider: "kaikki", languages: [pack.edition], fallback: false})
        if (!service.configure_dictionaries(selection, service.follow_forms)) return
        service.source_language = language
        if (manager.mode === "online_only") manager.mode = "prefer_local"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        Label { text: qsTr("Offline dictionaries"); font.pixelSize: 17; font.bold: true }
        Label {
            text: qsTr("Download Kaikki definitions for offline lookup. Choose the word language and the language of the definitions.")
            wrapMode: Text.WordWrap
            opacity: 0.7
            Layout.fillWidth: true
        }
        Label { text: qsTr("Word language"); font.pixelSize: 12; font.bold: true }
        NotebookLanguagePicker {
            objectName: "offline_dictionary_word_language"
            Accessible.name: qsTr("Offline dictionary word language")
            options: [{code: "", name: qsTr("All word languages")}].concat(Array.from(settings.service.language_options))
            language_code: settings.word_language
            enabled: !settings.manager.busy
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            onLanguage_selected: function(code) {
                settings.word_language = settings.manager.normalize_word_language(code) || code
            }
        }
        Label { text: qsTr("Definition language"); font.pixelSize: 12; font.bold: true }
        RowLayout {
            Layout.fillWidth: true
            SearchableComboBox {
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
            text: qsTr("Compressed download: %1. Full editions can be several GB; indexing needs extra space. Only the selected word language is kept.")
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
        Label { text: qsTr("Use downloaded definitions"); font.pixelSize: 12; font.bold: true }
        SearchableComboBox {
            objectName: "offline_dictionary_mode"
            Accessible.name: qsTr("Downloaded dictionary lookup policy")
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
            text: qsTr("Use an installed dictionary for lookup below. This sets its word and definition languages; other sources stay selected. Online-only mode switches to prefer downloaded data. All-language packs use the current lookup word language.")
            font.pixelSize: 11
            opacity: 0.65
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            text: qsTr("Installed dictionaries")
            visible: settings.manager.packs.length > 0
            font.bold: true
            Layout.fillWidth: true
        }
        Repeater {
            model: settings.manager.packs
            ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                Label {
                    text: qsTr("%1 → %2 definitions · %3 entries · %4 on disk").arg(parent.modelData.word_language_name)
                        .arg(parent.modelData.name).arg(parent.modelData.records).arg(parent.modelData.size_text)
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    NotebookButton {
                        objectName: "activate_offline_dictionary_" + (parent.parent.modelData.word_language || "all") + "_" + parent.parent.modelData.edition
                        text: checked ? qsTr("Selected for lookup") : qsTr("Use for lookup")
                        checked: settings.is_selected(parent.parent.modelData)
                        enabled: !settings.manager.busy && !!settings.lookup_language(parent.parent.modelData)
                        Accessible.name: qsTr("%1: %2 to %3 definitions").arg(text)
                            .arg(settings.lookup_language(parent.parent.modelData).toUpperCase()).arg(parent.parent.modelData.name)
                        onClicked: settings.activate(parent.parent.modelData)
                    }
                    NotebookButton {
                        objectName: "remove_offline_dictionary_" + (parent.parent.modelData.word_language || "all") + "_" + parent.parent.modelData.edition
                        text: qsTr("Remove…")
                        quiet: true
                        enabled: !settings.manager.busy
                        onClicked: {
                            remove_confirmation.edition = parent.parent.modelData.edition
                            remove_confirmation.word_language = parent.parent.modelData.word_language
                            remove_confirmation.open()
                        }
                    }
                }
                Label {
                    visible: !settings.lookup_language(parent.modelData)
                    text: qsTr("Choose a valid lookup word language before using this all-language pack.")
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
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
