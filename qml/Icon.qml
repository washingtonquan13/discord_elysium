import QtQuick

// A monochrome vector icon tinted with `color` (GPU shader, no image copies).
Item {
    id: root
    property string name
    property color color: Theme.textMuted
    property int size: 20
    width: size
    height: size

    Image {
        id: glyph
        anchors.fill: parent
        source: root.name ? "qrc:/icons/" + root.name + ".svg" : ""
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        smooth: true
        visible: false
    }
    ShaderEffect {
        anchors.fill: parent
        property variant source: glyph
        property color color: root.color
        fragmentShader: "qrc:/shaders/tint.frag.qsb"
        visible: glyph.status === Image.Ready
    }
}
