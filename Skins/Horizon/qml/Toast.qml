import QtQuick

Rectangle {
    id: toast
    function show(msg) { tt.text = msg; opacity = 1; tm.restart(); }
    anchors.horizontalCenter: parent.horizontalCenter
    y: parent.height - 120
    z: 100
    height: 42; width: tt.implicitWidth + 40; radius: 21
    color: Theme.glassHi; border.color: Theme.lineHi
    opacity: 0
    Behavior on opacity { NumberAnimation { duration: 200 } }
    Text { id: tt; anchors.centerIn: parent; color: Theme.text; font.family: Theme.font; font.pixelSize: 14 }
    Timer { id: tm; interval: 2400; onTriggered: toast.opacity = 0 }
}
