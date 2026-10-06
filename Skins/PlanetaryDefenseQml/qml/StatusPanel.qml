import QtQuick
import Orbiter.Launcher 1.0
import "Format.js" as Format

// what the next flight uses; readiness is green with a graphics client, amber in console mode
GlassPanel {
    id: sp
    property int rowHeight: 32
    readonly property var s: Launcher.setup
    readonly property bool consoleMode: Format.isConsole(s)
    implicitHeight: col.implicitHeight + 24
    Column {
        id: col
        x: 16; y: 12
        width: parent.width - 32
        Item {
            width: parent.width; height: 28
            PanelTitle { width: parent.width - edit.width - 10; text: "SYSTEM STATUS"; dot: sp.consoleMode ? Theme.warn : Theme.ok }
            TextLink { id: edit; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: "EDIT  →"; onClicked: app.go("SETTINGS") }
        }
        Item {
            width: parent.width; height: sp.rowHeight + 4
            Text { anchors.verticalCenter: parent.verticalCenter; text: "READINESS"; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 1.6 }
            Rectangle {
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                height: 24; width: rd.implicitWidth + 22; radius: 12
                color: Qt.rgba(0, 0, 0, 0.35)
                border.color: sp.consoleMode ? Theme.warn : Theme.ok
                Text { id: rd; anchors.centerIn: parent; text: sp.consoleMode ? "AMBER · CONSOLE ONLY" : "GREEN"; color: sp.consoleMode ? Theme.warn : Theme.ok; font.family: Theme.mono; font.pixelSize: 11; font.bold: true }
            }
        }
        StatusRow { rowHeight: sp.rowHeight; label: "Graphics"; value: Format.client(sp.s); dot: sp.consoleMode ? Theme.warn : Theme.ok }
        StatusRow { rowHeight: sp.rowHeight; label: "Device"; value: sp.s.device || "—"; dot: Theme.info; visible: !sp.consoleMode }
        StatusRow { rowHeight: sp.rowHeight; label: "Display"; value: Format.display(sp.s); dot: Theme.info; visible: !sp.consoleMode }
        StatusRow { rowHeight: sp.rowHeight; label: "Add-ons"; value: (sp.s.activeModules || 0) + " active"; dot: Theme.info }
        StatusRow { rowHeight: sp.rowHeight; label: "Physics"; value: Format.physics(sp.s); dot: Theme.accent }
    }
}
