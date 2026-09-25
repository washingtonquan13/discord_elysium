import QtQuick

Rectangle {
    property int count: 0
    property color ring: Theme.rail
    visible: count > 0
    height: 16
    width: Math.max(16, label.implicitWidth + 9)
    radius: 8
    color: Theme.red
    border.color: ring
    border.width: 0
    Text {
        id: label
        anchors.centerIn: parent
        text: count > 99 ? "99+" : count
        color: "white"
        font.family: Theme.font
        font.pixelSize: 11
        font.bold: true
    }
}
