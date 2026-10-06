import QtQuick

// sun just above the limb, with a lens streak
Canvas {
    id: c
    width: 900; height: 300
    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        var x = width / 2, y = height / 2;
        ctx.save();
        ctx.translate(x, y); ctx.scale(1, 0.03);
        var s = ctx.createRadialGradient(0, 0, 0, 0, 0, width / 2);
        s.addColorStop(0, "rgba(255,245,225,0.75)");
        s.addColorStop(0.35, "rgba(255,210,150,0.18)");
        s.addColorStop(1, "rgba(255,200,140,0)");
        ctx.fillStyle = s;
        ctx.beginPath(); ctx.arc(0, 0, width / 2, 0, Math.PI * 2); ctx.fill();
        ctx.restore();
        var g = ctx.createRadialGradient(x, y, 0, x, y, 150);
        g.addColorStop(0, "rgba(255,255,255,1)");
        g.addColorStop(0.05, "rgba(255,249,232,0.95)");
        g.addColorStop(0.14, "rgba(255,222,165,0.42)");
        g.addColorStop(0.4, "rgba(255,190,120,0.1)");
        g.addColorStop(1, "rgba(255,180,110,0)");
        ctx.fillStyle = g;
        ctx.beginPath(); ctx.arc(x, y, 150, 0, Math.PI * 2); ctx.fill();
    }
}
