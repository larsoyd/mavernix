// the flat glass of a dock on the left or right, drawn for the left and mirrored for the right
// rounded on the side away from the screen's edge, with a dark rim, a faint inner line and a light top

import QtQuick
import "ThemeValues.js" as ThemeValues

Canvas {
    id: shelf

    required property var looks
    property bool mirrored: false

    readonly property real radius: Math.max(1, looks.corner_radius[0] * width + looks.corner_radius[1])
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onMirroredChanged: requestPaint()
    onLooksChanged: requestPaint()

    // the rim follows this outline, which stays open along the screen's edge
    function outline(context, round) {
        const right = width - 1
        const bottom = height - 1
        const r = radius
        context.beginPath()
        context.moveTo(0, 1)
        context.lineTo(right - r, 1)
        context.bezierCurveTo(right - round * r, 1, right, 1 + round * r, right, 1 + r)
        context.lineTo(right, bottom - r)
        context.bezierCurveTo(right, bottom - round * r, right - round * r, bottom, right - r, bottom)
        context.lineTo(0, bottom)
    }

    onPaint: {
        const context = getContext("2d")
        context.reset()
        if (mirrored) {
            context.translate(width, 0)
            context.scale(-1, 1)
        }
        // a quarter circle's control points, as the original draws its corners
        const round = 0.4477152
        context.lineWidth = 2
        outline(context, round)
        context.strokeStyle = ThemeValues.color(looks.outline)
        context.stroke()
        context.globalCompositeOperation = "destination-out"
        outline(context, round)
        context.fillStyle = "#ffffff"
        context.fill()
        context.globalCompositeOperation = "source-over"
        outline(context, round)
        context.fillStyle = ThemeValues.color(looks.fill)
        context.fill()

        context.save()
        outline(context, round)
        context.clip()
        outline(context, round)
        context.strokeStyle = ThemeValues.color(looks.inner_line)
        context.stroke()
        context.restore()

        // the light along the top, down the round of its corner
        const right = width - 1
        const r = radius
        const band = looks.highlight_width ?? 1.25
        context.beginPath()
        context.moveTo(0, 1)
        context.lineTo(right - r, 1)
        context.bezierCurveTo(right - round * r, 1, right, 1 + round * r, right, 1 + r)
        context.bezierCurveTo(right, 1 + r - (1 - round) * (r - band), right - round * r, 1 + band, right - r, 1 + band)
        context.lineTo(0, 1 + band)
        context.closePath()
        context.fillStyle = ThemeValues.color(looks.highlight)
        context.fill()
    }
}
