import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: options
    required property var service
    signal changed()
    Layout.fillWidth: true
    spacing: 4
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Search"); font.pixelSize: 11; opacity: 0.65 }
        ComboBox {
            objectName: "dictionary_mode"
            model: options.service.dictionary_modes
            textRole: "name"
            valueRole: "code"
            currentIndex: model.findIndex(function(item) { return item.code === options.service.dictionary_mode })
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Accessible.name: qsTr("Dictionary search sources")
            onActivated: {
                options.service.dictionary_mode = currentValue
                options.changed()
            }
        }
        Label {
            visible: edition.visible
            text: qsTr("Kaikki in")
            font.pixelSize: 11
            opacity: 0.65
        }
        ComboBox {
            id: edition
            objectName: "definition_language"
            visible: options.service.dictionary_mode !== "freedictionary" || options.service.dictionary_fallback
            model: options.service.definition_languages
            textRole: "name"
            valueRole: "code"
            currentIndex: model.findIndex(function(item) { return item.code === options.service.definition_language })
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Accessible.name: qsTr("Kaikki definition language, separate from word language")
            onActivated: {
                options.service.definition_language = currentValue
                options.changed()
            }
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        CheckBox {
            objectName: "dictionary_fallback"
            visible: options.service.dictionary_mode !== "both"
            text: qsTr("Try the other source if needed")
            checked: options.service.dictionary_fallback
            onToggled: {
                options.service.dictionary_fallback = checked
                options.changed()
            }
        }
        CheckBox {
            objectName: "follow_dictionary_forms"
            text: qsTr("Look up base forms too")
            checked: options.service.follow_forms
            onToggled: {
                options.service.follow_forms = checked
                options.changed()
            }
        }
    }
    Label {
        text: options.service.dictionary_mode === "freedictionary" && !options.service.dictionary_fallback
            ? qsTr("FreeDictionaryAPI provides English definitions.")
            : qsTr("Kaikki uses the selected definition language; FreeDictionaryAPI uses English. Coverage varies by edition.")
        font.pixelSize: 10
        opacity: 0.6
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}
