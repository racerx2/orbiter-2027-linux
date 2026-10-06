import QtQuick

// a scenario folder in the card grid
Rectangle {
    id: card
    property string title
    property int count: 0
    signal clicked()
    radius: 12
    color: ma.containsMouse ? Theme.glassHi : Theme.glass
    border.color: ma.containsMouse ? Theme.lineHi : Theme.line
    Canvas {
        x: 18; y: 22
        width: 64; height: 50
        onPaint: {
            var c = getContext("2d");
            c.reset();
            c.fillStyle = Theme.tint(0.22);
            c.strokeStyle = Theme.accent; c.lineWidth = 2; c.lineJoin = "round";
            c.beginPath(); c.moveTo(2, 10); c.lineTo(22, 10); c.lineTo(28, 4); c.lineTo(62, 4); c.lineTo(62, 48); c.lineTo(2, 48); c.closePath();
            c.fill(); c.stroke();
        }
    }
    Column {
        x: 18; anchors.bottom: parent.bottom; anchors.bottomMargin: 16
        width: parent.width - 36
        spacing: 5
        Text { text: "FOLDER"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1.8 }
        Text { text: card.title; width: parent.width; elide: Text.ElideRight; color: Theme.text; font.family: Theme.font; font.pixelSize: 16; font.weight: Font.DemiBold }
        Text { text: card.count + (card.count === 1 ? " scenario" : " scenarios"); color: Theme.textDim; font.family: Theme.font; font.pixelSize: 13 }
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: card.clicked() }
}
