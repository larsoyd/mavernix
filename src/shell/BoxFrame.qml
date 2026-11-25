// The box behind a menu, or anything that looks like one: a rounded fill
// with an optional shadow, rim and pointer. It fills its parent. The looks
// are shaped like the theme's [menu] table (docs/themes/, "Menus").

import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import "ThemeValues.js" as ThemeValues

Item {
    id: frame

    required property var looks

    // Corners that stay square because the box hangs from something there.
    property bool squareTopLeft: false
    property bool squareTopRight: false

    // the pointer tip's distance from the box's left edge, or from its top on a side edge
    // negative for none
    property real pointerX: -1
    // bottom, left or right
    property string pointerEdge: "bottom"
    readonly property bool hasPointer: pointerX >= 0 && looks.pointer !== undefined
    // the pointer is drawn for the bottom edge and turned onto its edge
    readonly property real pointerTurn: pointerEdge === "left" ? 90 : pointerEdge === "right" ? -90 : 0
    readonly property point pointerBase: pointerEdge === "left" ? Qt.point(0, pointerX)
        : pointerEdge === "right" ? Qt.point(width, pointerX) : Qt.point(pointerX, height)

    anchors.fill: parent

    RectangularShadow {
        visible: frame.looks.shadow !== undefined
        anchors.fill: parent
        radius: frame.looks.corner_radius
        color: ThemeValues.color(frame.looks.shadow?.color)
        blur: frame.looks.shadow?.blur ?? 0
        offset: Qt.vector2d(frame.looks.shadow?.offset[0] ?? 0, frame.looks.shadow?.offset[1] ?? 0)
        cached: false
    }

    // The rim: one colour or a list, each line one pixel further out.
    readonly property var rimColors: {
        if (looks.rim === undefined) {
            return []
        }
        return Array.isArray(looks.rim) ? looks.rim : [looks.rim]
    }
    Repeater {
        model: frame.rimColors
        Rectangle {
            required property int index
            required property string modelData
            readonly property int distance: index + 1

            anchors.fill: parent
            anchors.margins: -distance
            color: "transparent"
            border.width: 1
            border.color: ThemeValues.color(modelData)
            topLeftRadius: frame.squareTopLeft ? 0 : frame.looks.corner_radius + distance
            topRightRadius: frame.squareTopRight ? 0 : frame.looks.corner_radius + distance
            bottomLeftRadius: frame.looks.corner_radius + distance
            bottomRightRadius: frame.looks.corner_radius + distance
        }
    }

    Rectangle {
        anchors.fill: parent
        color: ThemeValues.color(frame.looks.background)
        topLeftRadius: frame.squareTopLeft ? 0 : frame.looks.corner_radius
        topRightRadius: frame.squareTopRight ? 0 : frame.looks.corner_radius
        bottomLeftRadius: frame.looks.corner_radius
        bottomRightRadius: frame.looks.corner_radius
    }

    // Drawn over the rim, which so ends where the pointer begins.
    Shape {
        id: pointer
        visible: frame.hasPointer
        readonly property real halfWidth: (frame.looks.pointer?.width ?? 0) / 2
        readonly property real length: frame.looks.pointer?.height ?? 0
        x: frame.pointerBase.x
        y: frame.pointerBase.y
        rotation: frame.pointerTurn
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: ThemeValues.color(frame.looks.background)
            strokeColor: "transparent"
            startX: -pointer.halfWidth
            startY: 0
            PathLine { x: pointer.halfWidth; y: 0 }
            PathLine { x: 0; y: pointer.length }
            PathLine { x: -pointer.halfWidth; y: 0 }
        }
    }
    // The rim's lines down the pointer's sides. One Shape per line, since a
    // Shape's paths cannot be made by a Repeater.
    Repeater {
        model: frame.hasPointer ? frame.rimColors : []
        Shape {
            required property int index
            required property string modelData
            // Half a pixel more puts the one-pixel line on whole pixels.
            readonly property real out: index + 0.5
            x: frame.pointerBase.x + (frame.pointerEdge === "left" ? -out : frame.pointerEdge === "right" ? out : 0)
            y: frame.pointerBase.y + (frame.pointerEdge === "bottom" ? out : 0)
            rotation: frame.pointerTurn
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                fillColor: "transparent"
                strokeColor: ThemeValues.color(modelData)
                strokeWidth: 1
                capStyle: ShapePath.FlatCap
                startX: -pointer.halfWidth
                startY: 0
                PathLine { x: 0; y: pointer.length }
                PathLine { x: pointer.halfWidth; y: 0 }
            }
        }
    }
}
