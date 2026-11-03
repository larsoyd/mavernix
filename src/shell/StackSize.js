// how far the clipboard stack may grow for its tile's size
// as cells, list rows and a share of the free screen

.pragma library

const names = ["small", "medium", "large"]

const sizes = {
    small: { columns: 3, rows: 2, scale: 0.75, share: 0.4, listRows: 8, longest: 32 },
    medium: { columns: 4, rows: 3, scale: 1, share: 0.5, listRows: 12, longest: 44 },
    large: { columns: 6, rows: 4, scale: 1, share: 0.75, listRows: 20, longest: 60 },
}

function named(name) {
    return names.includes(name) ? name : "medium"
}

function bounds(name) {
    return sizes[named(name)]
}

// the most a list menu may take in room, never under three rows while the room has them
function listHeight(name, rowHeight, padding, room) {
    const bound = bounds(name)
    const wanted = Math.min(bound.listRows * rowHeight + 2 * padding, room * bound.share)
    const least = Math.min(room, 3 * rowHeight + 2 * padding)
    return Math.floor(Math.max(least, wanted))
}
