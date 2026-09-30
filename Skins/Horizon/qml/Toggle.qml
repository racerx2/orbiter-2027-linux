import QtQuick

// on/off switch; it shows 'on' and only asks for a change, the owner sets 'on'
Rectangle {
    id: t
    property bool on: false
    signal toggled()
    width: 46; height: 26; radius: 13
    opacity: enabled ? 1 : 0.4
    color: on ? Theme.accent : Qt.rgba(1, 1, 1, 0.12)
    Behavior on color { ColorAnimation { duration: 150 } }
    Rectangle {
        width: 20; height: 20; radius: 10; y: 3
        x: t.on ? t.width - width - 3 : 3
        color: t.on ? Theme.accentInk : Theme.textDim
        Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: t.toggled() }
}
