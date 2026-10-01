import QtQuick

// rounded nav button; the active one is filled blue
Rectangle {
    id: b
    property string label
    property bool active: false
    property color edge: Theme.accent
    signal clicked()
    height: 38
    width: txt.implicitWidth + 34
    radius: 10
    color: active ? Theme.blue : (ma.containsMouse ? Qt.rgba(0.153, 0.827, 1, 0.10) : Qt.rgba(0.02, 0.06, 0.11, 0.85))
    border.width: 1.5
    border.color: active ? Theme.blueHi : Qt.rgba(edge.r, edge.g, edge.b, ma.containsMouse ? 0.9 : 0.6)
    Behavior on color { ColorAnimation { duration: 140 } }
    Rectangle {
        z: -1
        anchors.fill: parent; anchors.margins: -3
        radius: b.radius + 3
        color: "transparent"
        border.width: 3
        border.color: Qt.rgba(b.edge.r, b.edge.g, b.edge.b, b.active || ma.containsMouse ? 0.22 : 0.08)
    }
    Text {
        id: txt
        anchors.centerIn: parent
        text: b.label
        color: b.active ? "#ffffff" : Theme.text
        font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Bold; font.letterSpacing: 1.6
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: b.clicked() }
}
