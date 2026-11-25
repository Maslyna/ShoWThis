import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "context_selection"
    anchors.fill: parent
    z: 20
    required property var words
    required property real scale_x
    required property real scale_y
    property bool motion_enabled: false
    property bool active: false
    visible: active
    onActiveChanged: if (!active) word_picker.close()

    signal accepted(string text)
    signal cancelled()

    property var selection_groups: []
    property var word_groups: ({})
    property var selected_indices: ({})
    readonly property string preview_text: combined_text()
    onPreview_textChanged: if (active) selection_feedback.acknowledge()
    property bool gesture_pressed: false
    property bool dragging: false
    property point press_point: Qt.point(0, 0)
    property point current_point: Qt.point(0, 0)
    property bool panel_moved: false
    property real panel_x: 0
    property real panel_y: 0

    onWidthChanged: abandon_gesture()
    onHeightChanged: abandon_gesture()
    onScale_xChanged: abandon_gesture()
    onScale_yChanged: abandon_gesture()

    function abandon_gesture() {
        gesture_pressed = false
        dragging = false
    }

    SystemPalette { id: system_palette }

    function clear_selection() {
        selection_groups = []
        word_groups = ({})
        selected_indices = ({})
        gesture_pressed = false
        dragging = false
    }

    function begin() {
        clear_selection()
        panel_moved = false
        active = true
        forceActiveFocus()
    }

    function cancel() {
        if (!active) return
        active = false
        word_picker.close()
        gesture_pressed = false
        dragging = false
        cancelled()
    }

    function apply_selection() {
        if (!active || preview_text.length === 0) return
        const text = preview_text
        active = false
        gesture_pressed = false
        dragging = false
        accepted(text)
    }

    function selectable_word(index) {
        const word = words[index]
        return word && typeof word.text === "string" && word.text.trim().length > 0
            && word.width > 0 && word.height > 0
    }

    function combined_text() {
        const groups = []
        for (let group = 0; group < selection_groups.length; ++group) {
            const text = []
            const indices = selection_groups[group]
            for (let position = 0; position < indices.length; ++position) {
                const index = indices[position]
                if (selected_indices[index] && selectable_word(index))
                    text.push(words[index].text.trim())
            }
            if (text.length > 0) groups.push(text.join(" "))
        }
        return groups.join("\n")
    }

    function add_indices(indices) {
        const selected = Object.assign({}, selected_indices)
        const owners = Object.assign({}, word_groups)
        const groups = selection_groups.slice()
        const new_group = []
        for (let position = 0; position < indices.length; ++position) {
            const index = indices[position]
            selected[index] = true
            // Ownership survives deselection, so corrections restore reading order.
            if (owners[index] === undefined) {
                owners[index] = groups.length
                new_group.push(index)
            }
        }
        if (new_group.length > 0) groups.push(new_group)
        word_groups = owners
        selection_groups = groups
        selected_indices = selected
    }

    function select_rectangle() {
        const left = Math.min(press_point.x, current_point.x)
        const right = Math.max(press_point.x, current_point.x)
        const top = Math.min(press_point.y, current_point.y)
        const bottom = Math.max(press_point.y, current_point.y)
        const indices = []
        // The OCR list already supplies reading order within each new group.
        for (let index = 0; index < words.length; ++index) {
            if (!selectable_word(index)) continue
            const word = words[index]
            const center_x = (word.x + word.width / 2) * scale_x
            const center_y = (word.y + word.height / 2) * scale_y
            if (center_x >= left && center_x <= right && center_y >= top && center_y <= bottom)
                indices.push(index)
        }
        if (indices.length > 0) add_indices(indices)
    }

    function toggle_index(index) {
        if (!selectable_word(index)) return
        if (selected_indices[index]) {
            const selected = Object.assign({}, selected_indices)
            delete selected[index]
            selected_indices = selected
        } else {
            add_indices([index])
        }
    }

    function toggle_word(x, y) {
        for (let index = 0; index < words.length; ++index) {
            if (!selectable_word(index)) continue
            const word = words[index]
            const left = word.x * scale_x
            const top = word.y * scale_y
            if (x < left || x > left + word.width * scale_x
                    || y < top || y > top + word.height * scale_y) continue
            toggle_index(index)
            return
        }
    }

    function update_gesture(x, y) {
        current_point = Qt.point(x, y)
        const dx = x - press_point.x
        const dy = y - press_point.y
        const threshold = Qt.styleHints.startDragDistance
        if (dx * dx + dy * dy >= threshold * threshold) dragging = true
    }

    function bounded_position(value, available) {
        return Math.max(0, Math.min(value, Math.max(0, available)))
    }

    Keys.onEscapePressed: function(event) {
        if (active) {
            cancel()
            event.accepted = true
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: root.active && !word_picker.visible
        context: Qt.WindowShortcut
        onActivated: root.cancel()
    }
    Shortcut {
        sequence: "Ctrl+L"
        enabled: root.active && !word_picker.visible
        onActivated: word_picker.open()
    }
    RecognizedWordPicker {
        id: word_picker
        objectName: "context_word_picker"
        words: root.words
        multiple: true
        selected_indices: root.selected_indices
        onWord_toggled: function(index) { root.toggle_index(index) }
        onClosed: if (root.active) root.forceActiveFocus()
    }

    Repeater {
        model: root.active ? root.words : []
        delegate: Rectangle {
            required property var modelData
            required property int index
            objectName: "context_selection_word_" + index
            visible: root.selected_indices[index] === true && root.selectable_word(index)
            x: modelData.x * root.scale_x
            y: modelData.y * root.scale_y
            width: Math.max(0, modelData.width * root.scale_x)
            height: Math.max(0, modelData.height * root.scale_y)
            color: Qt.alpha(system_palette.highlight, 0.32)
            border.color: system_palette.highlight
            border.width: 1
            radius: 2
        }
    }

    Rectangle {
        objectName: "context_selection_rectangle"
        visible: root.gesture_pressed && root.dragging
        x: Math.min(root.press_point.x, root.current_point.x)
        y: Math.min(root.press_point.y, root.current_point.y)
        width: Math.abs(root.current_point.x - root.press_point.x)
        height: Math.abs(root.current_point.y - root.press_point.y)
        color: Qt.alpha(system_palette.highlight, 0.12)
        border.color: system_palette.highlight
        border.width: 1
    }

    MouseArea {
        objectName: "context_selection_gestures"
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        preventStealing: true
        cursorShape: Qt.CrossCursor
        onPressed: function(mouse) {
            root.forceActiveFocus()
            if (mouse.button !== Qt.LeftButton) return
            root.press_point = Qt.point(mouse.x, mouse.y)
            root.current_point = root.press_point
            root.dragging = false
            root.gesture_pressed = true
        }
        onPositionChanged: function(mouse) {
            if (root.gesture_pressed) root.update_gesture(mouse.x, mouse.y)
        }
        onReleased: function(mouse) {
            if (mouse.button !== Qt.LeftButton || !root.gesture_pressed) return
            root.update_gesture(mouse.x, mouse.y)
            if (root.dragging) root.select_rectangle()
            else root.toggle_word(mouse.x, mouse.y)
            root.gesture_pressed = false
            root.dragging = false
        }
        onCanceled: {
            root.gesture_pressed = false
            root.dragging = false
        }
        onWheel: function(wheel) { wheel.accepted = true }
    }

    NotebookSurface {
        id: controls
        objectName: "context_selection_controls"
        width: Math.max(0, Math.min(440, root.width - 24))
        height: content.implicitHeight + topPadding + bottomPadding
        x: root.bounded_position(root.panel_moved ? root.panel_x : root.width - width - 12,
                                 root.width - width)
        y: root.bounded_position(root.panel_moved ? root.panel_y : root.height - height - 12,
                                 root.height - height)
        padding: 12
        palette.windowText: system_palette.windowText
        palette.text: system_palette.text
        palette.base: system_palette.base
        palette.highlight: system_palette.highlight
        palette.highlightedText: system_palette.highlightedText
        background: Rectangle {
            color: system_palette.window
            radius: 9
            border.color: Qt.alpha(system_palette.windowText, 0.2)
        }

        // Catch panel gaps below its content, never above the buttons or preview.
        MouseArea {
            anchors.fill: parent
            onPressed: root.forceActiveFocus()
            onWheel: function(wheel) { wheel.accepted = true }
            acceptedButtons: Qt.AllButtons
        }

        ColumnLayout {
            id: content
            anchors.fill: parent
            spacing: 8

            Item {
                Layout.fillWidth: true
                implicitHeight: 28
                Label {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Select context")
                    color: system_palette.windowText
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Label {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Drag to move")
                    color: Qt.alpha(system_palette.windowText, 0.65)
                    font.pixelSize: 11
                }
                MouseArea {
                    objectName: "context_selection_drag_handle"
                    anchors.fill: parent
                    cursorShape: Qt.SizeAllCursor
                    preventStealing: true
                    property point origin: Qt.point(0, 0)
                    property point panel_origin: Qt.point(0, 0)
                    onPressed: function(mouse) {
                        root.forceActiveFocus()
                        origin = mapToItem(root, mouse.x, mouse.y)
                        panel_origin = Qt.point(controls.x, controls.y)
                        root.panel_x = controls.x
                        root.panel_y = controls.y
                        root.panel_moved = true
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const point = mapToItem(root, mouse.x, mouse.y)
                        root.panel_x = root.bounded_position(panel_origin.x + point.x - origin.x,
                                                            root.width - controls.width)
                        root.panel_y = root.bounded_position(panel_origin.y + point.y - origin.y,
                                                            root.height - controls.height)
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Drag rectangles to add words, click to toggle, or choose words from the list.")
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: system_palette.windowText
                font.pixelSize: 12
            }
            NotebookButton {
                objectName: "choose_context_words"
                text: qsTr("Choose words…")
                Layout.fillWidth: true
                onClicked: word_picker.open()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Choose context words · Ctrl+L")
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 80
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                background: Rectangle {
                    color: system_palette.base
                    radius: 5
                    border.color: Qt.alpha(system_palette.windowText, 0.18)
                    FeedbackTint {
                        id: selection_feedback
                        objectName: "context_selection_feedback"
                        tint: system_palette.highlight
                        motion_enabled: root.motion_enabled
                        radius: 5
                    }
                }
                TextArea {
                    objectName: "context_selection_preview"
                    text: root.preview_text
                    placeholderText: qsTr("Selected context appears here")
                    readOnly: true
                    selectByMouse: true
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    Accessible.name: qsTr("Selected context")
                    color: system_palette.text
                    selectionColor: system_palette.highlight
                    selectedTextColor: system_palette.highlightedText
                    font.pixelSize: 13
                    padding: 8
                    background: null
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                NotebookButton {
                    objectName: "context_selection_clear"
                    text: qsTr("Clear")
                    quiet: true
                    onClicked: root.clear_selection()
                }
                Item { Layout.fillWidth: true }
                NotebookButton {
                    objectName: "context_selection_cancel"
                    text: qsTr("Cancel")
                    onClicked: root.cancel()
                }
                NotebookButton {
                    objectName: "context_selection_apply"
                    text: qsTr("Use context")
                    primary: true
                    enabled: root.preview_text.length > 0
                    onClicked: root.apply_selection()
                }
            }
        }
    }
}
