// the plate of layouts hanging below a screen's top while a window is dragged there
// the compositor says where it is and which zone is picked, this only draws it

import QtQuick
import Quickshell
import Quickshell.Wayland
import "ThemeValues.js" as ThemeValues

PanelWindow {
    id: picker

    required property var settings
    // the compositor's last word for this screen
    property var told: null

    readonly property var looks: settings.theme.zone_picker ?? ({})
    readonly property var motion: (settings.theme.animation ?? ({})).zone_picker ?? ({})
    readonly property var fonts: settings.theme.fonts
    readonly property string themeDirectory: settings.theme_directory

    // what was last shown stays drawn while the plate slides away
    property var shownPlate: null
    onToldChanged: {
        if (told !== null && told.state !== "hidden") {
            shownPlate = told
        }
    }
    readonly property string phase: told?.state ?? "hidden"
    readonly property var box: shownPlate?.plate ?? ({ x: 0, y: 0, width: 0, height: 0 })
    readonly property var hover: phase === "open" ? (told.hover ?? null) : null
    readonly property var hoverCell: hover === null ? null : shownPlate.minis[hover[0]].zones[hover[1]]

    readonly property var name: looks.name ?? ({})
    readonly property var face: fonts.zone_picker ?? fonts.interface
    readonly property real nameRoom: Math.ceil(name.shadow_blur ?? 3) + (name.shadow_drop ?? 2)
    readonly property real nameHeight: Math.ceil(face.size * 1.5) + 2 * nameRoom
    readonly property real nameGap: name.gap ?? 6

    // wide enough for a zone's name under a plate of one layout, never wider than the screen
    readonly property real wide: Math.min(screen?.width ?? box.width, Math.max(box.width, name.most_width ?? 360))
    readonly property real left: Math.max(0, Math.min((screen?.width ?? wide) - wide,
                                                      Math.round(box.x + box.width / 2 - wide / 2)))

    // how many pixels of the plate hang below the top
    readonly property real wanted: phase === "open" ? box.height : phase === "peek" ? (shownPlate?.peek ?? 0) : 0
    property real out: wanted
    Behavior on out {
        NumberAnimation {
            duration: picker.motion.slide_ms ?? 250
            easing.type: Easing.Bezier
            easing.bezierCurve: (picker.motion.bezier ?? [0.25, 0.1, 0.25, 1.0]).concat([1, 1])
        }
    }

    visible: phase !== "hidden" || out > 0

    anchors.top: true
    anchors.left: true
    margins.top: box.y
    margins.left: left
    implicitWidth: Math.max(1, wide)
    implicitHeight: Math.max(1, box.height + nameGap + nameHeight)
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "mavernix-zones"
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    color: "transparent"
    mask: Region {}

    Item {
        id: hanger
        anchors.fill: parent
        clip: true

        Item {
            id: plate
            x: picker.box.x - picker.left
            y: picker.out - picker.box.height
            width: picker.box.width
            height: picker.box.height

            // hung from the top like a sheet: its upper corners stay out of sight
            Fill {
                readonly property var background: picker.looks.background ?? ({ color: "#202020e0", radius: 10 })
                anchors.fill: parent
                anchors.topMargin: -(background.radius ?? 0)
                fill: background
                themeDirectory: picker.themeDirectory
            }

            Repeater {
                model: picker.shownPlate?.minis ?? []
                Item {
                    id: mini
                    required property var modelData
                    x: modelData.x - picker.box.x
                    y: modelData.y - picker.box.y
                    width: modelData.width
                    height: modelData.height

                    Fill {
                        anchors.fill: parent
                        fill: picker.looks.screen ?? ({ color: "#ffffff20", radius: 2 })
                        themeDirectory: picker.themeDirectory
                    }
                    Repeater {
                        model: mini.modelData.zones
                        Fill {
                            required property var modelData
                            x: modelData.x - mini.modelData.x
                            y: modelData.y - mini.modelData.y
                            width: modelData.width
                            height: modelData.height
                            fill: modelData.ok ? (picker.looks.zone ?? ({ color: "#ffffff60", radius: 2 }))
                                               : (picker.looks.zone_refused ?? ({ color: "#ffffff18", radius: 2 }))
                            themeDirectory: picker.themeDirectory
                        }
                    }
                }
            }

            Fill {
                readonly property real reach: picker.looks.mark_reach ?? 2
                visible: picker.hoverCell !== null
                x: (picker.hoverCell?.x ?? 0) - picker.box.x - reach
                y: (picker.hoverCell?.y ?? 0) - picker.box.y - reach
                width: (picker.hoverCell?.width ?? 0) + 2 * reach
                height: (picker.hoverCell?.height ?? 0) + 2 * reach
                fill: picker.looks.mark ?? ({ color: "#ffffff90", radius: 3, border: { color: "#ffffff", width: 2 } })
                themeDirectory: picker.themeDirectory
            }
        }

        Item {
            id: letters
            // the name stays while it fades, so it does not empty first
            property string shownName: ""
            readonly property string toldName: picker.hover !== null ? (picker.told.name ?? "") : ""
            onToldNameChanged: {
                if (toldName !== "") {
                    shownName = toldName
                }
            }
            opacity: toldName !== "" ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: picker.motion.name_fade_ms ?? 100 }
            }
            visible: opacity > 0

            // whole pixels, or the layer stretches the letters
            width: Math.ceil(label.width) + 2 * picker.nameRoom
            height: picker.nameHeight
            x: Math.round((picker.wide - width) / 2)
            y: Math.round(picker.out + picker.nameGap)
            layer.enabled: true
            layer.effect: CastShadow {
                look: picker.name
                ink: ThemeValues.color(picker.name.color ?? "#ffffff")
            }

            Text {
                id: label
                x: picker.nameRoom
                y: picker.nameRoom
                width: Math.min(implicitWidth, picker.wide - 2 * picker.nameRoom - 2 * (picker.name.inset ?? 8))
                elide: Text.ElideRight
                text: letters.shownName
                color: "white"
                font.family: picker.face.family
                font.pixelSize: picker.face.size
                font.bold: picker.face.bold ?? false
            }
        }
    }
}
