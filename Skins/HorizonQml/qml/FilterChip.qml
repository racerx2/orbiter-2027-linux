import QtQuick

// selectable pill: filters and rail entries
Rectangle {
    id: c
    property string label
    property string count: ""
    property bool active: false
    property real maxWidth: 100000
    signal clicked()
    height: 34
    width: Math.min(lbl.implicitWidth + (cnt.visible ? cnt.implicitWidth + 8 : 0) + 28, maxWidth)
    radius: 17
    color: active ? Qt.rgba(0.96, 0.65, 0.14, 0.16) : (ma.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04))
    border.color: active ? Theme.accent : Theme.line
    Text {
        id: lbl
        x: 14; anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, c.width - 28 - (cnt.visible ? cnt.implicitWidth + 8 : 0))
        elide: Text.ElideRight
        text: c.label
        color: c.active ? Theme.text : Theme.textDim
        font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
    }
    Text {
        id: cnt
        visible: c.count !== ""
        x: lbl.x + lbl.width + 8; anchors.verticalCenter: parent.verticalCenter
        text: c.count
        color: Theme.textFaint
        font.family: Theme.font; font.pixelSize: 12
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: c.clicked() }
}
