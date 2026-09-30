import QtQuick
import Orbiter.Launcher 1.0

// mark, page tabs and help
Item {
    id: bar
    property string current: "PLAY"
    signal navigate(string tab)
    width: parent ? parent.width : 1400
    height: 80
    z: 50

    Row {
        id: brand
        x: app.margin; height: 80; spacing: 12
        OrbitMark { anchors.verticalCenter: parent.verticalCenter }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "ORBITER"; color: Theme.text
            font.family: Theme.font; font.pixelSize: 23; font.weight: Font.Bold; font.letterSpacing: 7
        }
    }
    Row {
        x: brand.x + brand.width + (app.width < 1300 ? 28 : 56); height: 80
        Repeater {
            model: [["PLAY", "PLAY"], ["SCENARIOS", "SCENARIOS"], ["ADD-ONS", "ADDONS"], ["SETTINGS", "SETTINGS"], ["ABOUT", "ABOUT"]]
            NavTab {
                label: modelData[0]
                active: bar.current === modelData[1]
                onClicked: bar.navigate(modelData[1])
            }
        }
    }
    Rectangle {
        anchors.right: parent.right; anchors.rightMargin: app.margin
        y: 22; width: 36; height: 36; radius: 18
        color: hm.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
        border.color: Theme.lineHi
        Text { anchors.centerIn: parent; text: "?"; color: Theme.text; font.family: Theme.font; font.pixelSize: 17; font.weight: Font.DemiBold }
        MouseArea { id: hm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Launcher.help(bar.current === "ADDONS" ? "modules" : "scenarios") }
    }
    Rectangle { x: app.margin; y: 80; width: parent.width - 2 * app.margin; height: 1; color: Theme.line }
}
