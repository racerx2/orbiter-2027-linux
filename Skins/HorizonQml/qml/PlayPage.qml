import QtQuick
import Orbiter.Launcher 1.0
import "Format.js" as Format

// Play: the selected scenario up front, the flight setup, and cards to pick from
Item {
    id: play
    focus: true
    readonly property string path: Launcher.currentScenario
    readonly property var info: Launcher.scenarioInfo(path)
    readonly property bool isScn: Launcher.currentIsScenario
    readonly property bool fav: Launcher.favourites.indexOf(path) >= 0
    readonly property var setup: Launcher.setup
    readonly property bool roomy: height >= 620
    readonly property int cardW: 250
    readonly property int cardGap: 22
    readonly property var cards: {
        var out = [], seen = {};
        function add(p, tag) { if (p && !seen[p] && out.length < 8) { seen[p] = true; out.push({ path: p, tag: tag }); } }
        var all = Launcher.scenarios;
        for (var i = 0; i < all.length; i++)
            if (!all[i].isFolder && all[i].path === "(Current state)") add(all[i].path, "CONTINUE");
        var r = Launcher.recent;
        for (i = 0; i < r.length; i++) add(r[i], "RECENT");
        var f = Launcher.favourites;
        for (i = 0; i < f.length; i++) add(f[i], "FAVOURITE");
        return out;
    }
    readonly property int cardCount: Math.max(0, Math.min(cards.length, Math.floor((width - 2 * app.margin + cardGap) / (cardW + cardGap))))

    function overline() {
        if (!path) return "CHOOSE A SCENARIO";
        if (!isScn) return "FOLDER";
        if (path === "(Current state)") return "CONTINUE  ·  WHERE YOUR LAST FLIGHT ENDED";
        if (Launcher.recent.length && Launcher.recent[0] === path) return "LAST LAUNCHED  ·  " + (info.folder || "SCENARIOS").toUpperCase();
        return (info.folder || "SCENARIOS").toUpperCase();
    }
    function step(d) {
        if (!cardCount) return;
        var i = -1;
        for (var k = 0; k < cardCount; k++) if (cards[k].path === path) i = k;
        i = (i < 0 ? 0 : Math.max(0, Math.min(cardCount - 1, i + d)));
        Launcher.currentScenario = cards[i].path;
    }
    function launch() { if (Launcher.canLaunch && isScn) Launcher.launch(path); }

    Keys.onReturnPressed: launch()
    Keys.onEnterPressed: launch()
    Keys.onLeftPressed: step(-1)
    Keys.onRightPressed: step(1)

    onPathChanged: fade.restart()
    NumberAnimation { id: fade; target: hero; property: "opacity"; from: 0.25; to: 1; duration: 260; easing.type: Easing.OutCubic }

    Column {
        id: hero
        x: app.margin + 8; y: play.roomy ? 40 : 20
        width: Math.max(320, Math.min(740, play.width - 2 * app.margin - panel.width - 60))
        Text {
            text: play.overline()
            color: Theme.accent
            font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 3
        }
        Item { width: 1; height: 10 }
        Text {
            width: parent.width
            text: play.path ? (play.info.name || play.path) : "No scenario selected"
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.font; font.pixelSize: play.height > 700 ? 54 : 42; font.weight: Font.DemiBold
            lineHeight: 0.95
        }
        Item { width: 1; height: 18 }
        Flow {
            width: parent.width
            spacing: 10
            visible: play.isScn
            Chip { label: Format.vessel(play.info); dot: Theme.accent; visible: label !== "" }
            Chip { label: Format.place(play.info); dot: Theme.info; visible: label !== "" }
            Chip { label: Format.date(play.info); dot: Theme.textDim; visible: label !== "" }
            Chip { label: play.info.vesselCount + (play.info.vesselCount === 1 ? " vessel" : " vessels"); dot: Theme.textDim; visible: play.info.vesselCount > 0 }
        }
        Item { width: 1; height: play.isScn ? 20 : 0 }
        Text {
            width: Math.min(640, parent.width)
            text: Launcher.currentDescription
            visible: text !== ""
            wrapMode: Text.WordWrap; maximumLineCount: play.height > 700 ? 5 : 3; elide: Text.ElideRight
            textFormat: Text.PlainText
            color: Theme.textDim
            font.family: Theme.font; font.pixelSize: 16
            lineHeight: 1.3
        }
        Item { width: 1; height: 28 }
        Row {
            spacing: 14
            LaunchButton { enabled: Launcher.canLaunch && play.isScn; height: play.roomy ? 64 : 54; width: play.roomy ? 280 : 230; onClicked: play.launch() }
            GhostButton { label: "DETAILS"; height: play.roomy ? 64 : 54; onClicked: app.go("SCENARIOS") }
            GhostButton {
                star: true; on: play.fav; enabled: play.isScn; height: play.roomy ? 64 : 54
                onClicked: {
                    var was = play.fav, on = Launcher.toggleFavourite(play.path);
                    app.notice(on ? "Added to favourites" : (was ? "Removed from favourites" : "Favourites are full"));
                }
            }
        }
        Item { width: 1; height: 12 }
        Text {
            visible: play.isScn
            text: "Press Enter to launch"
            color: Theme.textFaint
            font.family: Theme.font; font.pixelSize: 13; font.letterSpacing: 0.5
        }
    }

    Column {
        id: panel
        anchors.right: parent.right; anchors.rightMargin: app.margin
        y: play.roomy ? 32 : 16
        width: 360
        GlassPanel {
            width: parent.width; height: setupCol.implicitHeight + 28
            Column {
                id: setupCol
                x: 22; y: 14
                width: parent.width - 44
                Item {
                    width: parent.width; height: 28
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "FLIGHT SETUP"; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 2.6 }
                    TextLink { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: "EDIT  →"; onClicked: app.go("SETTINGS") }
                }
                StatusRow { label: "Graphics"; value: Format.client(play.setup); dot: Format.isConsole(play.setup) ? Theme.textFaint : Theme.ok }
                StatusRow { label: "Device"; value: play.setup.device || ""; visible: value !== "" }
                StatusRow { label: "Display"; value: Format.display(play.setup) }
                StatusRow { label: "Add-ons"; value: (play.setup.activeModules || 0) + " active" }
                StatusRow { label: "Physics"; value: Format.physics(play.setup); dot: Theme.info }
                Item {
                    width: parent.width; height: 44
                    Text { anchors.verticalCenter: parent.verticalCenter; x: 18; text: "Start paused"; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 14 }
                    Toggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; on: Launcher.startPaused; onToggled: Launcher.startPaused = !Launcher.startPaused }
                }
            }
        }
    }

    Item {
        visible: play.roomy
        x: app.margin + 8; y: play.height - 200 - 58
        width: play.width - 2 * app.margin - 16; height: 24
        Text { text: "CONTINUE, RECENT AND FAVOURITES"; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 2.6 }
        TextLink { anchors.right: parent.right; text: "ALL SCENARIOS  →"; font.pixelSize: 13; onClicked: app.go("SCENARIOS") }
    }
    Row {
        visible: play.roomy
        x: app.margin + 8; y: play.height - 200 - 22
        spacing: play.cardGap
        Repeater {
            model: play.cards.slice(0, play.cardCount)
            ScenarioCard {
                readonly property var ci: Launcher.scenarioInfo(modelData.path)
                width: play.cardW; height: 200
                title: ci.name || modelData.path
                folder: ci.folder || "Scenarios"
                kind: Format.kind(ci)
                seed: Format.seed(modelData.path)
                when: modelData.tag
                meta: [Format.vessel(ci), Format.place(ci)].filter(function (s) { return s !== ""; }).join("  ·  ")
                selected: play.path === modelData.path
                onClicked: Launcher.currentScenario = modelData.path
                onDoubleClicked: Launcher.launch(modelData.path)
            }
        }
    }
    Text {
        visible: play.roomy && play.cards.length === 0
        x: app.margin + 8; y: play.height - 200 - 22
        width: play.width - 2 * app.margin
        wrapMode: Text.WordWrap
        text: "The state your last flight ended in, the scenarios you launch and the ones you mark as favourites appear here."
        color: Theme.textDim
        font.family: Theme.font; font.pixelSize: 15
    }
}
