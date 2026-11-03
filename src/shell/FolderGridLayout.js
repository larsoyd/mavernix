// How big a folder's grid in the dock gets, and how many columns and rows it
// shows. Every number comes from the theme's [dock.folder.grid] table
// (docs/themes/dock.md, "Folders in the dock").
//
// The grid is sized for a stage: the share `stage_share` of the free width
// and height. As many cells as the stage holds are shown at once, each
// counted with `fit_spacing` more room than it takes; the rest scroll. The
// shown cells make a near square, and shrink if the stage is too small for
// them at full size.

.pragma library

// Columns and rows for `count` cells: as many columns as its square root,
// and one more when the last row would otherwise hold no more cells than one
// fewer than there are rows (so 3 cells make 3 x 1, 7 make 4 x 2).
function nearSquare(count) {
    let columns = Math.ceil(Math.sqrt(count))
    let rows = Math.ceil(count / columns)
    const inLastRow = count % columns
    if (inLastRow !== 0 && inLastRow <= rows - 1) {
        columns += 1
        rows = Math.ceil(count / columns)
    }
    return { columns: columns, rows: rows }
}

// columns, rows in all, rows in view and cell scale up to 1 for a free area
// a clipboard stack's bound caps the share and the cells shown
function layout(looks, entryCount, freeWidth, freeHeight, bound) {
    const cellWidth = looks.cell_width ?? 112
    const cellHeight = looks.cell_height ?? 112
    const sidePadding = looks.side_padding ?? 12
    const firstRowTop = looks.first_row_top ?? 36
    const bottomPadding = looks.bottom_padding ?? 8
    const stageShare = Math.min(looks.stage_share ?? 1, bound?.share ?? 1)
    const fitSpacing = looks.fit_spacing ?? 0

    const stageWidth = Math.floor(Math.min(freeWidth * stageShare, freeWidth - 2 * sidePadding))
    const stageHeight = Math.floor(Math.min(freeHeight * stageShare, freeHeight - firstRowTop - bottomPadding))
    if (bound) {
        return bounded(looks, entryCount, stageWidth, stageHeight, bound)
    }

    const cellsThatFit = Math.max(1, Math.floor(
        stageWidth / (cellWidth + fitSpacing) * (stageHeight / (cellHeight + fitSpacing))))
    const shown = nearSquare(Math.min(entryCount, cellsThatFit))

    // Cells shrink to fit the stage, in whole pixels, but never grow.
    const cellScale = Math.min(1, Math.floor(stageWidth / shown.columns) / cellWidth,
                               Math.floor(stageHeight / shown.rows) / cellHeight)
    return {
        columns: shown.columns,
        shownRows: shown.rows,
        rows: Math.ceil(entryCount / shown.columns),
        cellScale: cellScale,
    }
}

// smallest a bounded cell gets before a row or column is let go
const leastBoundedScale = 0.5

// at most the bound's columns by rows, and on a small screen rows go first
// then columns while cells would shrink under half, so a few still show
function bounded(looks, entryCount, stageWidth, stageHeight, bound) {
    const cellWidth = looks.cell_width ?? 112
    const cellHeight = looks.cell_height ?? 112
    const shown = Math.min(entryCount, bound.columns * bound.rows)
    let columns = Math.min(nearSquare(shown).columns, bound.columns)
    let rows = Math.ceil(shown / columns)
    const scaleFor = (columns, rows) => Math.min(Math.floor(cellWidth * bound.scale) / cellWidth,
                                                 Math.floor(stageWidth / columns) / cellWidth,
                                                 Math.floor(stageHeight / rows) / cellHeight)
    while (scaleFor(columns, rows) < leastBoundedScale && rows + columns > 2) {
        if (rows > 1) {
            rows -= 1
        } else {
            columns -= 1
        }
    }
    return {
        columns: columns,
        shownRows: rows,
        rows: Math.ceil(entryCount / columns),
        cellScale: scaleFor(columns, rows),
    }
}
