import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

OverlayDialog {
    id: window
    objectName: "translator_dialog"
    title: qsTr("Translator")
    minimumWidth: 360
    minimumHeight: 400
    width: 760
    height: 600

    readonly property var translator: learning.translator
    readonly property bool wide_layout: width >= 640
    property bool updating_result: false
    property string feedback: ""
    property string configuration_error: ""
    property bool result_ready: false
    readonly property var card_fields: [
        { name: qsTr("Word translation"), value: "translation" },
        { name: qsTr("Context translation"), value: "context_translation" },
        { name: qsTr("Meaning"), value: "meaning" },
        { name: qsTr("Notes"), value: "notes" },
        { name: qsTr("Context"), value: "context" }
    ]
    SystemPalette { id: colors; colorGroup: SystemPalette.Active }
    SystemPalette { id: disabled_colors; colorGroup: SystemPalette.Disabled }

    function invalidate() {
        copy_acknowledgment.stop()
        result_tint.reset()
        result_ready = false
        translator.clear_session()
        updating_result = true
        result.text = ""
        updating_result = false
        feedback = ""
        configuration_error = ""
    }
    function prepare(text, source_language, field) {
        invalidate()
        source.text = text
        if (source_language.length > 0)
            overlay.translator_source_language = source_language
        const index = card_fields.findIndex(function(item) { return item.value === field })
        card_field.currentIndex = index >= 0 ? index : 3
        source.forceActiveFocus()
    }
    function translate() {
        if (!translate_button.enabled)
            return
        invalidate()
        translator.translate_session(source.text, overlay.translator_source_language,
                                     overlay.translator_target_language)
    }
    function append(field, text) {
        const language = field === "translation" || field === "context_translation"
            ? overlay.translator_target_language : ""
        feedback = overlay.add_to_selected_card(field, text, language)
            ? qsTr("Added to card draft. Save in the notebook to keep it.")
            : qsTr("Select a card in the notebook before adding text.")
    }
    onVisibleChanged: {
        if (!visible) {
            copy_acknowledgment.stop()
        }
    }
    Timer {
        id: copy_acknowledgment
        interval: 1200
    }
    onClosed: {
        setup.close()
        translator.clear_session()
    }
    Shortcut {
        sequence: "Ctrl+Return"
        enabled: window.visible && window.activeFocus
        onActivated: window.translate()
    }
    Shortcut {
        sequence: "Ctrl+Enter"
        enabled: window.visible && window.activeFocus
        onActivated: window.translate()
    }
    Shortcut {
        sequence: "Escape"
        enabled: window.visible && window.activeFocus
        onActivated: {
            if (setup.visible) setup.close()
            else window.close()
        }
    }
    Connections {
        target: overlay
        function onTranslator_languages_changed() { window.invalidate() }
        function onSelected_card_changed() { window.feedback = "" }
    }
    Connections {
        target: window.translator
        function onConfiguration_changed() { window.invalidate() }
        function onError_occurred(message) {
            if (setup.visible)
                window.configuration_error = message
        }
        function onSession_translated(text) {
            window.updating_result = true
            result.text = text
            window.updating_result = false
            copy_acknowledgment.stop()
            window.result_ready = true
            if (overlay.motion_enabled && window.visible)
                result_tint.acknowledge()
        }
    }

    ScrollView {
        id: page
        objectName: "translator_content_scroll"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: action_footer.top
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: page.availableWidth
            height: Math.max(implicitHeight, page.availableHeight)
            spacing: 16
            // Configuration and text scroll; request and card actions stay in view.
            Pane {
                id: translation_page
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: window.wide_layout ? 20 : 12
                palette.window: colors.window
                palette.windowText: colors.windowText
                palette.base: colors.base
                palette.alternateBase: colors.alternateBase
                palette.text: colors.text
                palette.button: colors.button
                palette.buttonText: colors.buttonText
                palette.highlight: colors.highlight
                palette.highlightedText: colors.highlightedText
                palette.mid: colors.mid
                palette.dark: colors.dark
                palette.light: colors.light
                palette.shadow: colors.shadow
                palette.placeholderText: Qt.alpha(colors.text, 0.5)
                palette.disabled.text: disabled_colors.text
                palette.disabled.buttonText: disabled_colors.buttonText
                background: Item {}
                ColumnLayout {
                    anchors.fill: parent
                    spacing: window.wide_layout ? 12 : 8
                    GridLayout {
                        Layout.fillWidth: true
                        columns: window.wide_layout ? 3 : 2
                        columnSpacing: 12
                        rowSpacing: 8
                        Label {
                            visible: window.wide_layout
                            text: qsTr("Translator")
                            font.pixelSize: 25
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            Layout.row: 0
                            Layout.column: 0
                        }
                        SearchableComboBox {
                            objectName: "translator_provider"
                            model: window.translator.providers
                            textRole: "name"
                            valueRole: "id"
                            currentIndex: window.translator.providers.findIndex(function(provider) {
                                return provider.id === window.translator.provider_id
                            })
                            onActivated: window.translator.provider_id = currentValue
                            Layout.row: 0
                            Layout.column: window.wide_layout ? 1 : 0
                            Layout.columnSpan: 1
                            Layout.preferredWidth: window.wide_layout ? 220 : -1
                            Layout.fillWidth: !window.wide_layout
                            Layout.minimumWidth: 0
                            Accessible.name: qsTr("Translation provider")
                        }
                        NotebookButton {
                            objectName: "translator_setup"
                            text: qsTr("Setup")
                            quiet: true
                            Layout.row: 0
                            Layout.column: window.wide_layout ? 2 : 1
                            onClicked: setup.open()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        NotebookLanguagePicker {
                            objectName: "translator_source_language"
                            options: learning.language_options
                            motion_enabled: overlay.motion_enabled
                            language_code: overlay.translator_source_language
                            onLanguage_selected: function(code) { overlay.translator_source_language = code }
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Accessible.name: qsTr("Source language")
                        }
                        NotebookButton {
                            objectName: "translator_swap"
                            text: "⇄"
                            Accessible.name: qsTr("Swap languages")
                            ToolTip.visible: hovered
                            ToolTip.text: Accessible.name
                            onClicked: {
                                const previous = overlay.translator_source_language
                                overlay.translator_source_language = overlay.translator_target_language
                                overlay.translator_target_language = previous
                            }
                        }
                        NotebookLanguagePicker {
                            objectName: "translator_target_language"
                            options: learning.language_options
                            motion_enabled: overlay.motion_enabled
                            language_code: overlay.translator_target_language
                            onLanguage_selected: function(code) { overlay.translator_target_language = code }
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Accessible.name: qsTr("Target language")
                        }
                    }
                    Label {
                        objectName: "translator_error"
                        visible: text.length > 0
                        text: window.translator.session_error
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: window.wide_layout ? 2 : 1
                        rowSpacing: 12
                        columnSpacing: 12
                        NotebookSurface {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            Layout.minimumHeight: window.wide_layout ? 210 : 180
                            padding: 14
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 8
                                Label { text: qsTr("Source text"); font.pixelSize: 12; font.weight: Font.DemiBold }
                                ScrollView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    TextArea {
                                        id: source
                                        objectName: "translator_source_text"
                                        placeholderText: qsTr("Enter text to translate")
                                        wrapMode: TextEdit.Wrap
                                        textFormat: TextEdit.PlainText
                                        selectByMouse: true
                                        font.pixelSize: 18
                                        background: Item {}
                                        padding: 0
                                        Accessible.name: qsTr("Source text")
                                        onTextChanged: window.invalidate()
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: qsTr("%1 characters").arg(source.length); opacity: 0.5; font.pixelSize: 11; Layout.fillWidth: true }
                                    NotebookButton {
                                        objectName: "translator_clear"
                                        text: qsTr("Clear")
                                        quiet: true
                                        enabled: source.length > 0 || result.length > 0
                                        onClicked: { source.clear(); window.invalidate(); source.forceActiveFocus() }
                                    }
                                }
                            }
                        }
                        NotebookSurface {
                            id: result_surface
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: 1
                            Layout.minimumHeight: window.wide_layout ? 210 : 180
                            padding: 14
                            FeedbackTint {
                                id: result_tint
                                objectName: "translation_result_feedback"
                                parent: result_surface.background
                                anchors.fill: parent
                                tint: colors.highlight
                                motion_enabled: overlay.motion_enabled
                                radius: 9
                            }
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 8
                                Label {
                                    objectName: "translator_result_heading"
                                    text: window.result_ready ? qsTr("Translation ready") : qsTr("Translation")
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                ScrollView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    TextArea {
                                        id: result
                                        objectName: "translator_result_text"
                                        placeholderText: window.translator.session_busy ? qsTr("Translating…") : qsTr("Translation appears here")
                                        wrapMode: TextEdit.Wrap
                                        textFormat: TextEdit.PlainText
                                        selectByMouse: true
                                        font.pixelSize: 18
                                        background: Item {}
                                        padding: 0
                                        Accessible.name: qsTr("Translation result")
                                        onTextChanged: {
                                            copy_acknowledgment.stop()
                                            window.result_ready = false
                                            result_tint.reset()
                                            if (!window.updating_result && window.translator.session_busy)
                                                window.translator.clear_session()
                                            window.feedback = ""
                                        }
                                    }
                                }
                                NotebookButton {
                                    id: copy_button
                                    objectName: "translator_copy"
                                    text: copy_acknowledgment.running ? qsTr("Copied") : qsTr("Copy")
                                    implicitWidth: Math.max(38, copy_metrics.advanceWidth(qsTr("Copy")) + 28,
                                                            copy_metrics.advanceWidth(qsTr("Copied")) + 28)
                                    quiet: true
                                    motion_enabled: overlay.motion_enabled
                                    FontMetrics { id: copy_metrics; font: copy_button.font }
                                    enabled: result.text.trim().length > 0
                                    Layout.alignment: Qt.AlignRight
                                    onClicked: {
                                        overlay.copy_text(result.text)
                                        copy_acknowledgment.restart()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Pane {
        id: action_footer
        objectName: "translator_actions"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        padding: window.wide_layout ? 20 : 12
        palette: translation_page.palette
        background: Rectangle { color: colors.window }
        ColumnLayout {
            width: parent.width
            spacing: window.wide_layout ? 12 : 8
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: !window.translator.provider_id.length ? qsTr("Choose a provider to enable translation.")
                        : !window.translator.configured ? qsTr("Open Setup to complete provider settings.")
                        : window.translator.session_busy ? qsTr("Translating…") : qsTr("Text is sent only when you press Translate.")
                    font.pixelSize: 11
                    opacity: 0.65
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                NotebookButton {
                    objectName: "translator_cancel"
                    text: qsTr("Cancel")
                    visible: window.translator.session_busy
                    onClicked: window.translator.clear_session()
                }
                NotebookButton {
                    id: translate_button
                    objectName: "translator_translate"
                    text: qsTr("Translate")
                    primary: true
                    enabled: !window.translator.session_busy && window.translator.configured
                        && source.text.trim().length > 0
                        && overlay.translator_source_language.length > 0
                        && overlay.translator_target_language.length > 0
                    onClicked: window.translate()
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Qt.alpha(colors.windowText, 0.12) }
            Label {
                objectName: "translator_selected_card"
                text: overlay.has_selected_card ? qsTr("Add to card: %1").arg(overlay.selected_card_word)
                    : qsTr("Select a notebook card to add text")
                textFormat: Text.PlainText
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                ToolTip.visible: card_hover.hovered
                ToolTip.text: text
                HoverHandler { id: card_hover }
            }
            GridLayout {
                columns: window.wide_layout ? 3 : 2
                Layout.fillWidth: true
                SearchableComboBox {
                    id: card_field
                    objectName: "translator_card_field"
                    model: window.card_fields
                    textRole: "name"
                    valueRole: "value"
                    currentIndex: 3
                    Layout.fillWidth: true
                    Layout.columnSpan: window.wide_layout ? 1 : 2
                    Layout.minimumWidth: 0
                    Accessible.name: qsTr("Card destination field")
                }
                NotebookButton {
                    objectName: "translator_add_result"
                    text: qsTr("Add result")
                    enabled: overlay.has_selected_card && result.text.trim().length > 0
                    Layout.fillWidth: !window.wide_layout
                    onClicked: window.append(card_field.currentValue, result.text)
                }
                NotebookButton {
                    objectName: "translator_add_source"
                    text: qsTr("Add source to context")
                    Layout.columnSpan: 1
                    Layout.fillWidth: !window.wide_layout
                    enabled: overlay.has_selected_card && source.text.trim().length > 0
                    onClicked: window.append("context", source.text)
                }
            }
            Label {
                objectName: "translator_feedback"
                text: window.feedback
                visible: text.length > 0
                textFormat: Text.PlainText
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    Popup {
        id: setup
        objectName: "translator_settings_popup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - 24, 620)
        height: Math.min(window.height - 24, settings.implicitHeight + 24)
        modal: false
        focus: true
        palette: translation_page.palette
        padding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        ColumnLayout {
            anchors.fill: parent
            Label {
                text: window.configuration_error
                visible: text.length > 0
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                NotebookTranslatorSettings {
                    id: settings
                    width: parent.width
                    translator: window.translator
                }
            }
        }
    }
}
