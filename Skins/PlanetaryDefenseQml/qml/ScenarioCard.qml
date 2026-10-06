import QtQuick

Item {
    id: card
    property string title
    property string folder
    property string meta
    property string kind
    property string when
    property bool selected: false
    property int seed: 0
    signal clicked()
    signal doubleClicked()
    width: 270; height: 212

    Rectangle {
        anchors.fill: bg; anchors.margins: -5
        radius: 17
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        opacity: card.selected ? 0.35 : 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
    }
    Rectangle {
        id: bg
        width: parent.width; height: parent.height
        y: ma.containsMouse ? -5 : 0
        Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        radius: 12
        color: ma.containsMouse ? Theme.glassHi : Theme.glass
        border.width: card.selected ? 2 : 1
        border.color: card.selected ? Theme.accent : (ma.containsMouse ? Theme.lineHi : Theme.line)
        MiniOrbit {
            x: 1; y: 1
            width: parent.width - 2; height: parent.height - 92
            kind: card.kind
            seed: card.seed
            corner: 11
        }
        Rectangle {
            visible: card.when !== ""
            x: parent.width - width - 10; y: 10
            height: 22; width: tag.implicitWidth + 16; radius: 11
            color: Qt.rgba(0.02, 0.04, 0.08, 0.7)
            Text {
                id: tag
                anchors.centerIn: parent
                text: card.when
                color: card.when === "CONTINUE" ? Theme.accentHi : Theme.text
                font.family: Theme.font; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1.6
            }
        }
        Column {
            x: 16; y: parent.height - 78
            width: parent.width - 32
            spacing: 5
            Text { text: card.folder.toUpperCase(); color: Theme.textFaint; font.family: Theme.font; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.8 }
            Text { text: card.title; width: parent.width; elide: Text.ElideRight; color: Theme.text; font.family: Theme.font; font.pixelSize: 16; font.weight: Font.DemiBold }
            Text { text: card.meta; width: parent.width; elide: Text.ElideRight; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 13 }
        }
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: card.clicked(); onDoubleClicked: card.doubleClicked() }
}
