// the band dragged over the bare desktop to pick files
// let go it stays put and fades over the theme's fade_ms

import QtQuick
import "ThemeValues.js" as ThemeValues

Rectangle {
    id: band

    required property bool dragging
    required property rect span
    required property var look

    readonly property int fadeMs: look?.fade_ms ?? 0

    visible: (dragging || fade.running) && (span.width > 0 || span.height > 0)
    x: span.x
    y: span.y
    width: span.width
    height: span.height
    color: ThemeValues.color(look?.color ?? "#ffffff30")
    border.width: look?.border_width ?? 1
    border.color: ThemeValues.color(look?.border_color ?? "#ffffff90")

    onDraggingChanged: {
        fade.stop()
        opacity = 1
        if (!dragging) {
            fade.start()
        }
    }

    NumberAnimation {
        id: fade
        target: band
        property: "opacity"
        from: 1
        to: 0
        duration: band.fadeMs
    }
}
