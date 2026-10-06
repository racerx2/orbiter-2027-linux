import QtQuick

// small planet-and-ring mark drawn in code
Canvas {
    width: 34; height: 34
    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.fillStyle = Theme.text;
        ctx.beginPath(); ctx.arc(17, 17, 6.5, 0, Math.PI * 2); ctx.fill();
        ctx.save();
        ctx.translate(17, 17); ctx.rotate(-0.45); ctx.scale(1, 0.38);
        ctx.strokeStyle = Theme.accent; ctx.lineWidth = 3.2;
        ctx.beginPath(); ctx.arc(0, 0, 15, 0, Math.PI * 2); ctx.stroke();
        ctx.restore();
        ctx.fillStyle = Theme.text;
        ctx.beginPath(); ctx.arc(17, 17, 6.5, Math.PI * 1.05, Math.PI * 1.95); ctx.fill();
    }
}
