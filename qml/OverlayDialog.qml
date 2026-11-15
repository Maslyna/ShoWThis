import QtQuick
import QtQuick.Controls

FocusScope {
    id: root
    default property alias contentData: body.data
    property string title: ""
    property real minimumWidth: 280
    property real minimumHeight: 200
    property bool user_geometry: false
    signal activated()
    signal closed()

    width: 600
    height: 480
    visible: false

    function constrain_geometry() {
        if (!parent)
            return
        width = Math.min(parent.width, Math.max(minimumWidth, width))
        height = Math.min(parent.height, Math.max(minimumHeight, height))
        x = Math.max(0, Math.min(x, parent.width - width))
        y = Math.max(0, Math.min(y, parent.height - height))
    }
    function open() {
        constrain_geometry()
        visible = true
        forceActiveFocus()
        activated()
    }
    function close() {
        if (!visible)
            return
        visible = false
        closed()
    }

    onWidthChanged: constrain_geometry()
    onHeightChanged: constrain_geometry()
    onXChanged: constrain_geometry()
    onYChanged: constrain_geometry()
    onActiveFocusChanged: if (activeFocus) activated()
    Keys.onEscapePressed: function(event) {
        close()
        event.accepted = true
    }

    // Explicit properties keep the chrome out of the default content alias.
    property SystemPalette system_palette: SystemPalette {}
    property Connections viewport_connections: Connections {
        target: root.parent
        function onWidthChanged() { root.constrain_geometry() }
        function onHeightChanged() { root.constrain_geometry() }
    }
    property Item chrome: Item {
        parent: root
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            color: root.system_palette.window
            border.color: root.activeFocus ? root.system_palette.highlight : root.system_palette.mid
        }
        MouseArea {
            anchors.fill: parent
            onPressed: function(mouse) { root.forceActiveFocus(); root.activated() }
            onWheel: function(wheel) { wheel.accepted = true }
        }
        TapHandler {
            onPressedChanged: if (pressed) {
                if (!root.activeFocus)
                    root.forceActiveFocus()
                root.activated()
            }
        }
        Item {
            id: header
            height: Math.min(40, root.height)
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            Label {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.right: close_button.left
                anchors.verticalCenter: parent.verticalCenter
                text: root.title
                color: root.system_palette.windowText
                elide: Text.ElideRight
                font.weight: Font.DemiBold
            }
            MouseArea {
                objectName: "dialog_drag_handle"
                anchors.left: parent.left
                anchors.right: close_button.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                cursorShape: Qt.SizeAllCursor
                property point press_position
                property point initial_position
                onPressed: function(mouse) {
                    press_position = mapToItem(root.parent, mouse.x, mouse.y)
                    initial_position = Qt.point(root.x, root.y)
                    root.forceActiveFocus()
                    root.activated()
                }
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return
                    const position = mapToItem(root.parent, mouse.x, mouse.y)
                    root.user_geometry = true
                    root.x = initial_position.x + position.x - press_position.x
                    root.y = initial_position.y + position.y - press_position.y
                    root.constrain_geometry()
                }
            }
            ToolButton {
                id: close_button
                objectName: "dialog_close"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 40
                height: parent.height
                text: "×"
                palette.buttonText: root.system_palette.windowText
                Accessible.name: qsTr("Close")
                onClicked: root.close()
            }
        }
        Item {
            id: body
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            anchors.bottomMargin: 1
            clip: true
        }
        MouseArea {
            objectName: "dialog_resize_handle"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 22
            height: 22
            cursorShape: Qt.SizeFDiagCursor
            property point press_position
            property size initial_size
            onPressed: function(mouse) {
                press_position = mapToItem(root.parent, mouse.x, mouse.y)
                initial_size = Qt.size(root.width, root.height)
                root.forceActiveFocus()
                root.activated()
            }
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                const position = mapToItem(root.parent, mouse.x, mouse.y)
                root.user_geometry = true
                root.width = Math.min(root.parent.width - root.x,
                    Math.max(root.minimumWidth, initial_size.width + position.x - press_position.x))
                root.height = Math.min(root.parent.height - root.y,
                    Math.max(root.minimumHeight, initial_size.height + position.y - press_position.y))
                root.constrain_geometry()
            }
            Label {
                anchors.centerIn: parent
                text: "◢"
                color: root.system_palette.mid
            }
        }
    }
}
