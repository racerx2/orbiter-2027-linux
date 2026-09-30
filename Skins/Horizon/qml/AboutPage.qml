import QtQuick
import Orbiter.Launcher 1.0

// About: version and build, links, and the classic About page for credits and licences
Item {
    id: page
    focus: true
    readonly property var me: {
        var l = Launcher.skins;
        for (var i = 0; i < l.length; i++) if (l[i].id === Launcher.skin) return l[i];
        return { name: Launcher.skin, version: "", author: "" };
    }

    PageHeader {
        id: head
        x: app.margin + 8; y: 24
        overline: "ABOUT"
        title: "Orbiter"
        subtitle: "A free, open-source space flight simulator based on Newtonian mechanics."
    }
    GlassPanel {
        x: app.margin; y: head.y + head.height + 26
        width: Math.min(640, page.width - 2 * app.margin); height: info.implicitHeight + 36
        Column {
            id: info
            x: 24; y: 18
            width: parent.width - 48
            spacing: 6
            StatusRow { label: "Version"; value: Launcher.version }
            StatusRow { label: "Launchpad skin"; value: page.me.name + (page.me.version ? " " + page.me.version : "") + (page.me.author ? "  ·  " + page.me.author : ""); dot: Theme.info }
            StatusRow { label: "Launcher API"; value: "" + Launcher.apiVersion; dot: Theme.textDim }
            Item { width: 1; height: 8 }
            Text {
                width: parent.width
                text: Launcher.build
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: Theme.textDim; font.family: Theme.font; font.pixelSize: 13; lineHeight: 1.35
            }
        }
    }
    Row {
        x: app.margin + 8; y: head.y + head.height + 26 + info.implicitHeight + 36 + 30
        spacing: 12
        GhostButton { label: "CREDITS AND LICENCES"; height: 48; onClicked: Launcher.showClassic("about") }
        GhostButton { label: "ORBITER FORUM"; height: 48; onClicked: Launcher.openUrl("https://www.orbiter-forum.com/") }
        GhostButton { label: "SOURCE CODE"; height: 48; onClicked: Launcher.openUrl("https://github.com/orbitersim/orbiter") }
    }
}
