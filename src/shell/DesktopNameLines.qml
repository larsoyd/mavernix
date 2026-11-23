import QtQuick

// a name split as finder does it, first line at a word or else a letter
Item {
    id: split

    property string text
    property int most: 1
    property real room: 100
    property alias font: measure.font

    readonly property string flat: text.replace(/\n/g, " ")
    readonly property var lines: {
        if (most < 2 || measure.lineCount < 2) {
            return [flat]
        }
        const start = measure.positionAt(0, measure.contentHeight / measure.lineCount * 1.5)
        return [flat.slice(0, start).replace(/\s+$/, ""), flat.slice(start)]
    }

    visible: false

    TextEdit {
        id: measure
        width: split.room
        readOnly: true
        wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
        text: split.text
    }
}
