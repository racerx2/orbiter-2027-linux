import QtQuick

// stars and a faint Milky Way band, drawn once
Canvas {
    id: c
    property int seed: 20240
    property int count: 900

    function rng(a) {
        return function() {
            a |= 0; a = a + 0x6D2B79F5 | 0;
            var t = Math.imul(a ^ a >>> 15, 1 | a);
            t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
            return ((t ^ t >>> 14) >>> 0) / 4294967296;
        }
    }

    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        var w = width, h = height, r = rng(seed);
        // Milky Way haze along a diagonal
        var x0 = w * 0.30, y0 = -h * 0.15, x1 = w * 1.05, y1 = h * 0.80;
        for (var i = 0; i < 46; i++) {
            var t = r(), px = x0 + (x1 - x0) * t + (r() - 0.5) * 120, py = y0 + (y1 - y0) * t + (r() - 0.5) * 120;
            var rad = 70 + r() * 150, g = ctx.createRadialGradient(px, py, 0, px, py, rad);
            var tint = r() < 0.5 ? "rgba(120,140,220," : "rgba(200,170,210,";
            g.addColorStop(0, tint + (0.035 + r() * 0.03) + ")");
            g.addColorStop(1, tint + "0)");
            ctx.fillStyle = g;
            ctx.fillRect(px - rad, py - rad, rad * 2, rad * 2);
        }
        var nx = -(y1 - y0), ny = (x1 - x0), nl = Math.sqrt(nx * nx + ny * ny);
        nx /= nl; ny /= nl;
        for (i = 0; i < 2600; i++) {
            t = r();
            var off = (r() + r() + r() - 1.5) * 110;
            px = x0 + (x1 - x0) * t + nx * off; py = y0 + (y1 - y0) * t + ny * off;
            ctx.fillStyle = "rgba(220,228,255," + (0.08 + r() * 0.22) + ")";
            ctx.fillRect(px, py, 1, 1);
        }
        // field stars
        for (i = 0; i < count; i++) {
            px = r() * w; py = r() * h;
            var m = Math.pow(r(), 3.2), s = 0.6 + m * 1.7, a = 0.22 + m * 0.78, k = r();
            var col = k < 0.14 ? "rgba(175,205,255," : (k < 0.24 ? "rgba(255,222,185," : "rgba(255,255,255,");
            if (m > 0.8) {
                var gg = ctx.createRadialGradient(px, py, 0, px, py, s * 5);
                gg.addColorStop(0, col + "0.22)");
                gg.addColorStop(1, col + "0)");
                ctx.fillStyle = gg;
                ctx.fillRect(px - s * 5, py - s * 5, s * 10, s * 10);
            }
            ctx.fillStyle = col + a + ")";
            if (s < 1.2) ctx.fillRect(px, py, s, s);
            else { ctx.beginPath(); ctx.arc(px, py, s * 0.6, 0, Math.PI * 2); ctx.fill(); }
        }
    }
}
