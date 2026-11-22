// moving the pick with the keyboard among the desktop's icons
.pragma library

const STEPS = { left: [-1, 0], right: [1, 0], up: [0, -1], down: [0, 1] }

// the nearest icon that way, keeping close to the line, or the same one when none is
function next(places, from, direction) {
    if (from < 0 || from >= places.length) {
        return places.length > 0 ? 0 : -1
    }
    const [stepX, stepY] = STEPS[direction]
    let best = from
    let bestScore = Infinity
    places.forEach((place, index) => {
        const along = (place.x - places[from].x) * stepX + (place.y - places[from].y) * stepY
        const aside = Math.abs((place.x - places[from].x) * stepY + (place.y - places[from].y) * stepX)
        // well off the line counts three times as far
        const score = along + 3 * aside
        if (index !== from && along > 0 && score < bestScore) {
            best = index
            bestScore = score
        }
    })
    return best
}

// the first name at or after the text in order, whatever order the icons stand in, else the last
function typed(names, text) {
    const wanted = text.toLowerCase()
    let found = -1
    let last = -1
    names.forEach((name, index) => {
        const lower = name.toLowerCase()
        if (lower.localeCompare(wanted) >= 0 && (found < 0 || lower.localeCompare(names[found].toLowerCase()) < 0)) {
            found = index
        }
        if (last < 0 || lower.localeCompare(names[last].toLowerCase()) > 0) {
            last = index
        }
    })
    return found >= 0 ? found : last
}
