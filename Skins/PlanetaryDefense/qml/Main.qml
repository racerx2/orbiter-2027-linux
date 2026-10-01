import QtQuick
import Orbiter.Launcher 1.0

// Planetary Defense launcher skin: grid and stars, header with status and nav, pages, footer
FocusScope {
    id: app
    focus: true
    readonly property var pages: ({ CONTROL: "MissionControlPage.qml", MISSIONS: "MissionsPage.qml", SYSTEMS: "SystemsPage.qml", SETTINGS: "SettingsPage.qml", DEFENSE: "DefensePage.qml" })
    readonly property string page: (Launcher.page in pages) ? Launcher.page : "CONTROL"
    readonly property bool live: Launcher.active
    readonly property real margin: width < 1340 ? 18 : 28
    function go(p) { if (p in pages) Launcher.page = p; }
    function notice(msg) { toast.show(msg); }

    Rectangle { anchors.fill: parent; color: Theme.bg }
    Starfield { anchors.fill: parent; opacity: 0.55; count: 600 } // still: the view only redraws for the clock and time flow
    GridBackdrop { anchors.fill: parent }

    Header { id: header; current: app.page; onNavigate: function (tab) { app.go(tab); } }
    Rectangle { x: app.margin; y: 104; width: parent.width - 2 * app.margin; height: 1; color: Theme.line }

    Loader {
        id: pager
        x: 0; y: 112
        width: parent.width; height: parent.height - 112 - 44
        source: app.pages[app.page]
        focus: true
        onLoaded: forceActiveFocus()
    }
    Component.onCompleted: pager.forceActiveFocus()

    Rectangle { x: app.margin; y: parent.height - 44; width: parent.width - 2 * app.margin; height: 1; color: Theme.line }
    Text {
        x: app.margin + 4; y: parent.height - 30
        width: parent.width - 2 * app.margin - links.width - 30
        elide: Text.ElideRight
        text: "ORBITER " + Launcher.version + " · LINUX · STYLED AFTER PLANETARYDEFENSEFOUNDATION.COM · UNOFFICIAL"
        color: Theme.textFaint
        font.family: Theme.mono; font.pixelSize: 11
    }
    Row {
        id: links
        anchors.right: parent.right; anchors.rightMargin: app.margin + 4
        y: parent.height - 31
        spacing: 28
        TextLink { text: "HELP"; tint: Theme.textDim; onClicked: Launcher.help(app.page === "SYSTEMS" ? "modules" : "scenarios") }
        TextLink { text: "QUIT"; tint: Theme.textDim; onClicked: Launcher.quit() }
    }

    Toast { id: toast }

    Connections {
        target: Launcher
        function onReturnedFromClassic() { pager.forceActiveFocus(); }
    }
}
