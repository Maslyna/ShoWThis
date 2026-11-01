import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ItemDelegate {
    id: row
    required property var entry
    required property var service
    property bool is_history: false
    property bool selected: false
    readonly property string display_word: service.format_word(entry.word, entry.article || "")
    signal open_requested()
    signal selection_toggled(bool checked)
    signal delete_requested()
    signal export_requested()
    padding: 14
    implicitHeight: Math.max(86, contentItem.implicitHeight + topPadding + bottomPadding)
    onClicked: open_requested()
    Accessible.name: display_word + ", " + (entry.translation || entry.meaning || entry.summary || "")
    background: Rectangle {
        radius: 7
        color: row.selected ? Qt.alpha(row.palette.highlight, 0.12)
            : row.hovered ? Qt.alpha(row.palette.windowText, 0.055) : "transparent"
        border.color: row.selected ? Qt.alpha(row.palette.highlight, 0.45) : "transparent"
    }
    contentItem: RowLayout {
        spacing: 12
        CheckBox {
            objectName: "select_saved_" + row.entry.id
            visible: !row.is_history
            checked: row.selected
            Accessible.name: qsTr("Select %1 for export").arg(row.entry.word)
            onClicked: row.selection_toggled(checked)
        }
        ColumnLayout {
            spacing: 5
            Layout.fillWidth: true
            RowLayout {
                Label {
                    text: row.display_word
                    textFormat: Text.PlainText
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: (row.entry.language || "").toUpperCase()
                    font.pixelSize: 11
                    opacity: 0.55
                }
            }
            Label {
                text: row.entry.translation || row.entry.meaning || row.entry.summary || qsTr("Cached dictionary entry")
                textFormat: Text.PlainText
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                visible: text.length > 0
                text: row.entry.context || ""
                textFormat: Text.PlainText
                font.pixelSize: 12
                opacity: 0.6
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        NotebookButton {
            objectName: "row_actions_" + row.entry.id
            visible: !row.is_history
            text: "⋯"
            font.pixelSize: 22
            quiet: true
            Accessible.name: qsTr("Actions for %1").arg(row.entry.word)
            onClicked: row_menu.open()
            Menu {
                id: row_menu
                objectName: "saved_actions_" + row.entry.id
                palette: row.palette
                MenuItem { text: qsTr("Open word card"); onTriggered: row.open_requested() }
                MenuItem { text: qsTr("Export to Anki…"); onTriggered: row.export_requested() }
                MenuSeparator {}
                MenuItem { text: qsTr("Delete saved word…"); onTriggered: row.delete_requested() }
            }
        }
        Label { visible: row.is_history; text: "›"; font.pixelSize: 25; opacity: 0.45 }
    }
}
