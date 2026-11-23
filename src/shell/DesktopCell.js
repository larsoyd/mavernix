// metrics hold the title font's zero width and line height in whole pixels
.pragma library

// the square the icon picture sits in, a sixteenth of it on each side
function iconBox(iconSize) {
    return iconSize + 2 * Math.floor(iconSize / 16)
}

// four zeros at the tightest spacing up to twenty at a spacing of 100
function titleRoom(zero, spacing) {
    return Math.ceil(zero * (4 + 16 * Math.min(spacing, 100) / 100))
}

// a title under its icon or beside one larger than 16 takes two lines once the spacing passes 25
function titleLines(view) {
    return (!view.right || view.iconSize > 16) && view.spacing > 25 ? 2 : 1
}

function even(length) {
    const whole = Math.ceil(length)
    return whole + whole % 2
}

function cell(view, metrics) {
    const box = iconBox(view.iconSize)
    const room = view.right ? titleRoom(metrics.zero, view.spacing) : Math.max(titleRoom(metrics.zero, view.spacing), box)
    const title = Math.ceil(metrics.line * titleLines(view)) + (view.info ? Math.ceil(metrics.infoLine) : 0)
    if (!view.right) {
        return { width: even(10 + room + 1), height: even(2 + box + 2 + title + 4) }
    }
    const below = view.iconSize > 16 ? 6 : (box <= title + 2 ? 4 : 1)
    return { width: even(10 + room + 1 + box + 2), height: even(Math.max(title, box) + below) }
}

// where the picture and the title block start in a cell
function layout(view, size) {
    const inset = Math.floor(view.iconSize / 16)
    if (!view.right) {
        return { pictureX: (size.width - view.iconSize) / 2, pictureY: inset, titleX: 0, titleY: iconBox(view.iconSize) + 2 }
    }
    return { pictureX: inset, pictureY: (size.height - view.iconSize) / 2, titleX: iconBox(view.iconSize) + 2, titleY: 0 }
}
