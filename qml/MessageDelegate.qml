import QtQuick
import QtQuick.Controls.Basic

Item {
    id: msg
    required property int index
    required property string messageId
    required property string authorId
    required property string authorName
    required property string authorColor
    required property var avatar
    required property string time
    required property string fullTime
    required property string shortTime
    required property string html
    required property string plain
    required property bool grouped
    required property bool edited
    required property var attachments
    required property var embeds
    required property var reactions
    required property var reply
    required property bool mentionsMe
    required property string systemText
    required property bool pending
    required property bool failed
    required property string dayDivider
    required property bool firstUnread
    required property bool isBot
    required property bool isSelf
    required property string stickers
    required property bool jumbo

    signal openImage(string url, string original)
    signal openProfile(string userId, real x, real y)

    readonly property bool hovered: hover.hovered || toolbar.hovered
    readonly property bool hasReply: reply && reply.messageId !== undefined
    readonly property bool compact: grouped && !hasReply
    readonly property int gutter: 72

    height: column.height

    Column {
        id: column
        width: parent.width

        // date separator
        Item {
            visible: msg.dayDivider.length > 0
            width: parent.width
            height: visible ? 32 : 0
            Rectangle { anchors.verticalCenter: parent.verticalCenter; x: 16; width: parent.width - 32; height: 1; color: Theme.divider }
            Rectangle {
                anchors.centerIn: parent
                width: dayText.implicitWidth + 12
                height: 16
                color: Theme.chat
                Text { id: dayText; anchors.centerIn: parent; text: msg.dayDivider; color: Theme.textMuted; font.pixelSize: 12; font.weight: Font.DemiBold }
            }
        }

        // unread marker
        Item {
            visible: msg.firstUnread
            width: parent.width
            height: visible ? 20 : 0
            Rectangle { anchors.verticalCenter: parent.verticalCenter; x: 16; width: parent.width - 32; height: 1; color: Theme.newMarker }
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                width: 36; height: 14; radius: 3
                color: Theme.newMarker
                Text { anchors.centerIn: parent; text: "NEW"; color: "white"; font.pixelSize: 10; font.bold: true }
            }
        }

        Item {
            id: body
            width: parent.width
            height: content.height + (msg.compact ? 4 : 20)

            Rectangle {
                anchors.fill: parent
                color: msg.mentionsMe ? (msg.hovered ? Theme.mentionBgHover : Theme.mentionBg)
                     : (ListView.view && ListView.view.highlightRow === msg.index) ? "#3a3c63"
                     : msg.hovered ? Theme.messageHover : "transparent"
                Rectangle { visible: msg.mentionsMe; width: 2; height: parent.height; color: Theme.yellow }
            }

            HoverHandler { id: hover }

            // avatar or hover timestamp in the gutter
            Avatar {
                id: avatar
                visible: !msg.compact && msg.systemText.length === 0
                x: 16
                y: (msg.hasReply ? 26 : 0) + 18
                width: 40
                height: 40
                source: msg.avatar || ""
                fallback: msg.authorName.substring(0, 1).toUpperCase()
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        const p = mapToItem(null, width + 8, 0)
                        msg.openProfile(msg.authorId, p.x, p.y)
                    }
                }
            }
            Text {
                visible: msg.compact && msg.hovered
                x: 0
                width: msg.gutter - 8
                horizontalAlignment: Text.AlignRight
                y: 6
                text: msg.shortTime
                color: Theme.textFaint
                font.pixelSize: 11
            }
            Icon {
                visible: msg.systemText.length > 0
                x: 34
                y: 12
                name: "chevron-right"
                size: 16
                color: Theme.green
            }

            Column {
                id: content
                x: msg.gutter
                y: msg.compact ? 2 : 16
                width: parent.width - msg.gutter - 48
                spacing: 4

                // reply reference
                Item {
                    visible: msg.hasReply
                    width: parent.width
                    height: visible ? 22 : 0
                    // connector from the avatar column to the quoted message
                    Rectangle { x: -36; y: 10; width: 30; height: 2; radius: 1; color: Theme.divider }
                    Rectangle { x: -36; y: 10; width: 2; height: 14; radius: 1; color: Theme.divider }
                    Row {
                        spacing: 4
                        anchors.verticalCenter: parent.verticalCenter
                        Avatar {
                            visible: msg.hasReply && !msg.reply.deleted
                            width: 16; height: 16
                            source: msg.hasReply ? (msg.reply.avatar || "") : ""
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: msg.hasReply && !msg.reply.deleted
                            text: msg.hasReply ? "@" + (msg.reply.author || "") : ""
                            color: msg.hasReply ? (msg.reply.color || Theme.text) : Theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            width: content.width - 160
                            text: !msg.hasReply ? "" : msg.reply.deleted ? "Original message was deleted" : (msg.reply.content || "")
                            color: Theme.textMuted
                            font.pixelSize: 13
                            font.italic: msg.hasReply && msg.reply.deleted
                            elide: Text.ElideRight
                            maximumLineCount: 1
                            textFormat: Text.PlainText
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (msg.hasReply && !msg.reply.deleted) app.messages.jumpTo(msg.reply.messageId)
                    }
                }

                // author line
                Row {
                    visible: !msg.compact && msg.systemText.length === 0
                    spacing: 6
                    height: visible ? implicitHeight : 0
                    Text {
                        text: msg.authorName
                        color: msg.authorColor
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const p = mapToItem(null, width + 8, 0)
                                msg.openProfile(msg.authorId, p.x, p.y)
                            }
                        }
                    }
                    Rectangle {
                        visible: msg.isBot
                        width: botText.implicitWidth + 8
                        height: 15
                        radius: 3
                        color: Theme.accent
                        anchors.verticalCenter: parent.verticalCenter
                        Text { id: botText; anchors.centerIn: parent; text: "BOT"; color: "white"; font.pixelSize: 10; font.bold: true }
                    }
                    Text {
                        text: msg.time
                        color: Theme.textFaint
                        font.pixelSize: 12
                        anchors.baseline: parent.children[0].baseline
                        MouseArea { id: timeArea; anchors.fill: parent; hoverEnabled: true }
                        Tooltip { visible: timeArea.containsMouse; text: msg.fullTime }
                    }
                }

                // system message line
                Text {
                    visible: msg.systemText.length > 0
                    width: parent.width
                    text: msg.systemText + "  <span style='color:#6d6f78;font-size:12px'>" + msg.time + "</span>"
                    textFormat: Text.StyledText
                    color: Theme.textMuted
                    font.pixelSize: 15
                    wrapMode: Text.Wrap
                }

                // text
                RichText {
                    visible: msg.systemText.length === 0 && (msg.html.length > 0 || msg.edited)
                    width: parent.width
                    text: msg.html + (msg.edited ? " <span style='font-size:10px;color:#949ba4'>(edited)</span>" : "")
                    color: msg.failed ? Theme.red : msg.pending ? Theme.textMuted : Theme.text
                    font.pixelSize: msg.jumbo ? 40 : 15
                    onLinkClicked: (url) => {
                        if (url.startsWith("kestrel://user/")) {
                            const p = mapToItem(null, 0, 0)
                            msg.openProfile(url.substring(15), p.x + 40, p.y)
                        } else {
                            app.handleLink(url)
                        }
                    }
                }
                Text {
                    visible: msg.failed
                    text: "Failed to send. Right-click to copy the text."
                    color: Theme.red
                    font.pixelSize: 12
                }
                Text {
                    visible: msg.stickers.length > 0
                    text: "🏷 Sticker: " + msg.stickers
                    color: Theme.textMuted
                    font.pixelSize: 14
                    font.italic: true
                }

                // attachments
                Repeater {
                    model: msg.attachments
                    delegate: Loader {
                        required property var modelData
                        sourceComponent: (modelData.isImage || (modelData.isVideo && modelData.preview)) ? mediaAttachment : fileAttachment
                        property var att: modelData
                    }
                }

                // embeds
                Repeater {
                    model: msg.embeds
                    delegate: Loader {
                        required property var modelData
                        property var embed: modelData
                        sourceComponent: modelData.mediaOnly ? mediaEmbed : richEmbed
                    }
                }

                // reactions
                Flow {
                    visible: msg.reactions.length > 0
                    width: parent.width
                    spacing: 4
                    Repeater {
                        model: msg.reactions
                        delegate: Rectangle {
                            required property var modelData
                            height: 26
                            width: reactRow.implicitWidth + 16
                            radius: 8
                            color: modelData.me ? Qt.rgba(0.36, 0.43, 0.96, 0.15) : Theme.card
                            border.width: 1
                            border.color: modelData.me ? Theme.accent : (reactArea.containsMouse ? Theme.divider : "transparent")
                            Row {
                                id: reactRow
                                anchors.centerIn: parent
                                spacing: 6
                                Text {
                                    visible: modelData.emoji.length > 0
                                    text: modelData.emoji
                                    font.pixelSize: 15
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Avatar {
                                    visible: modelData.image.length > 0
                                    width: 18; height: 18; radius: 0
                                    placeholderColor: "transparent"
                                    source: modelData.image
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: modelData.count
                                    color: modelData.me ? "#c9cdfb" : Theme.textMuted
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            MouseArea {
                                id: reactArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: app.toggleReaction(msg.messageId, modelData.key, modelData.me)
                            }
                            Tooltip { visible: reactArea.containsMouse; text: ":" + modelData.name + ":" }
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: (mouse) => { if (msg.systemText.length === 0) contextMenu.popup() }
            }
        }
    }

    // ------------------------------------------------------------ hover toolbar
    Rectangle {
        id: toolbar
        property bool hovered: tbHover.hovered
        visible: msg.hovered && !msg.pending && msg.systemText.length === 0
        anchors.right: parent.right
        anchors.rightMargin: 16
        y: column.height - body.height - 16
        z: 5
        height: 34
        width: tbRow.implicitWidth + 4
        radius: 6
        color: Theme.chat
        border.color: Theme.rail
        HoverHandler { id: tbHover }
        Row {
            id: tbRow
            anchors.centerIn: parent
            IconButton { icon: "smile"; tip: "Add Reaction"; width: 30; height: 30; onClicked: quickReactions.popup() }
            IconButton { icon: "reply"; tip: "Reply"; width: 30; height: 30; onClicked: app.startReply(msg.messageId, msg.authorName) }
            IconButton { icon: "edit"; tip: "Edit"; width: 30; height: 30; visible: msg.isSelf; onClicked: app.startEdit(msg.messageId) }
            IconButton { icon: "more"; tip: "More"; width: 30; height: 30; onClicked: contextMenu.popup() }
        }
    }

    EmojiPicker {
        id: quickReactions
        x: parent.width - width - 16
        y: Math.max(-toolbar.y, toolbar.y - height)
        onPicked: (item) => app.toggleReaction(msg.messageId, item.reaction, false)
        function popup() { open() }
    }

    ContextMenu {
        id: contextMenu
        model: [
            { label: "Add Reaction", icon: "smile", action: () => quickReactions.open(), show: !msg.pending },
            { label: "Reply", icon: "reply", action: () => app.startReply(msg.messageId, msg.authorName), show: !msg.pending },
            { label: "Edit Message", icon: "edit", action: () => app.startEdit(msg.messageId), show: msg.isSelf && !msg.pending },
            { label: "Copy Text", icon: "copy", action: () => app.copyText(msg.plain), show: msg.plain.length > 0 },
            { label: "Copy Message Link", icon: "external", action: () => app.copyText("https://discord.com/channels/" + (app.guildId || "@me") + "/" + app.channelId + "/" + msg.messageId), show: !msg.pending },
            { label: "Copy Message ID", icon: "copy", action: () => app.copyText(msg.messageId), show: !msg.pending },
            { label: "Delete Message", icon: "trash", danger: true, action: () => window.confirmAction("Delete Message", "Are you sure you want to delete this message?", "Delete", () => app.deleteMessage(msg.messageId)), show: (msg.isSelf || app.canManageMessages) && !msg.pending }
        ]
    }

    // ------------------------------------------------------------ attachment/embed components
    Component {
        id: mediaAttachment
        Item {
            width: att.width
            height: att.height
            Rectangle { anchors.fill: parent; radius: 8; color: Theme.card }
            Avatar {
                id: media
                anchors.fill: parent
                radius: 8
                placeholderColor: Theme.card
                source: att.preview || ""
            }
            // spoiler cover
            Rectangle {
                id: spoiler
                anchors.fill: parent
                radius: 8
                visible: att.spoiler
                color: "#1e1f22"
                Text { anchors.centerIn: parent; text: "SPOILER"; color: "white"; font.bold: true; font.pixelSize: 14 }
            }
            Rectangle {
                visible: att.isVideo
                anchors.centerIn: parent
                width: 48; height: 48; radius: 24
                color: Qt.rgba(0, 0, 0, 0.6)
                Icon { anchors.centerIn: parent; name: "play"; size: 26; color: "white" }
            }
            Rectangle {
                visible: att.isGif && !att.isVideo
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 6
                width: 30; height: 18; radius: 4
                color: Qt.rgba(0, 0, 0, 0.6)
                Text { anchors.centerIn: parent; text: "GIF"; color: "white"; font.pixelSize: 11; font.bold: true }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (spoiler.visible) { spoiler.visible = false; return }
                    if (att.isVideo) app.handleLink(att.url)
                    else msg.openImage(att.viewer, att.url)
                }
            }
        }
    }

    Component {
        id: fileAttachment
        Rectangle {
            width: Math.min(420, content.width)
            height: 64
            radius: 8
            color: Theme.card
            border.color: Theme.rail
            Icon { id: fileIcon; x: 14; anchors.verticalCenter: parent.verticalCenter; name: "file"; size: 30; color: Theme.accent }
            Column {
                anchors.left: fileIcon.right
                anchors.leftMargin: 10
                anchors.right: dl.left
                anchors.verticalCenter: parent.verticalCenter
                Text { width: parent.width; text: att.filename; color: Theme.link; font.pixelSize: 15; elide: Text.ElideMiddle }
                Text { text: att.size; color: Theme.textMuted; font.pixelSize: 12 }
            }
            IconButton { id: dl; anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; icon: "download"; tip: "Download"; onClicked: app.handleLink(att.url) }
        }
    }

    Component {
        id: mediaEmbed
        Item {
            width: embed.imageWidth || 0
            height: embed.imageHeight || 0
            Avatar {
                anchors.fill: parent
                radius: 8
                placeholderColor: Theme.card
                source: embed.image || ""
            }
            Rectangle {
                visible: embed.isGifv
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 6
                width: 30; height: 18; radius: 4
                color: Qt.rgba(0, 0, 0, 0.6)
                Text { anchors.centerIn: parent; text: "GIF"; color: "white"; font.pixelSize: 11; font.bold: true }
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: msg.openImage(embed.imageFull, embed.url) }
        }
    }

    Component {
        id: richEmbed
        Rectangle {
            width: Math.min(520, content.width)
            height: embedCol.height + 24
            radius: 4
            color: Theme.card
            Rectangle { width: 4; height: parent.height; radius: 2; color: embed.color }
            Column {
                id: embedCol
                x: 16
                y: 12
                width: parent.width - 32 - (embed.thumbnail ? embed.thumbWidth + 16 : 0)
                spacing: 6
                Text { visible: !!embed.provider; text: embed.provider || ""; color: Theme.textMuted; font.pixelSize: 12; width: parent.width; elide: Text.ElideRight }
                Row {
                    visible: !!embed.author
                    spacing: 8
                    Avatar { visible: !!embed.authorIcon; width: 24; height: 24; source: embed.authorIcon || "" }
                    Text { text: embed.author || ""; color: Theme.textStrong; font.pixelSize: 14; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                }
                Text {
                    visible: !!embed.title
                    width: parent.width
                    text: embed.title || ""
                    color: embed.url ? Theme.link : Theme.textStrong
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    MouseArea { anchors.fill: parent; enabled: !!embed.url; cursorShape: Qt.PointingHandCursor; onClicked: app.handleLink(embed.url) }
                }
                RichText {
                    visible: !!embed.description
                    width: parent.width
                    text: embed.description || ""
                    font.pixelSize: 14
                    onLinkClicked: (url) => app.handleLink(url)
                }
                Flow {
                    width: parent.width
                    spacing: 8
                    visible: embed.fields && embed.fields.length > 0
                    Repeater {
                        model: embed.fields || []
                        delegate: Column {
                            required property var modelData
                            width: modelData.inline ? (embedCol.width - 16) / 3 : embedCol.width
                            spacing: 2
                            RichText { width: parent.width; text: modelData.name; font.pixelSize: 14; font.bold: true; color: Theme.textStrong }
                            RichText { width: parent.width; text: modelData.value; font.pixelSize: 14; onLinkClicked: (url) => app.handleLink(url) }
                        }
                    }
                }
                Avatar {
                    visible: !!embed.image
                    width: embed.imageWidth || 0
                    height: embed.image ? embed.imageHeight : 0
                    radius: 4
                    placeholderColor: Theme.rail
                    source: embed.image || ""
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: msg.openImage(embed.imageFull, embed.url || "") }
                }
                Text { visible: !!embed.footer; text: embed.footer || ""; color: Theme.textMuted; font.pixelSize: 12; width: parent.width; wrapMode: Text.Wrap }
            }
            Avatar {
                visible: !!embed.thumbnail
                anchors.right: parent.right
                anchors.rightMargin: 16
                y: 16
                width: embed.thumbWidth || 0
                height: embed.thumbHeight || 0
                radius: 4
                source: embed.thumbnail || ""
            }
        }
    }
}
