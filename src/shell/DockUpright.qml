// a box of the dock's strip whose content shows the right way up on the screen
// content is laid out in shownWidth by shownHeight, which are swapped on a side dock

import QtQuick
import "DockAxis.js" as DockAxis

Item {
    id: box

    required property string side
    default property alias content: inside.data
    readonly property bool sideways: DockAxis.sideways(side)
    readonly property real shownWidth: sideways ? height : width
    readonly property real shownHeight: sideways ? width : height

    Item {
        id: inside
        width: box.shownWidth
        height: box.shownHeight
        transform: Matrix4x4 {
            matrix: Qt.matrix4x4(DockAxis.matrix4(DockAxis.uprightMatrix(box.side, box.width, box.height)))
        }
    }
}
