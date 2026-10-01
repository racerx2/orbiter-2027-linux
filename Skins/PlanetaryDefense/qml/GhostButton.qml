import QtQuick

Rectangle {
    id: b
    property string label
    property bool star: false
    property bool on: false
    signal clicked()
    height: 64
    width: star ? height : txt.implicitWidth + 56
    opacity: enabled ? 1 : 0.4
    radius: 10
    color: ma.containsMouse ? Theme.tint(0.12) : Qt.rgba(0.02, 0.06, 0.11, 0.85)
    border.color: ma.containsMouse ? Theme.lineHi : Theme.line
    Behavior on color { ColorAnimation { duration: 150 } }
    Text {
        id: txt
        visible: !b.star
        anchors.centerIn: parent
        text: b.label
        color: Theme.text
        font.family: Theme.font; font.pixelSize: b.height < 50 ? 13 : 15; font.weight: Font.DemiBold; font.letterSpacing: b.height < 50 ? 2 : 3
    }
    Canvas {
        id: starIcon
        visible: b.star
        anchors.centerIn: parent
        width: 24; height: 24
        property bool filled: b.on
        onFilledChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            ctx.beginPath();
            for (var i = 0; i < 10; i++) {
                var a = -Math.PI / 2 + i * Math.PI / 5, rr = (i % 2) ? 5 : 11;
                ctx.lineTo(12 + rr * Math.cos(a), 12.5 + rr * Math.sin(a));
            }
            ctx.closePath();
            ctx.lineJoin = "round";
            ctx.lineWidth = 1.6;
            ctx.strokeStyle = filled ? Theme.accent : Theme.text;
            ctx.fillStyle = Theme.accent;
            if (filled) ctx.fill();
            ctx.stroke();
        }
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: b.clicked() }
}
