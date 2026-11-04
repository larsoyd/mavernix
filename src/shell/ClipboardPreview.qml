// a copy shown whole beside the list or grid it is in
// text wraps and ends in an ellipsis, a picture fits the room

import QtQuick
import "ThemeValues.js" as ThemeValues

Item {
    id: preview

    required property var looks
    required property var face
    property var words: ({})
    // { kind, excerpt, picture, width, height, added }, null for none
    property var file: null
    property real mostWidth: 380
    property real mostHeight: 420
    // as many lines as the height allows, 16 at most
    readonly property int mostLines: Math.max(1, Math.min(16, Math.floor(
        (mostHeight - 3 * inset - footer.height) / Math.max(1, lineMetrics.height * body.lineHeight))))
    // how long a rest on a copy takes before its preview shows
    readonly property int restMs: words.preview_delay_ms ?? 500

    readonly property bool picture: file?.kind === "image"
    readonly property real inset: 10
    readonly property real room: Math.max(40, mostWidth - 2 * inset)
    readonly property real tallRoom: Math.max(40, mostHeight - 3 * inset - footer.height)

    visible: opacity > 0
    opacity: file !== null ? 1 : 0
    Behavior on opacity {
        NumberAnimation { duration: 120 }
    }
    width: Math.round(Math.max(footer.width, picture ? shot.width : body.width) + 2 * inset)
    height: Math.round((picture ? shot.height : body.height) + footer.height + 3 * inset)

    // the last file shown stays while it fades
    property var shown: null
    onFileChanged: {
        if (file !== null) {
            shown = file
        }
    }

    function ago(when) {
        if (!(when instanceof Date) || isNaN(when.getTime())) {
            return ""
        }
        const seconds = (Date.now() - when.getTime()) / 1000
        if (seconds < 60) {
            return words.copied_now ?? "Copied just now"
        }
        if (seconds < 3600) {
            return (words.copied_minutes ?? "Copied %1 min ago").arg(Math.floor(seconds / 60))
        }
        if (seconds < 86400) {
            return (words.copied_hours ?? "Copied %1 h ago").arg(Math.floor(seconds / 3600))
        }
        return (words.copied_on ?? "Copied %1").arg(Qt.formatDateTime(when, "d MMM, HH:mm"))
    }

    BoxFrame {
        looks: preview.looks
    }
    FontMetrics {
        id: lineMetrics
        font: body.font
    }
    Text {
        id: body
        visible: !preview.picture
        x: preview.inset
        y: preview.inset
        width: Math.min(Math.ceil(implicitWidth), preview.room)
        text: visible ? (preview.shown?.excerpt ?? "") : ""
        textFormat: Text.PlainText
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        maximumLineCount: preview.mostLines
        elide: Text.ElideRight
        font.family: preview.face.family
        font.pixelSize: Math.round(preview.face.size * 0.85)
        lineHeight: 1.1
        color: ThemeValues.color(preview.looks.text_color)
    }
    Image {
        id: shot
        visible: preview.picture
        x: preview.inset
        y: preview.inset
        readonly property real fit: Math.min(1, 256 / Math.max(1, preview.shown?.width ?? 1),
                                             256 / Math.max(1, preview.shown?.height ?? 1),
                                             preview.room / Math.max(1, preview.shown?.width ?? 1),
                                             preview.tallRoom / Math.max(1, preview.shown?.height ?? 1))
        width: Math.max(1, Math.round((preview.shown?.width ?? 1) * fit))
        height: Math.max(1, Math.round((preview.shown?.height ?? 1) * fit))
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        source: visible ? (preview.shown?.picture ?? "") : ""
    }
    Text {
        id: footer
        x: preview.inset
        y: (preview.picture ? shot.y + shot.height : body.y + body.height) + preview.inset
        width: Math.min(Math.ceil(implicitWidth), preview.room)
        elide: Text.ElideRight
        text: {
            const file = preview.shown
            if (file === null) {
                return ""
            }
            const when = preview.ago(file.added)
            return file.kind === "image" ? [when, file.width + " × " + file.height].filter(part => part !== "").join(" · ")
                                         : when
        }
        font.family: preview.face.family
        font.pixelSize: Math.round(preview.face.size * 0.8)
        color: ThemeValues.color(preview.looks.disabled_text_color)
    }
}
