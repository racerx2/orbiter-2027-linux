.pragma library

// text for the facts Launcher.scenarioInfo returns

var months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

// "2001-04-07 17:58" -> "7 Apr 2001 · 17:58"; no date: Orbiter starts at the current time
function date(info) {
    if (!info || info.isFolder) return "";
    if (!info.date) return "Current time";
    var m = /^(\d+)-(\d+)-(\d+) (\d+:\d+)$/.exec(info.date);
    if (!m) return info.date;
    return parseInt(m[3], 10) + " " + months[parseInt(m[2], 10) - 1] + " " + m[1] + " · " + m[4];
}

function place(info) {
    if (!info || info.isFolder) return "";
    if (info.focusStatus === "Landed") {
        if (info.focusBase) return info.focusBase + (info.focusPad > 0 ? " pad " + info.focusPad : "") + " · " + info.focusBody;
        return "Landed · " + info.focusBody;
    }
    if (info.focusStatus === "Orbiting") return "Orbiting " + info.focusBody;
    return info.system || "";
}

function vessel(info) {
    if (!info || !info.focus) return "";
    return info.focusClass && info.focusClass !== info.focus ? info.focus + " (" + info.focusClass + ")" : info.focus;
}

// thumbnail kind for Thumb.qml from where the focus vessel is
function kind(info) {
    if (!info || info.isFolder) return "orbit";
    var b = info.focusBody || "";
    if (info.focusStatus === "Landed") {
        if (b === "Mars") return "mars";
        if (b === "Earth") return "runwayday";
        return "moon";
    }
    if (b === "Earth" || b === "") return "orbit";
    return "lunarorbit";
}

function seed(path) {
    var h = 0;
    for (var i = 0; i < path.length; i++) h = (h * 31 + path.charCodeAt(i)) | 0;
    return Math.abs(h) % 100000;
}

function physics(s) {
    if (!s) return "";
    var on = [];
    if (s.nonsphericalGravity) on.push("nonspherical gravity");
    if (s.radiationPressure) on.push("radiation pressure");
    if (s.distributedMass) on.push("distributed mass");
    if (s.atmWind) on.push("wind");
    if (!on.length) return "Point-mass gravity";
    var t = on.join(", ");
    return t.charAt(0).toUpperCase() + t.slice(1);
}

function display(s) {
    if (!s) return "";
    if (s.fullscreen) return "Fullscreen";
    if (s.width && s.height) return "Window " + s.width + " × " + s.height;
    return "Window";
}
