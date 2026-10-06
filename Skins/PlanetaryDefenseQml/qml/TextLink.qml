import QtQuick

// small capitals link, e.g. "EDIT  →"
Text {
    id: l
    signal clicked()
    property color tint: Theme.accent
    color: !enabled ? Theme.textFaint : (ma.containsMouse ? Qt.lighter(l.tint, 1.25) : l.tint)
    font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 2
    MouseArea { id: ma; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: l.clicked() }
}
