import QtQuick
import Orbiter.Launcher 1.0

// Systems: the plugin modules of the classic Modules tab, by category, with their on/off switches
Item {
    id: page
    focus: true
    property string show: "ALL"             // ALL, ON, OFF
    readonly property string query: search.text.trim().toLowerCase()
    readonly property var mods: Launcher.modules
    readonly property int activeCount: {
        var n = 0;
        for (var i = 0; i < mods.length; i++) if (mods[i].active) n++;
        return n;
    }
    function keep(m) {
        if (show === "ON" && !m.active) return false;
        if (show === "OFF" && m.active) return false;
        return query === "" || m.name.toLowerCase().indexOf(query) >= 0 || m.info.toLowerCase().indexOf(query) >= 0;
    }
    readonly property var groups: {
        var out = [], idx = {};
        for (var i = 0; i < mods.length; i++) {
            var m = mods[i];
            if (!keep(m)) continue;
            if (!(m.category in idx)) { idx[m.category] = out.length; out.push({ name: m.category, mods: [] }); }
            out[idx[m.category]].mods.push(m);
        }
        return out;
    }

    PageHeader {
        id: head
        x: app.margin + 8; y: 24
        overline: "SYSTEMS"
        title: "Plugin modules"
        subtitle: page.mods.length + " installed  ·  " + page.activeCount + " active  ·  changes load or unload them at once, as in the classic Modules tab"
    }
    Row {
        x: app.margin + 8; y: head.y + head.height + 22
        spacing: 10
        FilterChip { label: "All"; active: page.show === "ALL"; onClicked: page.show = "ALL" }
        FilterChip { label: "Active"; count: "" + page.activeCount; active: page.show === "ON"; onClicked: page.show = "ON" }
        FilterChip { label: "Inactive"; count: "" + (page.mods.length - page.activeCount); active: page.show === "OFF"; onClicked: page.show = "OFF" }
        Item { width: 14; height: 1 }
        SearchBox { id: search; width: 300; placeholder: "Search add-ons" }
    }
    Row {
        anchors.right: parent.right; anchors.rightMargin: app.margin + 8
        y: head.y + head.height + 30
        spacing: 30
        TextLink { text: "ADD-ON SETTINGS  →"; onClicked: Launcher.showClassic("extra") }
        TextLink { text: "DEACTIVATE ALL"; tint: Theme.textDim; enabled: page.activeCount > 0; onClicked: Launcher.deactivateAllModules() }
    }

    Flickable {
        id: list
        x: app.margin; y: head.y + head.height + 80
        width: page.width - 2 * app.margin; height: page.height - y - 12
        contentHeight: groupsCol.implicitHeight + 20
        clip: true
        Column {
            id: groupsCol
            width: parent.width
            spacing: 22
            Repeater {
                model: page.groups
                Column {
                    width: groupsCol.width
                    spacing: 12
                    Text { x: 8; text: modelData.name.toUpperCase(); color: Theme.textDim; font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold; font.letterSpacing: 2.6 }
                    Flow {
                        width: parent.width
                        spacing: 14
                        Repeater {
                            model: modelData.mods
                            Rectangle {
                                id: card
                                width: Math.floor((groupsCol.width - 14 * (cols - 1)) / cols)
                                readonly property int cols: Math.max(1, Math.floor((groupsCol.width + 14) / 384))
                                height: 118
                                radius: 12
                                color: cm.containsMouse ? Theme.glassHi : Theme.glass
                                border.color: modelData.active ? Theme.tint(0.45) : Theme.line
                                MouseArea { id: cm; anchors.fill: parent; hoverEnabled: true }
                                Text {
                                    x: 18; y: 16; width: parent.width - 100
                                    text: modelData.name
                                    elide: Text.ElideRight
                                    color: Theme.text; font.family: Theme.font; font.pixelSize: 16; font.weight: Font.DemiBold
                                }
                                Toggle {
                                    anchors.right: parent.right; anchors.rightMargin: 18; y: 14
                                    on: modelData.active
                                    enabled: !modelData.locked
                                    onToggled: Launcher.setModuleActive(modelData.name, !modelData.active)
                                }
                                Text {
                                    x: 18; y: 44; width: parent.width - 36
                                    text: modelData.locked ? "Loaded from the command line; it can't be switched off here." : (modelData.info || "No description.")
                                    wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                    color: modelData.locked ? Theme.accent : Theme.textDim; font.family: Theme.font; font.pixelSize: 13; lineHeight: 1.2
                                }
                            }
                        }
                    }
                }
            }
            Text {
                visible: page.groups.length === 0
                x: 8
                text: page.mods.length === 0 ? "No plugin modules found in Modules/Plugin." : "No add-ons match."
                color: Theme.textDim; font.family: Theme.font; font.pixelSize: 15
            }
        }
    }
}
