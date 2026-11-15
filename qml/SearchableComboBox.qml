import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    editable: true
    editText: searching ? search_text : selection_label()

    // Keep the source model intact: every popup row stores its original index.
    property bool searching: false
    property string search_text: ""
    property int match_index: -1
    property int committed_index: -1
    property bool committing: false
    // A temporary binding preserves callers' declarative currentIndex bindings.
    Binding {
        target: control
        property: "currentIndex"
        value: control.committed_index
        when: control.committing
        restoreMode: Binding.RestoreBinding
    }
    property bool navigated: false
    readonly property var matching_indices: {
        const source = model
        const size = count
        const labelRole = textRole
        const codeRole = valueRole
        const query = search_text.trim().toLowerCase()
        const result = []
        for (let index = 0; index < size; ++index) {
            const label = textAt(index).toLowerCase()
            const value = valueAt(index)
            if (!query || label.indexOf(query) !== -1
                    || (value !== undefined && value !== null && String(value).toLowerCase().indexOf(query) !== -1))
                result.push(index)
        }
        return result
    }

    function selection_label() {
        return displayText
    }

    // Language pickers override this; ordinary choices only accept model rows.
    function accept_custom_value(input) {
        return false
    }

    function reset_highlight() {
        const selected = matching_indices.indexOf(currentIndex)
        match_index = search_text.length ? (matching_indices.length ? 0 : -1) : selected
        if (match_index < 0 && matching_indices.length)
            match_index = 0
        navigated = false
        position_highlight()
    }

    function position_highlight() {
        if (match_index >= 0)
            results.positionViewAtIndex(match_index, ListView.Contain)
    }

    function start_search(text) {
        searching = true
        search_text = text
        if (!popup.visible)
            popup.open()
    }

    function cancel_search() {
        searching = false
        search_text = ""
        if (popup.visible)
            popup.close()
        input.selectAll()
    }

    function commit_index(index) {
        if (index < 0 || index >= count)
            return
        committed_index = index
        committing = true
        activated(index)
        committing = false
        cancel_search()
        accepted()
    }

    function commit_input() {
        const text = search_text.trim()
        if (searching && !navigated) {
            for (let index = 0; index < count; ++index) {
                const value = valueAt(index)
                if (textAt(index).toLowerCase() === text.toLowerCase()
                        || (value !== undefined && value !== null && String(value).toLowerCase() === text.toLowerCase())) {
                    commit_index(index)
                    return
                }
            }
            if (matching_indices.length === 0 && accept_custom_value(text)) {
                cancel_search()
                accepted()
                return
            }
        }
        if (match_index >= 0 && match_index < matching_indices.length)
            commit_index(matching_indices[match_index])
    }

    function handle_key(event) {
        if (event.key === Qt.Key_Escape && (searching || popup.visible)) {
            cancel_search()
        } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
            const opening = !popup.visible
            if (!popup.visible) {
                search_text = ""
                popup.open()
            }
            if (matching_indices.length) {
                const step = event.key === Qt.Key_Down ? 1 : -1
                const starting = match_index < 0 || (opening && currentIndex < 0)
                match_index = starting ? (step > 0 ? 0 : matching_indices.length - 1)
                    : Math.max(0, Math.min(matching_indices.length - 1, match_index + step))
                navigated = true
                position_highlight()
            }
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            commit_input()
        } else {
            return
        }
        event.accepted = true
    }
    function handle_release(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Down || event.key === Qt.Key_Up
                || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            event.accepted = true
    }
    function handle_shortcut_override(event) {
        if ((searching || popup.visible)
                && (event.key === Qt.Key_Escape || event.key === Qt.Key_Down || event.key === Qt.Key_Up
                    || event.key === Qt.Key_Return || event.key === Qt.Key_Enter))
            event.accepted = true
    }

    onMatching_indicesChanged: reset_highlight()
    onActiveFocusChanged: {
        if (activeFocus)
            input.forceActiveFocus()
        else
            cancel_search()
    }
    Keys.priority: Keys.BeforeItem
    Keys.onShortcutOverride: function(event) { handle_shortcut_override(event) }
    Keys.onPressed: function(event) { handle_key(event) }
    Keys.onReleased: function(event) { handle_release(event) }

    // A wrapper keeps ComboBox's native prefix completion off our search field.
    contentItem: Item {
        implicitWidth: input.implicitWidth
        implicitHeight: input.implicitHeight
        TextField {
            id: input
            anchors.fill: parent
            text: control.searching ? control.search_text : control.selection_label()
            font: control.font
            color: control.palette.text
            selectionColor: control.palette.highlight
            selectedTextColor: control.palette.highlightedText
            verticalAlignment: TextInput.AlignVCenter
            leftPadding: 0
            rightPadding: 0
            topPadding: 0
            bottomPadding: 0
            background: null
            selectByMouse: true
            clip: true
            onTextEdited: control.start_search(text)
            onActiveFocusChanged: if (activeFocus) selectAll()
            Keys.priority: Keys.BeforeItem
            Keys.onShortcutOverride: function(event) { control.handle_shortcut_override(event) }
            Keys.onPressed: function(event) { control.handle_key(event) }
            Keys.onReleased: function(event) { control.handle_release(event) }
            TapHandler {
                onTapped: {
                    if (!control.popup.visible) {
                        control.search_text = ""
                        control.popup.open()
                        input.selectAll()
                    }
                }
            }
        }
    }

    popup: Popup {
        y: control.height
        width: control.width
        padding: 1
        focus: false
        implicitHeight: Math.min(280, Math.max(40, results.contentHeight + topPadding + bottomPadding))
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        onOpened: {
            control.reset_highlight()
            input.forceActiveFocus()
        }
        onClosed: control.cancel_search()
        contentItem: ListView {
            id: results
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.matching_indices : []
            currentIndex: control.match_index
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            delegate: ItemDelegate {
                required property int index
                required property var modelData
                width: results.width
                focusPolicy: Qt.NoFocus
                text: control.textAt(modelData)
                highlighted: index === control.match_index
                onClicked: control.commit_index(modelData)
            }
            ScrollIndicator.vertical: ScrollIndicator { }
            Label {
                anchors.centerIn: parent
                visible: control.matching_indices.length === 0
                text: qsTr("No matches")
                color: control.palette.placeholderText
            }
        }
    }
}
