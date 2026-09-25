import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root
    color: Theme.sidebar
    signal openProfile(string userId, real x, real y)

    ListView {
        id: list
        anchors.fill: parent
        anchors.topMargin: 0
        clip: true
        model: app.members
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        topMargin: 8
        bottomMargin: 8
        ScrollBar.vertical: ScrollBar {
            width: 6
            contentItem: Rectangle { radius: 3; color: Theme.elevated; opacity: 0.8 }
        }
        // ask Discord for more of the list as the user scrolls
        onContentYChanged: {
            const last = indexAt(10, contentY + height - 10)
            if (last > 0 && last % 100 > 80) app.requestMemberList(last)
        }

        delegate: Item {
            id: row
            required property string kind
            required property var userId
            required property var name
            required property var nameColor
            required property var avatar
            required property var status
            required property var activity
            required property var isBot
            required property var count
            width: list.width
            height: kind === "group" ? 40 : 44

            Text {
                visible: row.kind === "group"
                x: 16
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 6
                text: (row.name || "").toUpperCase() + " — " + (row.count || 0)
                color: Theme.textMuted
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }

            Item {
                visible: row.kind === "member"
                anchors.fill: parent
                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.topMargin: 1
                    anchors.bottomMargin: 1
                    radius: 4
                    color: area.containsMouse ? Theme.hover : "transparent"
                }
                Avatar {
                    id: av
                    x: 16
                    width: 32
                    height: 32
                    anchors.verticalCenter: parent.verticalCenter
                    source: row.avatar || ""
                    fallback: (row.name || "?").substring(0, 1).toUpperCase()
                    opacity: row.status === "offline" ? 0.4 : 1
                }
                StatusDot {
                    visible: row.status !== "offline"
                    status: row.status || "offline"
                    ring: area.containsMouse ? Theme.hover : Theme.sidebar
                    x: av.x + 20
                    y: av.y + 20
                }
                Column {
                    anchors.left: av.right
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    Row {
                        spacing: 4
                        width: parent.width
                        Text {
                            text: row.name || ""
                            width: Math.min(implicitWidth, parent.width - (row.isBot ? 40 : 0))
                            elide: Text.ElideRight
                            color: row.nameColor ? row.nameColor : Theme.channelIdle
                            opacity: row.status === "offline" ? 0.5 : 1
                            font.pixelSize: 15
                            font.weight: Font.Medium
                        }
                        Rectangle {
                            visible: !!row.isBot
                            width: 30; height: 15; radius: 3
                            color: Theme.accent
                            anchors.verticalCenter: parent.verticalCenter
                            Text { anchors.centerIn: parent; text: "BOT"; color: "white"; font.pixelSize: 10; font.bold: true }
                        }
                    }
                    Text {
                        visible: !!row.activity
                        width: parent.width
                        text: row.activity || ""
                        elide: Text.ElideRight
                        color: Theme.textMuted
                        font.pixelSize: 12
                    }
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        const p = mapToItem(null, 0, 0)
                        root.openProfile(row.userId, p.x - 320, p.y)
                    }
                }
            }
        }
    }
}
