// a box of nine theme pictures that reach past it by their margins, with a pointer piece on one edge
// the keys and pieces are in the theme docs under folders in the dock

import QtQuick

Item {
    id: box

    required property var looks
    required property string themeDirectory

    // the pointer tip's x from the box's left edge, or its y on a side edge, negative for none
    property real pointerX: -1
    property string pointerEdge: "bottom"

    anchors.fill: parent

    readonly property real pieceSize: looks.piece_size
    // The pictures' outer edges, and where the corner pictures end.
    readonly property real outerLeft: -looks.margins[0]
    readonly property real outerTop: -looks.margins[1]
    readonly property real outerRight: width + looks.margins[2]
    readonly property real outerBottom: height + looks.margins[3]
    readonly property real innerLeft: outerLeft + pieceSize
    readonly property real innerTop: outerTop + pieceSize
    readonly property real innerRight: outerRight - pieceSize
    readonly property real innerBottom: outerBottom - pieceSize

    readonly property bool hasPointer: pointerX >= 0 && looks.pointer_width !== undefined
    readonly property bool below: hasPointer && pointerEdge === "bottom"
    readonly property real pointerLeft:
        below ? Math.round(pointerX - looks.pointer_width / 2) : innerRight
    readonly property real pointerRight: below ? pointerLeft + looks.pointer_width : innerRight
    // the same along a side edge, clear of its corners
    readonly property bool beside: hasPointer && pointerEdge !== "bottom"
    readonly property real pointerTop: beside
        ? Math.max(innerTop, Math.min(innerBottom - looks.pointer_width, Math.round(pointerX - looks.pointer_width / 2)))
        : innerBottom
    readonly property real pointerBottom: beside ? pointerTop + looks.pointer_width : innerBottom

    component Piece: Image {
        required property string piece
        // a hidden piece is not read, a theme may lack the side pointers
        source: visible ? "file://" + box.themeDirectory + "/" + box.looks.pieces + "-" + piece + ".png" : ""
        fillMode: Image.Stretch
    }

    Piece { piece: "tl"; x: box.outerLeft; y: box.outerTop; width: box.pieceSize; height: box.pieceSize }
    Piece { piece: "tm"; x: box.innerLeft; y: box.outerTop; width: box.innerRight - box.innerLeft; height: box.pieceSize }
    Piece { piece: "tr"; x: box.innerRight; y: box.outerTop; width: box.pieceSize; height: box.pieceSize }

    component SideEdge: Item {
        id: column
        required property string piece
        readonly property bool pointed: box.beside && box.pointerEdge === (piece === "l" ? "left" : "right")
        x: piece === "l" ? box.outerLeft : box.innerRight
        y: box.innerTop
        width: box.pieceSize
        height: box.innerBottom - box.innerTop
        readonly property real pointerFrom: pointed ? box.pointerTop - box.innerTop : height
        Piece { piece: column.piece; width: parent.width; height: column.pointerFrom }
        Piece {
            // without the side pointer picture the plain edge stands in
            property bool missing: false
            onStatusChanged: missing = missing || status === Image.Error
            piece: column.piece + (missing ? "" : "t")
            visible: column.pointed
            y: column.pointerFrom
            width: parent.width
            height: box.looks.pointer_width
        }
        Piece {
            piece: column.piece
            visible: column.pointed
            y: column.pointerFrom + box.looks.pointer_width
            width: parent.width
            height: column.height - y
        }
    }
    SideEdge { piece: "l" }
    Piece { piece: "c"; x: box.innerLeft; y: box.innerTop; width: box.innerRight - box.innerLeft; height: box.innerBottom - box.innerTop }
    SideEdge { piece: "r" }

    Piece { piece: "bl"; x: box.outerLeft; y: box.innerBottom; width: box.pieceSize; height: box.pieceSize }
    Piece { piece: "bm"; x: box.innerLeft; y: box.innerBottom; width: box.pointerLeft - box.innerLeft; height: box.pieceSize }
    Piece { piece: "bt"; visible: box.below; x: box.pointerLeft; y: box.innerBottom; width: box.pointerRight - box.pointerLeft; height: box.pieceSize }
    Piece { piece: "bm"; visible: box.below; x: box.pointerRight; y: box.innerBottom; width: box.innerRight - box.pointerRight; height: box.pieceSize }
    Piece { piece: "br"; x: box.innerRight; y: box.innerBottom; width: box.pieceSize; height: box.pieceSize }
}
