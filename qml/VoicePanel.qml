import QtQuick

Rectangle {
    height: 58
    color: Theme.panel

    Rectangle { width: parent.width; height: 1; color: Theme.divider; opacity: 0.5 }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: hangup.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Row {
            spacing: 6
            Icon {
                name: "signal"
                size: 16
                color: app.voiceState === "connected" ? (app.voicePing > 250 ? Theme.yellow : Theme.green) : Theme.yellow
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: app.voiceState === "connected" ? "Voice Connected" : app.voiceState === "error" ? "Connection Failed" : "Connecting…"
                color: app.voiceState === "connected" ? Theme.green : app.voiceState === "error" ? Theme.red : Theme.yellow
                font.pixelSize: 14
                font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
            Icon {
                visible: app.voiceEncrypted
                name: "shield"
                size: 14
                color: Theme.green
                anchors.verticalCenter: parent.verticalCenter
                MouseArea { id: shieldArea; anchors.fill: parent; hoverEnabled: true }
                Tooltip { visible: shieldArea.containsMouse; text: "End-to-end encrypted" }
            }
        }
        Text {
            width: parent.width
            text: app.voiceChannelName + (app.voiceState === "connected" && app.voicePing > 0 ? "  ·  " + app.voicePing + " ms" : "")
            color: Theme.textMuted
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
    IconButton {
        id: hangup
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        icon: "hangup"
        tip: "Disconnect"
        hoverColor: Theme.textStrong
        onClicked: app.leaveVoice()
    }
}
