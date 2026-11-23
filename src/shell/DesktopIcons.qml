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
import "DesktopPlaces.js" as DesktopPlaces
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
    readonly property string order: userChoices.value("desktop_files", "order") ?? "name"

    FolderContents {
        id: contents
        path: icons.shown ? icons.folder : ""
        order: icons.order
    }
    readonly property var entries: contents.entries

    // --- Where the icons go ---------------------------------------------------------

    readonly property real topRoom:
        (settings.config.panel?.position ?? "top") === "top" ? (settings.theme.panel?.height ?? 0) : 0
    readonly property real iconSize: looks?.icon_size ?? 48
    readonly property real rowPitch: looks?.row_pitch ?? 96
    readonly property real columnPitch: looks?.column_pitch ?? 96
    // The rows that fit between the panel and the room kept free at the bottom.
    readonly property int rows: Math.max(1, Math.floor(
        (height - topRoom - (looks?.top ?? 8) - (looks?.bottom_room ?? 100)) / rowPitch))

    readonly property var grid: ({
        width: width, top: topRoom + (looks?.top ?? 8), rows: rows, edge: looks?.edge_to_icon_middle ?? 60,
        iconSize: iconSize, columnPitch: columnPitch, rowPitch: rowPitch
    })
    // on the grid even icons placed earlier by hand sit in slots
    readonly property bool snapToGrid: userChoices.value("desktop_files", "snap_to_grid") ?? false
    readonly property bool deselectAfterOpening: userChoices.value("desktop_files", "deselect_after_opening") ?? false

    // how long a drag rests on a folder before it springs open, none without it
    readonly property int springMs: looks?.spring_ms ?? 0

    // icons glide only to places a person asked for, not when the screen changes size
    readonly property int moveMs: looks?.move_ms ?? 0
    property bool gliding: false
    function glideNext() {
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
        const put = putByHand[entry.name]
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
        const oldName = path.substring(path.lastIndexOf("/") + 1)
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
            // The desktop's own icon dropped here is placed; other files are moved in.
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
    readonly property var nameLook: looks?.name ?? ({})
    readonly property var pickedIndices: entries.map((entry, index) => index)
                                                .filter(index => pickedPaths.includes(entries[index].path))
    // the picked icons and the room their names can take, around which the drag picture is cut
    readonly property rect flockBox: {
        const half = Math.max(iconSize, (nameLook.most_width ?? 110) + 2 * (nameLook.padding ?? 6)) / 2
        const below = iconSize + (nameLook.gap ?? 4) + (nameLook.lines ?? 1) * (nameLook.height ?? 18) + 2
        if (pickedIndices.length === 0) {
            return Qt.rect(0, 0, 1, 1)
        }
        const lefts = pickedIndices.map(index => iconX(index) + iconSize / 2 - half)
        const tops = pickedIndices.map(index => iconY(index))
        const left = Math.floor(Math.min(...lefts))
        const top = Math.floor(Math.min(...tops))
        return Qt.rect(left, top, Math.ceil(Math.max(...lefts) + 2 * half) - left, Math.ceil(Math.max(...tops) + below) - top)
    }
    property string flockPicture: ""
    property point flockHold: Qt.point(0, 0)
    property point flockFrom: Qt.point(0, 0)
    property int heldIndex: -1
    // where in the held icon the pointer took it
    property point heldOffset: Qt.point(iconSize / 2, iconSize / 2)

    function takeDragPicture(index, holdX, holdY) {
        heldIndex = index
        heldOffset = Qt.point(holdX - iconX(index), holdY - iconY(index))
        flockFrom = Qt.point(flockBox.x, flockBox.y)
        flockHold = Qt.point(holdX - flockBox.x, holdY - flockBox.y)
        flock.grabToImage(result => { icons.flockPicture = result.url })
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
                model: icons.pickedIndices
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
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: parent.height + (icons.nameLook.gap ?? 4)
                    }
                }
            }
        }
        // the count, at the lower right of the icon the pointer holds
        Item {
            readonly property var look: icons.looks?.drag_badge ?? ({})
            readonly property real size: look.size ?? 18
            visible: icons.pickedIndices.length > 1 && icons.heldIndex >= 0
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
                text: icons.pickedIndices.length
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
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height + (fileItem.name.gap ?? 4)
            }

            // Drag data: the picked files' addresses, and their paths as plain text for
            // programs that take only text. Qt needs an item to move while dragging.
            Item {
                id: carried
                Drag.active: press.drag.active
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
                Drag.imageSource: icons.flockPicture !== "" ? icons.flockPicture : icons.iconSourceOf(fileItem.modelData)
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
                width: Math.max(parent.width, namePlate.width)
                height: namePlate.y + namePlate.height
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                drag.target: carried

                property bool armsRename: false
                onPressed: mouse => {
                    icons.forceActiveFocus()
                    renameSoon.stop()
                    const path = fileItem.modelData.path
                    armsRename = mouse.button === Qt.LeftButton && mouse.modifiers === Qt.NoModifier
                        && fileItem.picked && icons.pickedPaths.length === 1 && icons.frontmost
                        && mouse.y >= namePlate.y
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
                x: icons.iconX(icons.renamingIndex) + icons.iconSize / 2 - width / 2
                // moved up when a long name's lines would reach past the screen
                y: Math.max(0, Math.min(icons.iconY(icons.renamingIndex) + icons.iconSize + (name.gap ?? 4),
                                        icons.height - height - 4))
                // as wide as the text, up to the name's own width, then it wraps
                width: Math.min(Math.max(fieldMetrics.advanceWidth(input.text) + 10, 60),
                                (name.most_width ?? 110) + 2 * (name.padding ?? 6))
                height: Math.max(name.height ?? 18, input.contentHeight + 2)
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
                    horizontalAlignment: TextInput.AlignHCenter
                    focus: true
                    text: icons.entries[icons.renamingIndex]?.name ?? ""
                    color: ThemeValues.color(parent.field.text_color ?? "#000000")
                    selectionColor: ThemeValues.color(parent.field.selection_color ?? "#b4d5fe")
                    selectedTextColor: color
                    font.family: parent.face.family
                    font.pixelSize: parent.face.size
                    onAccepted: icons.finishRenaming(text)
                    Keys.onEscapePressed: icons.finishRenaming("")
                    // The name without its ending is selected, ready to be typed over; again
                    // when the keyboard arrives, since its arrival clears the selection.
                    function markStem() {
                        const dot = text.lastIndexOf(".")
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

    // The orders the desktop's menu offers, as the folder listing names them.
    readonly property var orders: ["name", "kind", "modified", "added"]
    readonly property int idNewFolder: 1
    readonly property int idBackground: 2
    readonly property int idOpen: 3
    readonly property int idTrash: 4
    readonly property int idCleanUp: 5
    readonly property int idDuplicate: 6
    readonly property int idRename: 7
    readonly property int idSnapToGrid: 8
    readonly property int orderBase: 100

    readonly property var menuItems: {
        // Every field the menu's box reads has to be there.
        const separator = { id: 0, label: "", separator: true, enabled: false,
                            checkable: false, checked: false, shortcut: [], children: [] }
        const entry = (id, label, checked) =>
            ({ id: id, label: label, separator: false, enabled: true,
               checkable: checked !== undefined, checked: checked ?? false, shortcut: [], children: [] })
        if (menuKind === "file") {
            const items = [entry(idOpen, words.open ?? "Open")]
            items.push(separator)
            if (pickedPaths.length === 1 && (wishes.rename_command ?? []).length > 0) {
                items.push(entry(idRename, words.rename ?? "Rename"))
            }
            items.push(entry(idTrash, words.trash ?? "Move to Trash"))
            if ((wishes.duplicate_command ?? []).length > 0) {
                items.push(separator)
                items.push(entry(idDuplicate, words.duplicate ?? "Duplicate"))
            }
            return items
        }
        const items = [entry(idNewFolder, words.new_folder ?? "New Folder")]
        if ((wishes.background_command ?? []).length > 0) {
            items.push(separator)
            items.push(entry(idBackground, words.change_background ?? "Change Background…"))
        }
        items.push(separator)
        if (words.clean_up !== undefined) {
            items.push(entry(idCleanUp, words.clean_up))
        }
        if (words.snap_to_grid !== undefined) {
            items.push(entry(idSnapToGrid, words.snap_to_grid, snapToGrid))
            items.push(separator)
        }
        for (let number = 0; number < orders.length; number++) {
            const word = words["sort_by_" + orders[number]]
            if (word !== undefined) {
                items.push(entry(orderBase + number, word, icons.order === orders[number]))
            }
        }
        return items
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
        } else if (id === idCleanUp) {
            cleanUp()
        } else if (id === idSnapToGrid) {
            // the icons stay in their slots when it is turned off again
            if (!snapToGrid) {
                cleanUp()
            }
            userChoices.set("desktop_files", "snap_to_grid", !snapToGrid)
        } else if (id === idDuplicate) {
            duplicatePicked()
        } else if (id === idRename) {
            startRenaming()
        } else if (id >= orderBase) {
            glideNext()
            userChoices.set("desktop_files", "places", ({}))
            userChoices.set("desktop_files", "order", orders[id - orderBase])
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
