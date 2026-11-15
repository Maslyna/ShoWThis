import QtQuick
import QtQuick.Controls

SearchableComboBox {
    id: picker
    required property var options
    property string language_code
    property bool compact: false
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
    ToolTip.visible: hovered
    ToolTip.text: qsTr("Choose a language or enter its code, e.g. fr, es, ja, pt-BR. Availability depends on the dictionary or translation service.")
}
