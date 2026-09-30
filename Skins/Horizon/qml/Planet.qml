import QtQuick

// sunrise over Earth from orbit, drawn once
Canvas {
    id: c
    readonly property real r: height * 1.28
    readonly property real cx: width * 0.74
    readonly property real cy: height * 0.555 + r
    readonly property real sunAngle: -Math.PI / 2 - 0.2
    readonly property real sunX: cx + (r + 4) * Math.cos(sunAngle)
    readonly property real sunY: cy + (r + 4) * Math.sin(sunAngle)

    function rng(a) {
        return function() {
            a |= 0; a = a + 0x6D2B79F5 | 0;
            var t = Math.imul(a ^ a >>> 15, 1 | a);
            t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
            return ((t ^ t >>> 14) >>> 0) / 4294967296;
        }
    }

    onWidthChanged: repaint.restart()
    onHeightChanged: repaint.restart()
    Timer { id: repaint; interval: 120; onTriggered: c.requestPaint() } // once the size settles, not at every step

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        var rnd = rng(77);
        var lx = cx + r * 1.02 * Math.cos(sunAngle), ly = cy + r * 1.02 * Math.sin(sunAngle);
        // atmosphere glow all round
        var g = ctx.createRadialGradient(cx, cy, r * 0.995, cx, cy, r * 1.075);
        g.addColorStop(0, "rgba(70,150,255,0.42)");
        g.addColorStop(0.3, "rgba(50,110,230,0.14)");
        g.addColorStop(1, "rgba(0,0,0,0)");
        ctx.fillStyle = g;
        ctx.beginPath(); ctx.arc(cx, cy, r * 1.075, 0, Math.PI * 2); ctx.fill();
        // brighter glow near the sun
        g = ctx.createRadialGradient(lx, ly, 0, lx, ly, r * 0.75);
        g.addColorStop(0, "rgba(160,215,255,0.55)");
        g.addColorStop(0.35, "rgba(90,160,255,0.16)");
        g.addColorStop(1, "rgba(0,0,0,0)");
        ctx.fillStyle = g;
        ctx.beginPath(); ctx.arc(cx, cy, r * 1.06, 0, Math.PI * 2); ctx.arc(cx, cy, r, 0, Math.PI * 2, true); ctx.fill();
        // night disc
        ctx.save();
        ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.clip();
        ctx.fillStyle = "#01060e";
        ctx.fillRect(cx - r, cy - r, r * 2, r * 2);
        // city lights on the night side
        for (var k = 0; k < 26; k++) {
            var ax = cx + (rnd() - 0.5) * r * 1.6, ay = cy - r + 60 + rnd() * height * 0.5;
            var d = Math.sqrt((ax - lx) * (ax - lx) + (ay - ly) * (ay - ly));
            if (d < r * 0.55) continue;
            for (var j = 0; j < 22; j++) {
                ctx.fillStyle = "rgba(255,190,110," + (0.15 + rnd() * 0.45) + ")";
                ctx.fillRect(ax + (rnd() - 0.5) * 60, ay + (rnd() - 0.5) * 22, 1.3, 1.3);
            }
        }
        // day side near the sun
        g = ctx.createRadialGradient(lx, ly, 0, lx, ly, r * 0.95);
        g.addColorStop(0, "rgba(125,195,250,1)");
        g.addColorStop(0.08, "rgba(58,128,200,1)");
        g.addColorStop(0.28, "rgba(18,58,110,0.95)");
        g.addColorStop(0.6, "rgba(4,18,40,0.55)");
        g.addColorStop(1, "rgba(0,0,0,0)");
        ctx.fillStyle = g;
        ctx.fillRect(cx - r, cy - r, r * 2, r * 2);
        // cloud streaks on the lit side
        for (k = 0; k < 360; k++) {
            var t = rnd() * Math.PI * 2, rr = Math.pow(rnd(), 0.7) * r * 0.7;
            var px = lx + Math.cos(t) * rr * 1.7, py = ly + Math.abs(Math.sin(t)) * rr * 0.6;
            var fall = Math.max(0, 1 - Math.sqrt((px - lx) * (px - lx) + (py - ly) * (py - ly)) / (r * 0.75));
            var tilt = Math.atan2(py - cy, px - cx) + Math.PI / 2;
            ctx.save();
            ctx.translate(px, py); ctx.rotate(tilt + (rnd() - 0.5) * 0.25); ctx.scale(1, 0.12 + rnd() * 0.14);
            ctx.fillStyle = "rgba(235,245,255," + (fall * (0.035 + rnd() * 0.09)) + ")";
            ctx.beginPath(); ctx.arc(0, 0, 6 + rnd() * 28, 0, Math.PI * 2); ctx.fill();
            ctx.restore();
        }
        ctx.restore();
        // bright limb arc on the sun side
        for (k = 0; k < 18; k++) {
            var span = 0.9 - k * 0.045;
            ctx.strokeStyle = "rgba(195,232,255," + (0.035 + k * 0.03) + ")";
            ctx.lineWidth = k < 16 ? 3 : 1.6;
            ctx.beginPath(); ctx.arc(cx, cy, r, sunAngle - span * 0.6, sunAngle + span); ctx.stroke();
        }
    }
}
