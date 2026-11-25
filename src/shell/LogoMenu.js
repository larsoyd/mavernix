// the logo menu's items from panel.logo_menu, where an entry with entries is a submenu
// ticked names a shell state that ticks it, state with label_on swaps the label while that state holds

.pragma library

// a submenu's entry n is 1000 + 100 * its parent's index + n
const nested = 1000

function items(entries, holds) {
    return entries.map((entry, index) => item(entry, index, holds,
        (entry.entries ?? []).map((child, number) => item(child, nested + 100 * index + number, holds, []))))
}

function item(entry, id, holds, children) {
    const ticked = entry.ticked !== undefined
    const on = entry.state !== undefined && holds(entry.state)
    return { id: id, label: (on ? entry.label_on ?? entry.label : entry.label) ?? "", enabled: true,
             separator: entry.separator ?? false, checkable: ticked, checked: ticked && holds(entry.ticked),
             shortcut: [], children: children }
}

function entryFor(entries, id) {
    if (id < nested) {
        return entries[id]
    }
    const parent = entries[Math.floor((id - nested) / 100)]
    return parent?.entries?.[(id - nested) % 100]
}
