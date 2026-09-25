import QtQuick

// Selectable rich text (rendered from Discord markdown in C++).
TextEdit {
    id: root
    signal linkClicked(string url)
    readOnly: true
    selectByMouse: true
    persistentSelection: false
    wrapMode: TextEdit.Wrap
    textFormat: TextEdit.RichText
    color: Theme.text
    selectionColor: "#3d5afe"
    selectedTextColor: "white"
    font.family: Theme.font
    font.pixelSize: 15
    activeFocusOnPress: false
    onLinkActivated: (link) => root.linkClicked(link)

    HoverHandler {
        cursorShape: root.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
    }
}
