// a place is { x, y } of an icon's top left corner, a grid's cells run down from its right edge
.pragma library

function slotPlace(grid, slot) {
    const column = Math.floor(slot / grid.rows)
    return { x: grid.right - (column + 1) * grid.cellWidth + grid.pictureX,
             y: grid.top + (slot % grid.rows) * grid.cellHeight + grid.pictureY }
}

function slotAt(grid, x, y) {
    const column = Math.max(0, Math.round((grid.right - grid.cellWidth + grid.pictureX - x) / grid.cellWidth))
    const row = Math.max(0, Math.min(grid.rows - 1, Math.round((y - grid.top - grid.pictureY) / grid.cellHeight)))
    return column * grid.rows + row
}

// each icon takes a whole cell around its picture
function coversSlot(grid, place, slot) {
    const slotted = slotPlace(grid, slot)
    return place.x < slotted.x + grid.cellWidth && slotted.x < place.x + grid.cellWidth
        && place.y < slotted.y + grid.cellHeight && slotted.y < place.y + grid.cellHeight
}

// nearest slot to each place or the next free one after it
function freeSlots(grid, places, taken) {
    return places.map(place => {
        let slot = slotAt(grid, place.x, place.y)
        while (taken[slot]) {
            slot++
        }
        taken[slot] = true
        return slot
    })
}

// moving icons go to free slots around the kept ones
function onGrid(grid, kept, moving) {
    const taken = {}
    freeSlots(grid, kept, taken)
    return freeSlots(grid, moving, taken).map(slot => slotPlace(grid, slot))
}

// null in put means not placed by hand, those flow into slots nobody covers
// on the grid the hand placed ones not loose get their nearest free slots first
function places(grid, put, snap) {
    const result = put.slice()
    const snapped = result.filter(place => place !== null && snap && !place.loose)
    const kept = result.filter(place => place !== null && !snapped.includes(place))
    const taken = {}
    // far enough for the flow and for every slot a snapped icon can reach
    const farthest = Math.max(0, ...result.filter(place => place !== null)
                                         .map(place => slotAt(grid, place.x, place.y) + 2 * grid.rows))
    const slotsToCheck = Math.max(result.length + 4 * kept.length, farthest)
    for (let slot = 0; slot < slotsToCheck; slot++) {
        taken[slot] = kept.some(place => coversSlot(grid, place, slot))
    }
    const slots = freeSlots(grid, snapped, taken)
    for (let index = 0; index < result.length; index++) {
        if (snapped.includes(result[index])) {
            result[index] = slotPlace(grid, slots[snapped.indexOf(result[index])])
        }
    }
    let slot = 0
    for (let index = 0; index < result.length; index++) {
        if (result[index] !== null) {
            continue
        }
        while (taken[slot]) {
            slot++
        }
        result[index] = slotPlace(grid, slot)
        slot++
    }
    return result
}

// when the cell changes finder scales each icon's distance from the first slot by the cell
// and places again an icon that ends up off the screen
function rescaled(from, to, put, screen) {
    const middle = (grid, place) => ({ x: place.x + grid.iconSize / 2, y: place.y + grid.iconSize / 2 })
    const before = middle(from, slotPlace(from, 0))
    const after = middle(to, slotPlace(to, 0))
    return put.map(place => {
        if (place === null) {
            return null
        }
        const was = middle(from, place)
        const x = after.x - Math.floor((before.x - was.x) * to.cellWidth / from.cellWidth)
        const y = after.y - Math.floor((before.y - was.y) * to.cellHeight / from.cellHeight)
        if (x < 0 || x >= screen.width || y < 0 || y >= screen.height) {
            return null
        }
        const moved = { x: x - to.iconSize / 2, y: y - to.iconSize / 2 }
        return place.loose ? Object.assign(moved, { loose: true }) : moved
    })
}
