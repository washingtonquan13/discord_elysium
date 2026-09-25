import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    minimumWidth: 940
    minimumHeight: 500
    visible: true
    title: app.channelName ? (app.guildId ? "#" : "@") + app.channelName + " — Kestrel" : "Kestrel"
    color: Theme.rail
    font.family: Theme.font

    onActiveChanged: if (active) app.markCurrentRead()
    onClosing: (close) => {
        if (appSettings.minimizeToTray && app.loggedIn) {
            close.accepted = false
            window.hide()
        }
    }

    Loader {
        anchors.fill: parent
        active: !app.loggedIn
        sourceComponent: LoginView {}
    }

    Item {
        id: main
        anchors.fill: parent
        visible: app.loggedIn

        RowLayout {
            anchors.fill: parent
            spacing: 0

            GuildRail {
                Layout.preferredWidth: 72
                Layout.fillHeight: true
            }

            // channel / DM column
            Rectangle {
                Layout.preferredWidth: 240
                Layout.fillHeight: true
                color: Theme.sidebar
                radius: 8
                // square off the right side; round top-left like Discord
                Rectangle { anchors.right: parent.right; width: 8; height: parent.height; color: Theme.sidebar }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 8; color: Theme.sidebar }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Loader {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        sourceComponent: app.guildId ? channelSidebar : dmSidebar
                    }
                    VoicePanel {
                        Layout.fillWidth: true
                        visible: app.voiceState !== "disconnected"
                    }
                    UserPanel {
                        Layout.fillWidth: true
                        onOpenSettings: settingsDialog.open()
                        onOpenProfile: (id, x, y) => window.showProfile(id, x, y)
                    }
                }
            }

            ChatView {
                id: chat
                Layout.fillWidth: true
                Layout.fillHeight: true
                onOpenImage: (url, original) => viewer.show(url, original)
                onOpenProfile: (id, x, y) => window.showProfile(id, x, y)
            }

            MemberList {
                Layout.preferredWidth: 240
                Layout.fillHeight: true
                visible: appSettings.showMemberList && app.channelId.length > 0 && (app.guildId.length > 0 || app.channelType === 3)
                onOpenProfile: (id, x, y) => window.showProfile(id, x, y)
            }
        }

        // connecting overlay
        Rectangle {
            anchors.fill: parent
            color: Theme.rail
            visible: !app.ready
            opacity: visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
            Column {
                anchors.centerIn: parent
                spacing: 16
                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    source: "qrc:/icons/app.png"
                    width: 88
                    height: 88
                    RotationAnimation on rotation {
                        from: -6; to: 6; duration: 900
                        loops: Animation.Infinite
                        easing.type: Easing.InOutSine
                        running: !app.ready
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: app.connectionState === "reconnecting" ? "Reconnecting…" : "Connecting…"
                    color: Theme.textMuted
                    font.pixelSize: 14
                    font.family: Theme.font
                }
            }
        }

        // reconnect banner
        Rectangle {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 8
            visible: app.ready && app.connectionState !== "connected"
            width: bannerText.implicitWidth + 32
            height: 32
            radius: 16
            color: Theme.yellow
            Text {
                id: bannerText
                anchors.centerIn: parent
                text: "Connection lost — reconnecting…"
                color: "#1e1f22"
                font.pixelSize: 13
                font.bold: true
                font.family: Theme.font
            }
        }
    }

    Component { id: channelSidebar; ChannelSidebar {} }
    Component { id: dmSidebar; DmSidebar {} }

    SettingsDialog { id: settingsDialog }
    ImageViewer { id: viewer }
    ProfilePopup { id: profile }

    function showProfile(userId, x, y) {
        profile.showFor(userId, x, y)
    }

    // toast
    Rectangle {
        id: toast
        property alias text: toastText.text
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 90
        width: Math.min(toastText.implicitWidth + 32, parent.width - 80)
        height: toastText.implicitHeight + 20
        radius: 8
        color: Theme.popup
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Text {
            id: toastText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, window.width - 112)
            color: Theme.textStrong
            wrapMode: Text.Wrap
            font.pixelSize: 14
            font.family: Theme.font
        }
        Timer { id: toastTimer; interval: 3500; onTriggered: toast.opacity = 0 }
    }
    Connections {
        target: app
        function onToast(text) {
            toast.text = text
            toast.opacity = 1
            toastTimer.restart()
        }
    }

    QuickSwitcher { id: switcher }

    // generic confirmation dialog
    function confirmAction(title, text, button, action) {
        confirmBox.title = title
        confirmBox.text = text
        confirmBox.button = button
        confirmBox.action = action
        confirmBox.open()
    }
    Popup {
        id: confirmBox
        property string title
        property string text
        property string button
        property var action
        anchors.centerIn: parent
        width: 440
        modal: true
        focus: true
        padding: 0
        background: Rectangle { color: Theme.chat; radius: 8 }
        contentItem: Column {
            Column {
                padding: 16
                spacing: 12
                width: confirmBox.width
                Text { text: confirmBox.title; color: Theme.textStrong; font.pixelSize: 20; font.weight: Font.DemiBold }
                Text { text: confirmBox.text; color: Theme.text; font.pixelSize: 15; wrapMode: Text.Wrap; width: confirmBox.width - 32 }
            }
            Rectangle {
                width: confirmBox.width
                height: 64
                color: Theme.sidebar
                radius: 8
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Button {
                        width: 96; height: 38
                        onClicked: confirmBox.close()
                        contentItem: Text { text: "Cancel"; color: Theme.text; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { color: "transparent" }
                    }
                    Button {
                        id: confirmButton
                        width: 96; height: 38
                        onClicked: { confirmBox.close(); if (confirmBox.action) confirmBox.action() }
                        contentItem: Text { text: confirmBox.button; color: "white"; font.pixelSize: 14; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 4; color: confirmButton.hovered ? Qt.darker(Theme.red, 1.15) : Theme.red }
                    }
                }
            }
        }
        Keys.onReturnPressed: { confirmBox.close(); if (confirmBox.action) confirmBox.action() }
    }

    Shortcut {
        sequence: "Ctrl+,"
        onActivated: settingsDialog.open()
    }
    Shortcut {
        sequences: ["Ctrl+K", "Ctrl+T"]
        enabled: app.ready
        onActivated: switcher.open()
    }
    Shortcut {
        sequence: "Alt+Down"
        enabled: app.ready
        onActivated: window.stepChannel(1)
    }
    Shortcut {
        sequence: "Alt+Up"
        enabled: app.ready
        onActivated: window.stepChannel(-1)
    }
    function stepChannel(delta) {
        const m = app.guildId ? app.channels : app.dms
        const ids = []
        for (let i = 0; i < m.rowCount(); i++) {
            const idx = m.index(i, 0)
            if (app.guildId && m.data(idx, 257) !== "channel") continue   // KindRole
            const t = app.guildId ? m.data(idx, 260) : 0                    // TypeRole
            if (t === 2 || t === 13) continue
            ids.push(m.data(idx, app.guildId ? 258 : 257))                 // IdRole
        }
        const i = ids.indexOf(app.channelId)
        if (ids.length > 0) app.selectChannel(ids[(i + delta + ids.length) % ids.length])
    }
}
