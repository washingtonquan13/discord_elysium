import QtQuick
import QtQuick.Controls.Basic

Item {
    Rectangle {
        id: header
        width: parent.width
        height: 48
        color: "transparent"
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: "Direct Messages"
            color: Theme.textStrong
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rail; opacity: 0.8 }
    }

    ListView {
        id: list
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        clip: true
        topMargin: 8
        model: app.dms
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            width: 6
            contentItem: Rectangle { radius: 3; color: Theme.elevated; opacity: 0.8 }
        }
        delegate: Item {
            id: row
            required property string itemId
            required property string name
            required property var avatar
            required property var status
            required property bool unread
            required property int mentions
            required property bool isGroup
            required property string subtitle
            readonly property bool isSelected: app.channelId === itemId
            width: list.width
            height: 44

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 1
                anchors.bottomMargin: 1
                radius: 4
                color: row.isSelected ? Theme.selected : area.containsMouse ? Theme.hover : "transparent"
            }
            Avatar {
                id: av
                x: 16
                width: 32
                height: 32
                anchors.verticalCenter: parent.verticalCenter
                source: row.avatar || ""
                fallback: row.name.substring(0, 1).toUpperCase()
                placeholderColor: row.isGroup ? Theme.accent : Theme.selected
            }
            StatusDot {
                visible: !row.isGroup
                status: row.status || "offline"
                ring: row.isSelected ? Theme.selected : area.containsMouse ? Theme.hover : Theme.sidebar
                x: av.x + av.width - 12
                y: av.y + av.height - 12
            }
            Column {
                anchors.left: av.right
                anchors.leftMargin: 12
                anchors.right: badge.visible ? badge.left : parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    width: parent.width
                    text: row.name
                    elide: Text.ElideRight
                    font.pixelSize: 15
                    font.weight: row.unread || row.isSelected ? Font.DemiBold : Font.Medium
                    color: row.isSelected || row.unread ? Theme.textStrong : area.containsMouse ? Theme.text : Theme.channelIdle
                }
                Text {
                    visible: row.subtitle.length > 0
                    text: row.subtitle
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
            }
            Badge {
                id: badge
                count: row.mentions
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
            }
            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: app.selectChannel(row.itemId)
            }
        }
    }
}
