// which screen the dock starts on and how long its slide between two takes
// by the rules of the Dock of OS X 10.9

.pragma library

function overlaps(a, b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height
}

// the strip just past a screen's edge on the dock's side
function beyond(at, side) {
    if (side === "left") {
        return { x: at.x - 3, y: at.y, width: 2, height: at.height }
    }
    if (side === "right") {
        return { x: at.x + at.width + 1, y: at.y, width: 2, height: at.height }
    }
    return { x: at.x, y: at.y + at.height + 1, width: at.width, height: 2 }
}

// the main screen or the outermost of those past it on the dock's side
// screens are { name, x, y, width, height }
function home(screens, mainName, side) {
    let at = screens.find(screen => screen.name === mainName)
    if (at === undefined) {
        return ""
    }
    const sideways = side === "left" || side === "right"
    const firstFound = screens.slice().sort(sideways ? (a, b) => a.y - b.y || a.x - b.x
                                                     : (a, b) => a.x - b.x || a.y - b.y)
    for (let hops = 0; hops < screens.length; hops++) {
        const next = firstFound.find(screen => overlaps(beyond(at, side), screen))
        if (next === undefined) {
            break
        }
        at = next
    }
    return at.name
}

// the slide's length in ms for a dock that travels this far
function slideMs(travel) {
    return 60 * Math.floor(Math.log(Math.abs(travel) * 0.5 + 1) + 1)
}
