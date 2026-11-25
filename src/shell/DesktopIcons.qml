// The files on the desktop: the user's Desktop folder as icons with their
// names, in columns from the screen's far top corner. Icons can be picked,
// opened, dragged, dropped on and renamed; an icon dragged on the desktop
// stays where it was put.
//
// The shell only shows and picks; whatever is done to a file is done by the
// commands in the config's [desktop_files] (`gio` by default). Looks: the
// theme's [desktop.icons] table; a theme without it shows no files.

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Shell.Native
import "DesktopCell.js" as DesktopCell
import "DesktopPlaces.js" as DesktopPlaces
import "DesktopView.js" as DesktopView
import "DesktopKeys.js" as DesktopKeys
import "DesktopShortcut.js" as DesktopShortcut
import "DroppedFiles.js" as DroppedFiles
import "ThemeValues.js" as ThemeValues

Item {
    id: icons

    IconSource {
        id: themeIcons
    }

    // The shell's UserChoices keep the chosen order; the screen is for the menu's window.
    required property var settings
    required property var userChoices
    required property var screen
    // a dock on the right keeps its room, as Finder lays the desktop out in the screen less the dock
    property real dockOnRight: 0
    required property var sounds

    readonly property var looks: (settings.theme.desktop ?? ({})).icons ?? null
    readonly property var wishes: settings.config.desktop_files ?? ({})
    readonly property var words: wishes.words ?? ({})
    readonly property var fonts: settings.theme.fonts
    readonly property string themeDirectory: settings.theme_directory

    // The settings' Desktop pane can turn the icons off without a new login.
    readonly property bool shown: looks !== null && (userChoices.value("desktop_files", "show") ?? false)
    visible: shown

    readonly property string folder: DesktopShortcut.folder(Quickshell, settings)
    // finder's view options, kept in the user's choices
    function chosen(key) {
        return userChoices.value("desktop_files", key)
    }
    readonly property var view: ({
        iconSize: DesktopView.iconSize(chosen("icon_size") ?? 64),
        spacing: Math.max(1, Math.round(chosen("grid_spacing") ?? 54)),
        textSize: Math.max(10, Math.min(16, Math.round(chosen("text_size") ?? 12))),
        right: chosen("label_position") === "right",
        align: ["top", "center", "bottom"].includes(chosen("label_align")) ? chosen("label_align") : "top",
        info: chosen("show_item_info") === true,
        preview: chosen("show_icon_preview") ?? true
    })
    readonly property string arrangeBy: DesktopView.arrangement(chosen("arrange_by"), chosen("snap_to_grid"))
    // sorted icons take the slots in order and go back to them when dragged
    readonly property bool keepArranged: DesktopView.keepsArranged(arrangeBy)
    // on the grid even icons placed earlier by hand sit in slots
    readonly property bool snapToGrid: arrangeBy === "grid"
    readonly property bool deselectAfterOpening: chosen("deselect_after_opening") ?? false

    FolderContents {
        id: contents
        path: icons.shown ? icons.folder : ""
        // icons not put by hand flow by name, or in the order an old sort chose
        order: icons.keepArranged ? icons.arrangeBy : (icons.chosen("order") ?? "name")
        itemInfo: icons.shown && icons.view.info
    }
    readonly property var entries: contents.entries
    readonly property var infos: contents.infos

    // --- Where the icons go ---------------------------------------------------------

    readonly property real topRoom:
        (settings.config.panel?.position ?? "top") === "top" ? (settings.theme.panel?.height ?? 0) : 0
    readonly property real iconSize: view.iconSize
    readonly property var face: fonts.desktop_icon ?? fonts.interface
    FontMetrics {
        id: titleMetrics
        font.family: icons.face.family
        font.pixelSize: icons.view.textSize
        font.bold: icons.face.bold ?? false
    }
    FontMetrics {
        id: infoMetrics
        font.family: icons.face.family
        font.pixelSize: Math.max(9, icons.view.textSize - 2)
        font.bold: icons.face.bold ?? false
    }
    // finder's typeset line, which fits lucida grande's lines from 9 to 16 on the reference desktop
    function lineOf(metrics) {
        return Math.round(metrics.ascent) + Math.round(metrics.descent) + 1
    }
    // a measuring call does not follow a new font size, the font's height does
    readonly property real zeroWidth: titleMetrics.height > 0 ? Math.round(titleMetrics.advanceWidth("0")) : 0
    readonly property var cell: DesktopCell.cell(view, { zero: zeroWidth,
                                                         line: lineOf(titleMetrics), infoLine: lineOf(infoMetrics) })
    readonly property var inCell: DesktopCell.layout(view, cell)
    onCellChanged: console.info(`[desktop] view icon=${view.iconSize} spacing=${view.spacing} text=${view.textSize} right=${view.right} info=${view.info} zero=${zeroWidth} line=${lineOf(titleMetrics)} cell=${cell.width}x${cell.height}`)
    // finder keeps this far from the screen's edges
    readonly property real margin: looks?.margin ?? 4
    // The rows that fit between the panel and the room kept free at the bottom.
    readonly property int rows: Math.max(1, Math.floor((height - topRoom - margin - (looks?.bottom_room ?? 100)) / cell.height))

    readonly property var grid: ({
        right: width - margin - dockOnRight, top: topRoom + margin, rows: rows, cellWidth: cell.width, cellHeight: cell.height,
        pictureX: inCell.pictureX, pictureY: inCell.pictureY, iconSize: iconSize
    })

    // a name's lines and its item info, sized from the text size the theme's name look was made at
    readonly property var nameLook: looks?.name ?? ({})
    readonly property real lineHeight: DesktopCell.pitch(lineOf(titleMetrics), view.textSize)
    readonly property real nameRoom: DesktopCell.titleRoom(zeroWidth, view.spacing)
    readonly property int nameLines: DesktopCell.titleLines(view)
    // the name's place from the icon picture's corner, its plate reaching padding past its letters
    readonly property point namePlace: view.right
        ? Qt.point(inCell.titleX - inCell.pictureX - (nameLook.padding ?? 6), 0)
        : Qt.point(0, inCell.titleY - inCell.pictureY - 1)
    // a name beside its icon meets the picture's top or bottom or centres as finder's does
    function nameY(block, title, hasInfo) {
        if (!view.right) {
            return namePlace.y
        }
        const room = cell.height - block
        const y = view.align === "bottom" ? iconSize - block
            : view.align === "center"
                ? DesktopCell.centredTitle(cell.height, title, hasInfo ? lineOf(infoMetrics) - 1 : 0) - inCell.pictureY
            : 0
        return Math.max(-inCell.pictureY, Math.min(y, room - inCell.pictureY))
    }
    function infoText(entry) {
        const info = infos[entry.path] ?? ({})
        if (info.items !== undefined) {
            const count = words["items_" + (info.items === 0 ? "none" : info.items === 1 ? "one" : "many")]
                ?? (info.items === 0 ? "No items" : info.items === 1 ? "1 item" : "%1 items")
            return count.arg(Number(info.items).toLocaleString(Qt.locale(), "f", 0))
        }
        if (info.width !== undefined) {
            const number = value => Number(value).toLocaleString(Qt.locale(), "f", 0)
            return number(info.width) + "\u2006×\u2006" + number(info.height)
        }
        if (info.seconds !== undefined) {
            const two = value => String(value).padStart(2, "0")
            const hours = Math.floor(info.seconds / 3600)
            const rest = two(Math.floor(info.seconds % 3600 / 60)) + ":" + two(info.seconds % 60)
            return hours > 0 ? hours + ":" + rest : rest
        }
        return ""
    }

    // how long a drag rests on a folder before it springs open, none without it
    readonly property int springMs: looks?.spring_ms ?? 0

    // icons glide only to places a person asked for, not when the screen changes size
    readonly property int moveMs: looks?.move_ms ?? 0
    property bool gliding: false
    // finder moves icons evenly for a new icon size or spacing and eases them otherwise
    property bool glidesEvenly: false
    function glideNext(evenly) {
        glidesEvenly = evenly === true
        gliding = true
        glideEnd.restart()
    }
    Timer {
        id: glideEnd
        interval: icons.moveMs + 1000
        onTriggered: icons.gliding = false
    }

    // Files put by hand: name -> offset of the icon's left edge from the
    // screen's far side and its top from the screen's top. Measured from the
    // far side so the icons stay beside that edge on other screen sizes.
    readonly property var putByHand: userChoices.value("desktop_files", "places") ?? ({})

    // one place per entry, same order
    readonly property var places: DesktopPlaces.places(grid, entries.map(entry => {
        const put = icons.keepArranged ? undefined : putByHand[entry.name]
        return put === undefined ? null : { x: width - put[0], y: put[1], loose: put[2] === true }
    }), snapToGrid)
    function iconX(index) {
        return places[index]?.x ?? 0
    }
    function iconY(index) {
        return places[index]?.y ?? 0
    }

    // Remembers places put by hand ({ name: { x: left, y: top } }) and drops
    // files that are gone.
    function rememberPlaces(moved) {
        const next = {}
        for (const entry of entries) {
            const place = moved[entry.name]
            if (place !== undefined) {
                const stored = [Math.round(width - place.x), Math.round(place.y)]
                next[entry.name] = place.loose ? stored.concat([true]) : stored
            } else if (putByHand[entry.name] !== undefined) {
                next[entry.name] = putByHand[entry.name]
            }
        }
        userChoices.set("desktop_files", "places", next)
    }

    // dragged icon's middle lands at the point, all picked ones stay on screen
    // and under the panel, on the grid they take free slots
    function dropPicked(draggedPath, middleX, middleY) {
        const draggedIndex = entries.findIndex(entry => entry.path === draggedPath)
        if (draggedIndex < 0) {
            return
        }

        const byX = middleX - iconSize / 2 - iconX(draggedIndex)
        const byY = middleY - iconSize / 2 - iconY(draggedIndex)
        const drop = { names: [], dropped: [], kept: [] }
        for (let index = 0; index < entries.length; index++) {
            if (isPicked(entries[index].path)) {
                const point = { x: Math.max(0, Math.min(width - iconSize, iconX(index) + byX)),
                                y: Math.max(topRoom, Math.min(height - iconSize, iconY(index) + byY)) }
                drop.names.push(entries[index].name)
                drop.dropped.push(point)
                // put down where the drag let go, so only the snap glides
                iconItems.itemAt(index)?.jump(point)
            } else if (putByHand[entries[index].name] !== undefined) {
                drop.kept.push(places[index])
            }
        }
        pendingDrop = drop
        heldKeys.check()
    }
    property var pendingDrop: null
    // super alone held at the drop turns the grid the other way for it, as command does in finder
    HeldKeys {
        id: heldKeys
        onChecked: held => {
            const drop = icons.pendingDrop
            icons.pendingDrop = null
            if (drop === null) {
                return
            }
            const commandAlone = held.super && !held.alt && !held.ctrl && !held.shift
            const snap = commandAlone ? !icons.snapToGrid : icons.snapToGrid
            const loose = drop.dropped.map(point => ({ x: point.x, y: point.y, loose: icons.snapToGrid }))
            const decided = snap ? DesktopPlaces.onGrid(icons.grid, drop.kept, drop.dropped) : loose
            icons.glideNext()
            icons.rememberPlaces(icons.placesByName(drop.names, decided))
            // a place the drop left as it was sends no change, so those icons are sent back by hand
            drop.names.forEach((name, number) => {
                const index = icons.entries.findIndex(entry => entry.name === name)
                const now = icons.places[index]
                if (now !== undefined && now.x === decided[number].x && now.y === decided[number].y) {
                    iconItems.itemAt(index)?.settle()
                }
            })
        }
    }

    // Snaps every hand-placed icon to its nearest free slot.
    function cleanUp() {
        const names = []
        const put = []
        for (let index = 0; index < entries.length; index++) {
            if (putByHand[entries[index].name] !== undefined) {
                names.push(entries[index].name)
                put.push(places[index])
            }
        }
        glideNext()
        rememberPlaces(placesByName(names, DesktopPlaces.onGrid(grid, [], put)))
    }
    // finder's clean up by: every icon into the slots in that order, once
    function cleanUpBy(order) {
        const byName = {}
        contents.pathsInOrder(order).forEach((path, slot) => {
            const entry = entries.find(found => found.path === path)
            if (entry !== undefined) {
                byName[entry.name] = DesktopPlaces.slotPlace(grid, slot)
            }
        })
        glideNext()
        rememberPlaces(byName)
    }
    readonly property bool canCleanUp: !keepArranged && entries.length > 0

    // a view change ends a rename, and a new cell scales hand placed icons about the first slot
    function chooseView(key, value) {
        if (chosen(key) === value) {
            return
        }
        renamingPath = ""
        const before = grid
        userChoices.set("desktop_files", key, value)
        if (["icon_size", "grid_spacing", "label_position"].includes(key) && !keepArranged) {
            rescalePlaces(before)
        }
        glideNext(key === "icon_size" || key === "grid_spacing")
    }
    function rescalePlaces(before) {
        const names = entries.map(entry => entry.name).filter(name => putByHand[name] !== undefined)
        const put = names.map(name => {
            const stored = putByHand[name]
            return { x: width - stored[0], y: stored[1], loose: stored[2] === true }
        })
        const moved = DesktopPlaces.rescaled(before, grid, put, { width: width, height: height })
        const next = {}
        names.forEach((name, number) => {
            const place = moved[number]
            if (place !== null) {
                const stored = [Math.round(width - place.x), Math.round(place.y)]
                next[name] = place.loose ? stored.concat([true]) : stored
            }
        })
        userChoices.set("desktop_files", "places", next)
    }
    // sorted icons leave their slots as hand placed ones, so nothing moves when the sort is turned off
    function arrange(by) {
        if (by === arrangeBy) {
            return
        }
        renamingPath = ""
        glideNext()
        if (keepArranged && !DesktopView.keepsArranged(by)) {
            rememberPlaces(placesByName(entries.map(entry => entry.name), places))
        } else if (by === "grid") {
            cleanUp()
        }
        userChoices.set("desktop_files", "arrange_by", by)
        userChoices.set("desktop_files", "snap_to_grid", undefined)
    }

    // finder's view options panel, one for the desktop, where it was last put
    property bool viewOptionsShown: false
    function toggleViewOptions() {
        viewOptionsShown = !viewOptionsShown
    }
    function themeIconPath(name) {
        return themeIcons.path(name, "")
    }
    DesktopViewOptions {
        desktop: icons
    }

    function placesByName(names, points) {
        const byName = {}
        names.forEach((name, number) => { byName[name] = points[number] })
        return byName
    }

    // --- What is picked ---------------------------------------------------------------

    property var pickedPaths: []
    // picked files dim while a window is in front of the desktop
    readonly property bool frontmost: Window.active

    function isPicked(path) {
        return pickedPaths.includes(path)
    }
    function pickOnly(path) {
        pickedPaths = [path]
    }
    function togglePicked(path) {
        pickedPaths = isPicked(path) ? pickedPaths.filter(other => other !== path) : pickedPaths.concat([path])
    }
    function pickNone() {
        pickedPaths = []
    }
    onEntriesChanged: {
        const there = entries.map(entry => entry.path)
        pickedPaths = pickedPaths.filter(path => there.includes(path))
    }

    // --- What is done to files ------------------------------------------------------------

    // with a token from the click, so a program that has the file open brings its window back
    ActivatedStart {
        id: starter
    }
    function run(command, more) {
        if ((command ?? []).length > 0) {
            starter.run(command.concat(more), icons)
        }
    }
    // An executable .desktop file is started, with any files given to it.
    function isLauncher(path) {
        return entries.some(entry => entry.path === path && entry.isLauncher)
    }
    function launch(launcher, files) {
        starter.run(["gio", "launch", launcher].concat(files ?? []), icons)
    }
    function openPicked() {
        const launchers = pickedPaths.filter(path => isLauncher(path))
        const files = pickedPaths.filter(path => !isLauncher(path))
        for (const launcher of launchers) {
            launch(launcher)
            leaveGhost(entries.findIndex(entry => entry.path === launcher))
        }
        // one at a time from its icon, so its window can grow out of it
        if ((wishes.open_from_icon_command ?? []).length > 0) {
            files.forEach(openFromIcon)
        } else if (files.length > 0) {
            run(wishes.open_command, files)
        }
        afterOpening()
    }
    function openFromIcon(file) {
        const fromIcon = wishes.open_from_icon_command ?? []
        run(fromIcon.length > 0 ? fromIcon : wishes.open_command, fromIcon.length > 0 ? [file, file] : [file])
    }

    // a program opened from its icon leaves a ghost that grows to twice its size as it fades
    readonly property int launchMs: looks?.launch_ms ?? 0
    function leaveGhost(index) {
        if (launchMs > 0 && index >= 0) {
            launchGhost.createObject(icons, { x: iconX(index), y: iconY(index),
                                              source: themeIcons.path(entries[index].icon, entries[index].genericIcon) })
        }
    }
    Component {
        id: launchGhost
        Image {
            id: ghost
            property real grown: 0
            width: icons.iconSize
            height: icons.iconSize
            sourceSize: Qt.size(width * 2 * Screen.devicePixelRatio, height * 2 * Screen.devicePixelRatio)
            scale: 1 + grown
            opacity: 0.6 * (1 - grown)
            NumberAnimation on grown {
                from: 0
                to: 1
                duration: icons.launchMs
                onFinished: ghost.destroy()
            }
        }
    }

    // where each icon is, for windows that come from one and go back into it
    IconOrigins {
        id: iconOrigins
    }
    function tellOrigins() {
        const boxes = {}
        if (shown) {
            entries.forEach((entry, index) => {
                boxes[entry.path] = [iconX(index), iconY(index), iconSize, iconSize]
            })
        }
        iconOrigins.place(screen.name, boxes)
    }
    onPlacesChanged: Qt.callLater(tellOrigins)
    onShownChanged: Qt.callLater(tellOrigins)
    function afterOpening() {
        if (deselectAfterOpening) {
            pickNone()
        }
    }
    // the sound comes once the files are in the trash, as finder's does
    function trashPicked() {
        if (pickedPaths.length > 0 && (wishes.trash_command ?? []).length > 0) {
            trashing.createObject(icons, { command: wishes.trash_command.concat(pickedPaths) })
        }
    }
    Component {
        id: trashing
        Process {
            running: true
            onExited: code => {
                if (code === 0) {
                    icons.sounds.play("file-trash")
                }
                destroy()
            }
        }
    }
    function duplicatePicked() {
        if (pickedPaths.length > 0) {
            run(wishes.duplicate_command, pickedPaths)
        }
    }

    property string renamingPath: ""
    // a second slow click on the name of the one picked file renames it, once a double click is ruled out
    Timer {
        id: renameSoon
        property string path: ""
        interval: Qt.styleHints.mouseDoubleClickInterval
        onTriggered: {
            if (icons.pickedPaths.length === 1 && icons.pickedPaths[0] === path) {
                icons.startRenaming()
            }
        }
    }
    function startRenaming() {
        if (pickedPaths.length === 1 && (wishes.rename_command ?? []).length > 0) {
            renamingPath = pickedPaths[0]
        }
    }
    function finishRenaming(newName) {
        const path = renamingPath
        renamingPath = ""
        // places are kept by the name shown, and a launcher shows its Name and not its file's
        const oldName = entries.find(entry => entry.path === path)?.name ?? path.substring(path.lastIndexOf("/") + 1)
        if (newName !== "" && newName !== oldName) {
            if (putByHand[oldName] !== undefined) {
                const places = Object.assign({}, putByHand)
                places[newName] = places[oldName]
                delete places[oldName]
                userChoices.set("desktop_files", "places", places)
            }
            run(wishes.rename_command, [path, newName])
        }
    }

    function newFolder() {
        run(wishes.new_folder_command, [folder, words.new_folder_name ?? "New Folder"])
    }
    function takeIn(paths) {
        if (paths.length > 0) {
            run(wishes.move_in_command, [folder].concat(paths))
        }
    }

    // --- The bare desktop: the band, the desktop's menu, dropped files ---------------------

    // The band: where the press was, where the pointer is, and what was picked
    // before it began (kept with Control or Shift).
    property bool banding: false
    property point bandFrom: Qt.point(0, 0)
    property point bandTo: Qt.point(0, 0)
    property var pickedBeforeBand: []
    readonly property rect band: Qt.rect(Math.min(bandFrom.x, bandTo.x), Math.min(bandFrom.y, bandTo.y),
                                         Math.abs(bandTo.x - bandFrom.x), Math.abs(bandTo.y - bandFrom.y))

    function pickInBand() {
        const reach = looks?.picked_reach ?? 4
        const touched = []
        for (let index = 0; index < entries.length; index++) {
            const left = iconX(index) - reach
            const top = iconY(index) - reach
            const side = iconSize + 2 * reach
            const apart = left > band.x + band.width || left + side < band.x
                || top > band.y + band.height || top + side < band.y
            if (!apart) {
                touched.push(entries[index].path)
            }
        }
        pickedPaths = pickedBeforeBand.filter(path => !touched.includes(path)).concat(touched)
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onPressed: mouse => {
            icons.forceActiveFocus()
            renameSoon.stop()
            if (mouse.button === Qt.RightButton) {
                icons.pickNone()
                icons.openMenu("desktop", mouse.x, mouse.y)
                return
            }
            const adding = (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0
            icons.pickedBeforeBand = adding ? icons.pickedPaths : []
            if (!adding) {
                icons.pickNone()
            }
            icons.bandFrom = Qt.point(mouse.x, mouse.y)
            icons.bandTo = icons.bandFrom
            icons.banding = true
        }
        onPositionChanged: mouse => {
            if (icons.banding) {
                icons.bandTo = Qt.point(mouse.x, mouse.y)
                icons.pickInBand()
            }
        }
        onReleased: icons.banding = false
    }

    property string carriedPath: ""

    DropArea {
        anchors.fill: parent
        onDropped: drop => {
            const paths = DroppedFiles.paths(drop)
            if (paths.length === 0) {
                return
            }
            // the desktop's own icon dropped here is placed, or slides back to its sorted slot
            if (icons.carriedPath !== "" && paths.includes(icons.carriedPath) && icons.keepArranged) {
                drop.accepted = false
                return
            }
            if (icons.carriedPath !== "" && paths.includes(icons.carriedPath)) {
                icons.dropPicked(icons.carriedPath, drop.x - icons.heldOffset.x + icons.iconSize / 2,
                                 drop.y - icons.heldOffset.y + icons.iconSize / 2)
            } else {
                icons.takeIn(paths)
            }
            drop.accept(Qt.MoveAction)
        }
    }

    focus: true
    Keys.onDeletePressed: trashPicked()
    Keys.onEscapePressed: pickNone()
    Keys.onSpacePressed: previewPicked()
    Keys.onReturnPressed: startRenaming()
    Keys.onPressed: event => {
        const arrows = { [Qt.Key_Left]: "left", [Qt.Key_Right]: "right", [Qt.Key_Up]: "up", [Qt.Key_Down]: "down" }
        const control = (event.modifiers & Qt.ControlModifier) !== 0
        if (arrows[event.key] !== undefined) {
            movePick(arrows[event.key], (event.modifiers & Qt.ShiftModifier) !== 0)
        } else if (control && event.key === Qt.Key_A) {
            pickedPaths = entries.map(entry => entry.path)
        } else if (control && event.key === Qt.Key_O) {
            openPicked()
        } else if (control && event.key === Qt.Key_D) {
            duplicatePicked()
        } else if (control && event.key === Qt.Key_Backspace) {
            trashPicked()
        } else if ((event.modifiers & Qt.MetaModifier) !== 0 && event.key === Qt.Key_J) {
            toggleViewOptions()
        } else if (!control && (event.modifiers & (Qt.AltModifier | Qt.MetaModifier)) === 0
                   && event.text.trim() !== "") {
            typeToPick(event.text)
        } else {
            return
        }
        event.accepted = true
    }

    // arrows move the pick to the nearest icon that way, shift adds it
    function movePick(direction, adding) {
        const from = entries.findIndex(entry => entry.path === pickedPaths[pickedPaths.length - 1])
        const to = DesktopKeys.next(places, from, direction)
        if (to < 0) {
            return
        }
        const path = entries[to].path
        pickedPaths = adding ? pickedPaths.filter(other => other !== path).concat([path]) : [path]
    }

    // letters typed close together spell the start of a name
    property string typedSoFar: ""
    Timer {
        id: typingPause
        interval: 1000
        onTriggered: icons.typedSoFar = ""
    }
    function typeToPick(text) {
        typedSoFar += text
        typingPause.restart()
        const found = DesktopKeys.typed(entries.map(entry => entry.name), typedSoFar)
        if (found >= 0) {
            pickOnly(entries[found].path)
        }
    }

    function previewPicked() {
        if (pickedPaths.length > 0) {
            run(wishes.preview_command, pickedPaths)
        }
    }

    // --- What a drag carries -----------------------------------------------------------------

    readonly property string linkEmblem: themeIcons.find(looks?.link_emblem?.icon ?? "")
    function iconSourceOf(entry) {
        return themeIcons.path(entry.icon, entry.genericIcon)
    }
    // the icons the last press carries, so the drag picture is built per press and not per pick
    property var flockIndices: []
    // the picked icons and the room their names can take, around which the drag picture is cut
    readonly property rect flockBox: {
        if (flockIndices.length === 0) {
            return Qt.rect(0, 0, 1, 1)
        }
        const lefts = flockIndices.map(index => iconX(index) - inCell.pictureX - 2)
        const tops = flockIndices.map(index => iconY(index) - inCell.pictureY - 2)
        const left = Math.floor(Math.min(...lefts))
        const top = Math.floor(Math.min(...tops))
        return Qt.rect(left, top, Math.ceil(Math.max(...lefts) + cell.width + 4) - left,
                       Math.ceil(Math.max(...tops) + cell.height + 4) - top)
    }
    property string flockPicture: ""
    // a drag waits for its own press's picture, or past the deadline goes with the file's icon
    property bool flockLate: false
    readonly property bool flockReady: flockPicture !== "" || flockLate
    property point flockHold: Qt.point(0, 0)
    property point flockFrom: Qt.point(0, 0)
    property int heldIndex: -1
    // where in the held icon the pointer took it
    property point heldOffset: Qt.point(iconSize / 2, iconSize / 2)

    function takeDragPicture(index, holdX, holdY) {
        flockIndices = entries.map((entry, at) => at).filter(at => pickedPaths.includes(entries[at].path))
        heldIndex = index
        heldOffset = Qt.point(holdX - iconX(index), holdY - iconY(index))
        flockFrom = Qt.point(flockBox.x, flockBox.y)
        flockHold = Qt.point(holdX - flockBox.x, holdY - flockBox.y)
        flockPicture = ""
        flockLate = false
        const asked = Date.now()
        flockDeadline.restart()
        flock.grabToImage(result => {
            flockDeadline.stop()
            icons.flockPicture = result.url
            console.info(`[desktop] drag picture ready ms=${Date.now() - asked} icons=${icons.flockIndices.length}`)
        })
    }
    Timer {
        id: flockDeadline
        interval: 150
        onTriggered: {
            icons.flockLate = true
            console.warn(`[desktop] drag picture late ms=${interval} icons=${icons.flockIndices.length}`)
        }
    }

    // drawn beside the screen, only its picture is ever shown
    Item {
        id: flock
        x: icons.width + 100
        width: icons.flockBox.width
        height: icons.flockBox.height

        Item {
            anchors.fill: parent
            opacity: icons.looks?.drag_opacity ?? 1

            Repeater {
                model: icons.flockIndices
                Item {
                    required property int modelData
                    readonly property var entry: icons.entries[modelData] ?? ({ icon: "", path: "", name: "" })
                    x: icons.iconX(modelData) - icons.flockBox.x
                    y: icons.iconY(modelData) - icons.flockBox.y
                    width: icons.iconSize
                    height: icons.iconSize

                    DesktopIconPicture {
                        anchors.fill: parent
                        entry: parent.entry
                        iconSource: icons.iconSourceOf(parent.entry)
                        preview: icons.view.preview
                        frame: icons.looks?.preview_frame ?? null
                        linkEmblem: icons.linkEmblem
                        linkEmblemShare: icons.looks?.link_emblem?.share ?? 1
                    }
                    DesktopName {
                        look: icons.nameLook
                        text: parent.entry.name
                        face: icons.fonts.desktop_icon ?? icons.fonts.interface
                        lit: true
                        picked: true
                        themeDirectory: icons.themeDirectory
                        pixelSize: icons.view.textSize
                        lineHeight: icons.lineHeight
                        room: icons.nameRoom
                        most: icons.nameLines
                        alignLeft: icons.view.right
                        info: icons.view.info ? icons.infoText(parent.entry) : ""
                        infoPixelSize: infoMetrics.font.pixelSize
                        x: icons.view.right ? icons.namePlace.x : Math.floor((parent.width - width) / 2)
                        y: icons.nameY(height, titleHeight, shownInfo !== "")
                    }
                }
            }
        }
        // the count, at the lower right of the icon the pointer holds
        Item {
            readonly property var look: icons.looks?.drag_badge ?? ({})
            readonly property real size: look.size ?? 18
            visible: icons.flockIndices.length > 1 && icons.heldIndex >= 0
            width: Math.max(size, count.implicitWidth + size / 2)
            height: size
            x: icons.iconX(icons.heldIndex) - icons.flockBox.x + icons.iconSize - width + size / 4
            y: icons.iconY(icons.heldIndex) - icons.flockBox.y + icons.iconSize - height + size / 4

            Fill {
                anchors.fill: parent
                fill: Object.assign({}, parent.look.fill ?? { color: "#e0201c" }, { radius: parent.size / 2 })
                themeDirectory: icons.themeDirectory
            }
            Text {
                id: count
                anchors.centerIn: parent
                text: icons.flockIndices.length
                color: ThemeValues.color(parent.look.text_color ?? "#ffffff")
                font.family: (icons.fonts.desktop_icon ?? icons.fonts.interface).family
                font.pixelSize: Math.round(parent.size * 0.65)
                font.bold: true
            }
        }
    }

    // a drop nobody took slides the picture back to the icons, decelerating, as finder's does
    readonly property int slideBackMs: looks?.slide_back_ms ?? 0
    function slideBack() {
        if (slideBackMs > 0 && flockPicture !== "") {
            pointerPlace.ask()
        }
    }
    PointerPlace {
        id: pointerPlace
        onFound: (x, y) => {
            slider.from = Qt.point(x - icons.screen.x - icons.flockHold.x, y - icons.screen.y - icons.flockHold.y)
            slider.restart()
        }
    }
    PanelWindow {
        id: slider
        property point from: Qt.point(0, 0)
        property real done: 0
        function restart() {
            sliding.restart()
        }
        screen: icons.screen
        visible: sliding.running
        anchors {
            left: true
            right: true
            top: true
            bottom: true
        }
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
        WlrLayershell.namespace: "mavernix-slide-back"
        color: "transparent"
        mask: Region {}

        Image {
            source: icons.flockPicture
            x: slider.from.x + (icons.flockFrom.x - slider.from.x) * slider.done
            y: slider.from.y + (icons.flockFrom.y - slider.from.y) * slider.done
        }
        NumberAnimation {
            id: sliding
            target: slider
            property: "done"
            from: 0
            to: 1
            duration: icons.slideBackMs
            easing.type: Easing.OutSine
        }
    }

    // --- The icons -------------------------------------------------------------------------

    Repeater {
        id: iconItems
        model: icons.entries

        DesktopIconMotion {
            id: fileItem
            required property var modelData
            required property int index
            readonly property bool picked: icons.isPicked(modelData.path)
            readonly property var name: icons.looks?.name ?? ({})

            place: Qt.point(icons.iconX(index), icons.iconY(index))
            duration: icons.gliding ? icons.moveMs : 0
            evenly: icons.glidesEvenly
            width: icons.iconSize
            height: icons.iconSize

            Fill {
                readonly property real reach: icons.looks?.picked_reach ?? 4
                visible: fileItem.picked
                anchors.fill: parent
                anchors.margins: -reach
                fill: icons.looks?.picked ?? ({ color: "#00000040", radius: 4 })
                themeDirectory: icons.themeDirectory
            }
            DesktopIconPicture {
                anchors.fill: parent
                entry: fileItem.modelData
                iconSource: icons.iconSourceOf(fileItem.modelData)
                preview: icons.view.preview
                frame: icons.looks?.preview_frame ?? null
                linkEmblem: icons.linkEmblem
                linkEmblemShare: icons.looks?.link_emblem?.share ?? 1
            }

            // finder's name bubble shows while picked or while something is dragged over it
            DesktopName {
                id: namePlate
                look: fileItem.name
                text: fileItem.modelData.name
                face: icons.fonts.desktop_icon ?? icons.fonts.interface
                lit: (fileItem.picked || fileDrop.containsDrag) && !fileItem.blinkedOff
                picked: fileItem.picked
                away: !icons.frontmost
                lettersShown: icons.renamingPath !== fileItem.modelData.path
                themeDirectory: icons.themeDirectory
                pixelSize: icons.view.textSize
                lineHeight: icons.lineHeight
                room: icons.nameRoom
                most: icons.nameLines
                alignLeft: icons.view.right
                info: icons.view.info ? icons.infoText(fileItem.modelData) : ""
                infoPixelSize: infoMetrics.font.pixelSize
                x: icons.view.right ? icons.namePlace.x : Math.floor((parent.width - width) / 2)
                y: icons.nameY(height, titleHeight, shownInfo !== "")
            }

            // Drag data: the picked files' addresses, and their paths as plain text for
            // programs that take only text. Qt needs an item to move while dragging.
            Item {
                id: carried
                Drag.active: press.drag.active && icons.flockReady
                Drag.onActiveChanged: {
                    if (Drag.active) {
                        icons.carriedPath = fileItem.modelData.path
                    }
                }
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction | Qt.MoveAction | Qt.LinkAction
                Drag.mimeData: ({
                    "text/uri-list": icons.pickedPaths.map(path => "file://" + encodeURI(path)).join("\r\n") + "\r\n",
                    "text/plain": icons.pickedPaths.join("\n")
                })
                // what follows the pointer: the picked icons as taken on press, else the icon for the file's kind
                // only the pressed icon loads one, at its size on screen, loads block the main thread
                Drag.imageSource: !press.pressed ? ""
                                  : icons.flockPicture !== "" ? icons.flockPicture : icons.iconSourceOf(fileItem.modelData)
                Drag.imageSourceSize: icons.flockPicture !== "" ? Qt.size(-1, -1)
                                      : Qt.size(icons.iconSize * Screen.devicePixelRatio, icons.iconSize * Screen.devicePixelRatio)
                Drag.hotSpot: icons.flockPicture !== "" ? icons.flockHold
                                                        : Qt.point(icons.iconSize / 2, icons.iconSize / 2)
                Drag.onDragFinished: dropAction => {
                    x = 0
                    y = 0
                    icons.carriedPath = ""
                    if (dropAction === Qt.IgnoreAction) {
                        icons.slideBack()
                    }
                }
            }

            // a drag held over a folder blinks it three times and opens it, as finder's spring loaded folders do
            property int blinks: 0
            readonly property bool blinkedOff: blinks % 2 === 1
            readonly property bool springs: fileItem.modelData.isFolder && icons.springMs > 0
            Timer {
                id: springWait
                interval: icons.springMs
                onTriggered: springBlink.start()
            }
            Timer {
                id: springBlink
                interval: icons.looks?.spring_blink_ms ?? 83
                repeat: true
                triggeredOnStart: true
                onTriggered: {
                    if (fileItem.blinks < 3) {
                        fileItem.blinks += 1
                        return
                    }
                    stop()
                    fileItem.blinks = 0
                    icons.openFromIcon(fileItem.modelData.path)
                }
            }

            // Files dropped on a program launcher are given to its program; files
            // dropped on a folder are moved into it.
            DropArea {
                id: fileDrop
                enabled: fileItem.modelData.isLauncher || fileItem.modelData.isFolder
                anchors.fill: parent
                onContainsDragChanged: {
                    springBlink.stop()
                    fileItem.blinks = 0
                    if (containsDrag && fileItem.springs) {
                        springWait.restart()
                    } else {
                        springWait.stop()
                    }
                }
                // An icon dragged over itself is left to the desktop, which places it.
                onEntered: drag => {
                    if (icons.carriedPath !== "" && fileItem.picked) {
                        drag.accepted = false
                    }
                }
                onDropped: drop => {
                    const paths = DroppedFiles.paths(drop)
                        .filter(path => path !== fileItem.modelData.path)
                    if (paths.length === 0) {
                        return
                    }
                    if (fileItem.modelData.isLauncher) {
                        icons.launch(fileItem.modelData.path, paths)
                        icons.afterOpening()
                        drop.accept(Qt.CopyAction)
                    } else {
                        icons.run(icons.wishes.move_in_command, [fileItem.modelData.path].concat(paths))
                        drop.accept(Qt.MoveAction)
                    }
                }
            }
            Fill {
                readonly property real reach: icons.looks?.picked_reach ?? 4
                visible: fileDrop.containsDrag && !fileItem.blinkedOff
                anchors.fill: parent
                anchors.margins: -reach
                fill: icons.looks?.picked ?? ({ color: "#00000040", radius: 4 })
                themeDirectory: icons.themeDirectory
            }

            MouseArea {
                id: press
                x: Math.min(0, namePlate.x)
                y: Math.min(0, namePlate.y)
                width: Math.max(parent.width, namePlate.x + namePlate.width) - x
                height: Math.max(parent.height, namePlate.y + namePlate.height) - y
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                drag.target: carried

                property bool armsRename: false
                onPressed: mouse => {
                    icons.forceActiveFocus()
                    renameSoon.stop()
                    const path = fileItem.modelData.path
                    armsRename = mouse.button === Qt.LeftButton && mouse.modifiers === Qt.NoModifier
                        && fileItem.picked && icons.pickedPaths.length === 1 && icons.frontmost
                        && namePlate.contains(mapToItem(namePlate, mouse.x, mouse.y))
                    if (mouse.button === Qt.RightButton) {
                        // A file that is not picked becomes the only one picked.
                        if (!fileItem.picked) {
                            icons.pickOnly(path)
                        }
                        const place = mapToItem(icons, mouse.x, mouse.y)
                        icons.openFileMenu(path, fileItem.modelData.isFolder, place.x, place.y)
                    } else if ((mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0) {
                        icons.togglePicked(path)
                    } else if (!fileItem.picked) {
                        icons.pickOnly(path)
                    }
                    if (mouse.button === Qt.LeftButton) {
                        const held = mapToItem(icons, mouse.x, mouse.y)
                        icons.takeDragPicture(fileItem.index, held.x, held.y)
                    }
                }
                // A press on one of several picked files keeps them all picked so they can
                // be dragged together; released without a drag, it picks that file alone.
                onClicked: mouse => {
                    if (mouse.button === Qt.LeftButton
                        && (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) === 0) {
                        icons.pickOnly(fileItem.modelData.path)
                    }
                    if (armsRename) {
                        renameSoon.path = fileItem.modelData.path
                        renameSoon.restart()
                    }
                }
                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton) {
                        icons.openPicked()
                    }
                }
            }
        }
    }

    DesktopBand {
        dragging: icons.banding
        span: icons.band
        look: icons.looks?.band
    }

    // --- Typing a new name ----------------------------------------------------------------
    //
    // The field for a new name: Return takes it, Escape or a press elsewhere
    // keeps the old one. It is a window of its own above the desktop because
    // the compositor gives the keyboard on request only to the layers in front,
    // and the background layer gets it only from a press.

    readonly property int renamingIndex: entries.findIndex(entry => entry.path === renamingPath)

    PanelWindow {
        screen: icons.screen
        visible: icons.renamingPath !== "" && icons.renamingIndex >= 0
        anchors {
            left: true
            right: true
            top: true
            bottom: true
        }
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: visible ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
        WlrLayershell.namespace: "desktop-rename"
        color: "transparent"

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onPressed: icons.finishRenaming("")
        }

        Loader {
            active: parent.visible
            sourceComponent: Rectangle {
                readonly property var field: icons.looks?.name_field ?? ({})
                readonly property var name: icons.looks?.name ?? ({})
                readonly property var face: icons.fonts.desktop_icon ?? icons.fonts.interface
                x: icons.iconX(icons.renamingIndex) + (icons.view.right ? icons.namePlace.x
                                                                         : icons.iconSize / 2 - width / 2)
                // moved up when a long name's lines would reach past the screen
                y: Math.max(0, Math.min(icons.iconY(icons.renamingIndex) + icons.nameY(height, height, false),
                                        icons.height - height - 4))
                // as wide as the text, up to the name's own width, then it wraps
                width: Math.min(Math.max(fieldMetrics.advanceWidth(input.text) + 10, 60),
                                icons.nameRoom + 2 * (name.padding ?? 6))
                height: Math.max(icons.lineHeight, input.contentHeight + 2)
                FontMetrics {
                    id: fieldMetrics
                    font: input.font
                }
                color: ThemeValues.color(field.color ?? "#ffffff")
                border.width: 1
                border.color: ThemeValues.color(field.border_color ?? "#808080")

                TextInput {
                    id: input
                    anchors.centerIn: parent
                    width: parent.width - 10
                    wrapMode: TextInput.WrapAtWordBoundaryOrAnywhere
                    horizontalAlignment: icons.view.right ? TextInput.AlignLeft : TextInput.AlignHCenter
                    focus: true
                    text: icons.entries[icons.renamingIndex]?.name ?? ""
                    color: ThemeValues.color(parent.field.text_color ?? "#000000")
                    selectionColor: ThemeValues.color(parent.field.selection_color ?? "#b4d5fe")
                    selectedTextColor: color
                    font.family: parent.face.family
                    font.pixelSize: icons.view.textSize
                    onAccepted: icons.finishRenaming(text)
                    Keys.onEscapePressed: icons.finishRenaming("")
                    // The name without its ending is selected, ready to be typed over; again
                    // when the keyboard arrives, since its arrival clears the selection.
                    function markStem() {
                        const dot = icons.entries[icons.renamingIndex]?.isLauncher ? -1 : text.lastIndexOf(".")
                        select(0, dot > 0 ? dot : text.length)
                    }
                    Component.onCompleted: {
                        forceActiveFocus()
                        markStem()
                    }
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            markStem()
                        }
                    }
                }
            }
        }
    }

    // --- The menus ---------------------------------------------------------------------------

    property string menuKind: ""
    property point menuAt: Qt.point(0, 0)

    function openMenu(kind, x, y) {
        menuAt = Qt.point(x, y)
        menuKind = kind
    }

    function openFileMenu(path, isFolder, x, y) {
        menuAt = Qt.point(x, y)
        menuKind = "file"
    }

    // finder's sort by menu: free, the grid, then the orders, and the orders clean up by offers
    readonly property var sortOrders: ["name", "kind", "modified", "created", "last_opened", "added", "size", "tags"]
    readonly property var cleanUpOrders: ["name", "modified", "created", "last_opened", "added", "size", "kind", "tags"]
    readonly property int idNewFolder: 1
    readonly property int idBackground: 2
    readonly property int idOpen: 3
    readonly property int idTrash: 4
    readonly property int idCleanUp: 5
    readonly property int idDuplicate: 6
    readonly property int idRename: 7
    readonly property int idViewOptions: 8
    readonly property int idCleanUpBy: 9
    readonly property int idSortBy: 10
    readonly property int idOpenDesktop: 11
    readonly property int idDesktopInfo: 12
    readonly property int idInfo: 13
    readonly property int idShowOriginal: 14
    readonly property int sortBase: 100
    readonly property int cleanUpBase: 200
    readonly property var arrangements: ["none", "grid"].concat(sortOrders)

    function orderWord(order) {
        return words["order_" + order]
    }

    // every field the menu's box reads has to be there
    function menuEntry(id, label, checked, enabled, children) {
        return { id: id, label: label, separator: false, enabled: enabled ?? true,
                 checkable: checked !== undefined && checked !== null, checked: checked ?? false,
                 shortcut: [], children: children ?? [] }
    }
    readonly property var menuSeparator: ({ id: 0, label: "", separator: true, enabled: false,
                                            checkable: false, checked: false, shortcut: [], children: [] })

    // the view part of the desktop's menu, also given to the menu bar's view menu
    readonly property var viewItems: {
        const items = []
        if (words.clean_up !== undefined) {
            items.push(menuEntry(idCleanUp, words.clean_up, null, canCleanUp))
        }
        const cleanUpChoices = cleanUpOrders.filter(order => orderWord(order) !== undefined)
            .map(order => menuEntry(cleanUpBase + cleanUpOrders.indexOf(order), orderWord(order)))
        if (words.clean_up_by !== undefined && cleanUpChoices.length > 0) {
            items.push(menuEntry(idCleanUpBy, words.clean_up_by, null, canCleanUp, cleanUpChoices))
        }
        const sortChoices = []
        arrangements.forEach((by, number) => {
            const word = by === "none" ? words.arrange_none : by === "grid" ? words.snap_to_grid : orderWord(by)
            if (word === undefined) {
                return
            }
            if (by === "name" && sortChoices.length > 0) {
                sortChoices.push(menuSeparator)
            }
            sortChoices.push(menuEntry(sortBase + number, word, arrangeBy === by))
            if (by === "none") {
                sortChoices.push(menuSeparator)
            }
        })
        if (words.sort_by !== undefined && sortChoices.length > 0) {
            items.push(menuEntry(idSortBy, words.sort_by, null, true, sortChoices))
        }
        const viewOptionsWord = viewOptionsShown ? words.hide_view_options : words.show_view_options
        if (viewOptionsWord !== undefined) {
            items.push(menuEntry(idViewOptions, viewOptionsWord))
        }
        return items
    }

    readonly property var menuItems: {
        if (menuKind === "file") {
            const items = [menuEntry(idOpen, words.open ?? "Open")]
            items.push(menuSeparator)
            if (pickedPaths.length === 1 && (wishes.rename_command ?? []).length > 0) {
                items.push(menuEntry(idRename, words.rename ?? "Rename"))
            }
            items.push(menuEntry(idTrash, words.trash ?? "Move to Trash"))
            if ((wishes.info_command ?? []).length > 0) {
                items.push(menuEntry(idInfo, words.info ?? "Get Info"))
            }
            const more = []
            if ((wishes.duplicate_command ?? []).length > 0) {
                more.push(menuEntry(idDuplicate, words.duplicate ?? "Duplicate"))
            }
            if (pickedPaths.length === 1 && isLauncher(pickedPaths[0]) && (wishes.show_original_command ?? []).length > 0) {
                more.push(menuEntry(idShowOriginal, words.show_original ?? "Show Original"))
            }
            return more.length > 0 ? items.concat([menuSeparator], more) : items
        }
        const items = [menuEntry(idNewFolder, words.new_folder ?? "New Folder")]
        if ((wishes.open_command ?? []).length > 0) {
            items.push(menuEntry(idOpenDesktop, words.open_desktop_folder ?? "Open Desktop Folder"))
        }
        if ((wishes.info_command ?? []).length > 0) {
            items.push(menuSeparator)
            items.push(menuEntry(idDesktopInfo, words.info ?? "Get Info"))
        }
        if ((wishes.background_command ?? []).length > 0) {
            items.push(menuSeparator)
            items.push(menuEntry(idBackground, words.change_background ?? "Change Background…"))
        }
        return viewItems.length > 0 ? items.concat([menuSeparator], viewItems) : items
    }

    function menuChosen(id) {
        menuKind = ""
        if (id === idNewFolder) {
            newFolder()
        } else if (id === idBackground) {
            run(wishes.background_command, [])
        } else if (id === idOpen) {
            openPicked()
        } else if (id === idTrash) {
            trashPicked()
        } else if (id === idDuplicate) {
            duplicatePicked()
        } else if (id === idRename) {
            startRenaming()
        } else if (id === idOpenDesktop) {
            run(wishes.open_command, [folder])
        } else if (id === idDesktopInfo) {
            run(wishes.info_command, [folder])
        } else if (id === idInfo) {
            run(wishes.info_command, pickedPaths)
        } else if (id === idShowOriginal) {
            run(wishes.show_original_command, pickedPaths.slice(0, 1))
        } else {
            viewChosen(id)
        }
    }
    function viewChosen(id) {
        if (id === idCleanUp && canCleanUp) {
            cleanUp()
        } else if (id === idViewOptions) {
            toggleViewOptions()
        } else if (id >= cleanUpBase && canCleanUp) {
            cleanUpBy(cleanUpOrders[id - cleanUpBase])
        } else if (id >= sortBase && id < cleanUpBase) {
            arrange(arrangements[id - sortBase])
        }
    }

    MenuOverlay {
        screen: icons.screen
        settings: icons.settings
        open: icons.menuKind !== ""
        items: icons.menuItems
        menuX: Math.max(0, Math.min(icons.menuAt.x, icons.width - menuWidth))
        menuY: Math.max(0, Math.min(icons.menuAt.y, icons.height - menuHeight))
        onChosen: id => icons.menuChosen(id)
        onDismissed: icons.menuKind = ""
    }
}
