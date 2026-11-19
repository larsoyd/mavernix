.pragma library

// alert sounds have a level of their own and are no program
const alertRoles = ["notification", "event"]
// what may name the program, most exact first
const hintKeys = ["pipewire.access.portal.app_id", "application.id", "application.icon-name",
                  "application.process.binary", "application.name"]
const ownNameKeys = ["application.name", "application.process.binary", "node.name"]

function listed(properties) {
    return !alertRoles.includes((properties["media.role"] ?? "").toLowerCase())
}

function hints(properties) {
    const found = []
    for (const key of hintKeys) {
        const value = properties[key] ?? ""
        if (value !== "") {
            found.push(value)
        }
    }
    return found
}

// a wine program names its sound and its window alike, in different case
function windowFor(hints, appIds) {
    for (const hint of hints) {
        const found = appIds.find(appId => appId.toLowerCase() === hint.toLowerCase())
        if (found !== undefined) {
            return found
        }
    }
    return ""
}

function ownName(properties) {
    return ownNameKeys.map(key => properties[key] ?? "").find(value => value !== "") ?? ""
}

function label(properties) {
    return ownName(properties).replace(/\.exe$/i, "")
}

// one row for each program, its streams counted from the list given
function groups(streams) {
    const rows = new Map()
    streams.forEach((stream, index) => {
        const name = ownName(stream.properties)
        if (name === "" || !listed(stream.properties)) {
            return
        }
        if (!rows.has(name)) {
            rows.set(name, { key: name, label: label(stream.properties), hints: hints(stream.properties),
                             members: [], loudness: 0, muted: true })
        }
        const row = rows.get(name)
        row.members.push(index)
        if (!stream.muted) {
            row.muted = false
            row.loudness = Math.max(row.loudness, Math.min(1, stream.volume))
        }
    })
    return Array.from(rows.values()).sort((one, other) =>
        order(one.label.toLowerCase(), other.label.toLowerCase()) || order(one.key, other.key))
}

function order(one, other) {
    return one < other ? -1 : (one > other ? 1 : 0)
}
