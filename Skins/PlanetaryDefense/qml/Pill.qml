import QtQuick

// status pill: dot and monospace text
Rectangle {
    id: p
    property string label
    property color dot: Theme.ok
    property real maxWidth: 260
    height: 26
    width: Math.min(row.implicitWidth + 24, maxWidth)
    radius: 13
    color: Qt.rgba(0.02, 0.06, 0.11, 0.85)
    border.color: Theme.line
    Row {
        id: row
        x: 12; anchors.verticalCenter: parent.verticalCenter
        spacing: 7
        Rectangle { width: 8; height: 8; radius: 4; color: p.dot; anchors.verticalCenter: parent.verticalCenter }
        Text {
            width: Math.min(implicitWidth, p.maxWidth - 39)
            elide: Text.ElideRight
            text: p.label
            color: Theme.text
            font.family: Theme.mono; font.pixelSize: 11
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
