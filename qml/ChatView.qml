import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    color: Theme.chat
    signal openImage(string url, string original)
    signal openProfile(string userId, real x, real y)

    readonly property bool hasChannel: app.channelId.length > 0

    // ------------------------------------------------------------ header
    Rectangle {
        id: header
        width: parent.width
        height: 48
        color: Theme.chat
        z: 2
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rail; opacity: 0.6 }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 12
            spacing: 8
            visible: root.hasChannel

            Icon {
                name: app.guildId ? (app.channelType === 5 ? "megaphone" : "hash") : "at"
                size: 22
                color: Theme.channelIdle
            }
            Text {
                text: app.channelName
                color: Theme.textStrong
                font.pixelSize: 16
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.maximumWidth: root.width * 0.35
            }
            Rectangle {
                visible: app.channelTopic.length > 0
                width: 1
                height: 24
                color: Theme.divider
            }
            Text {
                Layout.fillWidth: true
                text: app.channelTopic
                color: Theme.textMuted
                font.pixelSize: 14
                elide: Text.ElideRight
                maximumLineCount: 1
                textFormat: Text.PlainText
            }
            IconButton {
                icon: "members"
                iconSize: 22
                active: appSettings.showMemberList
                activeColor: Theme.textStrong
                tip: appSettings.showMemberList ? "Hide Member List" : "Show Member List"
                visible: app.guildId.length > 0 || app.channelType === 3
                onClicked: appSettings.showMemberList = !appSettings.showMemberList
            }
        }
    }

    // ------------------------------------------------------------ empty state
    Column {
        anchors.centerIn: parent
        visible: !root.hasChannel && app.ready
        spacing: 12
        Image { anchors.horizontalCenter: parent.horizontalCenter; source: "qrc:/icons/app.png"; width: 96; height: 96; opacity: 0.5 }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: app.guildId ? "No text channels you can see here." : "Pick a conversation to start chatting."
            color: Theme.textMuted
            font.pixelSize: 15
        }
    }

    // ------------------------------------------------------------ messages
    ListView {
        id: list
        anchors.top: header.bottom
        anchors.bottom: composerArea.top
        width: parent.width
        visible: root.hasChannel
        clip: true
        model: app.messages
        verticalLayoutDirection: ListView.BottomToTop
        cacheBuffer: 600
        bottomMargin: 8
        topMargin: 16
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 4000
        maximumFlickVelocity: 6000
        ScrollBar.vertical: ScrollBar {
            id: scrollbar
            width: 12
            onPressedChanged: if (!pressed) list.stick = list.nearBottom
            contentItem: Rectangle { implicitWidth: 8; radius: 4; color: Theme.elevated; opacity: scrollbar.active ? 0.9 : 0.5 }
            background: Rectangle { implicitWidth: 8; radius: 4; color: Theme.card; opacity: 0.4 }
        }

        header: Item {
            // the list header sits at the bottom in BottomToTop layout
            width: list.width
            height: 0
        }
        footer: Item {
            // "beginning of channel" / loading spinner at the top
            width: list.width
            height: app.messages.hasMore ? 56 : 140
            BusyIndicator {
                anchors.centerIn: parent
                running: app.messages.loading
                visible: app.messages.loading
                width: 32; height: 32
            }
            Column {
                visible: !app.messages.hasMore && !app.messages.loading
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 16
                spacing: 6
                Rectangle {
                    width: 68; height: 68; radius: 34
                    color: Theme.selected
                    Icon { anchors.centerIn: parent; name: app.guildId ? "hash" : "at"; size: 40; color: Theme.textStrong }
                }
                Text { text: app.guildId ? "Welcome to #" + app.channelName + "!" : app.channelName; color: Theme.textStrong; font.pixelSize: 28; font.weight: Font.Bold }
                Text { text: "This is the start of the conversation."; color: Theme.textMuted; font.pixelSize: 15 }
            }
        }

        delegate: MessageDelegate {
            width: list.width
            onOpenImage: (url, original) => root.openImage(url, original)
            onOpenProfile: (id, x, y) => root.openProfile(id, x, y)
        }

        // keep the newest message in view unless the user scrolled up
        property bool stick: true
        readonly property bool nearBottom: contentY >= originY + contentHeight + bottomMargin - height - 40
        onMovementEnded: stick = nearBottom
        onContentHeightChanged: if (stick) positionViewAtBeginning()
        onHeightChanged: if (stick) positionViewAtBeginning()
        onContentYChanged: maybeLoadMore()
        onCountChanged: maybeLoadMore()
        function maybeLoadMore() {
            if (count > 0 && visibleArea.yPosition < 0.15) app.messages.loadMore()
        }

        Connections {
            target: app.messages
            function onPositionRequested(row) {
                list.stick = false
                list.positionViewAtIndex(row, ListView.Center)
                list.highlightRow = row
                highlightTimer.restart()
            }
            function onNewMessageArrived(fromSelf) {
                if (fromSelf) list.stick = true
                if (list.stick) list.positionViewAtBeginning()
            }
            function onModelReset() {
                list.stick = true
                list.positionViewAtBeginning()
            }
        }
        property int highlightRow: -1
        Timer { id: highlightTimer; interval: 2000; onTriggered: list.highlightRow = -1 }
    }

    // jump to present
    Rectangle {
        anchors.bottom: composerArea.top
        anchors.bottomMargin: 12
        anchors.horizontalCenter: parent.horizontalCenter
        visible: root.hasChannel && !list.nearBottom && list.contentHeight > list.height * 1.5
        width: jumpText.implicitWidth + 28
        height: 30
        radius: 15
        color: Theme.accent
        Text { id: jumpText; anchors.centerIn: parent; text: "Jump to Present"; color: "white"; font.pixelSize: 13; font.weight: Font.DemiBold }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { list.stick = true; list.positionViewAtBeginning() } }
    }

    // ------------------------------------------------------------ composer
    Item {
        id: composerArea
        anchors.bottom: parent.bottom
        width: parent.width
        height: root.hasChannel ? composer.height + typing.height + 8 : 0
        visible: root.hasChannel

        Composer {
            id: composer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottom: typing.top
        }
        Item {
            id: typing
            anchors.bottom: parent.bottom
            width: parent.width
            height: 24
            Row {
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                visible: app.typingText.length > 0
                Row {
                    spacing: 3
                    anchors.verticalCenter: parent.verticalCenter
                    Repeater {
                        model: 3
                        Rectangle {
                            required property int index
                            width: 6; height: 6; radius: 3
                            color: Theme.text
                            SequentialAnimation on opacity {
                                loops: Animation.Infinite
                                running: app.typingText.length > 0
                                PauseAnimation { duration: index * 160 }
                                NumberAnimation { from: 0.3; to: 1; duration: 300 }
                                NumberAnimation { from: 1; to: 0.3; duration: 300 }
                                PauseAnimation { duration: (2 - index) * 160 }
                            }
                        }
                    }
                }
                Text {
                    text: app.typingText
                    textFormat: Text.StyledText
                    color: Theme.text
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    // drag & drop files to upload
    DropArea {
        anchors.fill: parent
        enabled: root.hasChannel && app.canAttach
        onDropped: (drop) => {
            if (drop.hasUrls) {
                for (let i = 0; i < drop.urls.length; i++) composer.addFile(drop.urls[i].toString())
            }
        }
        Rectangle {
            anchors.fill: parent
            visible: parent.containsDrag
            color: Qt.rgba(0.35, 0.43, 0.96, 0.25)
            border.color: Theme.accent
            border.width: 2
            Text { anchors.centerIn: parent; text: "Drop to upload to " + (app.guildId ? "#" : "@") + app.channelName; color: "white"; font.pixelSize: 20; font.bold: true }
        }
    }
}
