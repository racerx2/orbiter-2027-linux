import QtQuick

// blue launch button with a cyan glow
Item {
    id: b
    property string label: "LAUNCH"
    signal clicked()
    width: 240; height: 52
    opacity: enabled ? 1 : 0.4
    scale: ma.pressed ? 0.98 : 1
    Behavior on scale { NumberAnimation { duration: 100 } }
    Repeater {
        model: 4
        Rectangle {
            anchors.fill: parent; anchors.margins: -(index + 1) * 3
            radius: 10 + (index + 1) * 3
            color: "transparent"
            border.width: 3
            border.color: Theme.accent
            opacity: (ma.containsMouse && b.enabled ? 0.30 : 0.16) * (1 - index / 4)
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: 10
        border.width: 1.5
        border.color: Theme.accentHi
        gradient: Gradient {
            GradientStop { position: 0; color: ma.containsMouse && b.enabled ? "#4fa8ff" : Theme.blueHi }
            GradientStop { position: 1; color: Theme.blue }
        }
    }
    Row {
        anchors.centerIn: parent
        spacing: 12
        Canvas {
            width: 13; height: 16
            anchors.verticalCenter: parent.verticalCenter
            onPaint: {
                var ctx = getContext("2d");
                ctx.fillStyle = "#ffffff";
                ctx.beginPath(); ctx.moveTo(1, 1); ctx.lineTo(13, 8); ctx.lineTo(1, 15); ctx.closePath(); ctx.fill();
            }
        }
        Text {
            text: b.label
            color: "#ffffff"
            font.family: Theme.font; font.pixelSize: Math.round(b.height * 0.32); font.weight: Font.Bold; font.letterSpacing: 4
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: b.clicked() }
}
