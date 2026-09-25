import QtQuick
import QtQuick.Controls

Popup {
    id: sw
    width: 560
    height: 420
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.chat; radius: 8 }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.7) }

    property var results: []
    property int current: 0
    function refresh() { results = app.quickSwitch(field.text); current = 0 }
    function choose(item) {
        if (!item) return
        if (item.kind === "guild") app.selectGuild(item.id)
        else app.selectChannel(item.id)
        close()
    }
    onOpened: { field.text = ""; refresh(); field.forceActiveFocus() }

    Column {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        Text { text: "Where would you like to go?"; color: Theme.textStrong; font.pixelSize: 18; font.weight: Font.DemiBold }
        TextField {
            id: field
            width: parent.width
            height: 48
            font.pixelSize: 18
            color: Theme.text
            leftPadding: 14
            placeholderText: "Search channels, DMs and servers"
            placeholderTextColor: Theme.textFaint
            background: Rectangle { color: Theme.rail; radius: 6 }
            onTextChanged: sw.refresh()
            Keys.onDownPressed: sw.current = Math.min(sw.current + 1, sw.results.length - 1)
            Keys.onUpPressed: sw.current = Math.max(sw.current - 1, 0)
            Keys.onReturnPressed: sw.choose(sw.results[sw.current])
            Keys.onEnterPressed: sw.choose(sw.results[sw.current])
        }
        ListView {
            width: parent.width
            height: parent.height - 100
            clip: true
            model: sw.results
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 36
                radius: 4
                color: index === sw.current ? Theme.selected : "transparent"
                Row {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Item {
                        width: 22; height: 22
                        anchors.verticalCenter: parent.verticalCenter
                        Avatar { anchors.fill: parent; visible: !!modelData.image; source: modelData.image || "" }
                        Icon {
                            anchors.centerIn: parent
                            visible: !modelData.image
                            size: 20
                            name: modelData.kind === "voice" ? "speaker" : modelData.kind === "dm" ? "at" : modelData.kind === "guild" ? "folder" : "hash"
                            color: Theme.textMuted
                        }
                    }
                    Text {
                        text: modelData.name
                        color: modelData.unread ? Theme.textStrong : Theme.text
                        font.pixelSize: 16
                        font.weight: modelData.unread ? Font.DemiBold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.sub
                        color: Theme.textMuted
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: sw.current = index
                    onClicked: sw.choose(modelData)
                }
            }
        }
    }
}
