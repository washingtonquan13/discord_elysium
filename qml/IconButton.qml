import QtQuick

Item {
    id: root
    property string icon
    property int iconSize: 20
    property color color: Theme.textMuted
    property color hoverColor: Theme.text
    property color activeColor: Theme.red
    property bool active: false
    property string tip
    property bool showBackground: true
    signal clicked
    signal rightClicked

    width: 32
    height: 32

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: Theme.hover
        visible: root.showBackground && area.containsMouse
    }
    Icon {
        anchors.centerIn: parent
        name: root.icon
        size: root.iconSize
        color: root.active ? root.activeColor : (area.containsMouse ? root.hoverColor : root.color)
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (mouse) => mouse.button === Qt.RightButton ? root.rightClicked() : root.clicked()
    }
    Tooltip {
        visible: root.tip.length > 0 && area.containsMouse
        text: root.tip
    }
}
