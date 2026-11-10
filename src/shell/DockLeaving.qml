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
    // frames drawn since it was shown
    property int frames: 0
    signal drawn()

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
    onVisibleChanged: frames = 0

    // the dock's glass, when it has one of its own, and the dock over it
    ItemPicture {
        id: under
        width: leaving.width
        height: leaving.height
        y: Math.round(leaving.slid * leaving.travel)
        onTaken: leaving.oneTaken()
    }
    ItemPicture {
        id: over
        width: leaving.width
        height: leaving.height
        y: under.y
        onTaken: leaving.oneTaken()
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
        awaited = glass ? 2 : 1
        return (!glass || under.take(glass, pixelRatio)) && over.take(dock, pixelRatio)
    }

    function clear() {
        awaited = 0
        under.clear()
        over.clear()
    }

    // the second frame has the pictures on screen for sure
    FrameAnimation {
        running: leaving.visible && leaving.frames < 2
        onTriggered: {
            leaving.frames += 1
            if (leaving.frames === 2) {
                leaving.drawn()
            }
        }
    }
}
