import QtQuick

// scenario thumbnail painted in code: orbit, runway, moon, pad, atmo
Canvas {
    id: c
    property string kind: "orbit"
    property real corner: 12
    property int seed: 0

    function rng(a) {
        return function() {
            a |= 0; a = a + 0x6D2B79F5 | 0;
            var t = Math.imul(a ^ a >>> 15, 1 | a);
            t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
            return ((t ^ t >>> 14) >>> 0) / 4294967296;
        }
    }
    function vgrad(ctx, y0, y1, stops) {
        var g = ctx.createLinearGradient(0, y0, 0, y1);
        for (var i = 0; i < stops.length; i++) g.addColorStop(stops[i][0], stops[i][1]);
        return g;
    }
    function stars(ctx, r, n, maxY) {
        for (var i = 0; i < n; i++) {
            ctx.fillStyle = "rgba(255,255,255," + (0.2 + r() * 0.7) + ")";
            var s = r() < 0.1 ? 1.6 : 1;
            ctx.fillRect(r() * width, r() * maxY, s, s);
        }
    }

    onKindChanged: requestPaint()
    onSeedChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        var w = width, h = height, k = corner, r = rng(seed * 7919 + kind.length * 991 + 5);
        ctx.beginPath();
        ctx.moveTo(0, h); ctx.lineTo(0, k); ctx.arcTo(0, 0, k, 0, k); ctx.lineTo(w - k, 0); ctx.arcTo(w, 0, w, k, k); ctx.lineTo(w, h); ctx.closePath();
        ctx.clip();
        var g;
        if (kind === "orbit" || kind === "hst") {
            ctx.fillStyle = vgrad(ctx, 0, h, [[0, "#01030a"], [1, "#071630"]]);
            ctx.fillRect(0, 0, w, h);
            stars(ctx, r, 60, h * 0.6);
            var R = h * 2.4, cx = w * 0.45, cy = h * 0.52 + R;
            g = ctx.createRadialGradient(cx, cy, R, cx, cy, R * 1.05);
            g.addColorStop(0, "rgba(90,170,255,0.6)"); g.addColorStop(1, "rgba(90,170,255,0)");
            ctx.fillStyle = g; ctx.beginPath(); ctx.arc(cx, cy, R * 1.05, 0, Math.PI * 2); ctx.fill();
            g = ctx.createLinearGradient(0, h * 0.52, 0, h);
            g.addColorStop(0, "#7fc0f2"); g.addColorStop(0.08, "#2c6fb4"); g.addColorStop(0.5, "#123e73"); g.addColorStop(1, "#0a2446");
            ctx.fillStyle = g; ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2); ctx.fill();
            for (var i = 0; i < 26; i++) {
                ctx.fillStyle = "rgba(255,255,255," + (0.12 + r() * 0.25) + ")";
                ctx.save(); ctx.translate(r() * w, h * 0.62 + r() * h * 0.38); ctx.scale(1, 0.25);
                ctx.beginPath(); ctx.arc(0, 0, 6 + r() * 16, 0, Math.PI * 2); ctx.fill(); ctx.restore();
            }
            if (kind === "hst") {
                var hx = w * 0.58, hy = h * 0.3;
                ctx.save(); ctx.translate(hx, hy); ctx.rotate(-0.35);
                ctx.fillStyle = "#6f8fb8"; ctx.fillRect(-26, -12, 16, 7); ctx.fillRect(-26, 5, 16, 7);
                ctx.fillStyle = "#d9dde2"; ctx.fillRect(-12, -5, 30, 10);
                ctx.fillStyle = "#9aa3ad"; ctx.fillRect(18, -5, 4, 10);
                ctx.fillStyle = "#1a1f26"; ctx.beginPath(); ctx.arc(22, 0, 3, 0, Math.PI * 2); ctx.fill();
                ctx.restore();
            } else {
            // station silhouette
            var sx = w * 0.6, sy = h * 0.3;
            ctx.fillStyle = "#c9d3df"; ctx.fillRect(sx - 46, sy - 1, 92, 2.4);
            ctx.fillStyle = "#b8903f";
            var pxs = [-44, -32, 24, 36];
            for (i = 0; i < 4; i++) { ctx.fillRect(sx + pxs[i], sy - 15, 8, 13); ctx.fillRect(sx + pxs[i], sy + 3.4, 8, 13); }
            ctx.fillStyle = "#e3e8ee"; ctx.fillRect(sx - 8, sy - 4, 16, 8); ctx.fillRect(sx - 2.5, sy - 12, 5, 24);
            ctx.fillStyle = "#9aa6b4"; ctx.fillRect(sx - 14, sy - 3, 6, 6);
            }
        } else if (kind === "runway") {
            ctx.fillStyle = vgrad(ctx, 0, h, [[0, "#141a3a"], [0.45, "#6a3d6a"], [0.62, "#f09a5a"], [0.63, "#2a2226"], [1, "#0c0b10"]]);
            ctx.fillRect(0, 0, w, h);
            stars(ctx, r, 18, h * 0.3);
            ctx.fillStyle = "#1c1b21";
            ctx.beginPath(); ctx.moveTo(w * 0.47, h * 0.63); ctx.lineTo(w * 0.53, h * 0.63); ctx.lineTo(w * 0.85, h); ctx.lineTo(w * 0.15, h); ctx.closePath(); ctx.fill();
            for (i = 0; i < 9; i++) {
                var t = i / 8, yy = h * 0.64 + t * t * h * 0.36, half = (0.03 + t * 0.33) * w;
                ctx.fillStyle = "rgba(255,210,140,0.95)";
                ctx.fillRect(w / 2 - half - 2, yy, 2 + t * 2, 2 + t * 2); ctx.fillRect(w / 2 + half, yy, 2 + t * 2, 2 + t * 2);
                ctx.fillStyle = "rgba(255,255,255,0.8)"; ctx.fillRect(w / 2 - 0.5 - t, yy, 1 + t * 2, 1 + t * 3);
            }
            ctx.fillStyle = "#0d0c11"; ctx.fillRect(w * 0.08, h * 0.53, 6, h * 0.1); ctx.fillRect(w * 0.84, h * 0.5, 10, h * 0.13);
        } else if (kind === "moon") {
            ctx.fillStyle = "#010205"; ctx.fillRect(0, 0, w, h);
            stars(ctx, r, 70, h * 0.6);
            var ex = w * 0.2, ey = h * 0.24;
            ctx.fillStyle = "#0a1a33"; ctx.beginPath(); ctx.arc(ex, ey, 11, 0, Math.PI * 2); ctx.fill();
            g = ctx.createRadialGradient(ex - 6, ey - 3, 1, ex - 6, ey - 3, 13);
            g.addColorStop(0, "#9fd0ff"); g.addColorStop(0.6, "#2d6db3"); g.addColorStop(1, "rgba(20,60,120,0)");
            ctx.fillStyle = g; ctx.beginPath(); ctx.arc(ex, ey, 11, 0, Math.PI * 2); ctx.fill();
            var mR = w * 2.2, mcx = w * 0.4, mcy = h * 0.6 + mR;
            ctx.fillStyle = vgrad(ctx, h * 0.6, h, [[0, "#a9a9a6"], [0.3, "#6d6c69"], [1, "#2b2a28"]]);
            ctx.beginPath(); ctx.arc(mcx, mcy, mR, 0, Math.PI * 2); ctx.fill();
            for (i = 0; i < 16; i++) {
                var qx = r() * w, qy = h * 0.66 + r() * h * 0.34, qr = 3 + r() * 11 * (qy / h);
                ctx.save(); ctx.translate(qx, qy); ctx.scale(1, 0.32);
                ctx.fillStyle = "rgba(30,30,28,0.55)"; ctx.beginPath(); ctx.arc(0, 0, qr, 0, Math.PI * 2); ctx.fill();
                ctx.strokeStyle = "rgba(220,220,215,0.35)"; ctx.lineWidth = 1.4; ctx.beginPath(); ctx.arc(0, 0, qr, Math.PI * 0.1, Math.PI * 0.9); ctx.stroke();
                ctx.restore();
            }
            ctx.fillStyle = "rgba(120,220,255,0.9)"; ctx.fillRect(w * 0.3, h * 0.7, 3, 3); ctx.fillRect(w * 0.34, h * 0.71, 2, 2); ctx.fillRect(w * 0.27, h * 0.72, 2, 2);
        } else if (kind === "runwayday") {
            ctx.fillStyle = vgrad(ctx, 0, h, [[0, "#2f6fb4"], [0.5, "#a9d0ee"], [0.52, "#6f8a5a"], [1, "#3c5233"]]);
            ctx.fillRect(0, 0, w, h);
            for (i = 0; i < 14; i++) {
                ctx.fillStyle = "rgba(255,255,255," + (0.5 + r() * 0.4) + ")";
                ctx.save(); ctx.translate(r() * w, h * 0.12 + r() * h * 0.3); ctx.scale(1, 0.35);
                ctx.beginPath(); ctx.arc(0, 0, 8 + r() * 22, 0, Math.PI * 2); ctx.fill(); ctx.restore();
            }
            ctx.fillStyle = "#3a3d40";
            ctx.beginPath(); ctx.moveTo(w * 0.48, h * 0.52); ctx.lineTo(w * 0.52, h * 0.52); ctx.lineTo(w * 0.9, h); ctx.lineTo(w * 0.1, h); ctx.closePath(); ctx.fill();
            ctx.fillStyle = "rgba(255,255,255,0.85)";
            for (i = 0; i < 7; i++) { var ty = h * 0.55 + i * i * h * 0.012; ctx.fillRect(w / 2 - 1 - i * 0.4, ty, 2 + i * 0.8, 3 + i * 1.5); }
            var gx0 = w * 0.5, gy0 = h * 0.86;
            ctx.fillStyle = "#eef1f4";
            ctx.beginPath(); ctx.moveTo(gx0, gy0 - 16); ctx.lineTo(gx0 + 34, gy0 + 6); ctx.lineTo(gx0 + 10, gy0 + 4); ctx.lineTo(gx0, gy0 + 8); ctx.lineTo(gx0 - 10, gy0 + 4); ctx.lineTo(gx0 - 34, gy0 + 6); ctx.closePath(); ctx.fill();
            ctx.fillStyle = "#a9b2bb"; ctx.fillRect(gx0 - 12, gy0 + 2, 5, 3); ctx.fillRect(gx0 + 7, gy0 + 2, 5, 3);
        } else if (kind === "ice") {
            ctx.fillStyle = vgrad(ctx, 0, h, [[0, "#6f93b8"], [0.5, "#dfe9f2"], [0.55, "#f4f8fb"], [1, "#b9cad8"]]);
            ctx.fillRect(0, 0, w, h);
            ctx.fillStyle = "rgba(170,190,210,0.7)";
            ctx.beginPath(); ctx.moveTo(0, h * 0.56); ctx.lineTo(w * 0.25, h * 0.48); ctx.lineTo(w * 0.4, h * 0.55); ctx.lineTo(w * 0.62, h * 0.46); ctx.lineTo(w, h * 0.55); ctx.lineTo(w, h * 0.58); ctx.lineTo(0, h * 0.58); ctx.closePath(); ctx.fill();
            for (i = 0; i < 12; i++) {
                ctx.strokeStyle = "rgba(140,165,190," + (0.2 + r() * 0.3) + ")"; ctx.lineWidth = 1;
                var iy = h * 0.62 + r() * h * 0.36; ctx.beginPath(); ctx.moveTo(r() * w, iy); ctx.lineTo(r() * w, iy + (r() - 0.5) * 6); ctx.stroke();
            }
            var fx = w * 0.62, fy = h * 0.74;
            ctx.fillStyle = "#e9edf2"; ctx.beginPath(); ctx.moveTo(fx, fy - 7); ctx.lineTo(fx + 20, fy + 3); ctx.lineTo(fx - 20, fy + 3); ctx.closePath(); ctx.fill();
            ctx.fillStyle = "#ff5a3c"; ctx.fillRect(w * 0.3, h * 0.7, 3, 8); ctx.fillRect(w * 0.3 + 3, h * 0.7, 7, 4);
        } else if (kind === "mars") {
            ctx.fillStyle = vgrad(ctx, 0, h, [[0, "#3b2a2a"], [0.45, "#c79b7c"], [0.58, "#e0b894"], [0.6, "#a4552e"], [1, "#5a2a16"]]);
            ctx.fillRect(0, 0, w, h);
            ctx.fillStyle = "rgba(120,55,30,0.9)";
            ctx.beginPath(); ctx.moveTo(0, h * 0.6); ctx.lineTo(w * 0.18, h * 0.5); ctx.lineTo(w * 0.34, h * 0.58); ctx.lineTo(w * 0.52, h * 0.47); ctx.lineTo(w * 0.7, h * 0.57); ctx.lineTo(w, h * 0.52); ctx.lineTo(w, h * 0.62); ctx.lineTo(0, h * 0.62); ctx.closePath(); ctx.fill();
            for (i = 0; i < 18; i++) {
                ctx.fillStyle = "rgba(60,25,12," + (0.3 + r() * 0.4) + ")";
                ctx.save(); ctx.translate(r() * w, h * 0.66 + r() * h * 0.34); ctx.scale(1, 0.3);
                ctx.beginPath(); ctx.arc(0, 0, 2 + r() * 8, 0, Math.PI * 2); ctx.fill(); ctx.restore();
            }
            ctx.fillStyle = "rgba(255,240,220,0.9)"; ctx.beginPath(); ctx.arc(w * 0.78, h * 0.2, 4, 0, Math.PI * 2); ctx.fill();
        } else if (kind === "lunarorbit") {
            ctx.fillStyle = "#010205"; ctx.fillRect(0, 0, w, h);
            stars(ctx, r, 60, h * 0.7);
            var ex2 = w * 0.66, ey2 = h * 0.5;
            g = ctx.createRadialGradient(ex2 - 5, ey2 - 4, 1, ex2, ey2, 17);
            g.addColorStop(0, "#bfe3ff"); g.addColorStop(0.45, "#3f86c8"); g.addColorStop(0.9, "#12325c"); g.addColorStop(1, "rgba(18,50,92,0)");
            ctx.fillStyle = g; ctx.beginPath(); ctx.arc(ex2, ey2, 16, 0, Math.PI * 2); ctx.fill();
            var lR = w * 1.6, lcx = w * 0.3, lcy = h * 0.66 + lR;
            ctx.fillStyle = vgrad(ctx, h * 0.66, h, [[0, "#cfcfca"], [0.25, "#8e8d88"], [1, "#3a3936"]]);
            ctx.beginPath(); ctx.arc(lcx, lcy, lR, 0, Math.PI * 2); ctx.fill();
            for (i = 0; i < 20; i++) {
                var lx = r() * w, ly = h * 0.7 + r() * h * 0.3, lr = 2 + r() * 9 * (ly / h);
                ctx.save(); ctx.translate(lx, ly); ctx.scale(1, 0.3);
                ctx.fillStyle = "rgba(40,40,38,0.5)"; ctx.beginPath(); ctx.arc(0, 0, lr, 0, Math.PI * 2); ctx.fill(); ctx.restore();
            }
        } else if (kind === "pad") {
            ctx.fillStyle = vgrad(ctx, 0, h, [[0, "#0a1230"], [0.5, "#3a2a5a"], [0.8, "#e0804a"], [0.81, "#17121a"], [1, "#07060a"]]);
            ctx.fillRect(0, 0, w, h);
            stars(ctx, r, 22, h * 0.35);
            var bx = w * 0.52, by = h * 0.81;
            g = ctx.createRadialGradient(bx, by, 0, bx, by, 70);
            g.addColorStop(0, "rgba(255,240,200,0.95)"); g.addColorStop(0.2, "rgba(255,170,80,0.55)"); g.addColorStop(1, "rgba(255,120,40,0)");
            ctx.fillStyle = g; ctx.fillRect(bx - 70, by - 70, 140, 140);
            ctx.fillStyle = "#16121a"; ctx.fillRect(bx + 16, by - 60, 7, 60);
            for (i = 0; i < 6; i++) ctx.fillRect(bx + 10, by - 58 + i * 10, 13, 1.5);
            ctx.fillStyle = "#c56a2c"; ctx.fillRect(bx - 4, by - 58, 9, 50);
            ctx.beginPath(); ctx.moveTo(bx - 4, by - 58); ctx.lineTo(bx + 0.5, by - 66); ctx.lineTo(bx + 5, by - 58); ctx.fill();
            ctx.fillStyle = "#ece9e4"; ctx.fillRect(bx - 9, by - 50, 4, 42); ctx.fillRect(bx + 6, by - 50, 4, 42);
            ctx.beginPath(); ctx.moveTo(bx - 14, by - 22); ctx.lineTo(bx - 9, by - 40); ctx.lineTo(bx - 9, by - 18); ctx.fill();
        } else {
            ctx.fillStyle = vgrad(ctx, 0, h, [[0, "#23589a"], [0.52, "#a9d2f2"], [0.53, "#1c6a9c"], [1, "#0b3a62"]]);
            ctx.fillRect(0, 0, w, h);
            ctx.fillStyle = "#3e6b3a";
            ctx.beginPath(); ctx.moveTo(0, h * 0.53); ctx.lineTo(w * 0.28, h * 0.53); ctx.quadraticCurveTo(w * 0.36, h * 0.75, w * 0.22, h); ctx.lineTo(0, h); ctx.closePath(); ctx.fill();
            ctx.strokeStyle = "rgba(240,225,180,0.9)"; ctx.lineWidth = 2.2;
            ctx.beginPath(); ctx.moveTo(w * 0.28, h * 0.53); ctx.quadraticCurveTo(w * 0.36, h * 0.75, w * 0.22, h); ctx.stroke();
            for (i = 0; i < 30; i++) {
                var cyy = h * 0.2 + r() * h * 0.36;
                ctx.fillStyle = "rgba(255,255,255," + (0.35 + r() * 0.45) + ")";
                ctx.save(); ctx.translate(r() * w, cyy); ctx.scale(1, 0.3 + (cyy / h) * 0.2);
                ctx.beginPath(); ctx.arc(0, 0, 6 + r() * 18 * (1 - cyy / h), 0, Math.PI * 2); ctx.fill(); ctx.restore();
            }
            ctx.fillStyle = "#e9edf2";
            var gx = w * 0.62, gy = h * 0.34;
            ctx.beginPath(); ctx.moveTo(gx, gy); ctx.lineTo(gx - 22, gy + 7); ctx.lineTo(gx - 16, gy + 8); ctx.lineTo(gx + 2, gy + 3); ctx.closePath(); ctx.fill();
            ctx.fillRect(gx - 20, gy + 5, 26, 3);
        }
        // darken the bottom edge so text below sits well
        g = ctx.createLinearGradient(0, h * 0.6, 0, h);
        g.addColorStop(0, "rgba(3,6,12,0)"); g.addColorStop(1, "rgba(3,6,12,0.55)");
        ctx.fillStyle = g; ctx.fillRect(0, 0, w, h);
    }
}
