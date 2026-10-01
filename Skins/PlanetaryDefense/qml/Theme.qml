pragma Singleton
import QtQuick

// Planetary Defense palette: navy, cyan edges with glow, green/amber/red status, monospace data
QtObject {
    readonly property color bg: "#040a14"
    readonly property color text: "#e6f6ff"
    readonly property color textDim: "#8fb4c8"
    readonly property color textFaint: "#5a7a8e"
    readonly property color accent: "#27d3ff"
    readonly property color accentHi: "#7fe6ff"
    readonly property color accentInk: "#02121c"
    readonly property color info: "#2a8bff"
    readonly property color ok: "#39ff7a"
    readonly property color warn: "#ffc23d"
    readonly property color alert: "#ff3646"
    readonly property color blue: "#2a8bff"
    readonly property color blueHi: "#55a6ff"
    readonly property color glass: Qt.rgba(0.027, 0.071, 0.133, 0.90)
    readonly property color glassHi: Qt.rgba(0.047, 0.11, 0.2, 0.94)
    readonly property color line: Qt.rgba(0.153, 0.827, 1, 0.22)
    readonly property color lineHi: Qt.rgba(0.153, 0.827, 1, 0.55)
    readonly property string font: "Noto Sans"
    readonly property string mono: "monospace"
    function tint(a) { return Qt.rgba(0.153, 0.827, 1, a); }
}
