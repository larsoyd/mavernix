import QtQuick
import "ThemeValues.js" as ThemeValues

// a desktop icon's name, up to the theme's lines, with a plate behind each line while lit
Item {
    id: desktopName

    required property var look
    required property string text
    required property var face
    property bool lit: false
    property bool picked: false
    // while a window is in front a lit name's plate goes grey, and a picked one's letters too
    property bool away: false
    property bool lettersShown: true
    property string themeDirectory: ""

    readonly property var plate: (away ? look.away_background : undefined)
                                 ?? look.picked_background ?? ({ color: "#3a6ea5", radius: 4 })
    readonly property real room: look.most_width ?? 110
    readonly property real lineHeight: look.height ?? 18
    readonly property real padding: look.padding ?? 6

    DesktopNameLines {
        id: nameLines
        text: desktopName.text
        most: desktopName.look.lines ?? 1
        room: desktopName.room
        font: nameMetrics.font
    }
    FontMetrics {
        id: nameMetrics
        font.family: desktopName.face.family
        font.pixelSize: desktopName.face.size
        font.bold: desktopName.face.bold ?? false
    }
    readonly property var shownLines: nameLines.lines.map(line => nameMetrics.elidedText(line, Qt.ElideMiddle, room))
    readonly property var lineWidths: shownLines.map(line => nameMetrics.advanceWidth(line))
    readonly property real textWidth: Math.max(...lineWidths)

    width: textWidth + 2 * padding
    height: lineHeight * shownLines.length

    // a plate per line, each with the same plate a little lower as its dark edge
    Repeater {
        model: desktopName.lit ? desktopName.lineWidths : []
        Item {
            required property real modelData
            required property int index
            width: modelData + 2 * desktopName.padding
            height: desktopName.lineHeight
            x: (desktopName.width - width) / 2
            y: index * desktopName.lineHeight

            Fill {
                readonly property var edge: desktopName.look.picked_edge
                visible: edge !== undefined
                width: parent.width
                height: parent.height
                y: edge?.drop ?? 0
                fill: ({ color: edge?.color ?? "#00000000", radius: desktopName.plate.radius })
                themeDirectory: desktopName.themeDirectory
            }
            Fill {
                anchors.fill: parent
                fill: desktopName.plate
                themeDirectory: desktopName.themeDirectory
            }
        }
    }
    Item {
        id: letters
        readonly property real room: Math.ceil(desktopName.look.shadow_blur ?? 0) + (desktopName.look.shadow_drop ?? 1)
        visible: desktopName.lettersShown
        anchors.centerIn: parent
        // whole pixels, or the layer stretches the letters
        width: Math.ceil(desktopName.textWidth) + 2 * room
        height: desktopName.height + 2 * room
        layer.enabled: true
        // a lit name loses its shadow
        layer.effect: CastShadow {
            look: desktopName.lit ? null : desktopName.look
            ink: ThemeValues.color(desktopName.picked && desktopName.away
                                   ? desktopName.look.away_color ?? desktopName.look.color ?? "#ffffff"
                                   : desktopName.look.color ?? "#ffffff")
        }

        Repeater {
            model: desktopName.shownLines
            Text {
                required property string modelData
                required property int index
                x: letters.room + Math.round((desktopName.textWidth - (desktopName.lineWidths[index] ?? 0)) / 2)
                y: letters.room + index * desktopName.lineHeight
                height: desktopName.lineHeight
                verticalAlignment: Text.AlignVCenter
                text: modelData
                color: "white"
                font: nameMetrics.font
            }
        }
    }
}
