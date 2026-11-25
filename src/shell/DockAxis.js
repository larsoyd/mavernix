// puts the dock's strip, laid out as for a bottom dock, on its side of the screen
// in the strip x runs along the dock and y down to the screen's edge

.pragma library

function sideOf(value) {
    return value === "left" || value === "right" ? value : "bottom"
}

function sideways(side) {
    return side === "left" || side === "right"
}

function anchors(side) {
    return { left: side !== "right", right: side !== "left", top: sideways(side), bottom: true }
}

// window x is m[0] x + m[1] y + m[2], window y is m[3] x + m[4] y + m[5], for a strip thickness deep
function stripMatrix(side, thickness) {
    if (side === "left") {
        return [0, -1, thickness, 1, 0, 0]
    }
    if (side === "right") {
        return [0, 1, 0, 1, 0, 0]
    }
    return [1, 0, 0, 0, 1, 0]
}

function toStrip(side, thickness, x, y) {
    if (side === "left") {
        return { x: y, y: thickness - x }
    }
    if (side === "right") {
        return { x: y, y: x }
    }
    return { x: x, y: y }
}

// a strip box's content the right way up, sideways it is height wide and width high
function uprightMatrix(side, width, height) {
    if (side === "left") {
        return [0, 1, 0, -1, 0, height]
    }
    if (side === "right") {
        return [0, 1, 0, 1, 0, 0]
    }
    return [1, 0, 0, 0, 1, 0]
}

// where the window's top left corner is on its screen
function windowOrigin(side, screenWidth, screenHeight, width, height) {
    return { x: side === "right" ? screenWidth - width : 0, y: screenHeight - height }
}

// the sixteen numbers Matrix4x4 takes for one of the matrices above
function matrix4(numbers) {
    return [numbers[0], numbers[1], 0, numbers[2], numbers[3], numbers[4], 0, numbers[5], 0, 0, 1, 0, 0, 0, 0, 1]
}
