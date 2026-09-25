import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: popup
    property var profile: ({})
    width: 300
    padding: 0
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function showFor(userId, px, py) {
        profile = app.userProfile(userId)
        if (!profile.id) return
        x = Math.max(8, Math.min(px, parent.width - width - 8))
        y = Math.max(8, Math.min(py, parent.height - 420))
        open()
        Qt.callLater(() => { y = Math.max(8, Math.min(py, parent.height - height - 8)) })
    }

    background: Rectangle {
        color: Theme.popup
        radius: 8
        border.color: "#2b2d31"
    }

    contentItem: Column {
        spacing: 0
        Rectangle {
            width: popup.width
            height: 60
            radius: 8
            color: Theme.accent
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 8; color: Theme.accent }
        }
        Item {
            width: popup.width
            height: 50
            Rectangle {
                x: 16
                y: -40
                width: 88
                height: 88
                radius: 44
                color: Theme.popup
                Avatar {
                    anchors.centerIn: parent
                    width: 80
                    height: 80
                    source: popup.profile.avatar || ""
                    fallback: (popup.profile.name || "?").substring(0, 1)
                }
                StatusDot {
                    status: popup.profile.status || "offline"
                    ring: Theme.popup
                    width: 24
                    height: 24
                    x: 62
                    y: 62
                }
            }
        }
        Column {
            x: 16
            width: popup.width - 32
            spacing: 4
            bottomPadding: 16
            Text { text: popup.profile.name || ""; color: Theme.textStrong; font.pixelSize: 20; font.weight: Font.Bold; width: parent.width; elide: Text.ElideRight }
            Text { text: popup.profile.username || ""; color: Theme.text; font.pixelSize: 14 }
            Rectangle { width: parent.width; height: 1; color: Theme.divider; visible: roles.count > 0 }
            Text { visible: roles.count > 0; text: "ROLES"; color: Theme.textStrong; font.pixelSize: 12; font.bold: true; topPadding: 8 }
            Flow {
                width: parent.width
                spacing: 4
                Repeater {
                    id: roles
                    model: popup.profile.roles || []
                    delegate: Rectangle {
                        required property var modelData
                        height: 22
                        width: roleRow.implicitWidth + 12
                        radius: 4
                        color: Theme.card
                        Row {
                            id: roleRow
                            anchors.centerIn: parent
                            spacing: 5
                            Rectangle { width: 10; height: 10; radius: 5; color: modelData.color; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: modelData.name; color: Theme.text; font.pixelSize: 12 }
                        }
                    }
                }
            }
            Item { width: 1; height: 8 }
            Button {
                visible: !popup.profile.isSelf
                width: parent.width
                height: 36
                onClicked: { app.openDmWith(popup.profile.id); popup.close() }
                contentItem: Text { text: "Message"; color: "white"; font.pixelSize: 14; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: 4; color: parent.hovered ? Theme.accentHover : Theme.accent }
            }
            Column {
                visible: app.voiceState === "connected" && !popup.profile.isSelf
                width: parent.width
                topPadding: 8
                Text { text: "USER VOLUME"; color: Theme.textMuted; font.pixelSize: 12; font.bold: true }
                Slider {
                    width: parent.width
                    from: 0; to: 2
                    value: popup.profile.id ? appSettings.userVolume(popup.profile.id) : 1
                    onMoved: app.setUserVolume(popup.profile.id, value)
                }
            }
        }
    }
}
