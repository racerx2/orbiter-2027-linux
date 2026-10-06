// Horizon: the launcher's logic; the bindings in Main.ui call these, and update() gives every binding its names
var Logic = (function () {
    var PAGES = ["PLAY", "SCENARIOS", "ADDONS", "SETTINGS", "ABOUT"];
    var memo = { rev: -1, all: null, counts: Object.create(null), top: [] };

    function nonEmpty(s) { return s !== ""; }
    function page() { return PAGES.indexOf(Launcher.page) >= 0 ? Launcher.page : "PLAY"; }
    function go(p) { if (PAGES.indexOf(p) >= 0) Launcher.page = p; }
    function margin() { return view.width < 1300 ? 44 : 72; }

    // a page starts as the QML Loader made it: no search, the saved folder, all add-ons
    var lastPage = null;
    function fresh(p) {
        if (p === lastPage) return;
        lastPage = p;
        ui.scnView = "FOLDER";
        ui.folder = Launcher.state.folder || "";
        ui.query = "";
        ui.show = "ALL";
        ui.modQuery = "";
    }

    // a scenario card: the facts the card shows
    function cardOf(p, tag) {
        var ci = Launcher.scenarioInfo(p);
        return { path: p, tag: tag, title: ci.name || p, folder: ci.folder || "Scenarios", kind: Format.kind(ci),
                 seed: Format.seed(p), meta: [Format.vessel(ci), Format.place(ci)].filter(nonEmpty).join("  ·  ") };
    }

    // a grid card's picture and line, read once per scenario until the Launcher changes
    var infoMemo = { rev: -1, map: Object.create(null) };
    function cardInfo(p) {
        if (infoMemo.rev !== view.revision) infoMemo = { rev: view.revision, map: Object.create(null) };
        var c = infoMemo.map[p];
        if (!c) {
            var ci = Launcher.scenarioInfo(p);
            c = infoMemo.map[p] = { kind: Format.kind(ci), seed: Format.seed(p), meta: [Format.vessel(ci), Format.place(ci)].filter(nonEmpty).join("  ·  ") };
        }
        return c;
    }

    // Play: where the last flight ended, then recent, then favourites; at most 8
    function cards(all, recent, favs) {
        var out = [], seen = Object.create(null);
        function add(p, tag) { if (p && !seen[p] && out.length < 8) { seen[p] = true; out.push(cardOf(p, tag)); } }
        for (var i = 0; i < all.length; i++)
            if (!all[i].isFolder && all[i].path === "(Current state)") add(all[i].path, "CONTINUE");
        for (i = 0; i < recent.length; i++) add(recent[i], "RECENT");
        for (i = 0; i < favs.length; i++) add(favs[i], "FAVOURITE");
        return out;
    }

    // how many 250 px cards with 22 px gaps fit the Play page's row
    function cardCount(n) {
        return Math.max(0, Math.min(n, Math.floor((view.width - 2 * margin () + 22) / (250 + 22))));
    }

    function overline(path, isScn, info, recent, none, folder, cont, last) {
        if (!path) return none;
        if (!isScn) return folder;
        if (path === "(Current state)") return cont;
        var f = (info.folder || "SCENARIOS").toUpperCase();
        if (recent.length && recent[0] === path) return last + f;
        return f;
    }

    // Left and Right on Play: the next card that shows
    function step(d) {
        var c = cards(Launcher.scenarios, Launcher.recent, Launcher.favourites), n = cardCount(c.length), path = Launcher.currentScenario;
        if (!n) return;
        var i = -1;
        for (var k = 0; k < n; k++) if (c[k].path === path) i = k;
        i = (i < 0 ? 0 : Math.max(0, Math.min(n - 1, i + d)));
        Launcher.currentScenario = c[i].path;
    }

    function launch() {
        if (Launcher.canLaunch && Launcher.currentIsScenario) Launcher.launch(Launcher.currentScenario);
    }

    // the star: the toast says what happened
    function toggleFav(path, added, removed, full) {
        var was = Launcher.favourites.indexOf(path) >= 0, on = Launcher.toggleFavourite(path);
        toast(on ? added : (was ? removed : full));
    }

    // Scenarios: folders, counts (every scenario under a folder), the shown items
    function tree(all) {
        if (memo.rev === view.revision && memo.all) return memo;
        var counts = Object.create(null), top = [];
        for (var i = 0; i < all.length; i++) {
            var e = all[i];
            if (e.isFolder) { if (e.depth === 0) top.push(e); continue; }
            var parts = e.path.split("/"), acc = "";
            for (var k = 0; k < parts.length - 1; k++) {
                acc = acc ? acc + "/" + parts[k] : parts[k];
                counts[acc] = (counts[acc] || 0) + 1;
            }
        }
        memo = { rev: view.revision, all: all, counts: counts, top: top };
        return memo;
    }

    function countIn(f) { return memo.counts[f] || 0; }

    function open(f) {
        ui.scnView = "FOLDER";
        ui.folder = f;
        ui.query = "";
        var s = Launcher.state;
        s.folder = f;
        Launcher.state = s;
    }

    function showList(v) {
        ui.query = "";
        ui.scnView = v;
    }

    function items(all, favs, recent) {
        var out = [], i, q = (ui.query || "").trim().toLowerCase(), folder = ui.folder || "";
        if (q !== "") {
            for (i = 0; i < all.length; i++)
                if (!all[i].isFolder && all[i].path.toLowerCase().indexOf(q) >= 0) out.push(all[i]);
        } else if (ui.scnView === "FAV" || ui.scnView === "RECENT") {
            var list = ui.scnView === "FAV" ? favs : recent, byPath = Object.create(null);
            for (i = 0; i < all.length; i++) if (!(all[i].path in byPath)) byPath[all[i].path] = all[i]; // the first, as entryOf in QML
            for (i = 0; i < list.length; i++) if (byPath[list[i]]) out.push(byPath[list[i]]);
        } else {
            for (i = 0; i < all.length; i++) if (all[i].isFolder && all[i].folder === folder) out.push(all[i]);
            for (i = 0; i < all.length; i++) if (!all[i].isFolder && all[i].folder === folder) out.push(all[i]);
        }
        return out;
    }

    function crumbs(first, favName, recentName) {
        if (ui.scnView === "FAV") return [{ name: favName, path: "" }];
        if (ui.scnView === "RECENT") return [{ name: recentName, path: "" }];
        var out = [{ name: first, path: "" }], acc = "", parts = ui.folder ? ui.folder.split("/") : [];
        for (var i = 0; i < parts.length; i++) {
            acc = acc ? acc + "/" + parts[i] : parts[i];
            out.push({ name: parts[i], path: acc });
        }
        return out;
    }

    // Enter in the search box first selects the first match; Enter again launches it
    function enter(searching, shown) {
        var q = (ui.query || "").trim();
        if (searching && q !== "") {
            var first = null, has = false, path = Launcher.currentScenario;
            for (var i = 0; i < shown.length; i++) {
                if (shown[i].isFolder) continue;
                if (!first) first = shown[i].path;
                if (shown[i].path === path) has = true;
            }
            if (first && !has) { Launcher.currentScenario = first; return; }
        }
        launch ();
    }

    function vesselsLine(info, head, and, more) {
        var v = info.vessels || [], names = [];
        for (var i = 0; i < v.length; i++) names.push(v[i].name);
        var n = (info.vesselCount || 0) - v.length;
        return head + names.join(", ") + (n > 0 ? and + n + more : "");
    }

    // Add-ons: kept modules by category, in the order they first appear
    function groups(mods, show, query) {
        var out = [], idx = Object.create(null), q = (query || "").trim().toLowerCase();
        for (var i = 0; i < mods.length; i++) {
            var m = mods[i];
            if (show === "ON" && !m.active) continue;
            if (show === "OFF" && m.active) continue;
            if (q !== "" && m.name.toLowerCase().indexOf(q) < 0 && m.info.toLowerCase().indexOf(q) < 0) continue;
            if (!(m.category in idx)) { idx[m.category] = out.length; out.push({ name: m.category, mods: [] }); }
            out[idx[m.category]].mods.push(m);
        }
        return out;
    }

    // Settings: Classic first, then the skins
    function skins(list, classicName, classicText) {
        var out = [{ id: "", name: classicName, kind: "", author: "", description: classicText, compatible: true, reason: "", layout: false }];
        for (var i = 0; i < list.length; i++) out.push(list[i]);
        return out;
    }

    function kindText(e) {
        var k = { "qml+qss": "QML launcher and style sheet", "qml": "QML launcher", "qss": "Style sheet",
                  "forms+qss": "Qt Designer launcher and style sheet", "forms": "Qt Designer launcher" }[e.kind] || "";
        return [k, e.layout ? "Layout" : "", e.author ? "by " + e.author : ""].filter(nonEmpty).join("  ·  ");
    }

    function me(list) {
        for (var i = 0; i < list.length; i++) if (list[i].id === Launcher.skin) return list[i];
        return { name: Launcher.skin, version: "", author: "" };
    }

    return { page: page, go: go, margin: margin, fresh: fresh, cardInfo: cardInfo, cards: cards, cardCount: cardCount, overline: overline, step: step,
             launch: launch, toggleFav: toggleFav, tree: tree, countIn: countIn, open: open, showList: showList,
             items: items, crumbs: crumbs, enter: enter, vesselsLine: vesselsLine, groups: groups, skins: skins,
             kindText: kindText, me: me };
})();

Logic.fresh(Logic.page());

// the names every binding sees, once per refresh
function update() {
    Logic.fresh(Logic.page());
    var path = Launcher.currentScenario, all = Launcher.scenarios, recent = Launcher.recent, favs = Launcher.favourites;
    var setup = Launcher.setup, mods = Launcher.modules, skinList = Launcher.skins;
    var pageH = view.height - 139, activeCount = 0;
    for (var i = 0; i < mods.length; i++) if (mods[i].active) activeCount++;
    var shown = Logic.items(all, favs, recent);
    Logic.tree(all);
    var cards = Logic.cards(all, recent, favs);
    return {
        page: Logic.page(), margin: Logic.margin(), gap: view.width < 1300 ? 28 : 56,
        pageH: pageH, roomy: pageH >= 620, tall: pageH > 700, rh: pageH < 620 ? 31 : 38,
        path: path, info: Launcher.scenarioInfo(path), isScn: Launcher.currentIsScenario,
        fav: favs.indexOf(path) >= 0, setup: setup, recent: recent, favs: favs, all: all,
        cards: cards, cardCount: Logic.cardCount(cards.length),
        query: (ui.query || "").trim(), scnView: ui.scnView, folder: ui.folder || "", shown: shown,
        topFolders: Logic.tree(all).top,
        mods: mods, activeCount: activeCount, groups: Logic.groups(mods, ui.show, ui.modQuery),
        skinList: skinList, me: Logic.me(skinList)
    };
}
