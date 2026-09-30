import QtQuick

// selectable pill: filters and rail entries
Rectangle {
    id: c
    property string label
    property string count: ""
    property bool active: false
    signal clicked()
    height: 34
    width: row.implicitWidth + 28
    radius: 17
    color: active ? Qt.rgba(0.96, 0.65, 0.14, 0.16) : (ma.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04))
    border.color: active ? Theme.accent : Theme.line
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Text { text: c.label; color: c.active ? Theme.text : Theme.textDim; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
        Text { visible: c.count !== ""; text: c.count; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: c.clicked() }
}
