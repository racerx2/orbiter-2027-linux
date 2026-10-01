import QtQuick
import Orbiter.Launcher 1.0
import "Orbits.js" as Orbits

// about the skin, planetary defense in brief, the data sources and links
Item {
    id: page
    focus: true
    readonly property var me: {
        var l = Launcher.skins;
        for (var i = 0; i < l.length; i++) if (l[i].id === Launcher.skin) return l[i];
        return { name: Launcher.skin, version: "", author: "" };
    }
    readonly property var concepts: [
        ["RECONNAISSANCE", "A spacecraft flies to the object to measure its size, mass, spin and orbit before anything else is tried."],
        ["KINETIC IMPACTOR", "A spacecraft hits the object at high speed to change its velocity. Flown: NASA's DART hit Dimorphos on 26 Sep 2022 and shortened its orbit around Didymos by about 33 minutes."],
        ["MULTIPLE KINETIC IMPACTORS", "Several impacts, for a larger change or as a backup."],
        ["GRAVITY TRACTOR", "A spacecraft hovers next to the object for years and pulls it slowly with its own gravity."],
        ["ION BEAM", "A spacecraft points an ion engine's exhaust at the object to push it, with a second engine holding the spacecraft in place."],
        ["NUCLEAR STANDOFF", "A detonation near the surface heats one side so that the escaping material pushes the object."],
        ["NUCLEAR DISRUPTION", "Breaking the object apart: a last resort when warning time is short."],
        ["CIVIL DEFENSE", "Warning and evacuation of the area at risk when deflection is not possible."]
    ]
    readonly property real colW: Math.floor((width - 2 * app.margin - 24) / 2)

    Flickable {
        anchors.fill: parent
        anchors.leftMargin: app.margin; anchors.rightMargin: app.margin; anchors.topMargin: 6; anchors.bottomMargin: 8
        contentHeight: Math.max(leftCol.implicitHeight, rightCol.implicitHeight) + 12
        clip: true

        Column {
            id: leftCol
            width: page.colW
            spacing: 16
            GlassPanel {
                width: parent.width; height: about.implicitHeight + 30
                Column {
                    id: about
                    x: 18; y: 14; width: parent.width - 36
                    spacing: 8
                    PanelTitle { text: "THIS SKIN"; dot: Theme.accent; info: page.me.name + (page.me.version ? " " + page.me.version : "") }
                    Text {
                        width: parent.width; wrapMode: Text.WordWrap
                        text: "A mission-control launcher for Orbiter in the look of the Planetary Defense Foundation website. Unofficial: not affiliated with or endorsed by the Planetary Defense Foundation. The emblem is the skin's own drawing."
                        color: Theme.textDim; font.family: Theme.font; font.pixelSize: 13; lineHeight: 1.25
                    }
                    Row {
                        spacing: 10
                        GhostButton { label: "PLANETARYDEFENSEFOUNDATION.COM"; height: 38; onClicked: Launcher.openUrl("https://www.planetarydefensefoundation.com/") }
                        GhostButton { label: "CREDITS"; height: 38; onClicked: Launcher.showClassic("about") }
                    }
                    Text { text: "ORBITER " + Launcher.version + " · LAUNCHER API " + Launcher.apiVersion; color: Theme.textFaint; font.family: Theme.mono; font.pixelSize: 11 }
                }
            }
            GlassPanel {
                width: parent.width; height: src.implicitHeight + 30
                Column {
                    id: src
                    x: 18; y: 14; width: parent.width - 36
                    spacing: 6
                    PanelTitle { text: "DATA SOURCES"; dot: Theme.ok }
                    Repeater {
                        model: [
                            "Planets: E. M. Standish, Keplerian elements for approximate positions of the major planets (JPL), valid 1800–2050.",
                            "Near-Earth asteroids: two-body orbits from JPL element sets (Horizons and SBDB), one for each decade from 2000; positions are shown from 1995 to 2036, Apophis after its 2029 flyby to 2039.",
                            "Apophis flyby: JPL CNEOS close-approach data, orbit 220. No impact risk: off the JPL Sentry risk list since 2021.",
                            "The diagram and the clock are pictures for the launcher, not for navigation or risk assessment."
                        ]
                        Text { width: src.width; wrapMode: Text.WordWrap; text: "·  " + modelData; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 13; lineHeight: 1.2 }
                    }
                    Row {
                        spacing: 10; topPadding: 6
                        GhostButton { label: "JPL CNEOS"; height: 34; onClicked: Launcher.openUrl("https://cneos.jpl.nasa.gov/") }
                        GhostButton { label: "JPL SBDB"; height: 34; onClicked: Launcher.openUrl("https://ssd.jpl.nasa.gov/tools/sbdb_lookup.html") }
                    }
                }
            }
            GlassPanel {
                width: parent.width; height: neoCol.implicitHeight + 30
                Column {
                    id: neoCol
                    x: 18; y: 14; width: parent.width - 36
                    spacing: 4
                    PanelTitle { text: "THE THREE ASTEROIDS"; dot: Theme.warn }
                    Repeater {
                        model: Orbits.neos
                        Text {
                            readonly property var cur: Orbits.neoSet(modelData.name, 2461200.5 - 2400000.5).set
                            readonly property var sets: modelData.sets
                            width: neoCol.width; elide: Text.ElideRight
                            text: modelData.name.toUpperCase() + "  a " + cur.a.toFixed(3) + " AU  e " + cur.e.toFixed(3) + "  i " + cur.i.toFixed(2) + "°  P " + Math.round(360 / cur.n) + " d  · " + sets.length + " JPL element sets, " + Orbits.dayText(sets[0].from - 2400000.5).slice(0, 4) + "–" + Orbits.dayText(sets[sets.length - 1].to - 2400000.5).slice(0, 4)
                            color: Theme.textDim; font.family: Theme.mono; font.pixelSize: 11
                        }
                    }
                }
            }
        }

        GlassPanel {
            x: page.colW + 24
            width: page.colW; height: rightCol.implicitHeight + 30
            edge: Theme.alert
            Column {
                id: rightCol
                x: 18; y: 14; width: parent.width - 36
                spacing: 10
                PanelTitle { text: "PLANETARY DEFENSE IN BRIEF"; dot: Theme.alert }
                Repeater {
                    model: page.concepts
                    Column {
                        width: rightCol.width
                        spacing: 2
                        Text { text: modelData[0]; color: Theme.accent; font.family: Theme.font; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 1.6 }
                        Text { width: parent.width; wrapMode: Text.WordWrap; text: modelData[1]; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 13; lineHeight: 1.2 }
                    }
                }
            }
        }
    }
}
