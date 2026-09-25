import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    color: Theme.rail

    ListView {
        id: list
        anchors.fill: parent
        anchors.topMargin: 12
        model: app.guilds
        spacing: 8
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar { width: 0 }

        delegate: Item {
            id: row
            required property string kind
            required property string itemId
            required property var name
            required property var icon
            required property var initials
            required property var unread
            required property var mentions
            required property string folderColor
            required property bool inFolder
            required property bool expanded
            required property var folderIcons

            width: list.width
            height: kind === "separator" ? 2 : 48

            readonly property bool isSelected: kind === "home" ? app.guildId.length === 0
                                             : kind === "guild" ? app.guildId === itemId : false
            readonly property bool hovered: area.containsMouse

            // left selection pill
            Rectangle {
                visible: row.kind !== "separator"
                x: -4
                width: 8
                radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: "white"
                height: row.isSelected ? 40 : row.hovered ? 20 : (row.unread ? 8 : 0)
                Behavior on height { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            }

            Rectangle {
                visible: row.kind === "separator"
                anchors.centerIn: parent
                width: 32
                height: 2
                radius: 1
                color: Theme.divider
            }

            // folder background tray when expanded
            Rectangle {
                visible: row.inFolder || (row.kind === "folder" && row.expanded)
                anchors.horizontalCenter: parent.horizontalCenter
                width: 48
                height: parent.height + 8
                y: -4
                color: Qt.rgba(1, 1, 1, 0.04)
                radius: row.kind === "folder" ? 16 : 0
            }

            Item {
                id: tile
                visible: row.kind !== "separator"
                width: 48
                height: 48
                anchors.horizontalCenter: parent.horizontalCenter
                readonly property real r: (row.isSelected || row.hovered) ? 16 : 24

                // home button
                Rectangle {
                    visible: row.kind === "home"
                    anchors.fill: parent
                    radius: tile.r
                    color: row.isSelected || row.hovered ? Theme.accent : Theme.chat
                    Behavior on radius { NumberAnimation { duration: 120 } }
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/icons/app.png"
                        width: 30
                        height: 30
                        visible: !(row.isSelected || row.hovered)
                    }
                    Icon {
                        anchors.centerIn: parent
                        name: "dm"
                        size: 24
                        color: "white"
                        visible: row.isSelected || row.hovered
                    }
                }

                // server icon
                Avatar {
                    visible: row.kind === "guild" && row.icon
                    anchors.fill: parent
                    source: row.kind === "guild" ? row.icon : ""
                    radius: tile.r
                    Behavior on radius { NumberAnimation { duration: 120 } }
                }
                Rectangle {
                    visible: row.kind === "guild" && !row.icon
                    anchors.fill: parent
                    radius: tile.r
                    color: row.isSelected || row.hovered ? Theme.accent : Theme.chat
                    Behavior on radius { NumberAnimation { duration: 120 } }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 6
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: row.initials || ""
                        color: Theme.textStrong
                        font.pixelSize: text.length > 3 ? 11 : 15
                        font.weight: Font.Medium
                    }
                }

                // folder
                Rectangle {
                    visible: row.kind === "folder"
                    anchors.fill: parent
                    radius: 16
                    color: row.expanded ? "transparent" : Qt.rgba(Qt.color(row.folderColor).r, Qt.color(row.folderColor).g, Qt.color(row.folderColor).b, 0.3)
                    Icon {
                        visible: row.expanded
                        anchors.centerIn: parent
                        name: "folder"
                        size: 26
                        color: row.folderColor
                    }
                    Grid {
                        visible: !row.expanded
                        anchors.centerIn: parent
                        columns: 2
                        spacing: 4
                        Repeater {
                            model: row.kind === "folder" ? row.folderIcons : []
                            delegate: Avatar {
                                required property var modelData
                                width: 16
                                height: 16
                                radius: 8
                                source: modelData.icon
                                fallback: modelData.initials.substring(0, 1)
                                fallbackPixelSize: 8
                            }
                        }
                    }
                }

                Badge {
                    count: row.mentions || 0
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: -4
                    anchors.bottomMargin: -4
                    border.width: 3
                    visible: count > 0 && !(row.kind === "folder" && row.expanded)
                }
            }

            MouseArea {
                id: area
                anchors.fill: tile
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                enabled: row.kind !== "separator"
                onClicked: {
                    if (row.kind === "home") app.selectGuild("")
                    else if (row.kind === "guild") app.selectGuild(row.itemId)
                    else if (row.kind === "folder") app.guilds.toggleFolder(row.itemId)
                }
            }
            Tooltip {
                visible: area.containsMouse && !!row.name
                text: row.name || ""
                x: tile.x + tile.width + 12
                y: (row.height - implicitHeight) / 2
            }
        }
    }
}
