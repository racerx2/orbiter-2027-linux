import QtQuick
import Orbiter.Launcher 1.0
import "Orbits.js" as Orbits

// continue, recent and favourite scenarios, in the red style of the site's threat board
GlassPanel {
    id: mb
    property int maxRows: 6
    edge: Theme.alert
    color: Qt.rgba(0.09, 0.02, 0.04, 0.88)
    implicitHeight: col.implicitHeight + 26
    readonly property string path: Launcher.currentScenario
    readonly property var rows: {
        var out = [], seen = {};
        function add(p, tag) { if (p && !seen[p] && out.length < mb.maxRows) { seen[p] = true; out.push({ path: p, tag: tag }); } }
        var all = Launcher.scenarios;
        for (var i = 0; i < all.length; i++)
            if (!all[i].isFolder && all[i].path === "(Current state)") add(all[i].path, "CONTINUE");
        var r = Launcher.recent;
        for (i = 0; i < r.length; i++) add(r[i], "RECENT");
        var f = Launcher.favourites;
        for (i = 0; i < f.length; i++) add(f[i], "FAVOURITE");
        return out;
    }
    function step(d) {
        if (!rows.length) return;
        var i = -1;
        for (var k = 0; k < rows.length; k++) if (rows[k].path === path) i = k;
        i = i < 0 ? 0 : Math.max(0, Math.min(rows.length - 1, i + d));
        Launcher.currentScenario = rows[i].path;
    }

    Column {
        id: col
        x: 16; y: 12
        width: parent.width - 32
        spacing: 6
        PanelTitle { text: "MISSION BOARD"; dot: Theme.alert; info: mb.rows.length ? mb.rows.length + (mb.rows.length === 1 ? " ENTRY" : " ENTRIES") : "" }
        Text { text: "MISSION · EPOCH · BODY · STATUS"; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 1.4 }
        Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 0.21, 0.27, 0.35) }
        Repeater {
            model: mb.rows
            Rectangle {
                readonly property var ci: Launcher.scenarioInfo(modelData.path)
                readonly property bool sel: mb.path === modelData.path
                width: col.width; height: 40
                radius: 5
                color: sel ? Qt.rgba(1, 0.21, 0.27, 0.22) : (rm.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                border.color: sel ? Qt.rgba(1, 0.21, 0.27, 0.6) : "transparent"
                Text {
                    x: 8; y: 4; width: parent.width - 16 - tag.implicitWidth - 8; elide: Text.ElideRight
                    text: ci.name || modelData.path
                    color: Theme.text; font.family: Theme.mono; font.pixelSize: 12; font.bold: sel
                }
                Text {
                    id: tag
                    anchors.right: parent.right; anchors.rightMargin: 8; y: 5
                    text: modelData.tag
                    color: modelData.tag === "CONTINUE" ? Theme.accent : Theme.textFaint; font.family: Theme.font; font.pixelSize: 9; font.weight: Font.Bold; font.letterSpacing: 1.2
                }
                Text {
                    x: 8; y: 22; width: parent.width - 16; elide: Text.ElideRight
                    text: [ci.mjd !== undefined ? Orbits.dayText(ci.mjd) : "NOW", (ci.focusBody || ci.system || "—").toUpperCase(), ci.focusStatus === "Orbiting" ? "ORBIT" : (ci.focusStatus === "Landed" ? "LANDED" : "—")].join(" · ")
                    color: ci.focusStatus === "Landed" ? Theme.warn : (ci.focusStatus === "Orbiting" ? Theme.ok : Theme.textDim)
                    font.family: Theme.mono; font.pixelSize: 10
                }
                MouseArea {
                    id: rm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: Launcher.currentScenario = modelData.path
                    onDoubleClicked: Launcher.launch(modelData.path)
                }
            }
        }
        Text {
            visible: mb.rows.length === 0
            width: parent.width
            wrapMode: Text.WordWrap
            text: "Launched, favourite and continued missions show up here."
            color: Theme.textDim; font.family: Theme.font; font.pixelSize: 13
        }
    }
}
