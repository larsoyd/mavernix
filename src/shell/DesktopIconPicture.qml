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

    // with finder's show icon preview off every file shows the icon for its kind
    property bool preview: true

    readonly property bool isPicture: preview && entry.icon.startsWith("image-")
    // any other file's contents as the thumbnailers draw them, while there is a preview
    readonly property bool hasContents: preview && !isPicture && !entry.isFolder && !entry.isLauncher

    FramedPicture {
        id: framed
        anchors.fill: parent
        visible: iconPicture.isPicture && !failed
        source: iconPicture.isPicture ? "file://" + encodeURI(iconPicture.entry.path) : ""
        frame: iconPicture.frame
    }
    Image {
        id: contents
        anchors.fill: parent
        visible: status === Image.Ready
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
        source: iconPicture.hasContents ? "image://thumbnail/" + encodeURIComponent(iconPicture.entry.path) : ""
    }
    Image {
        anchors.fill: parent
        // also for a picture in a format that cannot be read, and while contents load
        visible: !framed.visible && !contents.visible
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
        // before its size is known it would draw the whole picture at full size
        source: sourceSize.width > 0 ? iconPicture.iconSource : ""
    }
    Image {
        visible: iconPicture.entry.isLink === true && iconPicture.linkEmblem !== ""
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: Math.round(parent.width * iconPicture.linkEmblemShare)
        height: width
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
        source: visible && sourceSize.width > 0 ? iconPicture.linkEmblem : ""
    }
}
