// finder's view options panel for the desktop, shown while no window is in front
import QtQuick
import QtQuick.Effects
import Quickshell
import Quickshell.Wayland
import "DesktopView.js" as DesktopView
import "ThemeValues.js" as ThemeValues

PanelWindow {
    id: panel

    required property var desktop
    readonly property var settings: desktop.settings
    readonly property var look: (settings.theme.desktop ?? ({})).view_options ?? null
    readonly property var words: desktop.words
    readonly property var font: settings.theme.fonts.view_options ?? settings.theme.fonts.interface
    readonly property string themeDirectory: settings.theme_directory

    // room around the body for its shadow
    readonly property real halo: 24
    readonly property real bodyWidth: look?.width ?? 201
    readonly property real titleHeight: look?.title_height ?? 16
    readonly property real bodyHeight: titleHeight + 250

    screen: desktop.screen
    visible: look !== null && desktop.viewOptionsShown && ToplevelManager.activeToplevel === null
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Top
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand
    WlrLayershell.namespace: "mavernix-view-options"
    anchors {
        left: true
        top: true
    }
    implicitWidth: bodyWidth + 2 * halo
    implicitHeight: bodyHeight + 2 * halo
    mask: Region {
        item: body
    }

    // the body's top left on the screen, where it was last put, kept inside the screen under the panel
    readonly property real screenWidth: screen?.width ?? 1280
    readonly property real screenHeight: screen?.height ?? 720
    readonly property var stored: desktop.chosen("view_options_place") ?? null
    property point dragged: Qt.point(-1, -1)
    readonly property point place: {
        const wanted = dragged.x >= 0 ? dragged
            : stored !== null ? Qt.point(screenWidth - stored[0], stored[1])
            : Qt.point(Math.round(screenWidth / 2 - bodyWidth / 2), Math.round(screenHeight / 4))
        return Qt.point(Math.round(Math.max(0, Math.min(screenWidth - bodyWidth, wanted.x))),
                        Math.round(Math.max(desktop.topRoom, Math.min(screenHeight - bodyHeight, wanted.y))))
    }
    margins.left: place.x - halo
    margins.top: place.y - halo

    function close() {
        desktop.viewOptionsShown = false
    }
    onVisibleChanged: {
        if (visible) {
            body.forceActiveFocus()
        }
    }

    RectangularShadow {
        anchors.fill: body
        visible: panel.look?.shadow !== undefined
        offset.x: panel.look?.shadow?.offset?.[0] ?? 0
        offset.y: panel.look?.shadow?.offset?.[1] ?? 0
        blur: panel.look?.shadow?.blur ?? 0
        color: ThemeValues.color(panel.look?.shadow?.color ?? "#00000000")
    }

    FocusScope {
        id: body
        x: panel.halo
        y: panel.halo
        width: panel.bodyWidth
        height: panel.bodyHeight
        focus: true

        Keys.onEscapePressed: panel.close()
        Keys.onPressed: event => {
            if (event.key === Qt.Key_J && (event.modifiers & Qt.MetaModifier) !== 0) {
                panel.close()
                event.accepted = true
            }
        }

        Rectangle {
            anchors.fill: parent
            color: ThemeValues.color(panel.look?.fill ?? "#ededed")
        }
        MouseArea {
            anchors.fill: parent
            onPressed: body.forceActiveFocus()
        }

        // the title bar: the panel's name, a close button, and a handle to move it by
        Item {
            id: titleBar
            width: parent.width
            height: panel.titleHeight
            Fill {
                anchors.fill: parent
                fill: panel.look?.title_fill ?? ({ color: "#e4e4e4" })
                themeDirectory: panel.themeDirectory
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: ThemeValues.color(panel.look?.title_line ?? "#a7a7a7")
            }
            Text {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -1
                text: panel.words.view_options_title ?? "Desktop"
                color: ThemeValues.color(panel.look?.title_color ?? "#808080")
                font.family: panel.font.family
                font.pixelSize: panel.font.size
            }
            MouseArea {
                anchors.fill: parent
                property point grip: Qt.point(0, 0)
                onPressed: mouse => {
                    body.forceActiveFocus()
                    grip = Qt.point(mouse.x, mouse.y)
                }
                onPositionChanged: mouse => {
                    panel.dragged = Qt.point(panel.place.x + mouse.x - grip.x, panel.place.y + mouse.y - grip.y)
                }
                onReleased: {
                    panel.desktop.userChoices.set("desktop_files", "view_options_place",
                                                [Math.round(panel.screenWidth - panel.place.x), Math.round(panel.place.y)])
                    panel.dragged = Qt.point(-1, -1)
                }
            }
            Item {
                x: 5
                y: 1
                width: 14
                height: 14
                ThemePicture {
                    id: closeArt
                    themeDirectory: panel.themeDirectory
                    path: panel.look?.close?.[closeArea.pressed ? "pressed" : closeArea.containsMouse ? "hover" : "normal"] ?? ""
                }
                Rectangle {
                    visible: !closeArt.ready
                    anchors.centerIn: parent
                    width: 11
                    height: 11
                    radius: 5.5
                    color: closeArea.containsMouse ? "#e0443e" : "transparent"
                    border.color: ThemeValues.color(panel.look?.title_color ?? "#808080")
                }
                MouseArea {
                    id: closeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: panel.close()
                }
            }
        }

        component Label: Text {
            height: 14
            verticalAlignment: Text.AlignVCenter
            color: ThemeValues.color(panel.look?.text_color ?? "#000000")
            font.family: panel.font.family
            font.pixelSize: panel.font.size
        }
        component Separator: Rectangle {
            x: 14
            width: 176
            height: 1
            color: ThemeValues.color(panel.look?.separator ?? "#898989")
        }
        component EndPicture: Item {
            property string path: ""
            property string icon: ""
            ThemePicture {
                id: endArt
                anchors.centerIn: parent
                sourceSize: Qt.size(parent.width * Screen.devicePixelRatio, parent.height * Screen.devicePixelRatio)
                width: parent.width
                height: parent.height
                smooth: true
                themeDirectory: panel.themeDirectory
                path: parent.path
            }
            Image {
                visible: !endArt.ready && parent.icon !== ""
                anchors.fill: parent
                sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                source: parent.icon
            }
        }

        readonly property real below: panel.titleHeight - 16
        readonly property string appIcon: panel.desktop.themeIconPath("application-x-desktop")

        Label {
            x: 18
            y: body.below + 28
            text: panel.words.icon_size ?? "Icon size:"
        }
        Label {
            x: 75
            y: body.below + 28
            text: panel.desktop.view.iconSize + " × " + panel.desktop.view.iconSize
        }
        EndPicture {
            x: 12
            y: body.below + 42
            width: 20
            height: 20
            icon: body.appIcon
        }
        SmallSlider {
            id: iconSizeSlider
            objectName: "iconSize"
            x: 38
            y: body.below + 47
            look: panel.look ?? ({})
            themeDirectory: panel.themeDirectory
            from: 16
            to: 128
            ticks: 9
            step: 4
            value: panel.desktop.view.iconSize
            onMoved: value => panel.desktop.chooseView("icon_size", DesktopView.iconSize(value))
        }
        EndPicture {
            x: 170
            y: body.below + 38
            width: 26
            height: 26
            icon: body.appIcon
        }

        Label {
            x: 18
            y: body.below + 71
            text: panel.words.grid_spacing ?? "Grid spacing:"
        }
        EndPicture {
            x: 16
            y: body.below + 89
            width: 16
            height: 16
            path: panel.look?.grid_small ?? ""
        }
        SmallSlider {
            objectName: "gridSpacing"
            x: 38
            y: body.below + 90
            look: panel.look ?? ({})
            themeDirectory: panel.themeDirectory
            from: 1
            to: 100
            ticks: 8
            step: 1
            value: panel.desktop.view.spacing
            onMoved: value => panel.desktop.chooseView("grid_spacing", Math.round(value))
        }
        EndPicture {
            x: 170
            y: body.below + 83
            width: 25
            height: 25
            path: panel.look?.grid_large ?? ""
        }

        Separator {
            y: body.below + 115
        }

        Label {
            x: 18
            y: body.below + 127
            text: panel.words.text_size ?? "Text size:"
        }
        SmallPopUpButton {
            objectName: "textSize"
            x: 77
            y: body.below + 124
            width: 70
            height: 22
            look: panel.look ?? ({})
            themeDirectory: panel.themeDirectory
            font: panel.font
            settings: panel.settings
            screen: panel.screen
            windowPlace: Qt.point(panel.place.x - panel.halo, panel.place.y - panel.halo)
            choices: [10, 11, 12, 13, 14, 15, 16].map(size => ({ value: size, label: String(size) }))
            value: panel.desktop.view.textSize
            onChosen: value => panel.desktop.chooseView("text_size", value)
        }

        Label {
            x: 18
            y: body.below + 149
            text: panel.words.label_position ?? "Label position:"
        }
        Row {
            x: 22
            y: body.below + 166
            spacing: 4
            Repeater {
                model: [["bottom", panel.words.label_bottom ?? "Bottom"], ["right", panel.words.label_right ?? "Right"]]
                SmallCheckBox {
                    required property var modelData
                    objectName: "label_" + modelData[0]
                    width: 64
                    round: true
                    look: panel.look ?? ({})
                    themeDirectory: panel.themeDirectory
                    font: panel.font
                    text: modelData[1]
                    checked: panel.desktop.view.right === (modelData[0] === "right")
                    onToggled: panel.desktop.chooseView("label_position", modelData[0])
                }
            }
        }

        Separator {
            y: body.below + 191
        }

        SmallCheckBox {
            objectName: "itemInfo"
            x: 19
            y: body.below + 197
            look: panel.look ?? ({})
            themeDirectory: panel.themeDirectory
            font: panel.font
            text: panel.words.show_item_info ?? "Show item info"
            checked: panel.desktop.view.info
            onToggled: checked => panel.desktop.chooseView("show_item_info", checked)
        }
        SmallCheckBox {
            objectName: "iconPreview"
            x: 19
            y: body.below + 214
            look: panel.look ?? ({})
            themeDirectory: panel.themeDirectory
            font: panel.font
            text: panel.words.show_icon_preview ?? "Show icon preview"
            checked: panel.desktop.view.preview
            onToggled: checked => panel.desktop.chooseView("show_icon_preview", checked)
        }

        Label {
            x: 19
            y: body.below + 235
            text: panel.words.sort_by_label ?? "Sort by:"
        }
        SmallPopUpButton {
            objectName: "sortBy"
            x: 88
            y: body.below + 232
            width: 105
            height: 22
            look: panel.look ?? ({})
            themeDirectory: panel.themeDirectory
            font: panel.font
            settings: panel.settings
            screen: panel.screen
            windowPlace: Qt.point(panel.place.x - panel.halo, panel.place.y - panel.halo)
            // the panel's own order, which puts the dates before the last opened
            choices: {
                const word = by => by === "none" ? panel.words.arrange_none : by === "grid" ? panel.words.snap_to_grid
                                                                                          : panel.words["order_" + by]
                const list = []
                for (const by of ["none", "", "grid", "", "name", "kind", "modified", "created", "last_opened", "added",
                                  "size", "tags"]) {
                    if (by === "") {
                        list.push({ separator: true })
                    } else if (word(by) !== undefined) {
                        list.push({ value: by, label: word(by) })
                    }
                }
                return list
            }
            value: panel.desktop.arrangeBy
            onChosen: value => panel.desktop.arrange(value)
        }
    }
}
