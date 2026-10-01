import QtQuick
import Orbiter.Launcher 1.0
import "Orbits.js" as Orbits

// time to Apophis's closest approach (JPL CNEOS CAD), from the system clock; frozen while the Launchpad is inactive
GlassPanel {
    id: cd
    property bool compact: false
    property bool narrow: false
    property var t: Orbits.countdown(Date.now())
    function tick() { t = Orbits.countdown(Date.now()); }
    height: compact ? 128 : 206
    Connections { target: Launcher; function onActiveChanged() { cd.tick(); } }
    Timer { interval: 100; repeat: true; running: Launcher.active && cd.visible; onTriggered: cd.tick() } // tenths: each update redraws the whole view
    Component.onCompleted: tick()

    Column {
        x: 16; y: 12
        width: parent.width - 32
        spacing: cd.compact ? 8 : 10
        PanelTitle { text: cd.t.past ? "SINCE APOPHIS CLOSEST APPROACH" : "APOPHIS FLYBY"; dot: Theme.ok; info: cd.t.past ? "T+" : "T−" }
        Row {
            id: tiles
            spacing: 6
            readonly property real tileW: (parent.width - 4 * spacing) / 5
            Repeater {
                model: [["DAYS", "days", 2], ["HOURS", "hours", 2], ["MINUTES", "minutes", 2], ["SECONDS", "seconds", 2], ["MS", "ms", 3]]
                Rectangle {
                    width: tiles.tileW; height: cd.compact ? 48 : 58
                    radius: 7
                    color: Qt.rgba(0.0, 0.05, 0.03, 0.9)
                    border.color: Qt.rgba(0.153, 0.827, 1, 0.45)
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: cd.compact ? 3 : 5
                        text: Orbits.pad(cd.t[modelData[1]], modelData[2])
                        color: Theme.ok
                        font.family: Theme.mono; font.pixelSize: cd.compact ? 22 : 26; font.bold: true
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom; anchors.bottomMargin: cd.compact ? 3 : 5
                        text: modelData[0]
                        color: Theme.textDim
                        font.family: Theme.font; font.pixelSize: 9; font.weight: Font.Bold; font.letterSpacing: 1.2
                    }
                }
            }
        }
        Text {
            width: parent.width; elide: Text.ElideRight
            text: cd.compact ? "13 APR 2029 21:45 UTC · 38,011 KM · NO IMPACT RISK" : (cd.narrow ? "APOPHIS · CLOSEST 13 APR 2029 21:45 UTC" : "99942 APOPHIS · CLOSEST APPROACH 13 APR 2029 21:45 UTC")
            color: Theme.text; font.family: Theme.mono; font.pixelSize: cd.compact ? 10 : 11
        }
        Column {
            visible: !cd.compact
            width: parent.width
            spacing: 4
            Text { width: parent.width; elide: Text.ElideRight; text: "38,011 KM FROM EARTH'S CENTRE · 7.42 KM/S"; color: Theme.textDim; font.family: Theme.mono; font.pixelSize: 11 }
            Text { width: parent.width; elide: Text.ElideRight; text: "NO IMPACT RISK · OFF JPL SENTRY RISK LIST 2021"; color: Theme.ok; font.family: Theme.mono; font.pixelSize: 11 }
            Text { width: parent.width; elide: Text.ElideRight; text: "JPL CNEOS CAD ORBIT 220 · ±<1 MIN (3σ)"; color: Theme.textFaint; font.family: Theme.mono; font.pixelSize: 10 }
        }
    }
}
