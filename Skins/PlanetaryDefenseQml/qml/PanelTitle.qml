import QtQuick

// uppercase panel heading with a status dot, as on the site's panels
Item {
    id: t
    property string text
    property color dot: Theme.ok
    property string info: ""
    width: parent ? parent.width : 300
    height: 24
    Rectangle { width: 9; height: 9; radius: 4.5; color: t.dot; anchors.verticalCenter: parent.verticalCenter }
    Rectangle { width: 15; height: 15; radius: 7.5; x: -3; color: "transparent"; border.width: 1; border.color: Qt.rgba(t.dot.r, t.dot.g, t.dot.b, 0.35); anchors.verticalCenter: parent.verticalCenter }
    Text {
        x: 18; anchors.verticalCenter: parent.verticalCenter
        width: t.width - x - (r.visible ? r.implicitWidth + 12 : 0)
        elide: Text.ElideRight
        text: t.text
        color: Theme.text
        font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Bold; font.letterSpacing: 2.2
    }
    Text {
        id: r
        visible: t.info !== ""
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, t.width * 0.5)
        elide: Text.ElideRight
        text: t.info
        color: Theme.textDim
        font.family: Theme.mono; font.pixelSize: 11
    }
}
