import QtQuick
import QtQuick.Controls

Dialog {
    id: confirmation
    property string description
    property string confirm_text: qsTr("Continue")
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(420, parent ? parent.width - 32 : 420)
    implicitHeight: description_label.implicitHeight + 128
    modal: true
    focus: true
    padding: 20
    contentItem: Label {
        id: description_label
        width: confirmation.width - confirmation.leftPadding - confirmation.rightPadding
        text: confirmation.description
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
    }
    footer: DialogButtonBox {
        implicitHeight: 62
        padding: 12
        spacing: 8
        NotebookButton {
            objectName: "cancel_action"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        NotebookButton {
            objectName: "confirm_action"
            text: confirmation.confirm_text
            primary: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
}
