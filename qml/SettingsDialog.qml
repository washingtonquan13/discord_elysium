import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: dialog
    width: parent ? parent.width : 900
    height: parent ? parent.height : 600
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { color: Theme.chat }
    property int page: 0

    onOpened: app.refreshDevices()
    onClosed: { app.stopMicTest(); micTest.checked = false; app.applyVoiceSettings() }

    component SectionTitle: Text {
        color: Theme.textMuted
        font.pixelSize: 12
        font.bold: true
        Layout.topMargin: 16
    }
    component Toggle: Switch {
        id: sw
        indicator: Rectangle {
            implicitWidth: 40
            implicitHeight: 24
            x: sw.width - width
            y: (sw.height - height) / 2
            radius: 12
            color: sw.checked ? Theme.green : "#80848e"
            Rectangle {
                x: sw.checked ? parent.width - width - 3 : 3
                y: 3
                width: 18; height: 18; radius: 9
                color: "white"
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        contentItem: Text {
            text: sw.text
            color: Theme.text
            font.pixelSize: 15
            verticalAlignment: Text.AlignVCenter
            rightPadding: 56
        }
    }
    component Radio: RadioButton {
        id: rb
        indicator: Rectangle {
            implicitWidth: 20
            implicitHeight: 20
            x: rb.leftPadding
            y: (rb.height - height) / 2
            radius: 10
            color: "transparent"
            border.width: 2
            border.color: rb.checked ? Theme.accent : Theme.textMuted
            Rectangle { anchors.centerIn: parent; width: 10; height: 10; radius: 5; color: Theme.accent; visible: rb.checked }
        }
        contentItem: Text { text: rb.text; color: Theme.text; leftPadding: rb.indicator.width + 10; font.pixelSize: 15; verticalAlignment: Text.AlignVCenter }
    }
    component StyledSlider: Slider {
        id: sl
        background: Rectangle {
            x: sl.leftPadding
            y: sl.topPadding + sl.availableHeight / 2 - height / 2
            width: sl.availableWidth
            height: 8
            radius: 4
            color: Theme.rail
            Rectangle { width: sl.visualPosition * parent.width; height: parent.height; radius: 4; color: Theme.accent }
        }
        handle: Rectangle {
            x: sl.leftPadding + sl.visualPosition * (sl.availableWidth - width)
            y: sl.topPadding + sl.availableHeight / 2 - height / 2
            width: 10
            height: 24
            radius: 3
            color: "white"
        }
    }
    component Dropdown: ComboBox {
        id: cb
        Layout.fillWidth: true
        implicitHeight: 40
        contentItem: Text {
            leftPadding: 12
            text: cb.displayText
            color: Theme.text
            font.pixelSize: 14
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle { color: Theme.rail; radius: 4 }
        popup.background: Rectangle { color: Theme.popup; radius: 4 }
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: cb.width
            highlighted: cb.highlightedIndex === index
            contentItem: Text { text: modelData; color: Theme.text; font.pixelSize: 14; elide: Text.ElideRight }
            background: Rectangle { color: highlighted ? Theme.hover : "transparent" }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // navigation
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: Math.max(220, dialog.width * 0.28)
            color: Theme.sidebar
            Column {
                anchors.right: parent.right
                anchors.rightMargin: 8
                y: 60
                width: 200
                spacing: 2
                Text { text: "USER SETTINGS"; color: Theme.textMuted; font.pixelSize: 12; font.bold: true; leftPadding: 10; bottomPadding: 6 }
                Repeater {
                    model: ["Voice & Audio", "Appearance", "Notifications", "Account"]
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        width: 200
                        height: 34
                        radius: 4
                        color: dialog.page === index ? Theme.selected : navArea.containsMouse ? Theme.hover : "transparent"
                        Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: modelData; color: dialog.page === index ? Theme.textStrong : Theme.textMuted; font.pixelSize: 15; font.weight: Font.Medium }
                        MouseArea { id: navArea; anchors.fill: parent; hoverEnabled: true; onClicked: dialog.page = index }
                    }
                }
                Rectangle { width: 200; height: 1; color: Theme.divider }
                Rectangle {
                    width: 200; height: 34; radius: 4
                    color: logoutArea.containsMouse ? Theme.hover : "transparent"
                    Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: "Log Out"; color: Theme.red; font.pixelSize: 15 }
                    MouseArea { id: logoutArea; anchors.fill: parent; hoverEnabled: true; onClicked: { dialog.close(); app.logout() } }
                }
            }
        }

        // pages
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: pages.height + 120
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            StackLayout {
                id: pages
                x: 40
                y: 60
                width: Math.min(660, parent.width - 120)
                currentIndex: dialog.page

                // ---------------------------------------------------- voice
                ColumnLayout {
                    spacing: 8
                    Text { text: "Voice & Audio"; color: Theme.textStrong; font.pixelSize: 20; font.bold: true }
                    Text {
                        visible: !app.voiceAvailable
                        text: "This build doesn't include voice support."
                        color: Theme.yellow
                    }
                    RowLayout {
                        spacing: 16
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            SectionTitle { text: "INPUT DEVICE" }
                            Dropdown {
                                model: app.inputDevices
                                currentIndex: Math.max(0, app.inputDevices.indexOf(appSettings.inputDevice || "Default"))
                                onActivated: (i) => appSettings.inputDevice = (i === 0 ? "" : app.inputDevices[i])
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            SectionTitle { text: "OUTPUT DEVICE" }
                            Dropdown {
                                model: app.outputDevices
                                currentIndex: Math.max(0, app.outputDevices.indexOf(appSettings.outputDevice || "Default"))
                                onActivated: (i) => appSettings.outputDevice = (i === 0 ? "" : app.outputDevices[i])
                            }
                        }
                    }
                    RowLayout {
                        spacing: 16
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            SectionTitle { text: "INPUT VOLUME" }
                            StyledSlider { Layout.fillWidth: true; from: 0; to: 2; value: appSettings.inputVolume; onMoved: appSettings.inputVolume = value }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            SectionTitle { text: "OUTPUT VOLUME" }
                            StyledSlider { Layout.fillWidth: true; from: 0; to: 2; value: appSettings.outputVolume; onMoved: appSettings.outputVolume = value }
                        }
                    }

                    SectionTitle { text: "INPUT MODE" }
                    Radio {
                        text: "Voice Activity"
                        checked: !appSettings.pushToTalk
                        onClicked: appSettings.pushToTalk = false
                    }
                    Radio {
                        text: "Push to Talk"
                        checked: appSettings.pushToTalk
                        onClicked: appSettings.pushToTalk = true
                    }

                    // push to talk key
                    ColumnLayout {
                        visible: appSettings.pushToTalk
                        Layout.fillWidth: true
                        SectionTitle { text: "SHORTCUT" }
                        Rectangle {
                            id: keyBox
                            property bool recording: false
                            Layout.preferredWidth: 300
                            height: 40
                            radius: 4
                            color: Theme.rail
                            border.color: recording ? Theme.red : "transparent"
                            focus: recording
                            Text {
                                x: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: keyBox.recording ? "Press a key or mouse button…" : (appSettings.pushToTalkKeyName || "No keybind set")
                                color: keyBox.recording ? Theme.red : Theme.text
                                font.pixelSize: 14
                            }
                            Keys.onPressed: (event) => {
                                if (!recording) return
                                appSettings.pushToTalkKey = event.nativeVirtualKey
                                appSettings.pushToTalkKeyName = app.keyName(event.key, event.nativeVirtualKey, event.text)
                                recording = false
                                event.accepted = true
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.AllButtons
                                onPressed: (mouse) => {
                                    if (!keyBox.recording) {
                                        keyBox.recording = true
                                        keyBox.forceActiveFocus()
                                        return
                                    }
                                    const map = { [Qt.MiddleButton]: [4, "Mouse 3"], [Qt.BackButton]: [5, "Mouse 4"], [Qt.ForwardButton]: [6, "Mouse 5"] }
                                    if (map[mouse.button]) {
                                        appSettings.pushToTalkKey = map[mouse.button][0]
                                        appSettings.pushToTalkKeyName = map[mouse.button][1]
                                        keyBox.recording = false
                                    }
                                }
                            }
                        }
                        Text { text: "Works while Kestrel is in the background (e.g. in a game)."; color: Theme.textMuted; font.pixelSize: 12 }
                    }

                    // sensitivity
                    ColumnLayout {
                        visible: !appSettings.pushToTalk
                        Layout.fillWidth: true
                        SectionTitle { text: "INPUT SENSITIVITY" }
                        Item {
                            Layout.fillWidth: true
                            height: 32
                            // live mic level behind the threshold slider
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                height: 8
                                radius: 4
                                color: Theme.rail
                                Rectangle {
                                    height: parent.height
                                    radius: 4
                                    width: parent.width * Math.max(0, Math.min(1, (app.inputLevel + 100) / 100))
                                    color: app.inputLevel > appSettings.vadThreshold ? Theme.green : Theme.yellow
                                }
                            }
                            Slider {
                                id: vad
                                anchors.fill: parent
                                from: -100
                                to: 0
                                value: appSettings.vadThreshold
                                onMoved: appSettings.vadThreshold = value
                                background: null
                                handle: Rectangle {
                                    x: vad.leftPadding + vad.visualPosition * (vad.availableWidth - width)
                                    y: vad.topPadding + vad.availableHeight / 2 - height / 2
                                    width: 10; height: 24; radius: 3
                                    color: "white"
                                }
                            }
                        }
                        Text { text: "Threshold: " + Math.round(appSettings.vadThreshold) + " dB. Speak — the bar turns green when you'd transmit."; color: Theme.textMuted; font.pixelSize: 12 }
                    }
                    Button {
                        id: micTest
                        checkable: true
                        Layout.topMargin: 8
                        implicitWidth: 140
                        implicitHeight: 36
                        onToggled: checked ? app.startMicTest() : app.stopMicTest()
                        contentItem: Text { text: micTest.checked ? "Stop Testing" : "Let's Check"; color: "white"; font.pixelSize: 14; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 4; color: micTest.checked ? Theme.red : Theme.accent }
                    }
                    Text { text: "Changes apply when you close settings."; color: Theme.textFaint; font.pixelSize: 12; Layout.topMargin: 8 }
                }

                // ---------------------------------------------------- appearance
                ColumnLayout {
                    spacing: 12
                    Text { text: "Appearance"; color: Theme.textStrong; font.pixelSize: 20; font.bold: true }
                    Toggle { Layout.fillWidth: true; text: "Play animated emoji and avatars"; checked: appSettings.animateImages; onToggled: appSettings.animateImages = checked }
                    Toggle { Layout.fillWidth: true; text: "Show member list"; checked: appSettings.showMemberList; onToggled: appSettings.showMemberList = checked }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: "Images are loaded at the size they're shown and cached on disk, so scrolling back through a channel doesn't re-download or hold full-size pictures in memory."
                        color: Theme.textMuted
                        font.pixelSize: 13
                    }
                }

                // ---------------------------------------------------- notifications
                ColumnLayout {
                    spacing: 12
                    Text { text: "Notifications"; color: Theme.textStrong; font.pixelSize: 20; font.bold: true }
                    Toggle { Layout.fillWidth: true; text: "Desktop notifications for DMs and mentions"; checked: appSettings.notifications; onToggled: appSettings.notifications = checked }
                    Toggle { Layout.fillWidth: true; text: "Keep running in the system tray when closed"; checked: appSettings.minimizeToTray; onToggled: appSettings.minimizeToTray = checked }
                }

                // ---------------------------------------------------- account
                ColumnLayout {
                    spacing: 12
                    Text { text: "My Account"; color: Theme.textStrong; font.pixelSize: 20; font.bold: true }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 96
                        radius: 8
                        color: Theme.rail
                        Avatar { id: accAv; x: 16; anchors.verticalCenter: parent.verticalCenter; width: 64; height: 64; source: app.selfAvatar; fallback: app.selfName.substring(0, 1) }
                        Column {
                            anchors.left: accAv.right
                            anchors.leftMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            Text { text: app.selfName; color: Theme.textStrong; font.pixelSize: 20; font.bold: true }
                            Text { text: app.selfUsername; color: Theme.textMuted; font.pixelSize: 14 }
                        }
                    }
                    Button {
                        implicitWidth: 120
                        implicitHeight: 36
                        onClicked: { dialog.close(); app.logout() }
                        contentItem: Text { text: "Log Out"; color: "white"; font.pixelSize: 14; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 4; color: Theme.red }
                    }
                }
            }
        }
    }

    // close button
    Column {
        anchors.right: parent.right
        anchors.rightMargin: 40
        y: 60
        spacing: 4
        Rectangle {
            width: 36; height: 36; radius: 18
            color: "transparent"
            border.color: Theme.textMuted
            border.width: 2
            Icon { anchors.centerIn: parent; name: "close"; size: 18; color: Theme.textMuted }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: dialog.close() }
        }
        Text { text: "ESC"; color: Theme.textMuted; font.pixelSize: 12; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter }
    }
}
