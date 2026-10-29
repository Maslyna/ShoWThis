import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: empty_state
    property string heading
    property string description
    property string action_text
    signal action_requested()

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.max(0, Math.min(340, parent.width - 32))
        spacing: 12
        Label {
            text: "Aa"
            visible: empty_state.height > 190
            font.pixelSize: 36
            font.weight: Font.Light
            color: system_palette.highlight
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: empty_state.heading
            font.pixelSize: 17
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            text: empty_state.description
            opacity: 0.65
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        NotebookButton {
            visible: text.length > 0
            text: empty_state.action_text
            Layout.alignment: Qt.AlignHCenter
            onClicked: empty_state.action_requested()
        }
    }

    SystemPalette { id: system_palette }
}
