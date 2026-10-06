import QtQuick

// card picture from Format.kind: the focus body with an orbit ring, or its surface with a marker when landed
Canvas {
    id: c
    property string kind: "orbit-Earth"
    property int seed: 0
    property real corner: 11
    onKindChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        var g = getContext("2d"), w = width, h = height, k = kind;
        g.reset();
        g.save();
        g.beginPath(); g.roundedRect(0, 0, w, h, corner, corner); g.clip();
        g.fillStyle = "#06101e"; g.fillRect(0, 0, w, h);
        g.strokeStyle = "rgba(39,211,255,0.06)"; g.lineWidth = 1;
        g.beginPath();
        for (var x = 0.5; x < w; x += 22) { g.moveTo(x, 0); g.lineTo(x, h); }
        for (var y = 0.5; y < h; y += 22) { g.moveTo(0, y); g.lineTo(w, y); }
        g.stroke();
        var a = seed;
        function rnd() { a = (a * 1103515245 + 12345) % 2147483648; return a / 2147483648; }
        g.fillStyle = "rgba(230,246,255,0.7)";
        for (var i = 0; i < 40; i++) g.fillRect(rnd() * w, rnd() * h, 1, 1);
        var parts = k.split("-"), landed = parts[0] === "landed", body = parts.slice(1).join("-");
        var cols = { Earth: "#1f8fc4", Moon: "#9a9a9a", Mars: "#c4643a", Venus: "#e8c27a", Mercury: "#b5ab9c", Jupiter: "#c8a27a", Saturn: "#d8c08a", Sun: "#ffcc66" };
        var col = cols[body] || "#8a8fb0";
        if (landed) {
            var R = w * 1.4, cx = w / 2, cy = h + R - h * 0.38;
            g.fillStyle = col;
            g.beginPath(); g.arc(cx, cy, R, 0, Math.PI * 2); g.fill();
            g.strokeStyle = "rgba(39,211,255,0.8)"; g.lineWidth = 1.5;
            g.beginPath(); g.arc(cx, cy, R, 0, Math.PI * 2); g.stroke();
            g.fillStyle = "#39ff7a";
            g.beginPath(); g.moveTo(cx, h * 0.62 - 14); g.lineTo(cx - 6, h * 0.62 - 2); g.lineTo(cx + 6, h * 0.62 - 2); g.closePath(); g.fill();
        } else {
            var r = Math.min(w, h) * 0.22, px = w * 0.5, py = h * 0.52;
            var grd = g.createRadialGradient(px - r * 0.4, py - r * 0.4, r * 0.1, px, py, r);
            grd.addColorStop(0, Qt.lighter(col, 1.6)); grd.addColorStop(1, Qt.darker(col, 2.2));
            g.fillStyle = grd;
            g.beginPath(); g.arc(px, py, r, 0, Math.PI * 2); g.fill();
            g.save(); g.translate(px, py); g.rotate(-0.35);
            g.strokeStyle = "rgba(39,211,255,0.85)"; g.lineWidth = 1.4;
            g.beginPath(); g.ellipse(-r * 2.1, -r * 0.6, r * 4.2, r * 1.2); g.stroke();
            g.fillStyle = "#ffc23d";
            g.beginPath(); g.arc(r * 2.1 * Math.cos(0.8), r * 0.6 * Math.sin(0.8), 3, 0, Math.PI * 2); g.fill();
            g.restore();
        }
        g.restore();
    }
}
