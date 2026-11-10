// which screen the dock starts on and how long its slide between two takes
// by the rules of the Dock of OS X 10.9

.pragma library

function overlaps(a, b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height
}

// the main screen or the lowest of those under it
// screens are { name, x, y, width, height }
function home(screens, mainName) {
    let at = screens.find(screen => screen.name === mainName)
    if (at === undefined) {
        return ""
    }
    const leftFirst = screens.slice().sort((a, b) => a.x - b.x || a.y - b.y)
    for (let hops = 0; hops < screens.length; hops++) {
        const under = { x: at.x, y: at.y + at.height + 1, width: at.width, height: 2 }
        const next = leftFirst.find(screen => overlaps(under, screen))
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
