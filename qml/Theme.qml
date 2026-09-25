pragma Singleton
import QtQuick

QtObject {
    // surfaces
    readonly property color rail: "#1e1f22"
    readonly property color sidebar: "#2b2d31"
    readonly property color chat: "#313338"
    readonly property color panel: "#232428"
    readonly property color input: "#383a40"
    readonly property color elevated: "#111214"
    readonly property color card: "#2b2d31"
    readonly property color hover: "#35373c"
    readonly property color selected: "#404249"
    readonly property color messageHover: "#2e3035"
    readonly property color divider: "#3f4147"
    readonly property color popup: "#111214"

    // text
    readonly property color text: "#dbdee1"
    readonly property color textStrong: "#f2f3f5"
    readonly property color textMuted: "#949ba4"
    readonly property color textFaint: "#6d6f78"
    readonly property color link: "#00a8fc"
    readonly property color channelIdle: "#80848e"

    // accents
    readonly property color accent: "#5b6ef5"
    readonly property color accentHover: "#4a5bd4"
    readonly property color green: "#23a55a"
    readonly property color yellow: "#f0b232"
    readonly property color red: "#f23f43"
    readonly property color mentionBg: "#3a3629"
    readonly property color mentionBgHover: "#403b2c"
    readonly property color newMarker: "#f23f43"

    readonly property string font: Qt.platform.os === "windows" ? "Segoe UI" : "Noto Sans"
    readonly property string mono: Qt.platform.os === "windows" ? "Consolas" : "DejaVu Sans Mono"

    function statusColor(s) {
        switch (s) {
        case "online": return green
        case "idle": return yellow
        case "dnd": return red
        case "streaming": return "#593695"
        default: return "#80848e"
        }
    }
}
