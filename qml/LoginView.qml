import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    color: Theme.rail
    Component.onCompleted: app.startQrLogin()
    Component.onDestruction: app.stopQrLogin()

    // soft background gradient
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#2a2250" }
            GradientStop { position: 1; color: "#141625" }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: 780
        height: 420
        radius: 8
        color: Theme.chat

        RowLayout {
            anchors.fill: parent
            anchors.margins: 32
            spacing: 40

            // token login
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 8

                Text {
                    text: "Welcome back!"
                    color: Theme.textStrong
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    text: "Scan the QR code with the Discord mobile app,\nor paste a token below."
                    color: Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 14
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: 16
                }
                Text {
                    text: "TOKEN"
                    color: Theme.textMuted
                    font.pixelSize: 12
                    font.bold: true
                }
                TextField {
                    id: tokenField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    color: Theme.text
                    font.pixelSize: 15
                    placeholderText: "Paste your account token"
                    placeholderTextColor: Theme.textFaint
                    padding: 10
                    background: Rectangle { color: Theme.rail; radius: 4 }
                    onAccepted: app.loginWithToken(text)
                }
                Button {
                    id: loginButton
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    Layout.preferredHeight: 44
                    enabled: tokenField.text.length > 20
                    onClicked: app.loginWithToken(tokenField.text)
                    contentItem: Text {
                        text: "Log In"
                        color: "white"
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: !loginButton.enabled ? Qt.darker(Theme.accent, 1.6) : loginButton.hovered ? Theme.accentHover : Theme.accent
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    text: app.loginMessage
                    visible: text.length > 0
                    color: Theme.red
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                }
                Item { Layout.fillHeight: true }
                Text {
                    Layout.fillWidth: true
                    text: "Third-party clients aren't endorsed by Discord. Your token is stored encrypted on this PC only."
                    color: Theme.textFaint
                    wrapMode: Text.Wrap
                    font.pixelSize: 11
                }
            }

            // QR login
            ColumnLayout {
                Layout.preferredWidth: 240
                Layout.alignment: Qt.AlignVCenter
                spacing: 12

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 176
                    height: 176
                    radius: 6
                    color: "white"
                    Image {
                        anchors.centerIn: parent
                        width: 160
                        height: 160
                        visible: app.qrUrl.length > 0 && app.scannedName.length === 0
                        source: app.qrUrl ? "image://qr/" + encodeURIComponent(app.qrUrl) : ""
                        sourceSize: Qt.size(320, 320)
                        smooth: false
                        cache: false
                        Rectangle {
                            anchors.centerIn: parent
                            width: 40; height: 40; radius: 8
                            color: "white"
                            Image { anchors.centerIn: parent; source: "qrc:/icons/app.png"; width: 32; height: 32 }
                        }
                    }
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: app.qrUrl.length === 0
                        visible: running
                    }
                    Avatar {
                        anchors.centerIn: parent
                        width: 96
                        height: 96
                        visible: app.scannedName.length > 0
                        source: app.scannedAvatar
                        fallback: app.scannedName.substring(0, 2)
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: app.scannedName ? "Check your phone!" : "Log in with QR Code"
                    color: Theme.textStrong
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: app.scannedName ? "Logging in as <b>" + app.scannedName + "</b>. Confirm on your phone to continue."
                                          : "Scan this with the <b>Discord mobile app</b> to log in instantly."
                    textFormat: Text.StyledText
                    color: Theme.textMuted
                    font.pixelSize: 14
                }
            }
        }
    }
}
