import QtQuick

Item {
    id: b
    property string label: "LAUNCH"
    signal clicked()
    width: 280; height: 64
    opacity: enabled ? 1 : 0.4
    scale: ma.pressed ? 0.98 : (ma.containsMouse && enabled ? 1.02 : 1)
    Behavior on scale { NumberAnimation { duration: 120 } }
    Item {
        anchors.fill: parent
        opacity: ma.containsMouse ? 1 : 0.6
        Behavior on opacity { NumberAnimation { duration: 150 } }
        Repeater {
            model: 6
            Rectangle {
                anchors.fill: parent; anchors.margins: -(index + 1) * 3
                radius: 11 + (index + 1) * 3
                color: "transparent"
                border.width: 3
                border.color: Theme.accent
                opacity: 0.16 * (1 - index / 6)
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: 11
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.accentHi }
            GradientStop { position: 1; color: Theme.accent }
        }
    }
    Row {
        anchors.centerIn: parent
        spacing: 14
        Canvas {
            width: 15; height: 18
            anchors.verticalCenter: parent.verticalCenter
            onPaint: {
                var ctx = getContext("2d");
                ctx.fillStyle = Theme.accentInk;
                ctx.beginPath(); ctx.moveTo(1, 1); ctx.lineTo(15, 9); ctx.lineTo(1, 17); ctx.closePath(); ctx.fill();
            }
        }
        Text {
            text: b.label
            color: Theme.accentInk
            font.family: Theme.font; font.pixelSize: Math.round(b.height * 0.31); font.weight: Font.Bold; font.letterSpacing: 4.5
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: b.clicked() }
}
