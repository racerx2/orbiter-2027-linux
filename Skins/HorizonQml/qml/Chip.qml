import QtQuick

Rectangle {
    id: chip
    property string label
    property color dot: Theme.textDim
    height: 32
    width: row.implicitWidth + 26
    radius: 16
    color: Qt.rgba(1, 1, 1, 0.07)
    border.color: Theme.line
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Rectangle { width: 7; height: 7; radius: 3.5; color: chip.dot; anchors.verticalCenter: parent.verticalCenter }
        Text { text: chip.label; color: Theme.text; font.family: Theme.font; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
    }
}
