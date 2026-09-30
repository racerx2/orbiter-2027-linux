import QtQuick
import Orbiter.Launcher 1.0

// Horizon launcher skin: backdrop, top bar and footer; the pages load below the bar
Rectangle {
    id: app
    color: Theme.bg
    readonly property var pages: ({ PLAY: "PlayPage.qml", SCENARIOS: "ScenariosPage.qml", ADDONS: "AddonsPage.qml", SETTINGS: "SettingsPage.qml", ABOUT: "AboutPage.qml" })
    readonly property string page: (Launcher.page in pages) ? Launcher.page : "PLAY"
    readonly property bool live: Launcher.active
    readonly property real margin: width < 1300 ? 44 : 72
    function go(p) { if (p in pages) Launcher.page = p; }
    function notice(msg) { toast.show(msg); }

    // backdrop: animated only while the Launchpad is the active window
    Item {
        id: sky
        width: parent.width + 60; height: parent.height
        Starfield { anchors.fill: parent }
        SequentialAnimation on x {
            running: true; paused: !app.live
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: -60; duration: 90000; easing.type: Easing.InOutSine }
            NumberAnimation { from: -60; to: 0; duration: 90000; easing.type: Easing.InOutSine }
        }
    }
    Planet { id: planet; anchors.fill: parent }
    SunGlare {
        x: planet.sunX - width / 2; y: planet.sunY - height / 2
        SequentialAnimation on opacity {
            running: true; paused: !app.live
            loops: Animation.Infinite
            NumberAnimation { from: 1; to: 0.82; duration: 3200; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.82; to: 1; duration: 3200; easing.type: Easing.InOutSine }
        }
    }
    Item {
        anchors.fill: parent
        opacity: app.page === "PLAY" ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(0.01, 0.02, 0.05, 0.88) }
                GradientStop { position: 0.4; color: Qt.rgba(0.01, 0.02, 0.05, 0.55) }
                GradientStop { position: 0.64; color: Qt.rgba(0.01, 0.02, 0.05, 0) }
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: 380
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0.01, 0.02, 0.05, 0) }
                GradientStop { position: 1; color: Qt.rgba(0.01, 0.02, 0.05, 0.9) }
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.01, 0.02, 0.05, app.page === "PLAY" ? 0 : 0.66)
        Behavior on color { ColorAnimation { duration: 250 } }
    }
    Rectangle {
        width: parent.width; height: 140
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(0.01, 0.02, 0.05, 0.85) }
            GradientStop { position: 1; color: Qt.rgba(0.01, 0.02, 0.05, 0) }
        }
    }

    TopBar { current: app.page; onNavigate: function (tab) { app.go(tab); } }

    Loader {
        id: pager
        x: 0; y: 81
        width: parent.width; height: parent.height - 81 - 58
        source: app.pages[app.page]
        focus: true
    }

    // footer
    Rectangle { x: app.margin; y: parent.height - 58; width: parent.width - 2 * app.margin; height: 1; color: Theme.line }
    Text {
        x: app.margin + 8; y: parent.height - 38
        text: ("ORBITER  " + Launcher.version + "  ·  LINUX").toUpperCase()
        color: Theme.textFaint
        font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 2
    }
    Row {
        anchors.right: parent.right; anchors.rightMargin: app.margin + 8
        y: parent.height - 38
        spacing: 34
        TextLink { text: "HELP"; tint: Theme.textDim; onClicked: Launcher.help(app.page === "ADDONS" ? "modules" : "scenarios") }
        TextLink { text: "QUIT"; tint: Theme.textDim; onClicked: Launcher.quit() }
    }

    Toast { id: toast }

    Connections {
        target: Launcher
        function onReturnedFromClassic() { pager.forceActiveFocus(); }
    }
}
