import QtQuick

Rectangle {
    id: feedback
    required property color tint
    property bool motion_enabled: false
    anchors.fill: parent
    color: tint
    opacity: 0
    enabled: false

    function acknowledge() {
        if (motion_enabled && visible) settle.restart()
        else reset()
    }
    function reset() {
        settle.stop()
        opacity = 0
    }
    onMotion_enabledChanged: if (!motion_enabled) reset()
    onVisibleChanged: if (!visible) reset()

    NumberAnimation {
        id: settle
        target: feedback
        property: "opacity"
        from: 0.12
        to: 0
        duration: 140
        easing.type: Easing.OutCubic
    }
}
