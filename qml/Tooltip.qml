import QtQuick
import QtQuick.Controls.Basic

ToolTip {
    id: tip
    delay: 350
    padding: 8
    font.family: Theme.font
    font.pixelSize: 13
    font.weight: Font.DemiBold
    contentItem: Text {
        text: tip.text
        font: tip.font
        color: Theme.textStrong
        wrapMode: Text.Wrap
    }
    background: Rectangle {
        color: Theme.popup
        radius: 5
    }
}
