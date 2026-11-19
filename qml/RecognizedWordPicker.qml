import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: picker
    required property var words
    property bool multiple: false
    property var selected_indices: ({})
    signal word_activated(int index)
    signal word_toggled(int index)
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 24 : 560)
    height: Math.min(520, implicitHeight, parent ? parent.height - 24 : 520)
    title: multiple ? qsTr("Choose context words") : qsTr("Recognized words")
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    onOpened: {
        word_list.currentIndex = word_list.count > 0 ? 0 : -1
        word_list.forceActiveFocus()
    }

    function activate(index) {
        if (index < 0 || index >= words.length || !words[index].text.trim().length) return
        if (multiple) word_toggled(index)
        else {
            close()
            word_activated(index)
        }
    }

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: picker.multiple
                ? qsTr("Use Up and Down to move, Space to include or exclude a word. Close this list, then choose Use context.")
                : qsTr("Use Up and Down to move, then Enter to open a word. Escape cancels.")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        ListView {
            id: word_list
            objectName: "recognized_word_list"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            implicitHeight: Math.min(320, contentHeight)
            model: picker.visible ? picker.words : []
            keyNavigationEnabled: true
            activeFocusOnTab: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Accessible.name: picker.title
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                    currentIndex = event.key === Qt.Key_Home ? 0 : count - 1
                    event.accepted = true
                }
            }
            delegate: CheckDelegate {
                id: row
                required property var modelData
                required property int index
                objectName: "recognized_word_" + index
                width: word_list.width
                text: modelData.text
                checkable: picker.multiple
                checked: picker.multiple && picker.selected_indices[index] === true
                highlighted: ListView.isCurrentItem
                focus: ListView.isCurrentItem
                indicator.visible: picker.multiple
                Accessible.name: text
                Accessible.role: picker.multiple ? Accessible.CheckBox : Accessible.Button
                Accessible.description: qsTr("Word %1 of %2. %3").arg(index + 1).arg(word_list.count).arg(modelData.context || "")
                contentItem: Label {
                    text: row.text
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    rightPadding: picker.multiple && row.indicator ? row.indicator.width + row.spacing : 0
                }
                onClicked: picker.activate(index)
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        picker.activate(index)
                        event.accepted = true
                    }
                }
            }
        }
    }
    footer: DialogButtonBox {
        NotebookButton {
            text: picker.multiple ? qsTr("Done") : qsTr("Cancel")
            onClicked: picker.close()
        }
    }
}
