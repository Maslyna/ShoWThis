import QtQuick
import QtQuick.Controls

OverlayDialog {
    id: root
    objectName: "notebook_dialog"
    title: qsTr("Notebook")
    width: 1100
    height: 760
    minimumWidth: 460
    minimumHeight: 360

    function open_word(word, context) { panel.open_word(word, context) }
    function open_vocabulary() { panel.open_vocabulary() }
    function apply_context(text) { panel.apply_context(text) }
    function add_to_card(field, text, target_language) {
        return panel.apply_translator_text(field, text, target_language)
    }
    function sync_selection() { overlay.update_selected_card(panel.card_id, panel.selected_word) }

    Component.onCompleted: sync_selection()
    LearningPanel {
        id: panel
        anchors.fill: parent
        service: learning
        visible: true
        shortcuts_enabled: root.activeFocus
        context_selection_available: !overlay.scanning && overlay.imageSize.width > 0
            && overlay.imageSize.height > 0 && overlay.words.length > 0
        onContext_selection_requested: overlay.begin_context_selection()
        onTranslator_requested: function(text, field) { overlay.open_translator(text, learning.source_language, field) }
        onExport_requested: function(ids) { overlay.export_vocabulary(ids) }
        onCard_idChanged: root.sync_selection()
        onSelected_wordChanged: root.sync_selection()
    }
}
