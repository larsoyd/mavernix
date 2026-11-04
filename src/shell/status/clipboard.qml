// every copy with a search field on top like the Help menu, option-click pauses it

import QtQuick
import QtQuick.Effects
import Shell.Native
import ".."
import "../ThemeValues.js" as ThemeValues

StatusItem {
    id: clipboardItem

    readonly property var history: panel.shell.clipboardHistory
    readonly property var words: (panel.settings.config.status ?? ({})).clipboard ?? ({})

    iconName: "edit-paste"
    opacity: history.paused && !lit ? 0.45 : 1
    takesOptionPress: true
    onOptionPressed: history.paused = !history.paused

    Connections {
        target: clipboardItem.panel.shell
        function onClipboardWanted() {
            if (clipboardItem.panel.screen === clipboardItem.panel.shell.mainScreen) {
                clipboardItem.panel.toggleMenu(clipboardItem)
            }
        }
    }

    popover: Component {
        FocusScope {
            id: box

            readonly property var looks: clipboardItem.panel.settings.theme.menu
            readonly property var face: clipboardItem.panel.settings.theme.fonts.menu
            readonly property real rowHeight: looks.item_height
            readonly property real pictureRowHeight: Math.round(rowHeight * 2)
            readonly property int mostRows: clipboardItem.words.shown_rows ?? 12
            // the list never runs past the screen, whatever shown_rows says
            readonly property real listRoom: Math.max(4 * rowHeight,
                (clipboardItem.panel.screen?.height ?? 800) * 0.8 - 7 * rowHeight)
            property int marked: -1
            property string contextFor: ""

            implicitWidth: clipboardItem.words.width ?? 360
            implicitHeight: column.height + 2 * looks.padding

            ClipboardSearch {
                id: found
                history: clipboardItem.history
                filter: field.text
            }

            function choose(row, plain) {
                const id = found.idAt(row)
                if (id === "") {
                    return
                }
                clipboardItem.history.choose(id, plain ?? false)
                clipboardItem.panel.closeMenu()
            }

            function mark(row) {
                if (list.count === 0) {
                    return
                }
                marked = Math.max(0, Math.min(list.count - 1, row))
                list.positionViewAtIndex(marked, ListView.Contain)
            }

            function rowsInView() {
                return Math.max(1, Math.floor(list.height / rowHeight) - 1)
            }

            // the row whose whole content shows beside the menu, after a rest on it
            property int previewed: -1
            onMarkedChanged: {
                previewed = -1
                resting.restart()
            }
            Timer {
                id: resting
                interval: preview.restMs
                onTriggered: box.previewed = box.contextFor === "" ? box.marked : -1
            }

            Connections {
                target: clipboardItem
                function onOpening() {
                    field.text = ""
                    box.marked = -1
                    box.contextFor = ""
                    list.positionViewAtBeginning()
                    scroller.flash()
                }
            }

            component RowText: Text {
                font.family: box.face.family
                font.pixelSize: box.face.size
                y: box.looks.text_baseline_row - baselineOffset
                elide: Text.ElideRight
            }

            component Separator: Item {
                width: parent.width
                height: box.looks.separator_height
                Rectangle {
                    y: box.looks.separator_line_row - 1
                    width: parent.width
                    height: 1
                    color: ThemeValues.color(box.looks.separator_color)
                }
            }

            component ActionRow: Item {
                id: action
                property string label
                property bool checked: false
                property bool enabled: true
                signal chosen()
                readonly property bool lit: enabled && hover.containsMouse
                width: parent.width
                height: box.rowHeight
                Fill {
                    anchors.fill: parent
                    visible: action.lit
                    fill: box.looks.highlight
                    themeDirectory: clipboardItem.panel.themeDirectory
                }
                RowText {
                    visible: action.checked
                    text: box.looks.check_mark
                    x: (box.looks.text_inset - implicitWidth) / 2
                    color: ThemeValues.color(action.lit ? box.looks.highlight_text_color : box.looks.text_color)
                }
                RowText {
                    x: box.looks.text_inset
                    width: parent.width - x - box.looks.shortcut_end_inset
                    text: action.label
                    color: ThemeValues.color(action.lit ? box.looks.highlight_text_color
                                                        : (action.enabled ? box.looks.text_color : box.looks.disabled_text_color))
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (action.enabled) {
                            action.chosen()
                        }
                    }
                }
            }

            Column {
                id: column
                y: box.looks.padding
                width: parent.width

                Item {
                    width: parent.width
                    height: Math.max(box.rowHeight, field.height + 6)
                    RowText {
                        id: searchLabel
                        x: box.looks.text_inset
                        anchors.verticalCenter: parent.verticalCenter
                        text: clipboardItem.words.search ?? "Search"
                        color: ThemeValues.color(box.looks.text_color)
                    }
                    SearchField {
                        id: field
                        focus: true
                        looks: clipboardItem.panel.settings.theme.search ?? ({})
                        face: box.face
                        themeDirectory: clipboardItem.panel.themeDirectory
                        x: searchLabel.x + searchLabel.implicitWidth + 8
                        width: parent.width - x - box.looks.shortcut_end_inset
                        anchors.verticalCenter: parent.verticalCenter
                        onTextChanged: {
                            box.marked = text === "" || list.count === 0 ? -1 : 0
                            list.positionViewAtBeginning()
                        }
                        onAccepted: box.choose(Math.max(0, box.marked))
                        onKeyPressed: event => {
                            const control = (event.modifiers & Qt.ControlModifier) !== 0
                            const option = (event.modifiers & Qt.AltModifier) !== 0
                            if (event.key === Qt.Key_Escape) {
                                if (box.contextFor !== "") {
                                    box.contextFor = ""
                                } else if (field.text !== "") {
                                    field.text = ""
                                } else {
                                    clipboardItem.panel.closeMenu()
                                }
                            } else if (event.key === Qt.Key_Down) {
                                box.mark(box.marked + 1)
                            } else if (event.key === Qt.Key_Up) {
                                box.mark(Math.max(0, box.marked - 1))
                            } else if (event.key === Qt.Key_PageDown) {
                                box.mark(box.marked + box.rowsInView())
                            } else if (event.key === Qt.Key_PageUp) {
                                box.mark(box.marked - box.rowsInView())
                            } else if (control && event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {
                                box.choose(event.key - Qt.Key_1)
                            } else if (control && event.key === Qt.Key_P && box.marked >= 0) {
                                clipboardItem.history.togglePin(found.idAt(box.marked))
                            } else if (option && (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete)
                                       && box.marked >= 0) {
                                clipboardItem.history.remove(found.idAt(box.marked))
                                box.marked = Math.min(box.marked, list.count - 1)
                            } else {
                                return
                            }
                            event.accepted = true
                        }
                    }
                }

                Separator {}

                ActionRow {
                    visible: clipboardItem.history.sensitive
                    label: clipboardItem.words.clear_sensitive ?? "Clear Sensitive Item"
                    onChosen: {
                        clipboardItem.history.clearSensitive()
                        clipboardItem.panel.closeMenu()
                    }
                }

                ListView {
                    id: list
                    width: parent.width
                    height: Math.min(contentHeight, box.mostRows * box.rowHeight, box.listRoom)
                    visible: count > 0
                    clip: true
                    interactive: contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds
                    model: found
                    onMovementStarted: box.previewed = -1

                    section.property: "section"
                    section.delegate: Item {
                        required property string section
                        width: list.width
                        height: clipboardItem.history.pinnedCount > 0 ? box.rowHeight : 0
                        RowText {
                            visible: parent.height > 0
                            x: box.looks.text_inset
                            text: parent.section === "pinned" ? (clipboardItem.words.pinned ?? "Pinned") : (clipboardItem.words.recent ?? "Recent")
                            color: ThemeValues.color(box.looks.disabled_text_color)
                        }
                    }

                    delegate: Item {
                        id: row
                        required property int index
                        required property string entryId
                        required property string kind
                        required property string label
                        required property bool pinned
                        required property url thumbnail
                        required property size pictureSize
                        required property int fileCount
                        required property string preview
                        required property string excerpt
                        required property date added

                        readonly property bool marked: box.marked === index
                        readonly property color ink: ThemeValues.color(marked ? box.looks.highlight_text_color
                                                                              : box.looks.text_color)
                        width: list.width
                        height: kind === "image" ? box.pictureRowHeight : box.rowHeight

                        Fill {
                            anchors.fill: parent
                            visible: row.marked
                            fill: box.looks.highlight
                            themeDirectory: clipboardItem.panel.themeDirectory
                        }

                        Image {
                            id: pinMark
                            readonly property bool offered: pinArea.containsMouse
                            visible: row.pinned || offered
                            opacity: row.pinned ? 1 : 0.4
                            source: clipboardItem.picture("pin")
                            width: 10
                            height: 10
                            sourceSize: Qt.size(10 * Screen.devicePixelRatio, 10 * Screen.devicePixelRatio)
                            x: Math.round((box.looks.text_inset - width) / 2)
                            anchors.verticalCenter: parent.verticalCenter
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                brightness: 1.0
                                colorization: 1.0
                                colorizationColor: row.ink
                            }
                        }

                        Image {
                            id: picture
                            visible: row.kind === "image"
                            x: box.looks.text_inset
                            height: row.height - 8
                            width: Math.round(Math.min(height * 2, height * row.pictureSize.width / Math.max(1, row.pictureSize.height)))
                            anchors.verticalCenter: parent.verticalCenter
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                            source: visible ? row.thumbnail : ""
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -1
                                color: "transparent"
                                border.color: Qt.rgba(0, 0, 0, 0.22)
                            }
                        }

                        RowText {
                            x: box.looks.text_inset + (picture.visible ? picture.width + 6 : 0)
                            y: row.kind === "image" ? Math.round((row.height - height) / 2) : box.looks.text_baseline_row - baselineOffset
                            width: shortcut.x - x - box.looks.shortcut_gap / 2
                            color: row.ink
                            text: {
                                if (row.kind === "image") {
                                    return (clipboardItem.words.image ?? "Image %1 × %2").arg(row.pictureSize.width).arg(row.pictureSize.height)
                                }
                                if (row.kind === "files" && row.fileCount > 1) {
                                    return (clipboardItem.words.files ?? "%1 Items").arg(row.fileCount)
                                }
                                return row.label
                            }
                        }

                        RowText {
                            id: shortcut
                            visible: row.index < 9
                            text: visible ? (box.looks.key_symbols?.Control ?? "Ctrl+") + (row.index + 1) : ""
                            x: parent.width - box.looks.shortcut_end_inset - implicitWidth
                            y: row.kind === "image" ? Math.round((row.height - height) / 2) : box.looks.text_baseline_row - baselineOffset
                            color: row.ink
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onEntered: box.marked = row.index
                            onClicked: mouse => {
                                if (mouse.button === Qt.RightButton) {
                                    const at = mapToItem(box, mouse.x, mouse.y)
                                    context.x = Math.min(at.x, box.width - context.width)
                                    context.y = Math.min(at.y, box.height - context.height)
                                    box.contextFor = row.entryId
                                    context.pinned = row.pinned
                                } else {
                                    box.choose(row.index)
                                }
                            }
                        }
                        MouseArea {
                            id: pinArea
                            width: box.looks.text_inset
                            height: parent.height
                            hoverEnabled: true
                            onEntered: box.marked = row.index
                            onClicked: clipboardItem.history.togglePin(row.entryId)
                        }
                    }
                }

                ActionRow {
                    visible: list.count === 0
                    enabled: false
                    label: clipboardItem.history.total === 0 ? (clipboardItem.words.empty ?? "No Copied Items") : (clipboardItem.words.no_matches ?? "No Matches")
                }

                Separator {}

                ActionRow {
                    label: clipboardItem.words.clear_history ?? "Clear History"
                    enabled: clipboardItem.history.total > clipboardItem.history.pinnedCount
                    onChosen: clipboardItem.history.clearHistory()
                }
                ActionRow {
                    label: clipboardItem.words.pause ?? "Pause Clipboard History"
                    checked: clipboardItem.history.paused
                    onChosen: clipboardItem.history.paused = !clipboardItem.history.paused
                }
            }

            // the overlay scroller of the time, shown while the list moves and once on opening
            Rectangle {
                id: scroller
                readonly property real trackTop: column.y + list.y + 2
                readonly property real trackLength: list.height - 4
                visible: list.visible && list.interactive
                x: box.width - width - 2
                y: trackTop + list.visibleArea.yPosition * trackLength
                width: 7
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
                Component.onCompleted: flash()
                Timer {
                    id: flashing
                    interval: 1000
                }
            }

            // the whole of the rested-on row beside the menu, on the side with room
            ClipboardPreview {
                id: preview
                readonly property var row: box.previewed >= 0 && box.contextFor === "" ? list.itemAtIndex(box.previewed) : null
                looks: box.looks
                face: box.face
                words: clipboardItem.words
                file: row === null ? null : { kind: row.kind, excerpt: row.excerpt, picture: row.thumbnail.toString(),
                                              width: row.pictureSize.width, height: row.pictureSize.height, added: row.added }
                // read again for each row, a mapped place does not follow the box
                readonly property real leftRoom: row !== null ? box.mapToItem(null, 0, 0).x : 0
                readonly property real rightRoom: (clipboardItem.panel.screen?.width ?? 1920) - leftRoom - box.width
                mostWidth: Math.min(380, Math.max(leftRoom, rightRoom) - 16)
                x: leftRoom >= width + 8 ? -width - 8 : box.width + 8
                y: row ? Math.max(0, Math.min(row.mapToItem(box, 0, 0).y - inset, box.height - height)) : y
            }

            // a press outside the small menu closes it
            MouseArea {
                anchors.fill: parent
                visible: box.contextFor !== ""
                onPressed: box.contextFor = ""
            }
            MenuBox {
                id: context
                property bool pinned: false
                visible: box.contextFor !== ""
                settings: clipboardItem.panel.settings
                looks: box.looks
                items: [
                    { id: 1, label: clipboardItem.words.copy ?? "Copy", enabled: true, separator: false, checkable: false, checked: false, shortcut: [], children: [] },
                    { id: 2, label: clipboardItem.words.copy_plain ?? "Copy as Plain Text", enabled: true, separator: false, checkable: false, checked: false, shortcut: [], children: [] },
                    { id: 0, label: "", enabled: false, separator: true, checkable: false, checked: false, shortcut: [], children: [] },
                    { id: 3, label: context.pinned ? (clipboardItem.words.unpin ?? "Unpin") : (clipboardItem.words.pin ?? "Pin"), enabled: true, separator: false, checkable: false, checked: false, shortcut: [], children: [] },
                    { id: 4, label: clipboardItem.words.remove ?? "Remove", enabled: true, separator: false, checkable: false, checked: false, shortcut: [], children: [] }
                ]
                onChosen: id => {
                    const entry = box.contextFor
                    box.contextFor = ""
                    if (id === 1 || id === 2) {
                        clipboardItem.history.choose(entry, id === 2)
                        clipboardItem.panel.closeMenu()
                    } else if (id === 3) {
                        clipboardItem.history.togglePin(entry)
                    } else if (id === 4) {
                        clipboardItem.history.remove(entry)
                    }
                }
            }
        }
    }
}
