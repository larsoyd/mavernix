// a picture of the dock that slides out of the screen the dock has left

import QtQuick
import Quickshell
import Quickshell.Wayland
import Shell.Native

PanelWindow {
    id: leaving

    // 0 in place, 1 out of sight
    property real slid: 0
    // how far the pictures go, in pixels
    property real travel: 0
    // the pictures are with the compositor, what they cover can go
    signal shown()

    anchors {
        left: true
        right: true
        bottom: true
    }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "mavernix-dock-leaving"
    color: "transparent"
    mask: Region {}
    visible: false

    // as large as what was pictured, whatever the window is told to be meanwhile
    Item {
        id: stand
        y: leaving.height - height

        Item {
            id: pictures
            width: stand.width
            height: stand.height
            y: Math.round(leaving.slid * leaving.travel)

            // the dock's glass, when it has one of its own, and the dock over it
            ItemPicture {
                id: under
                anchors.fill: parent
                onTaken: leaving.oneTaken()
            }
            ItemPicture {
                id: over
                anchors.fill: parent
                onTaken: leaving.oneTaken()
            }
        }
    }

    property int awaited: 0
    signal taken()
    function oneTaken() {
        awaited -= 1
        if (awaited === 0) {
            taken()
        }
    }

    // false when the pictures cannot be had, else taken follows
    function take(glass, dock, pixelRatio) {
        clear()
        stand.width = dock.width
        stand.height = dock.height
        awaited = glass ? 2 : 1
        return (!glass || under.take(glass, pixelRatio)) && over.take(dock, pixelRatio)
    }

    function clear() {
        awaited = 0
        under.clear()
        over.clear()
    }

    function show() {
        handedOver = false
        visible = true
    }

    // the compositor takes requests in order, so what is asked of it after
    // this window's first frame finds the pictures already there
    property bool handedOver: false
    Connections {
        target: leaving.visible && !leaving.handedOver ? pictures.Window.window : null
        function onFrameSwapped() {
            leaving.handedOver = true
            leaving.shown()
        }
    }
}
