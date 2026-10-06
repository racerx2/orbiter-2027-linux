// test skin: helpers for the bindings
var Logic = (function () {
    function cards() {
        var out = [], all = Launcher.scenarios;
        for (var i = 0; i < all.length; i++) if (!all[i].isFolder) out.push({ path: all[i].path, title: all[i].name });
        return out;
    }
    function many() {
        var out = [];
        for (var i = 0; i < (ui.many || 0); i++) out.push(i);
        return out;
    }
    return { cards: cards, many: many };
})();

function update() {
    var path = Launcher.currentScenario;
    return { path: path, info: Launcher.scenarioInfo(path), page: Launcher.page || "A" };
}
