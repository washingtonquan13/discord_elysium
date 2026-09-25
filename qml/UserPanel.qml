import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root
    signal openSettings
    signal openProfile(string userId, real x, real y)
    height: 52
    color: Theme.panel

    Rectangle {
        id: who
        x: 8
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - buttons.width - 20
        height: 40
        radius: 4
        color: whoArea.containsMouse ? Theme.hover : "transparent"

        Avatar {
            id: av
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            width: 32
            height: 32
            source: app.selfAvatar
            fallback: app.selfName.substring(0, 1)
        }
        StatusDot {
            status: app.selfStatus
            ring: whoArea.containsMouse ? Theme.hover : Theme.panel
            x: av.x + 20
            y: av.y + 20
        }
        Column {
            anchors.left: av.right
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                text: app.selfName
                color: Theme.textStrong
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: whoArea.containsMouse ? app.selfUsername : ({ online: "Online", idle: "Idle", dnd: "Do Not Disturb", invisible: "Invisible" }[app.selfStatus] || "Online")
                color: Theme.textMuted
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }
        MouseArea {
            id: whoArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: statusMenu.popup(0, -statusMenu.implicitHeight - 8)
        }

        Menu {
            id: statusMenu
            width: 220
            background: Rectangle { color: Theme.popup; radius: 6 }
            Repeater {
                model: [
                    { key: "online", label: "Online" },
                    { key: "idle", label: "Idle" },
                    { key: "dnd", label: "Do Not Disturb" },
                    { key: "invisible", label: "Invisible" }
                ]
                delegate: MenuItem {
                    required property var modelData
                    height: 36
                    contentItem: Row {
                        spacing: 10
                        leftPadding: 4
                        StatusDot { status: modelData.key === "invisible" ? "offline" : modelData.key; ring: Theme.popup; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: modelData.label; color: Theme.text; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                    }
                    background: Rectangle { radius: 3; color: parent.highlighted ? Theme.accent : "transparent" }
                    onTriggered: app.setStatus(modelData.key)
                }
            }
        }
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0
        IconButton {
            icon: app.selfMuted ? "mic-off" : "mic"
            active: app.selfMuted
            tip: app.selfMuted ? "Unmute" : "Mute"
            onClicked: app.toggleMute()
        }
        IconButton {
            icon: app.selfDeafened ? "headphones-off" : "headphones"
            active: app.selfDeafened
            tip: app.selfDeafened ? "Undeafen" : "Deafen"
            onClicked: app.toggleDeafen()
        }
        IconButton {
            icon: "gear"
            tip: "User Settings"
            onClicked: root.openSettings()
        }
    }
}
