import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: page
    required property var service
    property var selected_cards: []
    signal open_requested(var card)
    signal export_requested(var ids)
    signal delete_requested(var card)
    spacing: 12

    function focus_search() {
        search.forceActiveFocus()
        search.selectAll()
    }
    function toggle_selection(id: string, checked: bool) {
        let ids = selected_cards.slice()
        const index = ids.indexOf(id)
        if (checked && index < 0) ids.push(id)
        else if (!checked && index >= 0) ids.splice(index, 1)
        selected_cards = ids
    }

    Connections {
        target: page.service
        function onVocabulary_changed() {
            page.selected_cards = page.selected_cards.filter(function(id) { return page.service.vocabulary_model.contains(id) })
        }
    }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: search
            objectName: "vocabulary_search"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            placeholderText: qsTr("Search words, meanings, or sentences…")
            selectByMouse: true
            Accessible.name: qsTr("Search saved vocabulary")
            onTextChanged: page.service.vocabulary_model.filter = text
        }
        NotebookButton {
            objectName: "export_anki"
            text: page.selected_cards.length > 0 ? qsTr("Export selected (%1)…").arg(page.selected_cards.length) : qsTr("Export to Anki…")
            enabled: page.service.vocabulary.length > 0
            onClicked: page.export_requested(page.selected_cards)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Label {
            text: page.selected_cards.length > 0 ? qsTr("%1 selected for export").arg(page.selected_cards.length)
                : qsTr("%1 saved words").arg(list.count)
            opacity: 0.6
            Layout.fillWidth: true
        }
        NotebookButton {
            quiet: true
            text: page.selected_cards.length > 0 ? qsTr("Clear selection") : qsTr("Select shown")
            enabled: list.count > 0 || page.selected_cards.length > 0
            onClicked: page.selected_cards = page.selected_cards.length > 0 ? []
                : Array.from(page.service.vocabulary_model.visible_ids)
        }
    }
    NotebookSurface {
        Layout.fillWidth: true
        Layout.fillHeight: true
        padding: 6
        ListView {
            id: list
            objectName: "vocabulary_list"
            anchors.fill: parent
            clip: true
            spacing: 2
            model: page.service.vocabulary_model
            ScrollBar.vertical: ScrollBar {}
            NotebookEmpty {
                anchors.fill: parent
                visible: list.count === 0
                heading: page.service.vocabulary.length > 0 ? qsTr("No matching words") : qsTr("Start your collection")
                description: page.service.vocabulary.length > 0 ? qsTr("Try a different word or part of a sentence.")
                    : qsTr("Save words you discover on screen, with a meaning and a sentence to remember them by.")
                action_text: page.service.vocabulary.length > 0 ? qsTr("Clear search") : qsTr("Add your first word")
                onAction_requested: {
                    if (page.service.vocabulary.length > 0) search.clear()
                    else page.open_requested({})
                }
            }
            delegate: NotebookWordRow {
                required property var modelData
                width: list.width
                entry: modelData
                service: page.service
                selected: page.selected_cards.indexOf(entry.id) >= 0
                onOpen_requested: page.open_requested(entry)
                onSelection_toggled: function(checked) { page.toggle_selection(entry.id, checked) }
                onExport_requested: page.export_requested([entry.id])
                onDelete_requested: page.delete_requested(entry)
            }
        }
    }
}
