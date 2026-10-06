import QtQuick

Column {
    property string overline
    property string title
    property string subtitle
    spacing: 6
    Text { text: parent.overline; color: Theme.accent; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 3 }
    Text { text: parent.title; color: Theme.text; font.family: Theme.font; font.pixelSize: 44; font.weight: Font.DemiBold }
    Text { text: parent.subtitle; color: Theme.textDim; font.family: Theme.font; font.pixelSize: 15 }
}
