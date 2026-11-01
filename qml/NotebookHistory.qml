import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: page
    required property var service
    signal open_requested(var entry)
    signal settings_requested()
    spacing: 12

    function focus_search() {
        search.forceActiveFocus()
        search.selectAll()
    }

    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: search
            objectName: "history_search"
            placeholderText: qsTr("Find a word you looked up…")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            selectByMouse: true
            Accessible.name: qsTr("Search recent lookups")
        }
        NotebookButton { text: qsTr("History settings"); onClicked: page.settings_requested() }
    }
    Label {
        text: page.service.history_limit === 0 ? qsTr("History is paused. You can enable it in Settings.")
            : qsTr("%1 of %2 recent words · available offline · newest first")
                .arg(page.service.dictionary_history.length).arg(page.service.history_limit)
        opacity: 0.6
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    NotebookSurface {
        Layout.fillWidth: true
        Layout.fillHeight: true
        padding: 6
        ListView {
            id: list
            objectName: "dictionary_history"
            anchors.fill: parent
            clip: true
            spacing: 2
            model: page.service.dictionary_history.filter(function(item) {
                return [item.word, item.context, item.summary || ""].join(" ")
                    .toLowerCase().includes(search.text.toLowerCase())
            })
            ScrollBar.vertical: ScrollBar {}
            NotebookEmpty {
                anchors.fill: parent
                visible: list.count === 0
                heading: search.text.length > 0 ? qsTr("No matching lookups") : qsTr("Your discoveries will appear here")
                description: qsTr("Successful dictionary lookups are kept automatically. Save a word card when you want to keep your own translation and notes.")
            }
            delegate: NotebookWordRow {
                required property var modelData
                width: list.width
                entry: modelData
                service: page.service
                is_history: true
                onOpen_requested: page.open_requested(entry)
            }
        }
    }
}
