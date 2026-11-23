// a small slider with tick marks under it whose knob points at them, as finder's view options have
import QtQuick
import "ThemeValues.js" as ThemeValues

FocusScope {
    id: slider

    required property var look
    required property string themeDirectory
    property real from: 0
    property real to: 1
    property real value: 0
    property int ticks: 0
    // what an arrow key moves it by
    property real step: (to - from) / 20

    // while dragged, every new value
    signal moved(real value)

    readonly property var pictures: look.slider ?? ({})
    readonly property real knobWidth: 13
    readonly property real travel: width - knobWidth
    readonly property real share: to > from ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0
    implicitWidth: 128
    implicitHeight: 17
    activeFocusOnTab: true

    function moveTo(wanted) {
        const next = Math.max(from, Math.min(to, wanted))
        if (next !== value) {
            moved(next)
        }
    }
    Keys.onLeftPressed: moveTo(value - step)
    Keys.onRightPressed: moveTo(value + step)
    Keys.onDownPressed: moveTo(value - step)
    Keys.onUpPressed: moveTo(value + step)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
            moveTo(event.key === Qt.Key_Home ? from : to)
            event.accepted = true
        }
    }

    Fill {
        id: track
        x: 1
        y: 3
        width: slider.width - 3
        height: 5
        visible: slider.pictures.track !== undefined
        themeDirectory: slider.themeDirectory
        fill: ({ image: slider.pictures.track ?? "", slice: slider.pictures.track_slice ?? [0, 0, 0, 0] })
    }
    Rectangle {
        visible: !track.visible
        x: 1
        y: 4
        width: slider.width - 3
        height: 3
        radius: 1.5
        color: ThemeValues.color(slider.look.separator ?? "#a0a0a0")
    }
    Repeater {
        model: slider.ticks
        Item {
            required property int index
            x: Math.round(slider.knobWidth / 2 + slider.travel * index / Math.max(1, slider.ticks - 1))
            y: 13
            ThemePicture {
                id: tick
                themeDirectory: slider.themeDirectory
                path: slider.pictures.tick ?? ""
            }
            Rectangle {
                visible: !tick.ready
                width: 1
                height: 3
                color: ThemeValues.color(slider.look.separator ?? "#a0a0a0")
            }
        }
    }
    Item {
        id: knob
        x: Math.round(slider.travel * slider.share)
        y: 1
        width: slider.knobWidth
        height: 15
        ThemePicture {
            id: knobArt
            themeDirectory: slider.themeDirectory
            path: slider.pictures[area.pressed ? "thumb_pressed" : "thumb"] ?? ""
        }
        Rectangle {
            visible: !knobArt.ready
            y: 1
            width: parent.width
            height: 11
            radius: 3
            color: area.pressed ? "#d8d8d8" : "#ffffff"
            border.color: ThemeValues.color(slider.look.separator ?? "#a0a0a0")
        }
        Rectangle {
            visible: slider.activeFocus && [Qt.TabFocusReason, Qt.BacktabFocusReason].includes(slider.focusReason)
            anchors.fill: parent
            anchors.margins: -2
            radius: 4
            color: "transparent"
            border.width: 2
            border.color: Qt.alpha(ThemeValues.color(slider.look.accent ?? "#3a6ea5"), 0.6)
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        // a press on the knob keeps its grip, elsewhere the knob jumps under the pointer
        property real grip: slider.knobWidth / 2
        function follow(x) {
            slider.moveTo(slider.from + (slider.to - slider.from) * (x - grip) / Math.max(1, slider.travel))
        }
        onPressed: mouse => {
            slider.forceActiveFocus()
            grip = mouse.x >= knob.x && mouse.x < knob.x + knob.width ? mouse.x - knob.x : slider.knobWidth / 2
            follow(mouse.x)
        }
        onPositionChanged: mouse => follow(mouse.x)
    }
}
