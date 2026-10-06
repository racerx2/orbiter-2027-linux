// Planetary Defense: the launcher's logic; the bindings in Main.ui call these, and update() gives every binding its names
var Logic = (function () {
    var PAGES = ["CONTROL", "MISSIONS", "SYSTEMS", "SETTINGS", "DEFENSE"];
    var memo = { rev: -1, all: null, counts: Object.create(null), top: [], scenarios: 0 };
    var epoch = { path: null, mjd: 0 };
    var clock = { at: -1, t: null };
    var neoMemo = null;

    function nonEmpty(s) { return s !== ""; }
    function page() { return PAGES.indexOf(Launcher.page) >= 0 ? Launcher.page : "CONTROL"; }
    function go(p) { if (PAGES.indexOf(p) >= 0) Launcher.page = p; }
    function margin() { return view.width < 1340 ? 18 : 28; }

    // a page starts as the QML Loader made it: no search, the saved folder, all add-ons, a new epoch
    var lastPage = null;
    function fresh(p) {
        if (p === lastPage) return;
        lastPage = p;
        epoch = { path: null, mjd: 0 };
        ui.scnView = "FOLDER";
        ui.folder = Launcher.state.folder || "";
        ui.query = "";
        ui.show = "ALL";
        ui.modQuery = "";
    }

    // the diagram's epoch when the scenario has no date: the time the selection changed
    function nowMjd(path) {
        if (epoch.path !== path) epoch = { path: path, mjd: Orbits.mjdOfMs(Date.now()) };
        return epoch.mjd;
    }

    // the flyby clock, read once for all its tiles at each tick
    function clockNow() {
        var ms = Date.now();
        if (clock.t === null || Math.abs(ms - clock.at) >= 20) clock = { at: ms, t: Orbits.countdown(ms) };
        return clock.t;
    }

    function overline(path, isScn, info, recent, none, folder, cont, last) {
        if (!path) return none;
        if (!isScn) return folder;
        if (path === "(Current state)") return cont;
        var f = (info.folder || "SCENARIOS").toUpperCase();
        if (recent.length && recent[0] === path) return last + f;
        return f;
    }

    function launch() {
        if (Launcher.canLaunch && Launcher.currentIsScenario) Launcher.launch(Launcher.currentScenario);
    }

    // the star: the toast says what happened
    function toggleFav(path, added, removed, full) {
        var was = Launcher.favourites.indexOf(path) >= 0, on = Launcher.toggleFavourite(path);
        toast(on ? added : (was ? removed : full));
    }

    // the mission board: where the last flight ended, then recent, then favourites
    function boardRows(all, recent, favs, max) {
        var out = [], seen = Object.create(null);
        function add(p, tag) {
            if (!p || seen[p] || out.length >= max) return;
            seen[p] = true;
            var ci = Launcher.scenarioInfo(p);
            var st = ci.focusStatus === "Orbiting" ? "ORBIT" : (ci.focusStatus === "Landed" ? "LANDED" : "—");
            out.push({ path: p, tag: tag, name: ci.name || p,
                       line: [ci.mjd !== undefined ? Orbits.dayText(ci.mjd) : "NOW", (ci.focusBody || ci.system || "—").toUpperCase(), st].join(" · "),
                       tone: ci.focusStatus === "Landed" ? "warn" : (ci.focusStatus === "Orbiting" ? "ok" : "dim") });
        }
        for (var i = 0; i < all.length; i++)
            if (!all[i].isFolder && all[i].path === "(Current state)") add(all[i].path, "CONTINUE");
        for (i = 0; i < recent.length; i++) add(recent[i], "RECENT");
        for (i = 0; i < favs.length; i++) add(favs[i], "FAVOURITE");
        return out;
    }

    // Up and Down on Mission Control: the next board row
    function step(d, rows) {
        if (!rows || !rows.length) return;
        var i = -1, path = Launcher.currentScenario;
        for (var k = 0; k < rows.length; k++) if (rows[k].path === path) i = k;
        i = (i < 0 ? 0 : Math.max(0, Math.min(rows.length - 1, i + d)));
        Launcher.currentScenario = rows[i].path;
    }

    // the diagram's time flow offset; none while the diagram has not taken this refresh's epoch yet
    function flowOffset(d, base) { return d && d.baseMjd === base ? d.offset : 0; }

    // the diagram title's right side: the flow offset, or the epoch and where it came from
    function diagramInfo(d, base, fromScenario) {
        var o = flowOffset(d, base);
        if (o > 0) return "+" + Math.floor(o) + " D · " + Orbits.epochText(base + o);
        return "EPOCH " + Orbits.epochText(base) + (fromScenario ? " · SCENARIO" : " · NOW");
    }

    function focusPlanet(b) {
        if (b === "Moon") return "Earth";
        if (b === "Phobos" || b === "Deimos") return "Mars";
        return Orbits.planetNames.indexOf(b) >= 0 || b === "Sun" ? b : "";
    }

    function diagramNote(d, base, focusBody, system) {
        var n = [], hidden = [], mjd = base + flowOffset(d, base), neos = Orbits.neoNames;
        for (var i = 0; i < neos.length; i++) if (!Orbits.neoSet(neos[i], mjd).inside) hidden.push(neos[i].toUpperCase());
        if (focusBody !== "" && focusPlanet(focusBody) === "") n.push("FOCUS " + focusBody.toUpperCase() + " IS OUTSIDE THIS VIEW");
        if (system !== "" && system !== "Sol") n.push("DIAGRAM SHOWS SOL");
        if (hidden.length) n.push(hidden.join(", ") + " NOT SHOWN: THE JPL ELEMENT SETS COVER " + Orbits.NEO_YEARS);
        if (mjd < Orbits.MJD_MIN || mjd > Orbits.MJD_MAX) n.push("EPOCH OUTSIDE 1800–2050: PLANETS APPROXIMATE");
        return n.join(" · ");
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

    // Missions: folders, counts (every scenario under a folder), the shown items
    function tree(all) {
        if (memo.rev === view.revision && memo.all) return memo;
        var counts = Object.create(null), top = [], n = 0;
        for (var i = 0; i < all.length; i++) {
            var e = all[i];
            if (e.isFolder) { if (e.depth === 0) top.push(e); continue; }
            n++;
            var parts = e.path.split("/"), acc = "";
            for (var k = 0; k < parts.length - 1; k++) {
                acc = acc ? acc + "/" + parts[k] : parts[k];
                counts[acc] = (counts[acc] || 0) + 1;
            }
        }
        memo = { rev: view.revision, all: all, counts: counts, top: top, scenarios: n };
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

    // Systems: kept modules by category, in the order they first appear
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

    // Defense: each asteroid's elements now and the years its JPL sets cover
    function neoLines() {
        if (neoMemo) return neoMemo;
        var out = [], names = Orbits.neoNames;
        for (var i = 0; i < names.length; i++) {
            var r = Orbits.neoSet(names[i], 2461200.5 - 2400000.5), sets = Orbits.neoSets(names[i]);
            if (!r.set || !sets.length) continue;
            var c = r.set;
            out.push({ text: names[i].toUpperCase() + "  a " + c.a.toFixed(3) + " AU  e " + c.e.toFixed(3) + "  i " + c.i.toFixed(2) + "°  P " + Math.round(360 / c.n) + " d  · " +
                       sets.length + " JPL element sets, " + Orbits.dayText(sets[0].from - 2400000.5).slice(0, 4) + "–" + Orbits.dayText(sets[sets.length - 1].to - 2400000.5).slice(0, 4) });
        }
        neoMemo = out;
        return out;
    }

    return { page: page, go: go, margin: margin, fresh: fresh, nowMjd: nowMjd, clockNow: clockNow, overline: overline, launch: launch,
             toggleFav: toggleFav, boardRows: boardRows, step: step, flowOffset: flowOffset, diagramInfo: diagramInfo, diagramNote: diagramNote,
             cardInfo: cardInfo, tree: tree, countIn: countIn, open: open, showList: showList, items: items, crumbs: crumbs,
             enter: enter, vesselsLine: vesselsLine, groups: groups, skins: skins, kindText: kindText, me: me, neoLines: neoLines };
})();

Logic.fresh(Logic.page());

// the names every binding sees, once per refresh
function update() {
    Logic.fresh(Logic.page());
    var path = Launcher.currentScenario, all = Launcher.scenarios, recent = Launcher.recent, favs = Launcher.favourites;
    var setup = Launcher.setup, mods = Launcher.modules, skinList = Launcher.skins;
    var pageH = view.height - 156, info = Launcher.scenarioInfo(path), isScn = Launcher.currentIsScenario;
    var compact = view.width < 1340, low = pageH < 690, activeCount = 0;
    for (var i = 0; i < mods.length; i++) if (mods[i].active) activeCount++;
    var hasDate = isScn && info.mjd !== undefined;
    var now = Logic.nowMjd(path); // follows every selection, as onPathChanged in QML
    var t = Logic.tree(all);
    return {
        page: Logic.page(), margin: Logic.margin(), compact: compact, low: low, pageH: pageH,
        gap: compact ? 14 : 18, leftW: compact ? 370 : 400, rightW: compact ? 310 : 340,
        rh: pageH < 620 ? 31 : 38, srh: low ? 28 : 32,
        path: path, info: info, isScn: isScn, fav: favs.indexOf(path) >= 0,
        setup: setup, consoleMode: Format.isConsole(setup), recent: recent, favs: favs, all: all,
        scnCount: t.scenarios, hasDate: hasDate, baseMjd: hasDate ? info.mjd : now,
        board: Logic.boardRows(all, recent, favs, low ? 4 : 6),
        query: (ui.query || "").trim(), scnView: ui.scnView, folder: ui.folder || "", shown: Logic.items(all, favs, recent),
        topFolders: t.top,
        mods: mods, activeCount: activeCount, groups: Logic.groups(mods, ui.show, ui.modQuery),
        skinList: skinList, me: Logic.me(skinList)
    };
}
