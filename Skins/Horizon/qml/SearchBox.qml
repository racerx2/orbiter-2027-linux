import QtQuick

// one-line search field
Rectangle {
    id: box
    property alias text: input.text
    property string placeholder: "Search"
    function focusInput() { input.forceActiveFocus(); }
    height: 38
    radius: 19
    color: Qt.rgba(1, 1, 1, 0.06)
    border.color: input.activeFocus ? Theme.accent : Theme.line
    Canvas {
        id: lens
        x: 14; anchors.verticalCenter: parent.verticalCenter
        width: 14; height: 14
        onPaint: {
            var c = getContext("2d");
            c.reset();
            c.strokeStyle = Theme.textDim; c.lineWidth = 1.6;
            c.beginPath(); c.arc(6, 6, 4.5, 0, Math.PI * 2); c.stroke();
            c.beginPath(); c.moveTo(9.5, 9.5); c.lineTo(13, 13); c.stroke();
        }
    }
    TextInput {
        id: input
        x: 38; width: parent.width - 52
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentInk
        clip: true
        font.family: Theme.font; font.pixelSize: 14
        Keys.onEscapePressed: text = ""
    }
    Text {
        visible: !input.text && !input.activeFocus
        x: 38; anchors.verticalCenter: parent.verticalCenter
        text: box.placeholder
        color: Theme.textFaint
        font.family: Theme.font; font.pixelSize: 14
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.IBeamCursor; onClicked: input.forceActiveFocus(); z: -1 }
}
