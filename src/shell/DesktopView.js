// the desktop's view choices read as finder holds them
.pragma library

// none and grid leave icons where they are put, the others keep them sorted
const orders = ["name", "kind", "modified", "created", "last_opened", "added", "size", "tags"]

function iconSize(raw) {
    return Math.max(16, Math.min(128, Math.floor(raw / 4) * 4))
}

function keepsArranged(arrangeBy) {
    return orders.includes(arrangeBy)
}

// before arrange_by the grid was a switch, and an old sort only set the order icons flow in
function arrangement(stored, snap) {
    if (stored !== undefined && stored !== null) {
        return stored
    }
    return snap === true ? "grid" : "none"
}
