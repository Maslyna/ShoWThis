import QtQuick
import QtQuick.Controls

SearchableComboBox {
    id: picker
    required property var options
    property string language_code
    property bool compact: false
    property bool motion_enabled: false
    property bool feedback_ready: false
    Component.onCompleted: feedback_ready = true
    onLanguage_codeChanged: if (feedback_ready) language_feedback.acknowledge()
    signal language_selected(string code)
    model: options || []
    textRole: "name"
    valueRole: "code"
    currentIndex: (options || []).findIndex(function(item) { return item.code === picker.language_code })

    function selection_label() {
        const option = (options || []).find(function(item) { return item.code === picker.language_code })
        return compact ? language_code.toUpperCase() : option ? option.name : language_code
    }

    function accept_custom_value(input) {
        // ISO language codes may carry script/region subtags, e.g. zh-Hant or pt-BR.
        if (!/^[a-z]{2,3}(?:-[a-z0-9]{2,8})*$/i.test(input))
            return false
        language_selected(input)
        return true
    }

    onActivated: function(index) {
        if (index >= 0 && index < count)
            language_selected(String(valueAt(index)))
    }
    FeedbackTint {
        id: language_feedback
        objectName: "language_feedback"
        tint: picker.palette.highlight
        motion_enabled: picker.motion_enabled
        z: -0.5
        radius: 2
    }
    ToolTip.visible: hovered
    ToolTip.text: qsTr("Choose a language or enter its code, e.g. fr, es, ja, pt-BR. Availability depends on the dictionary or translation service.")
}
