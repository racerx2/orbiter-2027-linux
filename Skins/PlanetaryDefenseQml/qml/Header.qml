import QtQuick
import Orbiter.Launcher 1.0
import "Format.js" as Format

// emblem and title, status pills, nav buttons
Item {
    id: hd
    property string current: "CONTROL"
    signal navigate(string tab)
    width: parent ? parent.width : 1400
    height: 104
    z: 50
    readonly property var s: Launcher.setup
    readonly property int scnCount: {
        var n = 0, l = Launcher.scenarios;
        for (var i = 0; i < l.length; i++) if (!l[i].isFolder) n++;
        return n;
    }

    Row {
        id: brand
        x: app.margin; y: 8
        spacing: 12
        Emblem { width: 46; height: 46 }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            Text { text: "PLANETARY DEFENSE"; color: Theme.text; font.family: Theme.font; font.pixelSize: 21; font.weight: Font.Bold; font.letterSpacing: 4 }
            Text { text: "ORBITER MISSION CONTROL"; color: Theme.accent; font.family: Theme.font; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 3.4 }
        }
    }
    Flow {
        anchors.right: parent.right; anchors.rightMargin: app.margin
        y: 18
        width: Math.max(200, hd.width - brand.x - brand.width - 2 * app.margin - 20)
        layoutDirection: Qt.RightToLeft
        spacing: 8
        Pill { label: "API " + Launcher.apiVersion; dot: Theme.ok }
        Pill { label: "SCENARIOS " + hd.scnCount; dot: Theme.info }
        Pill { label: "MODULES " + (hd.s.activeModules || 0); dot: Theme.info }
        Pill {
            maxWidth: 300
            label: "GRAPHICS " + (Format.isConsole(hd.s) ? "CONSOLE" : hd.s.graphicsClient)
            dot: Format.isConsole(hd.s) ? Theme.alert : Theme.ok
        }
    }
    Row {
        x: app.margin; y: 62
        spacing: 10
        Repeater {
            model: [["MISSION CONTROL", "CONTROL"], ["MISSIONS", "MISSIONS"], ["SYSTEMS", "SYSTEMS"], ["SETTINGS", "SETTINGS"], ["DEFENSE", "DEFENSE"]]
            NavButton {
                label: modelData[0]
                active: hd.current === modelData[1]
                edge: modelData[1] === "DEFENSE" ? Theme.alert : Theme.accent
                onClicked: hd.navigate(modelData[1])
            }
        }
    }
}
