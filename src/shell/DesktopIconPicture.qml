import QtQuick

// a picture file as itself, small in the theme's frame, anything else by the icon for its kind
Item {
    id: iconPicture

    required property var entry
    required property string iconSource
    property var frame: null
    // laid over a link's icon from its lower left, at share of its size
    property string linkEmblem: ""
    property real linkEmblemShare: 1

    readonly property bool isPicture: entry.icon.startsWith("image-")

    FramedPicture {
        id: preview
        anchors.fill: parent
        visible: iconPicture.isPicture && !failed
        source: iconPicture.isPicture ? "file://" + encodeURI(iconPicture.entry.path) : ""
        frame: iconPicture.frame
    }
    Image {
        anchors.fill: parent
        // also for a picture in a format that cannot be read
        visible: !preview.visible
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
        source: iconPicture.iconSource
    }
    Image {
        visible: iconPicture.entry.isLink === true && iconPicture.linkEmblem !== ""
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: Math.round(parent.width * iconPicture.linkEmblemShare)
        height: width
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
        source: visible ? iconPicture.linkEmblem : ""
    }
}
