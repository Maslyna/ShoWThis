import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    focus: true
    Keys.onEscapePressed: function(event) {
        if (context_selection.active) {
            context_selection.cancel()
        } else if (translator_dialog.visible && (!notebook_dialog.visible || translator_dialog.z > notebook_dialog.z)) {
            translator_dialog.close()
        } else if (notebook_dialog.visible) {
            notebook_dialog.close()
        } else {
            overlay.hide_requested()
        }
        event.accepted = true
    }
    width: 1280
    height: 720

    readonly property real scaleX: overlay.imageSize.width > 0 ? width / overlay.imageSize.width : 1
    readonly property real scaleY: overlay.imageSize.height > 0 ? height / overlay.imageSize.height : 1
    property string hoveredWord: ""
    property rect hoveredRect: Qt.rect(0, 0, 0, 0)
    readonly property var selectedLanguages: overlay.selectedLanguage.length > 0
        ? overlay.selectedLanguage.split("+") : []
    readonly property bool context_selection_available: !overlay.scanning
        && overlay.imageSize.width > 0 && overlay.imageSize.height > 0 && overlay.words.length > 0
    readonly property bool dialogs_visible: notebook_dialog.visible || translator_dialog.visible
    property int dialog_z: 10
    property bool selecting_context: false
    property bool restore_notebook: false
    property bool restore_translator: false
    property var selection_focus_dialog: null

    onWidthChanged: arrange_dialogs()
    onHeightChanged: arrange_dialogs()

    function arrange_dialogs() {
        if (!notebook_dialog || !translator_dialog) return
        const available_width = Math.max(0, width - 24)
        const available_height = Math.max(0, height - 24)
        const adjacent = notebook_dialog.visible && translator_dialog.visible
            && !notebook_dialog.user_geometry && !translator_dialog.user_geometry
            && available_width >= notebook_dialog.minimumWidth + translator_dialog.minimumWidth + 12
        const notebook_width = adjacent
            ? Math.min(1120, Math.max(notebook_dialog.minimumWidth,
                Math.min(Math.floor((available_width - 12) * 0.58),
                    available_width - translator_dialog.minimumWidth - 12)))
            : Math.min(1120, available_width)
        const translator_width = adjacent
            ? Math.min(760, available_width - notebook_width - 12)
            : Math.min(760, available_width)
        const pair_width = notebook_width + translator_width + 12
        if (!notebook_dialog.user_geometry) {
            notebook_dialog.width = notebook_width
            notebook_dialog.height = Math.min(860, available_height)
            notebook_dialog.x = adjacent ? (width - pair_width) / 2 : (width - notebook_dialog.width) / 2
            notebook_dialog.y = (height - notebook_dialog.height) / 2
        }
        if (!translator_dialog.user_geometry) {
            translator_dialog.width = translator_width
            translator_dialog.height = Math.min(600, available_height)
            translator_dialog.x = adjacent ? notebook_dialog.x + notebook_width + 12
                : (width - translator_dialog.width) / 2
            translator_dialog.y = (height - translator_dialog.height) / 2
        }
        notebook_dialog.constrain_geometry()
        translator_dialog.constrain_geometry()
    }

    function activate_dialog(dialog) {
        dialog.z = ++dialog_z
        clearHover()
        languagePopup.close()
    }

    function focus_dialog() {
        if (context_selection.active) {
            context_selection.forceActiveFocus()
        } else if (translator_dialog.visible && (!notebook_dialog.visible || translator_dialog.z > notebook_dialog.z)) {
            translator_dialog.forceActiveFocus()
        } else if (notebook_dialog.visible) {
            notebook_dialog.forceActiveFocus()
        } else {
            root.forceActiveFocus()
        }
    }

    function show_notebook_word(word, context) {
        notebook_dialog.open_word(word, context)
        notebook_dialog.open()
        arrange_dialogs()
    }

    function show_notebook() {
        notebook_dialog.open_vocabulary()
        notebook_dialog.open()
        arrange_dialogs()
    }

    function hide_notebook() {
        restore_notebook = false
        notebook_dialog.close()
    }

    function show_translator(text, source_language, field) {
        if (text.length > 0 || source_language.length > 0 || field !== "notes")
            translator_dialog.prepare(text, source_language, field)
        translator_dialog.open()
        arrange_dialogs()
    }

    function bounded(value: real, maximum: real): real {
        return Math.max(12, Math.min(value, Math.max(12, maximum)))
    }

    SystemPalette {
        id: systemPalette
        colorGroup: SystemPalette.Active
    }

    function clearHover() {
        hoveredWord = ""
    }


    function begin_context_selection() {
        if (!context_selection_available) {
            overlay.cancel_context_selection()
            return
        }
        selecting_context = true
        restore_notebook = notebook_dialog.visible
        restore_translator = translator_dialog.visible
        selection_focus_dialog = translator_dialog.activeFocus ? translator_dialog : notebook_dialog
        notebook_dialog.visible = false
        translator_dialog.visible = false
        root.clearHover()
        languagePopup.close()
        context_selection.begin()
    }

    function finish_context_selection(text, accepted) {
        if (!selecting_context) return
        selecting_context = false
        context_selection.cancel()
        if (accepted) notebook_dialog.apply_context(text)
        notebook_dialog.visible = restore_notebook
        translator_dialog.visible = restore_translator
        arrange_dialogs()
        if (selection_focus_dialog && selection_focus_dialog.visible)
            selection_focus_dialog.forceActiveFocus()
        else
            focus_dialog()
    }

    function languageLabel(code: string): string {
        switch (code) {
        case "eng": return qsTr("English")
        case "deu": return qsTr("German")
        case "ukr": return qsTr("Ukrainian")
        case "rus": return qsTr("Russian")
        default: return code
        }
    }

    Connections {
        target: overlay
        function onWordsChanged() {
            root.clearHover()
            context_selection.cancel()
        }
        function onImageChanged() {
            const was_selecting = root.selecting_context
            context_selection.cancel()
            if (!overlay.visible && !was_selecting) {
                notebook_dialog.visible = false
                translator_dialog.visible = false
            }
        }
        function onScanningChanged() {
            if (overlay.scanning) context_selection.cancel()
        }
        function onVisibleChanged() {
            root.clearHover()
            languagePopup.close()
            if (!overlay.visible) context_selection.cancel()
            if (overlay.visible) Qt.callLater(root.focus_dialog)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: systemPalette.window
    }

    Image {
        anchors.fill: parent
        source: overlay.imageSource
        cache: false
        fillMode: Image.Stretch
    }

    Rectangle {
        anchors.fill: parent
        color: "#20000000"
    }

    Repeater {
        model: overlay.words

        delegate: Rectangle {
            id: wordBox
            required property var modelData
            required property int index
            objectName: "word_outline_" + index
            x: modelData.x * root.scaleX
            y: modelData.y * root.scaleY
            width: modelData.width * root.scaleX
            height: modelData.height * root.scaleY
            color: hover.hovered && !context_selection.active && !root.dialogs_visible ? Qt.alpha(systemPalette.highlight, 0.25) : "transparent"
            border.color: Qt.alpha(systemPalette.highlight, hover.hovered ? 1 : 0.45)
            border.width: hover.hovered || highlightsButton.checked ? 1 : 0
            radius: 2

            TapHandler {
                enabled: !context_selection.active && !root.dialogs_visible
                onTapped: {
                    root.clearHover()
                    overlay.open_notebook_word(wordBox.modelData.text, wordBox.modelData.context || "")
                }
            }

            HoverHandler {
                id: hover
                enabled: !context_selection.active && !root.dialogs_visible
                onHoveredChanged: {
                    if (hovered) {
                        root.hoveredRect = Qt.rect(wordBox.x, wordBox.y, wordBox.width, wordBox.height)
                        root.hoveredWord = wordBox.modelData.text
                    } else if (root.hoveredWord === wordBox.modelData.text) {
                        root.clearHover()
                    }
                }
            }
        }
    }

    // Keep the popup in this layer-shell surface, without a native popup window.
    Rectangle {
        id: wordPopup
        objectName: "wordPopup"
        visible: root.hoveredWord.length > 0 && !context_selection.active && !root.dialogs_visible
        z: 1
        width: Math.min(root.width, 360, popupText.implicitWidth + 24)
        height: popupText.implicitHeight + 20
        x: Math.max(0, Math.min(root.hoveredRect.x, root.width - width))
        y: root.hoveredRect.y + root.hoveredRect.height + height + 8 <= root.height
            ? root.hoveredRect.y + root.hoveredRect.height + 8
            : Math.max(0, root.hoveredRect.y - height - 8)
        color: systemPalette.window
        border.color: systemPalette.mid
        radius: 6

        Text {
            id: popupText
            objectName: "popupText"
            x: 12
            y: 10
            width: parent.width - 24
            text: root.hoveredWord
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: systemPalette.windowText
            font.pixelSize: 18
        }
    }

    Rectangle {
        id: toolbar
        objectName: "toolbar"
        visible: !context_selection.active && !root.dialogs_visible
        z: 2
        property bool moved: false
        property real freeX: 0
        property real freeY: 0
        x: root.bounded(moved ? freeX : (root.width - width) / 2, root.width - width - 12)
        y: root.bounded(moved ? freeY : root.height - height - 24, root.height - height - 12)
        width: Math.max(0, Math.min(760, root.width - 24))
        height: 60
        color: systemPalette.window
        border.color: Qt.alpha(systemPalette.windowText, 0.2)
        radius: 10

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 3
            anchors.bottomMargin: -3
            z: -1
            radius: parent.radius
            color: Qt.alpha(systemPalette.shadow, 0.2)
        }

        HoverHandler {
            onHoveredChanged: if (hovered) root.clearHover()
        }

        RowLayout {
            id: toolbarContent
            anchors.fill: parent
            anchors.margins: 10
            spacing: root.width < 640 ? 4 : 8

            Item {
                objectName: "toolbarDragHandle"
                Layout.preferredWidth: root.width < 640 ? 18 : 26
                Layout.fillHeight: true
                Accessible.name: qsTr("Move toolbar")
                ToolTip.visible: dragArea.containsMouse && !dragArea.pressed
                ToolTip.text: qsTr("Drag to move · Double-click to reset")

                Grid {
                    anchors.centerIn: parent
                    columns: 2
                    spacing: 4
                    Repeater {
                        model: 6
                        Rectangle {
                            width: 3
                            height: 3
                            radius: 1.5
                            color: systemPalette.windowText
                            opacity: 0.55
                        }
                    }
                }

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    property point pressPosition
                    property point startPosition
                    onPressed: function(mouse) {
                        languagePopup.close()
                        pressPosition = mapToItem(root, mouse.x, mouse.y)
                        startPosition = Qt.point(toolbar.x, toolbar.y)
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed)
                            return
                        const position = mapToItem(root, mouse.x, mouse.y)
                        toolbar.freeX = startPosition.x + position.x - pressPosition.x
                        toolbar.freeY = startPosition.y + position.y - pressPosition.y
                        toolbar.moved = true
                    }
                    onDoubleClicked: toolbar.moved = false
                }
            }

            ToolSeparator { Layout.fillHeight: true }

            NotebookButton {
                id: languageSelector
                objectName: "languageSelector"
                enabled: !overlay.scanning && overlay.languages.length > 0
                quiet: true
                Layout.preferredWidth: root.width < 640 ? 100 : 160
                text: root.selectedLanguages.length === 1
                    ? root.languageLabel(root.selectedLanguages[0]) + " ▾"
                    : qsTr("%1 OCR languages ▾").arg(root.selectedLanguages.length)
                onClicked: languagePopup.visible ? languagePopup.close() : languagePopup.open()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Choose OCR languages")
                Accessible.name: qsTr("Choose OCR languages")
            }

            ToolSeparator { Layout.fillHeight: true }

            BusyIndicator {
                visible: overlay.scanning
                running: overlay.scanning
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
            }

            Label {
                visible: root.width >= 640
                text: overlay.scanning ? qsTr("Recognizing…")
                    : overlay.error.length > 0 ? qsTr("OCR failed: %1").arg(overlay.error)
                    : overlay.words.length === 0 ? qsTr("No words found")
                    : qsTr("%1 words · Click to learn").arg(overlay.words.length)
                color: systemPalette.windowText
                opacity: 0.8
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
            }

            CheckBox {
                id: highlightsButton
                objectName: "word_outlines"
                text: root.width < 640 ? qsTr("Boxes") : qsTr("Word outlines")
                checked: true
                palette.windowText: systemPalette.windowText
                palette.buttonText: systemPalette.buttonText
                palette.text: systemPalette.text
                palette.highlight: systemPalette.highlight
                palette.base: systemPalette.base
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Toggle word outlines")
            }

            ToolSeparator { Layout.fillHeight: true }

            NotebookButton {
                objectName: "open_vocabulary"
                text: qsTr("Notebook")
                primary: true
                Layout.fillWidth: root.width < 640
                onClicked: overlay.open_notebook()
            }

            NotebookButton {
                objectName: "hide_overlay"
                text: qsTr("Hide")
                quiet: true
                Accessible.name: qsTr("Hide overlay")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Hide overlay · Esc\nThe app keeps running in the tray.")
                onClicked: overlay.hide_requested()
            }
        }
    }

    Popup {
        id: languagePopup
        objectName: "languagePopup"
        parent: root
        property var draft_languages: []
        width: Math.max(0, Math.min(320, root.width - 24))
        height: Math.min(implicitHeight, root.height - 24)
        x: root.bounded(toolbar.x + 44, root.width - width - 12)
        y: toolbar.y >= height + 20 ? toolbar.y - height - 8
            : root.bounded(toolbar.y + toolbar.height + 8, root.height - height - 12)
        padding: 16
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onAboutToShow: {
            draft_languages = root.selectedLanguages.slice()
            root.clearHover()
        }

        function toggle_language(code: string, selected: bool) {
            let draft = draft_languages.slice()
            const index = draft.indexOf(code)
            if (selected && index < 0)
                draft.push(code)
            else if (!selected && index >= 0)
                draft.splice(index, 1)
            draft_languages = draft
        }

        background: Rectangle {
            color: systemPalette.window
            border.color: Qt.alpha(systemPalette.windowText, 0.2)
            radius: 8
        }

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                text: qsTr("Recognition languages")
                font.bold: true
                color: systemPalette.windowText
            }

            Label {
                text: qsTr("Select any installed languages. Choose at least one before applying.")
                color: systemPalette.windowText
                opacity: 0.7
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ScrollView {
                Layout.fillWidth: true
                implicitHeight: Math.min(240, languageChoices.implicitHeight)
                Layout.preferredHeight: implicitHeight
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                Column {
                    id: languageChoices
                    width: parent.width

                    Repeater {
                        model: overlay.languages
                        delegate: CheckDelegate {
                            required property string modelData
                            objectName: "languageChoice_" + modelData
                            width: languageChoices.width
                            text: root.languageLabel(modelData)
                            autoExclusive: false
                            checked: languagePopup.draft_languages.indexOf(modelData) >= 0
                            enabled: !overlay.scanning
                            onClicked: languagePopup.toggle_language(modelData, checked)
                        }
                    }
                }
            }

            ToolSeparator {
                orientation: Qt.Horizontal
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                NotebookButton {
                    objectName: "cancelLanguages"
                    text: qsTr("Cancel")
                    onClicked: languagePopup.close()
                }
                Item { Layout.fillWidth: true }
                NotebookButton {
                    objectName: "applyLanguages"
                    text: qsTr("Apply")
                    primary: true
                    enabled: !overlay.scanning && languagePopup.draft_languages.length > 0
                    onClicked: {
                        if (languagePopup.draft_languages.length === 0) return
                        overlay.selectedLanguage = languagePopup.draft_languages.join("+")
                        languagePopup.close()
                    }
                }
            }
        }
    }

    NotebookDialog {
        id: notebook_dialog
        z: 10
        onActivated: root.activate_dialog(notebook_dialog)
        onClosed: root.focus_dialog()
    }

    TranslatorDialog {
        id: translator_dialog
        z: 11
        onActivated: root.activate_dialog(translator_dialog)
        onClosed: root.focus_dialog()
    }


    ContextSelection {
        id: context_selection
        z: 100000
        anchors.fill: parent
        words: overlay.words
        scale_x: root.scaleX
        scale_y: root.scaleY
        onAccepted: function(text) { overlay.complete_context_selection(text) }
        onCancelled: overlay.cancel_context_selection()
    }
}
