import QtQuick

// Rounded remote image with a placeholder while it loads.
Item {
    id: root
    property string source
    property string fallback
    property real radius: width / 2
    property color placeholderColor: Theme.selected
    property int fallbackPixelSize: Math.max(10, height * 0.34)

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.placeholderColor
        visible: img.status !== Image.Ready
        Text {
            anchors.centerIn: parent
            text: root.fallback
            color: Theme.textStrong
            font.family: Theme.font
            font.pixelSize: root.fallbackPixelSize
            font.weight: Font.Medium
            visible: root.fallback.length > 0
        }
    }
    Image {
        id: img
        anchors.fill: parent
        source: root.source
        asynchronous: true
        visible: false
        smooth: true
        mipmap: false
    }
    ShaderEffect {
        anchors.fill: parent
        visible: img.status === Image.Ready
        property variant source: img
        property real radius: Math.min(root.radius, Math.min(width, height) / 2)
        property size itemSize: Qt.size(width, height)
        fragmentShader: "qrc:/shaders/rounded.frag.qsb"
    }
}
