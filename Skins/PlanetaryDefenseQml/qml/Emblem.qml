import QtQuick

// the skin's own mark: Earth, a tilted orbit, an asteroid on it and an intercept arc
Canvas {
    id: c
    width: 48; height: 48
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        var g = getContext("2d"), w = width, h = height, cx = w / 2, cy = h / 2, R = Math.min(w, h) * 0.24;
        g.reset();
        var earth = g.createRadialGradient(cx - R * 0.35, cy - R * 0.35, R * 0.1, cx, cy, R);
        earth.addColorStop(0, "#7fe6ff");
        earth.addColorStop(0.55, "#1f8fc4");
        earth.addColorStop(1, "#0a2a44");
        g.fillStyle = earth;
        g.beginPath(); g.arc(cx, cy, R, 0, Math.PI * 2); g.fill();
        g.fillStyle = "rgba(2,10,20,0.55)";
        g.beginPath(); g.arc(cx, cy, R, -Math.PI * 0.15, Math.PI * 0.85); g.arc(cx + R * 0.35, cy + R * 0.1, R * 0.9, Math.PI * 0.85, -Math.PI * 0.15, true); g.fill();
        g.save();
        g.translate(cx, cy); g.rotate(-0.42);
        g.strokeStyle = "rgba(39,211,255,0.9)"; g.lineWidth = Math.max(1.2, w / 40);
        g.beginPath(); g.ellipse(-w * 0.44, -h * 0.17, w * 0.88, h * 0.34); g.stroke();
        var ax = w * 0.44 * Math.cos(-0.6), ay = h * 0.17 * Math.sin(-0.6);
        g.strokeStyle = "#39ff7a"; g.lineWidth = Math.max(1.4, w / 32);
        g.beginPath(); g.arc(ax - w * 0.2, ay + h * 0.02, w * 0.2, -0.25, 0.35); g.stroke();
        g.fillStyle = "#ffc23d";
        g.beginPath(); g.arc(ax, ay, Math.max(2, w / 16), 0, Math.PI * 2); g.fill();
        g.restore();
    }
}
