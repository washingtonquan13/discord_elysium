import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: picker
    signal picked(var item)
    width: 9 * 40 + 28
    height: 400
    padding: 0
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.sidebar; radius: 8; border.color: Theme.rail }

    property var rows: []
    property string hoverName: ""
    function refresh() { rows = app.emojiRows(search.text, 9) }
    onOpened: { search.text = ""; refresh(); search.forceActiveFocus() }

    Column {
        anchors.fill: parent
        TextField {
            id: search
            x: 12
            y: 12
            width: parent.width - 24
            height: 34
            placeholderText: "Find the perfect emoji"
            placeholderTextColor: Theme.textFaint
            color: Theme.text
            font.pixelSize: 14
            leftPadding: 10
            background: Rectangle { color: Theme.rail; radius: 4 }
            onTextChanged: searchDelay.restart()
            Keys.onReturnPressed: {
                if (picker.rows.length > 1 && picker.rows[1].items) { picker.picked(picker.rows[1].items[0]); picker.close() }
            }
            Timer { id: searchDelay; interval: 120; onTriggered: picker.refresh() }
        }
        Item { width: 1; height: 20 }
        ListView {
            id: list
            width: parent.width
            height: parent.height - 34 - 20 - 12 - 36
            clip: true
            model: picker.rows
            cacheBuffer: 400
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { width: 6; contentItem: Rectangle { radius: 3; color: Theme.elevated } }
            delegate: Item {
                required property var modelData
                width: list.width
                height: modelData.header ? 30 : 40
                Text {
                    visible: !!modelData.header
                    x: 14
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    text: (modelData.header || "").toUpperCase()
                    color: Theme.textMuted
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                Row {
                    visible: !modelData.header
                    x: 10
                    Repeater {
                        model: modelData.items || []
                        delegate: Rectangle {
                            required property var modelData
                            width: 40
                            height: 40
                            radius: 4
                            color: cell.containsMouse ? Theme.hover : "transparent"
                            Text {
                                visible: !!modelData.emoji
                                anchors.centerIn: parent
                                text: modelData.emoji || ""
                                font.pixelSize: 26
                            }
                            Avatar {
                                visible: !!modelData.image
                                anchors.centerIn: parent
                                width: 30; height: 30; radius: 0
                                placeholderColor: "transparent"
                                source: modelData.image || ""
                            }
                            MouseArea {
                                id: cell
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onContainsMouseChanged: if (containsMouse) picker.hoverName = ":" + modelData.name + ":"
                                onClicked: { picker.picked(modelData); picker.close() }
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            width: parent.width
            height: 36
            color: Theme.panel
            radius: 8
            Text {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                text: picker.hoverName
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
        }
    }
}
