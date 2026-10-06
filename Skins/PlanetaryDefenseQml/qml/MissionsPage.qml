import QtQuick
import Orbiter.Launcher 1.0
import "Format.js" as Format

// Missions: scenario folders on the left, cards in the middle, the selected scenario on the right
Item {
    id: page
    focus: true
    property string view: "FOLDER"          // FOLDER, FAV, RECENT
    property string folder: Launcher.state.folder || ""
    readonly property string query: search.text.trim().toLowerCase()
    readonly property var all: Launcher.scenarios
    readonly property string path: Launcher.currentScenario
    readonly property var info: Launcher.scenarioInfo(path)
    readonly property bool isScn: Launcher.currentIsScenario

    function open(f) {
        view = "FOLDER";
        folder = f;
        search.text = "";
        var s = Launcher.state; s.folder = f; Launcher.state = s;
    }
    function countIn(f) {
        var n = 0, pre = f + "/";
        for (var i = 0; i < all.length; i++) if (!all[i].isFolder && all[i].path.indexOf(pre) === 0) n++;
        return n;
    }
    function entryOf(p) {
        for (var i = 0; i < all.length; i++) if (all[i].path === p) return all[i];
        return null;
    }
    readonly property var topFolders: {
        var out = [];
        for (var i = 0; i < all.length; i++) if (all[i].depth === 0 && all[i].isFolder) out.push(all[i]);
        return out;
    }
    readonly property var items: {
        var out = [], i;
        if (query !== "") {
            for (i = 0; i < all.length; i++)
                if (!all[i].isFolder && all[i].path.toLowerCase().indexOf(query) >= 0) out.push(all[i]);
        } else if (view === "FAV" || view === "RECENT") {
            var list = view === "FAV" ? Launcher.favourites : Launcher.recent;
            for (i = 0; i < list.length; i++) { var e = entryOf(list[i]); if (e) out.push(e); }
        } else {
            for (i = 0; i < all.length; i++) if (all[i].isFolder && all[i].folder === folder) out.push(all[i]);
            for (i = 0; i < all.length; i++) if (!all[i].isFolder && all[i].folder === folder) out.push(all[i]);
        }
        return out;
    }
    function launch() { if (Launcher.canLaunch && isScn) Launcher.launch(path); }
    // Enter in the search box first selects the first match; Enter again launches it
    function enter() {
        if (search.editing && query !== "") {
            var first = null, shown = false;
            for (var i = 0; i < items.length; i++) {
                if (items[i].isFolder) continue;
                if (!first) first = items[i].path;
                if (items[i].path === path) shown = true;
            }
            if (first && !shown) { Launcher.currentScenario = first; return; }
        }
        launch();
    }
    Keys.onReturnPressed: enter()
    Keys.onEnterPressed: enter()

    // rail
    Flickable {
        id: rail
        x: app.margin; y: 24
        width: 250; height: page.height - 40
        contentHeight: railCol.implicitHeight
        clip: true
        Column {
            id: railCol
            width: parent.width
            spacing: 8
            Text { text: "MISSIONS"; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 2.6; bottomPadding: 6 }
            FilterChip { label: "All folders"; active: page.view === "FOLDER" && page.folder === "" && page.query === ""; onClicked: page.open("") }
            FilterChip { label: "Favourites"; count: "" + Launcher.favourites.length; active: page.view === "FAV" && page.query === ""; onClicked: { search.text = ""; page.view = "FAV"; } }
            FilterChip { label: "Recent"; count: "" + Launcher.recent.length; active: page.view === "RECENT" && page.query === ""; onClicked: { search.text = ""; page.view = "RECENT"; } }
            Rectangle { width: parent.width - 20; height: 1; color: Theme.line }
            Repeater {
                model: page.topFolders
                FilterChip {
                    maxWidth: railCol.width
                    label: modelData.name
                    count: "" + page.countIn(modelData.path)
                    active: page.view === "FOLDER" && page.query === "" && (page.folder === modelData.path || page.folder.indexOf(modelData.path + "/") === 0)
                    onClicked: page.open(modelData.path)
                }
            }
        }
    }

    // middle: search, breadcrumb, cards
    Item {
        id: mid
        x: rail.x + rail.width + 28; y: 24
        width: detail.x - x - 28; height: page.height - 40
        SearchBox { id: search; width: Math.min(420, parent.width); placeholder: "Search scenario names" }
        Row {
            y: 54; height: 24; spacing: 8
            visible: page.query === ""
            Repeater {
                model: {
                    if (page.view === "FAV") return [["Favourites", ""]];
                    if (page.view === "RECENT") return [["Recent", ""]];
                    var out = [["Scenarios", ""]], acc = "";
                    var parts = page.folder ? page.folder.split("/") : [];
                    for (var i = 0; i < parts.length; i++) { acc = acc ? acc + "/" + parts[i] : parts[i]; out.push([parts[i], acc]); }
                    return out;
                }
                Row {
                    spacing: 8
                    Text { visible: index > 0; text: "/"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: 14 }
                    TextLink { text: modelData[0]; font.pixelSize: 14; font.letterSpacing: 0.5; tint: index === 0 ? Theme.textDim : Theme.text; onClicked: page.open(modelData[1]) }
                }
            }
        }
        Text {
            y: 54; visible: page.query !== ""
            text: page.items.length + (page.items.length === 1 ? " scenario" : " scenarios") + " match"
            color: Theme.textDim; font.family: Theme.font; font.pixelSize: 14
        }
        GridView {
            id: grid
            y: 92; width: parent.width; height: parent.height - 92
            clip: true
            cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 232)))
            cellHeight: 196
            model: page.items
            delegate: Item {
                width: grid.cellWidth; height: grid.cellHeight
                FolderCard {
                    visible: modelData.isFolder
                    width: parent.width - 18; height: parent.height - 18
                    title: modelData.name
                    count: page.countIn(modelData.path)
                    onClicked: page.open(modelData.path)
                }
                ScenarioCard {
                    visible: !modelData.isFolder
                    readonly property var ci: modelData.isFolder ? ({}) : Launcher.scenarioInfo(modelData.path)
                    width: parent.width - 18; height: parent.height - 18
                    title: modelData.name
                    folder: modelData.folder || "Scenarios"
                    kind: Format.kind(ci)
                    seed: Format.seed(modelData.path)
                    when: ""
                    meta: [Format.vessel(ci), Format.place(ci)].filter(function (s) { return s !== ""; }).join("  ·  ")
                    selected: page.path === modelData.path
                    onClicked: Launcher.currentScenario = modelData.path
                    onDoubleClicked: Launcher.launch(modelData.path)
                }
            }
            Text {
                anchors.centerIn: parent
                visible: page.items.length === 0
                text: page.view === "FAV" ? "No favourites yet: open a scenario and press the star." : (page.view === "RECENT" ? "Scenarios you launch appear here." : "Nothing here.")
                color: Theme.textDim; font.family: Theme.font; font.pixelSize: 15
            }
        }
    }

    // right: the selected scenario
    GlassPanel {
        id: detail
        anchors.right: parent.right; anchors.rightMargin: app.margin
        y: 24
        width: Math.min(440, page.width * 0.3); height: page.height - 40
        Column {
            id: dcol
            x: 24; y: 22
            width: parent.width - 48
            spacing: 0
            Text {
                text: page.isScn ? (page.info.folder || "SCENARIOS").toUpperCase() : (page.path ? "FOLDER" : "NOTHING SELECTED")
                color: Theme.accent; font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 2.6
            }
            Item { width: 1; height: 8 }
            Text {
                width: parent.width
                text: page.path ? (page.info.name || page.path) : "Pick a scenario"
                wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                color: Theme.text; font.family: Theme.font; font.pixelSize: 26; font.weight: Font.DemiBold
            }
            Item { width: 1; height: 14 }
            Column {
                width: parent.width
                visible: page.isScn
                StatusRow { label: "Focus"; value: Format.vessel(page.info) || "—"; dot: Theme.accent }
                StatusRow { label: "Where"; value: Format.place(page.info) || "—"; dot: Theme.info }
                StatusRow { label: "Date"; value: Format.date(page.info); dot: Theme.textDim }
                StatusRow { label: "Vessels"; value: "" + (page.info.vesselCount || 0); dot: Theme.textDim }
                StatusRow { label: "System"; value: page.info.system || "—"; dot: Theme.textDim }
            }
            Item { width: 1; height: 16 }
            Row {
                spacing: 12
                visible: page.isScn
                LaunchButton { enabled: Launcher.canLaunch && page.isScn; width: 190; height: 50; onClicked: page.launch() }
                GhostButton {
                    star: true; height: 50; on: Launcher.favourites.indexOf(page.path) >= 0
                    onClicked: {
                        var was = Launcher.favourites.indexOf(page.path) >= 0, on = Launcher.toggleFavourite(page.path);
                        app.notice(on ? "Added to favourites" : (was ? "Removed from favourites" : "Favourites are full"));
                    }
                }
            }
            Item { width: 1; height: page.isScn ? 18 : 0 }
        }
        Flickable {
            x: 24; y: dcol.y + dcol.height
            width: parent.width - 48; height: parent.height - y - actions.height - 30
            contentHeight: descCol.implicitHeight
            clip: true
            Column {
                id: descCol
                width: parent.width
                spacing: 14
                Text {
                    width: parent.width
                    text: Launcher.currentDescription
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    color: Theme.textDim; font.family: Theme.font; font.pixelSize: 15; lineHeight: 1.3
                }
                Text {
                    visible: page.isScn && page.info.vessels && page.info.vessels.length > 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: {
                        var v = page.info.vessels || [], names = [];
                        for (var i = 0; i < v.length; i++) names.push(v[i].name);
                        var more = (page.info.vesselCount || 0) - v.length;
                        return "VESSELS   " + names.join(", ") + (more > 0 ? " and " + more + " more" : "");
                    }
                    color: Theme.textFaint; font.family: Theme.font; font.pixelSize: 13; lineHeight: 1.3
                }
            }
        }
        Row {
            id: actions
            x: 24; anchors.bottom: parent.bottom; anchors.bottomMargin: 20
            spacing: 26
            TextLink { text: "SAVE CURRENT STATE"; onClicked: Launcher.saveCurrentState() }
            TextLink { text: "CLEAR QUICKSAVES"; visible: page.folder === "Quicksave" || page.info.folder === "Quicksave"; onClicked: Launcher.clearQuicksaves() }
        }
    }
}
