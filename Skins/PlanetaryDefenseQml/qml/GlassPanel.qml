import QtQuick

// panel with a thin edge and an outer glow made of three transparent rings
Rectangle {
    id: p
    property color edge: Theme.accent
    property bool glow: true
    color: Theme.glass
    radius: 12
    border.color: Qt.rgba(edge.r, edge.g, edge.b, 0.85)
    border.width: 1.5
    Repeater {
        model: p.glow ? 3 : 0
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.margins: -(index + 1) * 2
            radius: p.radius + (index + 1) * 2
            color: "transparent"
            border.width: 2
            border.color: Qt.rgba(p.edge.r, p.edge.g, p.edge.b, [0.18, 0.09, 0.04][index])
        }
    }
}
