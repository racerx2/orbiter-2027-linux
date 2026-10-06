import QtQuick
import Orbiter.Launcher 1.0
import "Format.js" as Format

// Settings: what the next flight uses, the classic settings pages, and the launcher skin
Item {
    id: page
    focus: true
    readonly property var s: Launcher.setup
    readonly property var skins: {
        var out = [{ id: "", name: "Classic", kind: "", author: "", description: "The original Launchpad, unchanged.", compatible: true, reason: "" }];
        var l = Launcher.skins;
        for (var i = 0; i < l.length; i++) out.push(l[i]);
        return out;
    }
    function onOff(b) { return b ? "On" : "Off"; }
    readonly property int rh: page.height < 620 ? 31 : 38

    PageHeader {
        id: head
        x: app.margin + 8; y: 24
        overline: "SETTINGS"
        title: "Flight setup"
        subtitle: "The settings themselves are edited on the classic Launchpad pages; Back returns here."
    }

    Row {
        id: classic
        x: app.margin + 8; y: head.y + head.height + 18
        spacing: 10
        Text { anchors.verticalCenter: parent.verticalCenter; text: "CLASSIC PAGES"; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 2.6; rightPadding: 6 }
        Repeater {
            model: [["PARAMETERS", "parameters"], ["VIDEO", "video"], ["MODULES", "modules"], ["EXTRA", "extra"], ["SCENARIOS", "scenarios"]]
            GhostButton { label: modelData[0]; height: 40; onClicked: Launcher.showClassic(modelData[1]) }
        }
    }

    Column {
        id: left
        x: app.margin; y: classic.y + classic.height + 20
        width: Math.min(560, (page.width - 2 * app.margin - 40) / 2)
        spacing: 18
        GlassPanel {
            width: parent.width; height: setupCol.implicitHeight + 28
            Column {
                id: setupCol
                x: 22; y: 14
                width: parent.width - 44
                Text { text: "NEXT FLIGHT"; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 2.6; bottomPadding: 8 }
                StatusRow { rowHeight: page.rh; label: "Graphics client"; value: Format.client(page.s); dot: Format.isConsole(page.s) ? Theme.warn : Theme.ok }
                StatusRow { rowHeight: page.rh; label: "Device"; value: page.s.device || "—" }
                StatusRow { rowHeight: page.rh; label: "Display"; value: Format.display(page.s) }
                StatusRow { rowHeight: page.rh; label: "Active add-ons"; value: "" + (page.s.activeModules || 0) }
                StatusRow { rowHeight: page.rh; label: "Nonspherical gravity"; value: page.onOff(page.s.nonsphericalGravity); dot: page.s.nonsphericalGravity ? Theme.ok : Theme.textFaint }
                StatusRow { rowHeight: page.rh; label: "Radiation pressure"; value: page.onOff(page.s.radiationPressure); dot: page.s.radiationPressure ? Theme.ok : Theme.textFaint }
                StatusRow { rowHeight: page.rh; label: "Distributed mass"; value: page.onOff(page.s.distributedMass); dot: page.s.distributedMass ? Theme.ok : Theme.textFaint }
                StatusRow { rowHeight: page.rh; label: "Atmospheric wind"; value: page.onOff(page.s.atmWind); dot: page.s.atmWind ? Theme.ok : Theme.textFaint }
            }
        }
    }

    GlassPanel {
        x: left.x + left.width + 40; y: left.y
        width: page.width - x - app.margin; height: Math.min(skinCol.implicitHeight + 28, page.height - y - 12)
        Flickable {
            anchors.fill: parent
            contentHeight: skinCol.implicitHeight + 28
            clip: true
            Column {
                id: skinCol
                x: 22; y: 14
                width: parent.width - 44
                spacing: 10
                Text { text: "LAUNCHPAD SKIN"; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 2.6; bottomPadding: 4 }
                Repeater {
                    model: page.skins
                    Rectangle {
                        width: skinCol.width; height: sk.implicitHeight + 24
                        radius: 10
                        readonly property bool current: modelData.id === Launcher.skin
                        color: current ? Theme.tint(0.12) : Qt.rgba(1, 1, 1, 0.03)
                        border.color: current ? Theme.accent : Theme.line
                        Column {
                            id: sk
                            x: 16; y: 12
                            width: parent.width - 150
                            spacing: 4
                            Text { text: modelData.name; color: Theme.text; font.family: Theme.font; font.pixelSize: 16; font.weight: Font.DemiBold }
                            Text {
                                width: parent.width
                                text: [modelData.kind === "qml+qss" ? "QML launcher and style sheet" : modelData.kind === "qml" ? "QML launcher" : modelData.kind === "qss" ? "Style sheet" : "", modelData.layout ? "Layout" : "", modelData.author ? "by " + modelData.author : ""].filter(function (t) { return t !== ""; }).join("  ·  ")
                                visible: text !== ""
                                color: Theme.textFaint; font.family: Theme.font; font.pixelSize: 12
                            }
                            Text {
                                width: parent.width
                                text: modelData.compatible ? modelData.description : "Can't be used: " + modelData.reason
                                visible: text !== ""
                                wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                                textFormat: Text.PlainText
                                color: modelData.compatible ? Theme.textDim : Theme.accent; font.family: Theme.font; font.pixelSize: 13
                            }
                        }
                        Text {
                            visible: parent.current
                            anchors.right: parent.right; anchors.rightMargin: 18; anchors.verticalCenter: parent.verticalCenter
                            text: "ACTIVE"; color: Theme.accent; font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 2
                        }
                        GhostButton {
                            visible: !parent.current
                            enabled: modelData.compatible
                            anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                            label: "APPLY"; height: 40
                            onClicked: Launcher.setSkin(modelData.id)
                        }
                    }
                }
            }
        }
    }
}
