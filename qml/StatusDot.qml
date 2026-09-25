import QtQuick

Rectangle {
    property string status: "offline"
    property color ring: Theme.sidebar
    width: 14
    height: 14
    radius: width / 2
    color: ring
    Rectangle {
        anchors.centerIn: parent
        width: parent.width - 4
        height: width
        radius: width / 2
        color: Theme.statusColor(status)
        // offline is drawn hollow like Discord
        Rectangle {
            visible: status === "offline" || status === "invisible" || status === ""
            anchors.centerIn: parent
            width: parent.width * 0.45
            height: width
            radius: width / 2
            color: ring
        }
    }
}
