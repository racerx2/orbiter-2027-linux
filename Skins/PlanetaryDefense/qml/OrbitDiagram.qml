import QtQuick
import Orbiter.Launcher 1.0
import "Orbits.js" as Orbits

// top view of the inner solar system on the ecliptic (north up) at the scenario's date, with three near-Earth asteroids
GlassPanel {
    id: od
    property real baseMjd: 0
    property bool fromScenario: false
    property string focusBody: ""
    property string system: ""
    property bool flow: false
    property real offset: 0
    readonly property real mjd: baseMjd + offset
    readonly property var planetColors: ({ Mercury: "#b5ab9c", Venus: "#e8c27a", Earth: "#27d3ff", Mars: "#ff7a4a" })
    readonly property var neoColors: ({ Apophis: "#ffc23d", Bennu: "#ff9d3d", Didymos: "#39ffc0" })
    readonly property string focusPlanet: {
        var b = focusBody;
        if (b === "Moon") return "Earth";
        if (b === "Phobos" || b === "Deimos") return "Mars";
        return Orbits.planetNames.indexOf(b) >= 0 || b === "Sun" ? b : "";
    }
    readonly property string setKey: {
        var k = "";
        for (var i = 0; i < Orbits.neos.length; i++) {
            var r = Orbits.neoSet(Orbits.neos[i].name, mjd);
            k += r.set.epoch + (r.inside ? "+" : "-") + ";";
        }
        return k;
    }
    readonly property var hidden: {
        var out = [];
        for (var i = 0; i < Orbits.neos.length; i++) if (!Orbits.neoSet(Orbits.neos[i].name, mjd).inside) out.push(Orbits.neos[i].name.toUpperCase());
        return out;
    }
    readonly property string note: {
        var n = [];
        if (focusBody !== "" && focusPlanet === "") n.push("FOCUS " + focusBody.toUpperCase() + " IS OUTSIDE THIS VIEW");
        if (system !== "" && system !== "Sol") n.push("DIAGRAM SHOWS SOL");
        if (hidden.length) n.push(hidden.join(", ") + " NOT SHOWN: THE JPL ELEMENT SETS COVER " + Orbits.NEO_YEARS);
        if (mjd < Orbits.MJD_MIN || mjd > Orbits.MJD_MAX) n.push("EPOCH OUTSIDE 1800–2050: PLANETS APPROXIMATE");
        return n.join(" · ");
    }
    function scale() { return Math.min(view.width, view.height) / 2 / (2.272 * 1.04); }
    function sx(p) { return view.width / 2 + p.x * scale(); }
    function sy(p) { return view.height / 2 - p.y * scale(); }

    onBaseMjdChanged: { offset = 0; flowTimer.startMs = Date.now(); flowTimer.startOffset = 0; orbits.requestPaint(); }
    onSetKeyChanged: orbits.requestPaint()

    // time flow: 10 days per second of wall time while on and the Launchpad is active; stops at 2050
    Timer {
        id: flowTimer
        property real startMs: 0
        property real startOffset: 0
        interval: 33; repeat: true
        running: od.flow && Launcher.active && od.visible
        onRunningChanged: if (running) { startMs = Date.now(); startOffset = od.offset; }
        onTriggered: {
            var o = startOffset + (Date.now() - startMs) / 1000 * 10;
            if (od.baseMjd + o >= Orbits.MJD_MAX) { o = Math.max(0, Orbits.MJD_MAX - od.baseMjd); od.flow = false; }
            od.offset = o;
        }
    }

    PanelTitle {
        id: title
        x: 16; y: 12; width: parent.width - 32
        text: "ORBITAL DIAGRAM"
        dot: Theme.accent
        info: od.offset > 0 ? "+" + Math.floor(od.offset) + " D · " + Orbits.epochText(od.mjd) : "EPOCH " + Orbits.epochText(od.mjd) + (od.fromScenario ? " · SCENARIO" : " · NOW")
    }

    Item {
        id: view
        x: 12; y: 44
        width: parent.width - 24; height: parent.height - 44 - foot.height - 14
        clip: true
        onWidthChanged: orbits.requestPaint()
        onHeightChanged: orbits.requestPaint()

        Canvas {
            id: orbits
            anchors.fill: parent
            onPaint: {
                var g = getContext("2d"), cx = width / 2, cy = height / 2, s = od.scale(), i, k, pts;
                g.reset();
                g.strokeStyle = "rgba(39,211,255,0.10)"; g.lineWidth = 1;
                for (k = 1; k <= 2; k++) { g.beginPath(); g.arc(cx, cy, k * s, 0, Math.PI * 2); g.stroke(); }
                g.fillStyle = "rgba(143,180,200,0.55)"; g.font = "10px monospace";
                for (k = 1; k <= 2; k++) g.fillText(k + " AU", cx - k * s * 0.7071 - 34, cy + k * s * 0.7071 + 12);
                for (i = 0; i < Orbits.planetNames.length; i++) {
                    var n = Orbits.planetNames[i];
                    pts = Orbits.planetPath(n, od.baseMjd, 180);
                    g.strokeStyle = od.planetColors[n]; g.globalAlpha = 0.55; g.lineWidth = 1.2;
                    g.beginPath();
                    for (k = 0; k < pts.length; k++) { var px = cx + pts[k].x * s, py = cy - pts[k].y * s; if (k) g.lineTo(px, py); else g.moveTo(px, py); }
                    g.stroke();
                }
                for (i = 0; i < Orbits.neos.length; i++) {
                    var nn = Orbits.neos[i].name, r = Orbits.neoSet(nn, od.mjd);
                    pts = Orbits.neoPath(nn, od.mjd, 180);
                    g.strokeStyle = od.neoColors[nn]; g.globalAlpha = r.inside ? 0.8 : 0.22; g.lineWidth = 1;
                    g.setLineDash([4, 4]);
                    g.beginPath();
                    for (k = 0; k < pts.length; k++) { var qx = cx + pts[k].x * s, qy = cy - pts[k].y * s; if (k) g.lineTo(qx, qy); else g.moveTo(qx, qy); }
                    g.stroke();
                    g.setLineDash([]);
                }
                g.globalAlpha = 1;
                var sun = g.createRadialGradient(cx, cy, 0, cx, cy, 22);
                sun.addColorStop(0, "rgba(255,240,200,1)"); sun.addColorStop(0.25, "rgba(255,200,90,0.8)"); sun.addColorStop(1, "rgba(255,160,40,0)");
                g.fillStyle = sun; g.fillRect(cx - 22, cy - 22, 44, 44);
            }
        }

        Repeater {
            model: Orbits.planetNames
            Item {
                readonly property var p: Orbits.planet(modelData, od.mjd)
                x: od.sx(p); y: od.sy(p)
                Rectangle { x: -4.5; y: -4.5; width: 9; height: 9; radius: 4.5; color: od.planetColors[modelData] }
                Rectangle {
                    visible: od.focusPlanet === modelData
                    x: -11; y: -11; width: 22; height: 22; radius: 11
                    color: "transparent"; border.width: 2; border.color: Theme.ok
                }
                Text { x: 9; y: -16; text: modelData.toUpperCase(); color: od.planetColors[modelData]; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
            }
        }
        Rectangle {
            visible: od.focusPlanet === "Sun"
            x: view.width / 2 - 14; y: view.height / 2 - 14; width: 28; height: 28; radius: 14
            color: "transparent"; border.width: 2; border.color: Theme.ok
        }
        Repeater {
            model: Orbits.neos
            Item {
                readonly property var p: Orbits.neo(modelData.name, od.mjd)
                visible: p !== null
                x: p ? od.sx(p) : 0; y: p ? od.sy(p) : 0
                Rectangle { x: -3.5; y: -3.5; width: 7; height: 7; rotation: 45; color: od.neoColors[modelData.name] }
                Text { x: 8; y: 2; text: modelData.name.toUpperCase(); color: od.neoColors[modelData.name]; font.family: Theme.mono; font.pixelSize: 10 }
            }
        }
    }

    Column {
        id: foot
        x: 16; anchors.bottom: parent.bottom; anchors.bottomMargin: 12
        width: parent.width - 32
        spacing: 6
        Row {
            spacing: 12
            Toggle { anchors.verticalCenter: parent.verticalCenter; on: od.flow; onToggled: od.flow = !od.flow }
            Text { anchors.verticalCenter: parent.verticalCenter; text: "TIME FLOW · 10 DAYS / S"; color: Theme.textDim; font.family: Theme.mono; font.pixelSize: 11 }
            TextLink { anchors.verticalCenter: parent.verticalCenter; text: "RESET"; enabled: od.offset > 0; onClicked: { od.flow = false; od.offset = 0; } }
        }
        Text {
            width: parent.width; elide: Text.ElideRight
            text: "PLANETS: JPL APPROX. · NEOS: TWO-BODY, JPL ELEMENTS · NOT FOR NAVIGATION"
            color: Theme.textFaint; font.family: Theme.mono; font.pixelSize: 10
        }
        Text {
            visible: od.note !== ""
            width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            text: od.note
            color: Theme.warn; font.family: Theme.mono; font.pixelSize: 10
        }
    }
}
