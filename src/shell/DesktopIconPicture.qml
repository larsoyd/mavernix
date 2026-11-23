import QtQuick

// a picture file as itself, small in the theme's frame, anything else by the icon for its kind
Item {
    id: iconPicture

    required property var entry
    required property string iconSource
    property var frame: null

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
}
