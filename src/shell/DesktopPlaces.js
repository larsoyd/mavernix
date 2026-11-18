// a place is { x, y } of an icon's top left corner
.pragma library

// slots count down the columns from the screen's far side
function slotPlace(grid, slot) {
    const column = Math.floor(slot / grid.rows)
    return { x: grid.width - grid.edge - column * grid.columnPitch - grid.iconSize / 2,
             y: grid.top + (slot % grid.rows) * grid.rowPitch }
}

function slotAt(grid, x, y) {
    const column = Math.max(0, Math.round((grid.width - grid.edge - grid.iconSize / 2 - x) / grid.columnPitch))
    const row = Math.max(0, Math.min(grid.rows - 1, Math.round((y - grid.top) / grid.rowPitch)))
    return column * grid.rows + row
}

// each icon takes a box one pitch square centred on its picture
function coversSlot(grid, place, slot) {
    const halfGap = (grid.columnPitch - grid.iconSize) / 2
    const left = place.x - halfGap
    const slotted = slotPlace(grid, slot)
    const slotLeft = slotted.x - halfGap
    return left < slotLeft + grid.columnPitch && slotLeft < left + grid.columnPitch
        && place.y < slotted.y + grid.rowPitch && slotted.y < place.y + grid.rowPitch
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
// on the grid the hand placed ones get their nearest free slots first
function places(grid, put, snap) {
    const result = put.slice()
    const putPlaces = result.filter(place => place !== null)
    const taken = {}
    if (snap) {
        const slots = freeSlots(grid, putPlaces, taken)
        for (let index = 0; index < result.length; index++) {
            if (result[index] !== null) {
                result[index] = slotPlace(grid, slots.shift())
            }
        }
    } else {
        const slotsToCheck = result.length + 4 * putPlaces.length
        for (let slot = 0; slot < slotsToCheck; slot++) {
            taken[slot] = putPlaces.some(place => coversSlot(grid, place, slot))
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
