import QtQuick

Item {
    id: tab
    property string label
    property bool active: false
    signal clicked()
    width: txt.implicitWidth + 36
    height: 80
    Text {
        id: txt
        anchors.centerIn: parent
        text: tab.label
        color: Theme.text
        opacity: tab.active ? 1 : (ma.containsMouse ? 0.85 : 0.55)
        font.family: Theme.font; font.pixelSize: 14; font.weight: Font.DemiBold; font.letterSpacing: 2.4
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 18
        height: 3; radius: 1.5
        width: tab.active ? txt.implicitWidth : 0
        color: Theme.accent
        Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: tab.clicked() }
}
