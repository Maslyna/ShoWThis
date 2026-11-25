import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: empty_state
    property string heading
    property string description
    property string action_text
    property bool motion_enabled: false
    signal action_requested()

    Flickable {
        id: viewport
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, empty_content.implicitHeight + 32)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: empty_content
            width: Math.max(0, Math.min(340, viewport.width - 32))
            x: (viewport.width - width) / 2
            y: Math.max(16, (viewport.height - implicitHeight) / 2)
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
                objectName: "empty_state_action"
                visible: text.length > 0
                text: empty_state.action_text
                motion_enabled: empty_state.motion_enabled
                Layout.alignment: Qt.AlignHCenter
                onClicked: empty_state.action_requested()
                onActiveFocusChanged: if (activeFocus)
                    viewport.contentY = Math.max(0, viewport.contentHeight - viewport.height)
            }
        }
    }

    SystemPalette { id: system_palette }
}
