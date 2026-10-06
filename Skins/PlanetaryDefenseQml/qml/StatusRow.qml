import QtQuick

Item {
    id: row
    property string label
    property string value
    property color dot: Theme.ok
    property int rowHeight: 38
    width: parent ? parent.width : 300
    height: rowHeight
    Rectangle { width: 6; height: 6; radius: 3; color: row.dot; anchors.verticalCenter: parent.verticalCenter }
    Text {
        x: 18; anchors.verticalCenter: parent.verticalCenter
        text: row.label
        color: Theme.textDim
        font.family: Theme.font; font.pixelSize: 14
    }
    Text {
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, row.width - 130)
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
        text: row.value
        color: Theme.text
        font.family: Theme.font; font.pixelSize: 14; font.weight: Font.Medium
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
}
