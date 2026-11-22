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
            icons.glideNext()
            icons.rememberPlaces(icons.placesByName(drop.names,
                snap ? DesktopPlaces.onGrid(icons.grid, drop.kept, drop.dropped) : loose))
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

    function run(command, more) {
        if ((command ?? []).length > 0) {
            Quickshell.execDetached(command.concat(more))
        }
    }
    // An executable .desktop file is started, with any files given to it.
    function isLauncher(path) {
        return entries.some(entry => entry.path === path && entry.isLauncher)
    }
    function launch(launcher, files) {
        Quickshell.execDetached(["gio", "launch", launcher].concat(files ?? []))
    }
    function openPicked() {
        const launchers = pickedPaths.filter(path => isLauncher(path))
        const files = pickedPaths.filter(path => !isLauncher(path))
        for (const launcher of launchers) {
            launch(launcher)
            leaveGhost(entries.findIndex(entry => entry.path === launcher))
        }
        // one at a time from its icon, so its window can grow out of it
        const fromIcon = wishes.open_from_icon_command ?? []
        if (fromIcon.length > 0) {
            for (const file of files) {
                run(fromIcon, [file, file])
            }
        } else if (files.length > 0) {
            run(wishes.open_command, files)
        }
        afterOpening()
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
                icons.dropPicked(icons.carriedPath, drop.x, drop.y)
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
            readonly property var plate: (icons.frontmost ? undefined : name.away_background)
                                         ?? name.picked_background ?? ({ color: "#3a6ea5", radius: 4 })

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
            // A picture is shown as itself, small, in the theme's frame; everything
            // else by the icon for its kind.
            readonly property bool isPicture: modelData.icon.startsWith("image-")
            readonly property string iconSource: themeIcons.path(modelData.icon, modelData.genericIcon)
            FramedPicture {
                id: preview
                anchors.fill: parent
                visible: fileItem.isPicture && !failed
                source: fileItem.isPicture ? "file://" + encodeURI(fileItem.modelData.path) : ""
                frame: icons.looks?.preview_frame ?? null
            }
            Image {
                id: picture
                anchors.fill: parent
                // Also for a picture in a format that cannot be read.
                visible: !preview.visible
                sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                source: fileItem.iconSource
            }

            Item {
                id: namePlate
                width: Math.min(label.implicitWidth, fileItem.name.most_width ?? 110) + 2 * (fileItem.name.padding ?? 6)
                height: fileItem.name.height ?? 18
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height + (fileItem.name.gap ?? 4)

                // the same plate a little lower shows as a dark edge under it
                Fill {
                    readonly property var edge: fileItem.name.picked_edge
                    visible: fileItem.picked && edge !== undefined
                    width: parent.width
                    height: parent.height
                    y: edge?.drop ?? 0
                    fill: ({ color: edge?.color ?? "#00000000", radius: fileItem.plate.radius })
                    themeDirectory: icons.themeDirectory
                }
                Fill {
                    visible: fileItem.picked
                    anchors.fill: parent
                    fill: fileItem.plate
                    themeDirectory: icons.themeDirectory
                }
                Item {
                    id: letters
                    readonly property real room: Math.ceil(fileItem.name.shadow_blur ?? 0) + (fileItem.name.shadow_drop ?? 1)
                    anchors.centerIn: parent
                    // whole pixels, or the layer stretches the letters
                    width: Math.ceil(label.width) + 2 * room
                    height: Math.ceil(label.implicitHeight) + 2 * room
                    layer.enabled: true
                    // a picked name loses its shadow
                    layer.effect: CastShadow {
                        look: fileItem.picked ? null : fileItem.name
                        ink: ThemeValues.color(fileItem.picked && !icons.frontmost
                                               ? fileItem.name.away_color ?? fileItem.name.color ?? "#ffffff"
                                               : fileItem.name.color ?? "#ffffff")
                    }

                    Text {
                        id: label
                        visible: icons.renamingPath !== fileItem.modelData.path
                        x: letters.room
                        y: letters.room
                        width: Math.min(implicitWidth, fileItem.name.most_width ?? 110)
                        elide: Text.ElideMiddle
                        text: fileItem.modelData.name
                        color: "white"
                        readonly property var face: icons.fonts.desktop_icon ?? icons.fonts.interface
                        font.family: face.family
                        font.pixelSize: face.size
                        font.bold: face.bold ?? false
                    }
                }
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
                // What follows the pointer: a snapshot of the icon taken on press, else the
                // icon for the file's kind.
                Drag.imageSource: fileItem.dragPicture !== "" ? fileItem.dragPicture : fileItem.iconSource
                Drag.imageSourceSize: Qt.size(icons.iconSize, icons.iconSize)
                Drag.hotSpot: fileItem.dragPicture !== "" ? fileItem.dragHold
                                                          : Qt.point(icons.iconSize / 2, icons.iconSize / 2)
                Drag.onDragFinished: {
                    x = 0
                    y = 0
                    icons.carriedPath = ""
                }
            }

            property string dragPicture: ""
            property point dragHold: Qt.point(0, 0)
            function takeDragPicture(holdX, holdY) {
                dragHold = Qt.point(Math.max(0, Math.min(width, holdX)), Math.max(0, Math.min(height, holdY)))
                fileItem.grabToImage(result => { fileItem.dragPicture = result.url })
            }

            // Files dropped on a program launcher are given to its program; files
            // dropped on a folder are moved into it.
            DropArea {
                id: fileDrop
                enabled: fileItem.modelData.isLauncher || fileItem.modelData.isFolder
                anchors.fill: parent
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
                visible: fileDrop.containsDrag
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

                onPressed: mouse => {
                    icons.forceActiveFocus()
                    const path = fileItem.modelData.path
                    if (mouse.button === Qt.LeftButton) {
                        const held = mapToItem(fileItem, mouse.x, mouse.y)
                        fileItem.takeDragPicture(held.x, held.y)
                    }
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
                }
                // A press on one of several picked files keeps them all picked so they can
                // be dragged together; released without a drag, it picks that file alone.
                onClicked: mouse => {
                    if (mouse.button === Qt.LeftButton
                        && (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) === 0) {
                        icons.pickOnly(fileItem.modelData.path)
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
                y: icons.iconY(icons.renamingIndex) + icons.iconSize + (name.gap ?? 4)
                width: Math.max(input.implicitWidth + 10, 60)
                height: name.height ?? 18
                color: ThemeValues.color(field.color ?? "#ffffff")
                border.width: 1
                border.color: ThemeValues.color(field.border_color ?? "#808080")

                TextInput {
                    id: input
                    anchors.centerIn: parent
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
