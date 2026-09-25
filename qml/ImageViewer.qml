import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: viewer
    property string source
    property string original
    readonly property bool isGif: /\.gif(\?|$)/i.test(original)
    anchors.centerIn: parent
    width: parent ? parent.width : 800
    height: parent ? parent.height : 600
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Qt.rgba(0, 0, 0, 0.85) }

    function show(url, orig) {
        source = url
        original = orig
        open()
    }
    onClosed: source = ""

    MouseArea { anchors.fill: parent; onClicked: viewer.close() }

    AnimatedImage {
        id: gif
        anchors.centerIn: parent
        visible: viewer.isGif
        width: Math.min(implicitWidth, parent.width - 120)
        height: Math.min(implicitHeight, parent.height - 140)
        fillMode: Image.PreserveAspectFit
        source: viewer.isGif ? viewer.original : ""
        cache: false
        MouseArea { anchors.fill: parent }
    }
    Image {
        id: img
        visible: !viewer.isGif
        anchors.centerIn: parent
        width: Math.min(implicitWidth, parent.width - 120)
        height: Math.min(implicitHeight, parent.height - 140)
        fillMode: Image.PreserveAspectFit
        source: viewer.source
        asynchronous: true
        cache: false
        MouseArea { anchors.fill: parent } // don't close when clicking the image
    }
    BusyIndicator { anchors.centerIn: parent; running: img.status === Image.Loading || gif.status === Image.Loading; visible: running }
    Text {
        anchors.top: viewer.isGif ? gif.bottom : img.bottom
        anchors.topMargin: 10
        anchors.left: viewer.isGif ? gif.left : img.left
        text: "Open in browser"
        color: Theme.textStrong
        font.pixelSize: 14
        font.underline: linkArea.containsMouse
        visible: viewer.original.length > 0
        MouseArea { id: linkArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: app.handleLink(viewer.original) }
    }
}
