import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

NotebookSurface {
    id: dictionary
    required property var service
    required property string word
    required property int selected_sense
    required property var current_sense
    required property var active_source
    required property bool lookup_attempted
    required property bool wide_layout
    signal sense_selected(int index)
    signal manual_meaning_requested()
    signal headword_requested(string word)
    signal both_requested()
    objectName: "dictionary_pane"

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Dictionary"); font.pixelSize: 17; font.bold: true; Layout.fillWidth: true }
            NotebookButton {
                objectName: "search_both_dictionaries"
                text: qsTr("Search both")
                quiet: true
                font.pixelSize: 10
                visible: dictionary.service.dictionary_mode !== "both"
                enabled: !dictionary.service.dictionary_busy && dictionary.word.trim().length > 0
                onClicked: dictionary.both_requested()
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            NotebookEmpty {
                anchors.fill: parent
                visible: !dictionary.service.dictionary_busy && senses.count === 0
                heading: dictionary.lookup_attempted ? qsTr("No meaning found yet") : qsTr("What does it mean?")
                description: dictionary.lookup_attempted && dictionary.service.message.length > 0
                    ? dictionary.service.message : qsTr("Look up a word, then choose the meaning that fits your sentence.")
                action_text: dictionary.wide_layout ? "" : qsTr("Write my own meaning")
                onAction_requested: dictionary.manual_meaning_requested()
            }
            BusyIndicator {
                anchors.centerIn: parent
                running: dictionary.service.dictionary_busy
                visible: running
            }
            ListView {
                id: senses
                objectName: "dictionary_senses"
                anchors.fill: parent
                visible: count > 0
                clip: true
                spacing: 6
                model: dictionary.service.senses
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    required property int index
                    objectName: "dictionary_sense_" + index
                    width: senses.width
                    padding: 12
                    checked: dictionary.selected_sense === index
                    background: Rectangle {
                        radius: 7
                        color: row.checked ? Qt.alpha(dictionary.palette.highlight, 0.12)
                            : row.hovered ? Qt.alpha(dictionary.palette.windowText, 0.05) : "transparent"
                        border.color: row.checked ? dictionary.palette.highlight : Qt.alpha(dictionary.palette.windowText, 0.09)
                    }
                    contentItem: ColumnLayout {
                        spacing: 6
                        Label {
                            text: (row.modelData.provider === "kaikki" ? "Kaikki" : "FreeDictionaryAPI")
                                + " · " + (row.modelData.definition_language || "en").toUpperCase()
                                + " · " + (row.modelData.headword || dictionary.word)
                            textFormat: Text.PlainText
                            font.pixelSize: 10
                            opacity: 0.65
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Label {
                            text: (row.index + 1) + ".  " + row.modelData.definition
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Repeater {
                            model: row.modelData.form_of || []
                            NotebookButton {
                                required property var modelData
                                text: qsTr("Look up base form: %1").arg(modelData.word)
                                quiet: true
                                enabled: !dictionary.service.dictionary_busy
                                Layout.fillWidth: true
                                onClicked: dictionary.headword_requested(modelData.word)
                            }
                        }
                        Label {
                            visible: (row.modelData.articles || []).length > 0
                            text: (row.modelData.articles || []).join(" / ") + " " + dictionary.word
                            color: dictionary.palette.highlight
                            font.weight: Font.DemiBold
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Label {
                            text: [row.modelData.part_of_speech, (row.modelData.tags || []).join(", ")]
                                .filter(function(value) { return !!value }).join(" · ")
                            opacity: 0.55
                            font.pixelSize: 11
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    onClicked: dictionary.sense_selected(index)
                }
            }
        }
        Label {
            visible: dictionary.selected_sense >= 0
            text: (dictionary.current_sense.pronunciation || "")
                + ((dictionary.current_sense.examples || []).length > 0 ? "\n" + dictionary.current_sense.examples[0] : "")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            opacity: 0.65
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            NotebookButton {
                text: "FreeDictionaryAPI ↗"
                quiet: true
                font.pixelSize: 10
                onClicked: Qt.openUrlExternally("https://freedictionaryapi.com/")
            }
            NotebookButton {
                text: "Kaikki ↗"
                quiet: true
                font.pixelSize: 10
                onClicked: Qt.openUrlExternally("https://kaikki.org/")
            }
            Item { Layout.fillWidth: true }
            NotebookButton {
                visible: !!dictionary.active_source.url
                text: "Wiktionary · CC BY-SA ↗"
                quiet: true
                font.pixelSize: 10
                onClicked: {
                    const url = dictionary.active_source.url || ""
                    if (/^https:\/\/[a-z-]+\.wiktionary\.org\//.test(url)) Qt.openUrlExternally(url)
                }
            }
        }
    }
}
