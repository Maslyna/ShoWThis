import QtQuick
import QtQuick.Controls

ComboBox {
    id: picker
    required property var options
    property string language_code
    property bool compact: false
    signal language_selected(string code)
    model: options
    textRole: "name"
    valueRole: "code"
    editable: true
    currentIndex: (options || []).findIndex(function(item) { return item.code === picker.language_code })

    function refresh_label() {
        const option = (options || []).find(function(item) { return item.code === picker.language_code })
        editText = compact ? language_code.toUpperCase() : option ? option.name : language_code
    }
    onLanguage_codeChanged: refresh_label()
    onCompactChanged: refresh_label()
    Component.onCompleted: refresh_label()
    onActivated: {
        language_selected(options[currentIndex].code)
        refresh_label()
    }
    onAccepted: {
        const input = editText.trim()
        const option = options.find(function(item) {
            return item.name.toLowerCase() === input.toLowerCase() || item.code === input.toLowerCase()
        })
        language_selected(option ? option.code : input)
        refresh_label()
    }
    ToolTip.visible: hovered
    ToolTip.text: qsTr("Choose a language or enter its code, e.g. fr, es, ja. Availability depends on the dictionary or translation service.")
}
