import QtQuick
import Orbiter.Launcher 1.0
import "Format.js" as Format
import "Orbits.js" as Orbits

// home: the selected mission and the Apophis flyby clock, the orbital diagram, system status and the mission board
Item {
    id: page
    focus: true
    readonly property string path: Launcher.currentScenario
    readonly property var info: Launcher.scenarioInfo(path)
    readonly property bool isScn: Launcher.currentIsScenario
    readonly property bool fav: Launcher.favourites.indexOf(path) >= 0
    readonly property bool compact: width < 1340
    readonly property bool low: height < 690
    readonly property real gap: compact ? 14 : 18
    readonly property real leftW: compact ? 370 : 400
    readonly property real rightW: compact ? 310 : 340

    function overline() {
        if (!path) return "NO MISSION SELECTED";
        if (!isScn) return "FOLDER";
        if (path === "(Current state)") return "CONTINUE · LAST FLIGHT";
        if (Launcher.recent.length && Launcher.recent[0] === path) return "LAST LAUNCHED · " + (info.folder || "SCENARIOS").toUpperCase();
        return (info.folder || "SCENARIOS").toUpperCase();
    }
    function launch() { if (Launcher.canLaunch && isScn) Launcher.launch(path); }
    // the diagram's epoch: the scenario's date, or the time the selection changed when it has none
    property real nowMjd: Orbits.mjdOfMs(Date.now())
    readonly property bool hasDate: isScn && info.mjd !== undefined
    onPathChanged: nowMjd = Orbits.mjdOfMs(Date.now())

    Keys.onReturnPressed: launch()
    Keys.onEnterPressed: launch()
    Keys.onUpPressed: board.step(-1)
    Keys.onDownPressed: board.step(1)

    // left: selected mission over the countdown
    GlassPanel {
        id: mission
        x: app.margin; y: 6
        width: page.leftW; height: page.height - y - clock.height - page.gap - 8
        Item {
            anchors.fill: parent; anchors.margins: 2
            clip: true
            Column {
                x: 16; y: 12
                width: parent.width - 32
                spacing: page.low ? 8 : 11
                PanelTitle { text: page.overline(); dot: page.isScn ? Theme.accent : Theme.textFaint }
                Text {
                    width: parent.width
                    text: page.path ? (page.info.name || page.path) : "Pick a mission on the board or under MISSIONS"
                    wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                    color: Theme.text; font.family: Theme.font; font.pixelSize: page.low ? 20 : 24; font.weight: Font.Bold
                }
                Flow {
                    width: parent.width
                    height: Math.min(implicitHeight, page.low ? 32 : 72)
                    clip: true
                    spacing: 8
                    visible: page.isScn
                    Chip { label: Format.vessel(page.info); dot: Theme.accent; visible: label !== "" }
                    Chip { label: Format.place(page.info); dot: Theme.info; visible: label !== "" }
                    Chip { label: Format.date(page.info); dot: Theme.textDim; visible: label !== "" }
                }
                Text {
                    width: parent.width
                    text: Launcher.currentDescription
                    visible: text !== ""
                    wrapMode: Text.WordWrap; maximumLineCount: page.low ? 2 : 4; elide: Text.ElideRight
                    textFormat: Text.PlainText
                    color: Theme.textDim; font.family: Theme.font; font.pixelSize: 14; lineHeight: 1.25
                }
                Row {
                    spacing: 12
                    LaunchButton { enabled: Launcher.canLaunch && page.isScn; width: page.compact ? 220 : 250; height: page.low ? 46 : 52; onClicked: page.launch() }
                    GhostButton {
                        star: true; on: page.fav; enabled: page.isScn; height: page.low ? 46 : 52
                        onClicked: {
                            var was = page.fav, on = Launcher.toggleFavourite(page.path);
                            app.notice(on ? "Added to favourites" : (was ? "Removed from favourites" : "Favourites are full"));
                        }
                    }
                }
                Row {
                    spacing: 12
                    Toggle { anchors.verticalCenter: parent.verticalCenter; on: Launcher.startPaused; onToggled: Launcher.startPaused = !Launcher.startPaused }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "Start paused"; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 13 }
                    Text { visible: !page.low && page.isScn; anchors.verticalCenter: parent.verticalCenter; text: "·  Enter launches"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: 13 }
                }
            }
        }
    }
    Countdown {
        id: clock
        x: app.margin; y: page.height - height - 8
        width: page.leftW
        compact: page.low
        narrow: page.compact
    }

    // middle: orbital diagram
    OrbitDiagram {
        id: diagram
        x: mission.x + mission.width + page.gap; y: 6
        width: status.x - x - page.gap; height: page.height - y - 8
        baseMjd: page.hasDate ? page.info.mjd : page.nowMjd
        fromScenario: page.hasDate
        focusBody: page.isScn ? (page.info.focusBody || "") : ""
        system: page.isScn ? (page.info.system || "") : ""
    }

    // right: status over the mission board
    StatusPanel {
        id: status
        anchors.right: parent.right; anchors.rightMargin: app.margin
        y: 6
        width: page.rightW; height: implicitHeight
        rowHeight: page.low ? 28 : 32
    }
    MissionBoard {
        id: board
        anchors.right: parent.right; anchors.rightMargin: app.margin
        y: status.y + status.height + page.gap
        width: page.rightW
        height: implicitHeight
        maxRows: page.low ? 4 : 6
    }
}
