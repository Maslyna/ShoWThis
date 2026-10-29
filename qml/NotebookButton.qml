import QtQuick
import QtQuick.Controls

Button {
    id: button
    property bool primary: false
    property bool quiet: false
    property bool align_left: false
    implicitHeight: 38
    implicitWidth: Math.max(38, label.implicitWidth + 28)
    padding: 10
    hoverEnabled: true
    font.pixelSize: 13
    font.weight: primary ? Font.DemiBold : Font.Normal
    SystemPalette { id: system_palette }
    palette.button: system_palette.button
    palette.buttonText: system_palette.buttonText
    palette.windowText: system_palette.windowText
    palette.highlight: system_palette.highlight
    palette.highlightedText: system_palette.highlightedText

    contentItem: Label {
        id: label
        text: button.text
        font: button.font
        textFormat: Text.PlainText
        color: button.primary ? button.palette.highlightedText : button.palette.buttonText
        opacity: button.enabled ? 1 : 0.4
        horizontalAlignment: button.align_left ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 7
        color: button.primary ? button.palette.highlight
            : button.checked ? Qt.alpha(button.palette.highlight, 0.14)
            : button.hovered || button.down ? Qt.alpha(button.palette.windowText, 0.08)
            : button.quiet ? "transparent" : button.palette.button
        opacity: button.enabled ? (button.down ? 0.8 : 1) : 0.45
        border.width: button.visualFocus ? 2 : 1
        border.color: button.visualFocus || button.checked ? button.palette.highlight
            : button.primary || button.quiet ? "transparent" : Qt.alpha(button.palette.windowText, 0.15)
    }
}
