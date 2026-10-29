import QtQuick
import QtQuick.Controls

Pane {
    id: surface
    padding: 16
    background: Rectangle {
        color: surface.palette.base
        radius: 9
        border.color: Qt.alpha(surface.palette.windowText, 0.13)
    }
}
