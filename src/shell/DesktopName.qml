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
    property real pixelSize: face.size
    property real room: look.most_width ?? 110
    property real lineHeight: look.height ?? 18
    property int most: look.lines ?? 1
    // a title beside its icon starts at its left
    property bool alignLeft: false
    // finder's item info, a line in smaller letters under the name and never on its plate
    property string info: ""
    property real infoPixelSize: Math.max(9, pixelSize - 2)
    readonly property real padding: look.padding ?? 6

    DesktopNameLines {
        id: nameLines
        text: desktopName.text
        most: desktopName.most
        room: desktopName.room
        font: nameMetrics.font
    }
    FontMetrics {
        id: nameMetrics
        font.family: desktopName.face.family
        font.pixelSize: desktopName.pixelSize
        font.bold: desktopName.face.bold ?? false
    }
    FontMetrics {
        id: infoMetrics
        font.family: desktopName.face.family
        font.pixelSize: desktopName.infoPixelSize
        font.bold: desktopName.face.bold ?? false
    }
    // measuring calls do not follow a new font size, so each also reads the font's height
    readonly property var shownLines: nameLines.lines.map(line => nameMetrics.height > 0
                                                                  ? nameMetrics.elidedText(line, Qt.ElideMiddle, room) : line)
    readonly property var lineWidths: shownLines.map(line => nameMetrics.height > 0 ? nameMetrics.advanceWidth(line) : 0)
    readonly property string shownInfo: info === "" || infoMetrics.height <= 0 ? ""
                                                                               : infoMetrics.elidedText(info, Qt.ElideMiddle, room)
    readonly property real infoWidth: shownInfo === "" || infoMetrics.height <= 0 ? 0 : infoMetrics.advanceWidth(shownInfo)
    readonly property real textWidth: Math.max(infoWidth, ...lineWidths)
    readonly property real titleHeight: lineHeight * shownLines.length
    function lineX(width) {
        return alignLeft ? 0 : Math.round((textWidth - width) / 2)
    }

    // whole pixels, or the letters' layer lands between them
    width: Math.ceil(textWidth) + 2 * padding
    // finder's info sits under the title's bubble, 3 taller than its lines
    readonly property real infoTop: titleHeight + 3
    height: shownInfo === "" ? titleHeight : infoTop + Math.ceil(infoMetrics.height)

    // a plate per line, each with the same plate a little lower as its dark edge
    Repeater {
        model: desktopName.lit ? desktopName.lineWidths : []
        Item {
            required property real modelData
            required property int index
            width: Math.ceil(modelData) + 2 * desktopName.padding
            height: desktopName.lineHeight
            x: desktopName.lineX(modelData)
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
        x: desktopName.padding - room
        y: -room
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
                x: letters.room + desktopName.lineX(desktopName.lineWidths[index] ?? 0)
                y: letters.room + index * desktopName.lineHeight
                height: desktopName.lineHeight
                verticalAlignment: Text.AlignVCenter
                text: modelData
                color: "white"
                font: nameMetrics.font
            }
        }
        Text {
            visible: desktopName.shownInfo !== ""
            x: letters.room + desktopName.lineX(desktopName.infoWidth)
            y: letters.room + desktopName.infoTop
            text: desktopName.shownInfo
            color: "white"
            font: infoMetrics.font
        }
    }
}
