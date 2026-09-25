import QtQuick
import QtQuick.Controls.Basic

Menu {
    id: menu
    property var model: []
    width: 210
    padding: 6
    background: Rectangle { color: Theme.popup; radius: 6 }

    Instantiator {
        model: menu.model.filter(e => e.show !== false)
        delegate: MenuItem {
            id: item
            required property var modelData
            height: 32
            contentItem: Item {
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    text: item.modelData.label
                    color: item.highlighted ? "white" : (item.modelData.danger ? Theme.red : Theme.text)
                    font.pixelSize: 14
                    font.family: Theme.font
                }
                Icon {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    name: item.modelData.icon || ""
                    size: 16
                    color: item.highlighted ? "white" : (item.modelData.danger ? Theme.red : Theme.textMuted)
                }
            }
            background: Rectangle {
                radius: 3
                color: item.highlighted ? (item.modelData.danger ? Theme.red : Theme.accent) : "transparent"
            }
            onTriggered: item.modelData.action()
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
