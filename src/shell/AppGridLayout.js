// where the application grid puts its icons on a screen of any size
// in the proportions of Launchpad on OS X 10.9

.pragma library

// kept free above the grid for the search field, and between it and the dock
const searchRoom = 30
const dockGap = 30
// the least room above the first row and under the last
const leastTop = 35
const leastBottom = 16
// a row holds the icon, a gap, the name and this much or an eighth of its height
const nameGap = 11
const mostRowPadding = 20
// icons come in steps of 8, and a page drops rows before they go under this
const iconStep = 8
const leastIcon = 32

function rowsFitted(height, dockRoom, unit, rows) {
    const below = dockRoom + dockGap
    const spare = Math.floor(height - below - (unit + 40) * rows)
    const top = Math.max(Math.floor(spare * 0.15), leastTop)
    const bottom = Math.max(Math.floor(spare * 0.25), leastBottom)
    return {
        rows: rows,
        top: searchRoom + top,
        rowPitch: Math.trunc((height - below - searchRoom - top - bottom + 15) / rows),
        dotsAboveBottom: below + Math.floor(bottom / 2)
    }
}

// { rows, side, columnPitch, rowPitch, iconSize, firstIconTop, dotsAboveBottom }
// for a screen of this size with dockRoom taken by the dock on its dockSide, bottom by default
function fitted(screenWidth, height, dockRoom, nameHeight, columns, rows, largestIcon, dockSide) {
    const sideways = dockSide === "left" || dockSide === "right"
    const width = screenWidth - (sideways ? dockRoom : 0)
    const unit = Math.floor(width / (2 * columns + 1))
    // wider screens draw the columns together, a little less beside a side dock
    const drawnIn = (width > 1920 ? 0.6 : (width > 1680 ? 0.3 : (width > 1440 ? 0.2 : 0))) - (sideways ? 0.1 : 0)
    const margin = Math.floor(Math.floor(unit / 2) + 0.2 * width * drawnIn)
    const side = margin + (dockSide === "left" ? dockRoom : 0)
    const columnPitch = Math.trunc((width - 2 * margin) / columns)
    const taken = nameHeight + nameGap + 24

    const below = sideways ? 0 : dockRoom
    let shown = rowsFitted(height, below, unit, rows)
    while (shown.rows > 1 && Math.min(columnPitch, shown.rowPitch) - taken < leastIcon) {
        shown = rowsFitted(height, below, unit, shown.rows - 1)
    }
    const room = Math.floor((Math.min(columnPitch, shown.rowPitch) - taken) / iconStep) * iconStep
    const iconSize = Math.max(20, Math.min(largestIcon, room))
    const padding = Math.min(Math.ceil(shown.rowPitch / 8), mostRowPadding)
    return {
        rows: shown.rows,
        side: side,
        columnPitch: columnPitch,
        rowPitch: shown.rowPitch,
        iconSize: iconSize,
        firstIconTop: shown.top + shown.rowPitch - (padding + nameHeight + nameGap + iconSize),
        dotsAboveBottom: shown.dotsAboveBottom
    }
}
