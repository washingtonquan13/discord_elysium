import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

Column {
    id: root
    spacing: 0
    property var files: []
    property var tokens: ({})

    // ---- autocomplete for @mentions, #channels and :emoji:
    property var acItems: []
    property int acIndex: 0
    property int acStart: 0
    property string acTrigger: ""
    function updateAutocomplete() {
        const before = input.text.substring(0, input.cursorPosition)
        const m = before.match(/(^|\s)([@#:])([^\s@#:]{0,32})$/)
        if (!m || (m[2] === ":" && m[3].length < 2) || !app.canSend) { acItems = []; return }
        acTrigger = m[2]
        acStart = before.length - m[3].length - 1
        acItems = app.autocomplete(m[2], m[3])
        acIndex = 0
    }
    function insertToken(display, value, replaceFrom) {
        const from = replaceFrom >= 0 ? replaceFrom : input.cursorPosition
        const before = input.text.substring(0, from)
        const after = input.text.substring(input.cursorPosition)
        const ins = display + " "
        input.text = before + ins + after
        input.cursorPosition = before.length + ins.length
        if (display !== value) {
            const t = tokens
            t[display] = value
            tokens = t
        }
        acItems = []
        input.forceActiveFocus()
    }
    function wireText(text) {
        const keys = Object.keys(tokens).sort((a, b) => b.length - a.length)
        for (const k of keys) text = text.split(k).join(tokens[k])
        return text
    }

    function addFile(url) {
        const f = files.slice()
        f.push(url)
        files = f
    }

    Connections {
        target: app
        function onFocusComposer() { input.forceActiveFocus() }
        function onEditTextRequested(text) {
            input.text = text
            input.cursorPosition = text.length
            input.forceActiveFocus()
        }
        function onEditingChanged() { if (app.editingId.length === 0 && editBar.wasEditing) { input.text = ""; editBar.wasEditing = false } }
    }

    // reply bar
    Rectangle {
        visible: app.replyTo.messageId !== undefined
        width: parent.width
        height: visible ? 34 : 0
        radius: 8
        color: Theme.card
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 8; color: Theme.card }
        Text {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            text: "Replying to <b>" + (app.replyTo.author || "") + "</b>"
            textFormat: Text.StyledText
            color: Theme.textMuted
            font.pixelSize: 14
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Text {
                text: "@ " + (app.replyTo.mention === false ? "OFF" : "ON")
                color: app.replyTo.mention === false ? Theme.textMuted : Theme.link
                font.pixelSize: 13
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: app.setReplyMention(app.replyTo.mention === false) }
            }
            IconButton { icon: "close"; iconSize: 16; width: 24; height: 24; showBackground: false; onClicked: app.cancelReply() }
        }
    }

    // edit bar
    Rectangle {
        id: editBar
        property bool wasEditing: false
        visible: app.editingId.length > 0
        onVisibleChanged: if (visible) wasEditing = true
        width: parent.width
        height: visible ? 30 : 0
        radius: 8
        color: Theme.card
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 8; color: Theme.card }
        Text {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            text: "Editing message — <font color='#00a8fc'>escape</font> to cancel • <font color='#00a8fc'>enter</font> to save"
            textFormat: Text.StyledText
            color: Theme.textMuted
            font.pixelSize: 13
        }
    }

    // pending attachments
    Rectangle {
        visible: root.files.length > 0
        width: parent.width
        height: visible ? 120 : 0
        color: Theme.input
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.divider }
        ListView {
            anchors.fill: parent
            anchors.margins: 12
            orientation: ListView.Horizontal
            spacing: 12
            model: root.files
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: 120; height: 96; radius: 6
                color: Theme.card
                Image {
                    id: thumb
                    anchors.fill: parent
                    anchors.margins: 6
                    anchors.bottomMargin: 24
                    fillMode: Image.PreserveAspectFit
                    source: /\.(png|jpe?g|gif|webp|bmp)$/i.test(modelData) ? modelData : ""
                    sourceSize: Qt.size(200, 200)
                    asynchronous: true
                }
                Icon { visible: thumb.source == ""; anchors.centerIn: parent; anchors.verticalCenterOffset: -8; name: "file"; size: 36; color: Theme.accent }
                Text {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    x: 6
                    width: parent.width - 12
                    text: decodeURIComponent(modelData.split("/").pop())
                    elide: Text.ElideMiddle
                    color: Theme.text
                    font.pixelSize: 11
                }
                IconButton {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: -6
                    width: 24; height: 24
                    icon: "trash"
                    iconSize: 14
                    color: Theme.red
                    onClicked: {
                        const f = root.files.slice()
                        f.splice(index, 1)
                        root.files = f
                    }
                }
            }
        }
    }

    Rectangle {
        width: parent.width
        height: Math.min(Math.max(44, input.contentHeight + 22), 320)
        radius: (app.replyTo.messageId !== undefined || app.editingId.length > 0 || root.files.length > 0) ? 0 : 8
        color: Theme.input
        Rectangle { visible: parent.radius === 0; anchors.bottom: parent.bottom; width: parent.width; height: 8; radius: 8; color: Theme.input }

        IconButton {
            id: attach
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            icon: "plus-circle"
            iconSize: 24
            showBackground: false
            visible: app.canAttach
            tip: "Upload a file"
            onClicked: fileDialog.open()
        }

        IconButton {
            id: emojiButton
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            icon: "smile"
            iconSize: 24
            showBackground: false
            visible: app.canSend
            width: visible ? 32 : 0
            tip: "Select emoji"
            onClicked: picker.open()
        }

        ScrollView {
            id: scroll
            anchors.left: attach.visible ? attach.right : parent.left
            anchors.leftMargin: attach.visible ? 6 : 16
            anchors.right: emojiButton.left
            anchors.rightMargin: 4
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 2
            anchors.bottomMargin: 2
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            TextArea {
                id: input
                enabled: app.canSend
                wrapMode: TextArea.Wrap
                color: Theme.text
                font.pixelSize: 15
                font.family: Theme.font
                selectionColor: "#3d5afe"
                selectedTextColor: "white"
                placeholderText: !app.canSend ? "You do not have permission to send messages in this channel."
                                : "Message " + (app.guildId ? "#" : "@") + app.channelName
                placeholderTextColor: Theme.textFaint
                verticalAlignment: TextEdit.AlignVCenter
                topPadding: 11
                bottomPadding: 11
                background: null

                onTextChanged: {
                    if (text.length > 0) app.userTyping()
                    root.updateAutocomplete()
                }
                onCursorPositionChanged: if (root.acItems.length > 0) root.updateAutocomplete()

                Keys.onPressed: (event) => {
                    if (root.acItems.length > 0) {
                        if (event.key === Qt.Key_Down) { root.acIndex = (root.acIndex + 1) % root.acItems.length; event.accepted = true; return }
                        if (event.key === Qt.Key_Up) { root.acIndex = (root.acIndex - 1 + root.acItems.length) % root.acItems.length; event.accepted = true; return }
                        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            const it = root.acItems[root.acIndex]
                            root.insertToken(it.display, it.value, root.acStart)
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Escape) { root.acItems = []; event.accepted = true; return }
                    }
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                        event.accepted = true
                        if (text.trim().length === 0 && root.files.length === 0) return
                        app.sendMessage(root.wireText(text), root.files)
                        text = ""
                        root.files = []
                        root.tokens = {}
                    } else if (event.key === Qt.Key_Escape) {
                        if (app.editingId.length > 0) { app.cancelEdit(); text = "" }
                        else if (app.replyTo.messageId !== undefined) app.cancelReply()
                        else app.markCurrentRead()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Up && text.length === 0) {
                        app.startEdit("")
                        event.accepted = true
                    }
                }
            }
        }
    }

    // autocomplete popup
    Rectangle {
        id: acBox
        visible: root.acItems.length > 0
        parent: root.parent
        x: root.x
        y: root.y - height - 8
        width: root.width
        height: acList.contentHeight + 40
        radius: 8
        color: Theme.sidebar
        z: 20
        Text {
            x: 12; y: 10
            text: root.acTrigger === "@" ? "MEMBERS" : root.acTrigger === "#" ? "TEXT CHANNELS" : "EMOJI MATCHING"
            color: Theme.textMuted
            font.pixelSize: 12
            font.bold: true
        }
        ListView {
            id: acList
            x: 8
            y: 32
            width: parent.width - 16
            height: contentHeight
            interactive: false
            model: root.acItems
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: acList.width
                height: 34
                radius: 4
                color: index === root.acIndex ? Theme.selected : "transparent"
                Row {
                    x: 8
                    spacing: 10
                    anchors.verticalCenter: parent.verticalCenter
                    Item {
                        width: 24; height: 24
                        Avatar { anchors.fill: parent; visible: !!modelData.image; source: modelData.image || ""; radius: root.acTrigger === ":" ? 0 : 12; placeholderColor: "transparent" }
                        Text { anchors.centerIn: parent; visible: !!modelData.emoji; text: modelData.emoji || ""; font.pixelSize: 20 }
                        Icon { anchors.centerIn: parent; visible: !!modelData.icon; name: modelData.icon || ""; size: 20 }
                    }
                    Text {
                        text: modelData.label
                        color: modelData.color ? modelData.color : Theme.textStrong
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.sub || ""
                        color: Theme.textMuted
                        font.pixelSize: 13
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: root.acIndex = index
                    onClicked: root.insertToken(modelData.display, modelData.value, root.acStart)
                }
            }
        }
    }

    EmojiPicker {
        id: picker
        parent: root
        x: root.width - width
        y: -height - 8
        onPicked: (item) => root.insertToken(item.emoji ? item.emoji : ":" + item.name + ":", item.value, -1)
    }

    FileDialog {
        id: fileDialog
        title: "Upload files"
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            for (let i = 0; i < selectedFiles.length; i++) root.addFile(selectedFiles[i].toString())
            input.forceActiveFocus()
        }
    }
}
