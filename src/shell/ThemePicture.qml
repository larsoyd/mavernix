// a theme picture, ready only once read so a caller can draw a stand-in
import QtQuick

Image {
    property string themeDirectory: ""
    property string path: ""
    readonly property bool ready: status === Image.Ready
    source: path === "" ? "" : "file://" + (path.startsWith("/") ? "" : themeDirectory + "/") + path
    sourceSize: Qt.size(0, 0)
    smooth: false
}
