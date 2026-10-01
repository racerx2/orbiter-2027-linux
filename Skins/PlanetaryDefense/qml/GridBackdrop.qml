import QtQuick

// faint cyan grid on navy, drawn once per size
Canvas {
    id: c
    property int step: 44
    onWidthChanged: repaint.restart()
    onHeightChanged: repaint.restart()
    Timer { id: repaint; interval: 120; onTriggered: c.requestPaint() }
    onPaint: {
        var g = getContext("2d");
        g.reset();
        g.strokeStyle = "rgba(39,211,255,0.05)";
        g.lineWidth = 1;
        g.beginPath();
        for (var x = 0.5; x < width; x += step) { g.moveTo(x, 0); g.lineTo(x, height); }
        for (var y = 0.5; y < height; y += step) { g.moveTo(0, y); g.lineTo(width, y); }
        g.stroke();
        var v = g.createRadialGradient(width / 2, height * 0.45, 0, width / 2, height * 0.45, Math.max(width, height) * 0.75);
        v.addColorStop(0, "rgba(10,40,70,0.25)");
        v.addColorStop(1, "rgba(0,0,0,0.35)");
        g.fillStyle = v;
        g.fillRect(0, 0, width, height);
    }
}
