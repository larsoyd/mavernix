// where a desktop icon is drawn: it glides to each new place, eased at both
// ends, and jump puts it down at once, as where a drag lets go

import QtQuick

Item {
    id: motion

    required property point place
    property int duration: 0
    // an even glide in place of the eased one
    property bool evenly: false

    Component.onCompleted: {
        x = place.x
        y = place.y
    }
    onPlaceChanged: {
        glide.stop()
        toX.to = place.x
        toY.to = place.y
        glide.start()
    }

    // back to its place when that place did not change, as after a small drop
    function settle() {
        if (x !== place.x || y !== place.y) {
            glide.stop()
            toX.to = place.x
            toY.to = place.y
            glide.start()
        }
    }

    function jump(point) {
        glide.stop()
        x = point.x
        y = point.y
    }

    ParallelAnimation {
        id: glide
        NumberAnimation {
            id: toX
            target: motion
            property: "x"
            duration: motion.duration
            easing.type: motion.evenly ? Easing.Linear : Easing.Bezier
            easing.bezierCurve: [0.42, 0, 0.58, 1, 1, 1]
        }
        NumberAnimation {
            id: toY
            target: motion
            property: "y"
            duration: motion.duration
            easing.type: motion.evenly ? Easing.Linear : Easing.Bezier
            easing.bezierCurve: [0.42, 0, 0.58, 1, 1, 1]
        }
    }
}
