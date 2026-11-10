// the emoji menu, opened at the pointer, puts the chosen one into the window that had the keyboard

import QtQuick
import QtQuick.Effects
import Quickshell
import Quickshell.Wayland
import Shell.Native
import "ThemeValues.js" as ThemeValues

PanelWindow {
    id: picker

    required property var settings
    // where it opens when the pointer cannot be found
    property var mainScreen: null
    // told before it opens, so open menus close
    signal opening()

    readonly property var looks: settings.theme.menu
    readonly property var face: settings.theme.fonts.menu
    readonly property var wishes: settings.config.emoji ?? ({})
    readonly property string themeDirectory: settings.theme_directory

    readonly property int columns: Math.max(4, Math.min(12, wishes.columns ?? 8))
    readonly property int cell: 36
    readonly property int glyphSize: 24
    readonly property int inset: 8
    readonly property int edgeGap: 8
    readonly property int rowHeight: looks.item_height
    readonly property real panelHeight: settings.theme.panel?.height ?? 0
    // as many rows as asked for, fewer on a screen too low for them
    readonly property int gridRows: Math.max(3, Math.min(wishes.rows ?? 7,
        Math.floor(((screen?.height ?? 720) * 0.8 - box.chromeHeight) / cell)))

    readonly property var groupNames: ({
        recent: wishes.recent ?? "Recently Used",
        found: wishes.found ?? "Search Results",
        smileys: wishes.smileys ?? "Smileys",
        people: wishes.people ?? "People",
        nature: wishes.nature ?? "Nature",
        food: wishes.food ?? "Food & Drink",
        travel: wishes.travel ?? "Places",
        activities: wishes.activities ?? "Activities",
        objects: wishes.objects ?? "Objects",
        symbols: wishes.symbols ?? "Symbols",
        flags: wishes.flags ?? "Flags"
    })
    // each tab's icon name, and an emoji for an icon theme without it
    readonly property var tabs: [
        { group: "recent", icon: "emoji-recent", stand: "🕘" },
        { group: "smileys", icon: "emoji-people", stand: "😀" },
        { group: "people", icon: "emoji-body", stand: "👋" },
        { group: "nature", icon: "emoji-nature", stand: "🐻" },
        { group: "food", icon: "emoji-food", stand: "🍔" },
        { group: "travel", icon: "emoji-travel", stand: "🚗" },
        { group: "activities", icon: "emoji-activities", stand: "⚽" },
        { group: "objects", icon: "emoji-objects", stand: "💡" },
        { group: "symbols", icon: "emoji-symbols", stand: "🔣" },
        { group: "flags", icon: "emoji-flags", stand: "🏁" }
    ]

    property bool open: false
    // the pointer in this screen when it opened, negative for the middle
    property real pointerX: -1
    property real pointerY: -1
    property int marked: -1
    // the emoji whose skin tones are offered, and the marked one of them
    property int tonesFor: -1
    property int toneMarked: 0
    // counted as used once the menu is out of sight, so the grid stays still
    property string lastChosen: ""

    function toggle() {
        if (open) {
            open = false
        } else {
            pointerPlace.ask()
        }
    }

    function showAt(x, y) {
        const screens = Quickshell.screens
        const under = x < 0 ? undefined
            : screens.find(one => x >= one.x && x < one.x + one.width && y >= one.y && y < one.y + one.height)
        const target = under ?? mainScreen
        if (target === null) {
            console.warn("[emoji] open refused reason=no_screen")
            return
        }
        screen = target
        pointerX = under !== undefined ? x - under.x : -1
        pointerY = under !== undefined ? y - under.y : -1
        opening()
        open = true
    }

    function choose(text) {
        if (text === "") {
            return
        }
        lastChosen = text
        open = false
        typing.insert(text)
    }

    function mark(wanted) {
        if (wanted < 0) {
            return
        }
        marked = wanted
        const row = emoji.rowOf(wanted)
        if (row <= 1) {
            list.positionViewAtBeginning()
        } else {
            list.positionViewAtIndex(row, ListView.Contain)
        }
    }

    function offerTones(wanted) {
        const tones = emoji.tonesAt(wanted)
        if (tones.length === 0) {
            return false
        }
        marked = wanted
        toneMarked = Math.max(0, tones.indexOf(emoji.textAt(wanted)))
        tonesFor = wanted
        return true
    }

    function showGroup(group) {
        field.text = ""
        const section = emoji.sections.find(one => one.group === group)
        if (section !== undefined) {
            list.positionViewAtIndex(section.row, ListView.Beginning)
        }
    }

    function label(name) {
        return name.charAt(0).toUpperCase() + name.slice(1)
    }

    EmojiSet {
        id: emoji
        dataFile: picker.wishes.list ?? Quickshell.shellDir + "/../emoji/emoji.tsv"
        columns: picker.columns
        keepRecent: picker.wishes.keep_recent ?? 3 * picker.columns
    }

    TypedText {
        id: typing
        way: picker.wishes.insert ?? "paste"
    }

    PointerPlace {
        id: pointerPlace
        onFound: (x, y) => picker.showAt(x, y)
        onUnknown: picker.showAt(-1, -1)
    }

    IconSource {
        id: themeIcons
    }

    // --- Coming and going -----------------------------------------------------------

    readonly property var closeMotion: (settings.theme.animation ?? ({})).menu_close ?? ({ kind: "none" })

    visible: (open || goingAway.running) && keepShown.canShow
    KeepShown {
        id: keepShown
        target: picker
    }
    onVisibleChanged: {
        if (!visible) {
            countChosen()
        }
    }
    function countChosen() {
        if (lastChosen !== "") {
            emoji.used(lastChosen)
            lastChosen = ""
        }
    }

    onOpenChanged: {
        goingAway.stop()
        tonesFor = -1
        if (open) {
            countChosen()
            field.text = ""
            marked = -1
            list.positionViewAtBeginning()
            scroller.flash()
            shown.opacity = 1
            field.input.forceActiveFocus()
        } else if (closeMotion.kind === "fade") {
            goingAway.start()
        }
    }

    NumberAnimation {
        id: goingAway
        target: shown
        property: "opacity"
        to: 0
        duration: picker.closeMotion.duration_ms ?? 150
        easing.type: ThemeValues.easing(picker.closeMotion.curve ?? "linear")
    }

    anchors {
        left: true
        right: true
        top: true
        bottom: true
    }
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "mavernix-emoji"
    WlrLayershell.keyboardFocus: open ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    color: "transparent"

    // nothing while it fades, so a press goes straight through
    mask: Region {
        item: picker.open ? everything : null
    }

    MouseArea {
        id: everything
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: picker.open = false
    }

    Item {
        id: shown
        anchors.fill: parent

        FocusScope {
            id: box

            readonly property int gridWidth: picker.columns * picker.cell
            readonly property int chromeHeight: searchRow.height + nameRow.height + tabRow.height
                + 2 * picker.looks.separator_height + 2 * picker.looks.padding

            width: gridWidth + 2 * picker.inset
            height: column.height + 2 * picker.looks.padding

            // below and right of the pointer like a menu, on the other side where that runs out
            readonly property real leftMost: picker.edgeGap
            readonly property real rightMost: picker.width - width - picker.edgeGap
            readonly property real topMost: picker.panelHeight + picker.edgeGap
            readonly property real bottomMost: picker.height - height - picker.edgeGap
            x: {
                if (picker.pointerX < 0) {
                    return Math.round((picker.width - width) / 2)
                }
                const right = picker.pointerX + picker.edgeGap
                const wanted = right <= rightMost ? right : picker.pointerX - picker.edgeGap - width
                return Math.round(Math.max(leftMost, Math.min(rightMost, wanted)))
            }
            y: {
                if (picker.pointerY < 0) {
                    return Math.round(Math.max(topMost, (picker.height - height) / 2))
                }
                const below = picker.pointerY + picker.edgeGap
                const wanted = below <= bottomMost ? below : picker.pointerY - picker.edgeGap - height
                return Math.round(Math.max(topMost, Math.min(bottomMost, wanted)))
            }

            BoxFrame {
                looks: picker.looks
            }
            // a press on the box outside its controls must not close it
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
                onPressed: picker.tonesFor = -1
            }

            component Separator: Item {
                width: parent.width
                height: picker.looks.separator_height
                Rectangle {
                    y: picker.looks.separator_line_row - 1
                    width: parent.width
                    height: 1
                    color: ThemeValues.color(picker.looks.separator_color)
                }
            }

            component Glyph: Text {
                width: picker.cell
                height: picker.cell
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: picker.face.family
                font.pixelSize: picker.glyphSize
            }

            Column {
                id: column
                y: picker.looks.padding
                width: parent.width

                Item {
                    id: searchRow
                    width: parent.width
                    height: field.height + 2 * picker.inset
                    SearchField {
                        id: field
                        focus: true
                        looks: picker.settings.theme.search ?? ({})
                        face: picker.face
                        themeDirectory: picker.themeDirectory
                        placeholder: picker.wishes.search ?? "Search"
                        x: picker.inset
                        width: parent.width - 2 * picker.inset
                        anchors.verticalCenter: parent.verticalCenter
                        onTextChanged: {
                            // set here and not bound, so the count below is the new one
                            emoji.filter = text
                            picker.tonesFor = -1
                            picker.marked = text === "" || emoji.count === 0 ? -1 : 0
                            list.positionViewAtBeginning()
                        }
                        onAccepted: picker.choose(emoji.textAt(Math.max(0, picker.marked)))
                        onKeyPressed: event => {
                            const option = (event.modifiers & Qt.AltModifier) !== 0
                            const rowsInView = Math.max(1, picker.gridRows - 1)
                            if (picker.tonesFor >= 0) {
                                if (event.key === Qt.Key_Left) {
                                    picker.toneMarked = Math.max(0, picker.toneMarked - 1)
                                } else if (event.key === Qt.Key_Right) {
                                    picker.toneMarked = Math.min(tones.choices.length - 1, picker.toneMarked + 1)
                                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                    picker.choose(tones.choices[picker.toneMarked] ?? "")
                                } else {
                                    picker.tonesFor = -1
                                    // escape only puts the tones away, any other key goes on to the grid
                                    if (event.key !== Qt.Key_Escape) {
                                        return
                                    }
                                }
                            } else if (event.key === Qt.Key_Escape) {
                                if (field.text !== "") {
                                    field.text = ""
                                } else {
                                    picker.open = false
                                }
                            } else if (option && event.key === Qt.Key_Down) {
                                picker.offerTones(Math.max(0, picker.marked))
                            } else if (event.key === Qt.Key_Down) {
                                picker.mark(emoji.moved(picker.marked, 0, 1))
                            } else if (event.key === Qt.Key_Up) {
                                picker.mark(emoji.moved(picker.marked, 0, -1))
                            } else if (event.key === Qt.Key_Right && picker.marked >= 0) {
                                picker.mark(emoji.moved(picker.marked, 1, 0))
                            } else if (event.key === Qt.Key_Left && picker.marked >= 0) {
                                picker.mark(emoji.moved(picker.marked, -1, 0))
                            } else if (event.key === Qt.Key_PageDown) {
                                picker.mark(emoji.moved(picker.marked, 0, rowsInView))
                            } else if (event.key === Qt.Key_PageUp) {
                                picker.mark(emoji.moved(picker.marked, 0, -rowsInView))
                            } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                                tabRow.step(event.key === Qt.Key_Tab ? 1 : -1)
                            } else {
                                return
                            }
                            event.accepted = true
                        }
                    }
                }

                Item {
                    id: gridArea
                    width: parent.width
                    height: picker.gridRows * picker.cell

                    ListView {
                        id: list
                        x: picker.inset
                        width: box.gridWidth
                        height: parent.height
                        clip: true
                        interactive: contentHeight > height
                        boundsBehavior: Flickable.StopAtBounds
                        model: emoji.rows
                        onContentYChanged: picker.tonesFor = -1

                        delegate: Item {
                            id: row
                            required property var modelData
                            readonly property bool heading: modelData.group !== undefined
                            width: list.width
                            height: heading ? picker.rowHeight : picker.cell

                            Text {
                                visible: row.heading
                                x: Math.round((picker.cell - picker.glyphSize) / 2)
                                width: parent.width - 2 * x
                                y: picker.looks.text_baseline_row - baselineOffset
                                elide: Text.ElideRight
                                font.family: picker.face.family
                                font.pixelSize: picker.face.size
                                color: ThemeValues.color(picker.looks.disabled_text_color)
                                text: row.heading ? (picker.groupNames[row.modelData.group] ?? row.modelData.group) : ""
                            }

                            Repeater {
                                model: row.heading ? [] : row.modelData.cells
                                Item {
                                    id: place
                                    required property var modelData
                                    required property int index
                                    readonly property bool lit: picker.marked === row.modelData.first + index
                                    x: index * picker.cell
                                    width: picker.cell
                                    height: picker.cell

                                    Fill {
                                        anchors.fill: parent
                                        visible: place.lit
                                        fill: picker.looks.highlight
                                        themeDirectory: picker.themeDirectory
                                    }
                                    Glyph {
                                        text: place.modelData.text
                                    }
                                    // a corner mark on those with skin tones
                                    Canvas {
                                        visible: place.modelData.toned
                                        readonly property color ink: ThemeValues.color(
                                            place.lit ? picker.looks.highlight_text_color : picker.looks.disabled_text_color)
                                        onInkChanged: requestPaint()
                                        x: parent.width - width - 2
                                        y: parent.height - height - 2
                                        width: 5
                                        height: 5
                                        onPaint: {
                                            const context = getContext("2d")
                                            context.reset()
                                            context.fillStyle = ink
                                            context.beginPath()
                                            context.moveTo(width, 0)
                                            context.lineTo(width, height)
                                            context.lineTo(0, height)
                                            context.closePath()
                                            context.fill()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // over the list and not in it, so rows moving under a resting pointer mark nothing
                    MouseArea {
                        id: gridMouse
                        anchors.fill: list
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        property real lastX: -1
                        property real lastY: -1
                        property bool held: false

                        function emojiAt(x, y) {
                            const rowIndex = list.indexAt(x, y + list.contentY)
                            const data = rowIndex >= 0 ? emoji.rows[rowIndex] : undefined
                            const across = Math.floor(x / picker.cell)
                            if (data === undefined || data.cells === undefined || across < 0 || across >= data.cells.length) {
                                return -1
                            }
                            return data.first + across
                        }
                        onPositionChanged: mouse => {
                            if (mouse.x === lastX && mouse.y === lastY) {
                                return
                            }
                            lastX = mouse.x
                            lastY = mouse.y
                            const over = emojiAt(mouse.x, mouse.y)
                            if (over >= 0 && picker.tonesFor < 0) {
                                picker.marked = over
                            }
                        }
                        onPressed: {
                            held = false
                            picker.tonesFor = -1
                        }
                        onPressAndHold: mouse => held = picker.offerTones(emojiAt(mouse.x, mouse.y))
                        onClicked: mouse => {
                            const over = emojiAt(mouse.x, mouse.y)
                            if (held || over < 0) {
                                return
                            }
                            if (mouse.button === Qt.RightButton) {
                                picker.offerTones(over)
                            } else {
                                picker.choose(emoji.textAt(over))
                            }
                        }
                        onWheel: wheel => wheel.accepted = false
                    }

                    Text {
                        visible: emoji.count === 0
                        anchors.centerIn: parent
                        width: parent.width - 2 * picker.looks.text_inset
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        font.family: picker.face.family
                        font.pixelSize: picker.face.size
                        color: ThemeValues.color(picker.looks.disabled_text_color)
                        text: picker.wishes.none ?? "No Emoji Found"
                    }
                }

                Separator {}

                Item {
                    id: nameRow
                    width: parent.width
                    height: picker.rowHeight
                    Text {
                        x: picker.looks.text_inset
                        width: parent.width - 2 * x
                        y: picker.looks.text_baseline_row - baselineOffset
                        elide: Text.ElideRight
                        font.family: picker.face.family
                        font.pixelSize: picker.face.size
                        // the marked emoji, else where the grid stands, in grey
                        readonly property string markedName: {
                            emoji.count
                            return picker.label(emoji.nameAt(picker.marked))
                        }
                        readonly property string group: tabRow.hovered !== "" ? tabRow.hovered : tabRow.current
                        color: ThemeValues.color(markedName !== "" || tabRow.hovered !== "" ? picker.looks.text_color
                                                                                             : picker.looks.disabled_text_color)
                        text: tabRow.hovered === "" && markedName !== "" ? markedName : (picker.groupNames[group] ?? "")
                    }
                }

                Separator {}

                Item {
                    id: tabRow
                    width: parent.width
                    height: picker.cell - 8

                    // the group at the top of the grid
                    readonly property string current: {
                        // nothing is found there until the rows are made
                        const top = Math.max(0, list.indexAt(0, list.contentY + 1))
                        let found = ""
                        for (const section of emoji.sections) {
                            if (section.row <= top) {
                                found = section.group
                            }
                        }
                        return found
                    }
                    property string hovered: ""
                    readonly property var present: emoji.sections.map(one => one.group)

                    function step(direction) {
                        const groups = picker.tabs.map(tab => tab.group).filter(group => present.includes(group))
                        if (groups.length === 0) {
                            return
                        }
                        const now = groups.indexOf(current)
                        picker.showGroup(groups[(now + direction + groups.length) % groups.length])
                    }

                    Row {
                        x: picker.inset
                        Repeater {
                            model: picker.tabs
                            Item {
                                id: tab
                                required property var modelData
                                readonly property bool chosen: tabRow.current === modelData.group
                                // while searching every group can still be gone to
                                readonly property bool usable: field.text !== "" ? modelData.group !== "recent"
                                                                                   : tabRow.present.includes(modelData.group)
                                readonly property string picture: themeIcons.find(modelData.icon + "-symbolic")
                                readonly property color ink: ThemeValues.color(
                                    chosen || tabArea.containsMouse ? picker.looks.text_color : picker.looks.disabled_text_color)
                                width: box.gridWidth / picker.tabs.length
                                height: tabRow.height
                                opacity: usable ? 1 : 0.4

                                Image {
                                    visible: tab.picture !== ""
                                    anchors.centerIn: parent
                                    width: 16
                                    height: 16
                                    sourceSize: Qt.size(16, 16)
                                    source: tab.picture
                                    layer.enabled: true
                                    layer.effect: MultiEffect {
                                        brightness: 1.0
                                        colorization: 1.0
                                        colorizationColor: tab.ink
                                    }
                                }
                                Text {
                                    visible: tab.picture === ""
                                    anchors.centerIn: parent
                                    font.pixelSize: 15
                                    opacity: tab.chosen || tabArea.containsMouse ? 1 : 0.55
                                    text: tab.modelData.stand
                                }
                                MouseArea {
                                    id: tabArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onContainsMouseChanged: {
                                        if (containsMouse) {
                                            tabRow.hovered = tab.modelData.group
                                        } else if (tabRow.hovered === tab.modelData.group) {
                                            tabRow.hovered = ""
                                        }
                                    }
                                    onClicked: {
                                        if (tab.usable) {
                                            picker.showGroup(tab.modelData.group)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // the overlay scroller of the time, shown while the grid moves and once on opening
            Rectangle {
                id: scroller
                readonly property real trackTop: column.y + gridArea.y + 2
                readonly property real trackLength: list.height - 4
                visible: list.interactive
                x: box.width - width - 1
                y: trackTop + list.visibleArea.yPosition * trackLength
                width: 6
                height: Math.max(18, list.visibleArea.heightRatio * trackLength)
                radius: width / 2
                color: Qt.rgba(0, 0, 0, 0.45)
                border.color: Qt.rgba(1, 1, 1, 0.35)
                opacity: flashing.running ? 1 : 0
                Behavior on opacity {
                    NumberAnimation { duration: scroller.opacity > 0 ? 300 : 120 }
                }
                function flash() {
                    flashing.restart()
                }
                Connections {
                    target: list
                    function onContentYChanged() {
                        scroller.flash()
                    }
                }
                Timer {
                    id: flashing
                    interval: 1000
                }
            }

            // the marked emoji in every skin tone, over its cell or under it near the top
            Item {
                id: tones
                visible: picker.tonesFor >= 0
                readonly property var choices: visible ? emoji.tonesAt(picker.tonesFor) : []
                readonly property var rowItem: visible ? list.itemAtIndex(emoji.rowOf(picker.tonesFor)) : null
                readonly property point cellPlace: rowItem !== null
                    ? rowItem.mapToItem(box, (picker.tonesFor - rowItem.modelData.first) * picker.cell, 0) : Qt.point(0, 0)
                width: choices.length * picker.cell + 2 * picker.looks.padding
                height: picker.cell + 2 * picker.looks.padding
                x: Math.round(Math.max(picker.inset, Math.min(box.width - width - picker.inset,
                                                           cellPlace.x + picker.cell / 2 - width / 2)))
                y: Math.round(cellPlace.y - height - 2 >= column.y + gridArea.y ? cellPlace.y - height - 2
                                                                               : cellPlace.y + picker.cell + 2)

                BoxFrame {
                    looks: picker.looks
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                }
                Row {
                    x: picker.looks.padding
                    y: picker.looks.padding
                    Repeater {
                        model: tones.choices
                        Item {
                            id: tone
                            required property string modelData
                            required property int index
                            width: picker.cell
                            height: picker.cell
                            Fill {
                                anchors.fill: parent
                                visible: picker.toneMarked === tone.index
                                fill: picker.looks.highlight
                                themeDirectory: picker.themeDirectory
                            }
                            Glyph {
                                text: tone.modelData
                            }
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onPositionChanged: picker.toneMarked = tone.index
                                onClicked: picker.choose(tone.modelData)
                            }
                        }
                    }
                }
            }
        }
    }
}
