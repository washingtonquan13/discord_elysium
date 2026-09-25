import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    // server name header
    Rectangle {
        id: header
        width: parent.width
        height: 48
        color: "transparent"
        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: app.guildName
            color: Theme.textStrong
            font.pixelSize: 15
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rail; opacity: 0.8 }
    }

    ListView {
        id: list
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        clip: true
        model: app.channels
        boundsBehavior: Flickable.StopAtBounds
        topMargin: 8
        bottomMargin: 8
        reuseItems: true
        ScrollBar.vertical: ScrollBar {
            width: 6
            contentItem: Rectangle { radius: 3; color: Theme.elevated; opacity: 0.8 }
        }

        delegate: Item {
            id: row
            required property string kind
            required property string itemId
            required property string name
            required property int channelType
            required property bool unread
            required property int mentions
            required property bool muted
            required property bool collapsed
            required property var voiceMembers
            required property int userLimit

            readonly property bool isCategory: kind === "category"
            readonly property bool isVoice: channelType === 2 || channelType === 13
            readonly property bool isSelected: !isCategory && app.channelId === itemId
            readonly property bool inVoice: isVoice && app.voiceChannelId === itemId

            width: list.width
            height: isCategory ? 40 : 34 + (isVoice ? voiceColumn.height : 0)

            // category header
            Item {
                visible: row.isCategory
                anchors.fill: parent
                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    spacing: 2
                    Icon {
                        name: row.collapsed ? "chevron-right" : "chevron-down"
                        size: 12
                        color: catArea.containsMouse ? Theme.text : Theme.textMuted
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: row.name.toUpperCase()
                        color: catArea.containsMouse ? Theme.text : Theme.textMuted
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.2
                        elide: Text.ElideRight
                        width: row.width - 40
                    }
                }
                MouseArea {
                    id: catArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: app.channels.toggleCategory(row.itemId)
                }
            }

            // channel row
            Item {
                visible: !row.isCategory
                width: parent.width
                height: 34

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.topMargin: 1
                    anchors.bottomMargin: 1
                    radius: 4
                    color: row.isSelected ? Theme.selected : chArea.containsMouse ? Theme.hover : "transparent"
                }
                // unread pill
                Rectangle {
                    visible: row.unread && !row.isSelected
                    x: -4
                    width: 8
                    height: 8
                    radius: 4
                    color: "white"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Icon {
                    id: typeIcon
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    size: 20
                    name: row.channelType === 5 ? "megaphone" : row.isVoice ? "speaker" : row.channelType === 15 || row.channelType === 16 ? "forum" : "hash"
                    color: Theme.channelIdle
                }
                Text {
                    anchors.left: typeIcon.right
                    anchors.leftMargin: 6
                    anchors.right: badge.visible ? badge.left : parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.name
                    elide: Text.ElideRight
                    font.pixelSize: 15
                    font.weight: row.unread || row.isSelected ? Font.DemiBold : Font.Medium
                    color: row.isSelected ? Theme.textStrong
                         : row.unread ? Theme.textStrong
                         : row.muted ? Theme.textFaint
                         : chArea.containsMouse ? Theme.text : Theme.channelIdle
                }
                Badge {
                    id: badge
                    count: row.mentions
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    visible: row.isVoice && row.userLimit > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: (row.voiceMembers ? row.voiceMembers.length : 0) + "/" + row.userLimit
                    color: Theme.textMuted
                    font.pixelSize: 11
                }
                MouseArea {
                    id: chArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: app.selectChannel(row.itemId)
                }
            }

            // people in a voice channel
            Column {
                id: voiceColumn
                y: 34
                width: parent.width
                visible: row.isVoice
                Repeater {
                    model: row.isVoice ? row.voiceMembers : []
                    delegate: Item {
                        required property var modelData
                        width: voiceColumn.width
                        height: 30
                        Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: 36
                            anchors.rightMargin: 8
                            radius: 4
                            color: vmArea.containsMouse ? Theme.hover : "transparent"
                        }
                        Rectangle {
                            id: ring
                            x: 42
                            anchors.verticalCenter: parent.verticalCenter
                            width: 26
                            height: 26
                            radius: 13
                            color: "transparent"
                            border.width: 2
                            border.color: modelData.speaking ? Theme.green : "transparent"
                            Avatar {
                                anchors.centerIn: parent
                                width: 22
                                height: 22
                                source: modelData.avatar
                                fallback: modelData.name.substring(0, 1)
                            }
                        }
                        Text {
                            anchors.left: ring.right
                            anchors.leftMargin: 6
                            anchors.right: icons.left
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name
                            elide: Text.ElideRight
                            color: modelData.speaking ? Theme.textStrong : Theme.textMuted
                            font.pixelSize: 14
                        }
                        Row {
                            id: icons
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Rectangle {
                                visible: modelData.streaming
                                width: live.implicitWidth + 8; height: 16; radius: 8
                                color: Theme.red
                                anchors.verticalCenter: parent.verticalCenter
                                Text { id: live; anchors.centerIn: parent; text: "LIVE"; color: "white"; font.pixelSize: 10; font.bold: true }
                            }
                            Icon { visible: modelData.muted; name: "mic-off"; size: 16; color: Theme.textMuted }
                            Icon { visible: modelData.deafened; name: "headphones-off"; size: 16; color: Theme.textMuted }
                        }
                        MouseArea {
                            id: vmArea
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: (mouse) => {
                                const p = mapToItem(null, mouse.x, mouse.y)
                                window.showProfile(modelData.userId, p.x + 20, p.y)
                            }
                        }
                    }
                }
            }
        }
    }
}
