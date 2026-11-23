// a small check box with its label, from the theme's pictures or drawn in its colours
import QtQuick
import "ThemeValues.js" as ThemeValues

FocusScope {
    id: box

    required property var look
    required property string themeDirectory
    required property var font
    property string text: ""
    property bool checked: false
    // a radio button draws round and only ever turns itself on
    property bool round: false

    signal toggled(bool checked)

    readonly property bool held: area.pressed && area.containsMouse
    readonly property var pictures: (round ? look.radio : look.checkbox) ?? ({})
    implicitWidth: 18 + label.implicitWidth
    implicitHeight: 18
    activeFocusOnTab: true

    function press() {
        if (!round || !checked) {
            toggled(!checked)
        }
    }
    Keys.onSpacePressed: press()

    ThemePicture {
        id: art
        y: Math.round((box.height - height) / 2)
        themeDirectory: box.themeDirectory
        path: box.pictures[(box.checked ? "on" : "off") + (box.held ? "_pressed" : "")] ?? ""
    }
    Rectangle {
        visible: !art.ready
        x: 1
        y: Math.round((box.height - height) / 2)
        width: 12
        height: 12
        radius: box.round ? 6 : 2
        color: box.checked ? ThemeValues.color(box.look.accent ?? "#3a6ea5") : (box.held ? "#d8d8d8" : "#ffffff")
        border.color: ThemeValues.color(box.look.separator ?? "#a0a0a0")
        Rectangle {
            visible: box.checked
            anchors.centerIn: parent
            width: box.round ? 4 : 6
            height: width
            radius: box.round ? 2 : 1
            color: "#ffffff"
        }
    }
    // the keyboard's place, around the box, only when tab brought it there
    Rectangle {
        visible: box.activeFocus && [Qt.TabFocusReason, Qt.BacktabFocusReason].includes(box.focusReason)
        x: -1
        y: Math.round((box.height - 16) / 2)
        width: 16
        height: 16
        radius: box.round ? 8 : 4
        color: "transparent"
        border.width: 2
        border.color: Qt.alpha(ThemeValues.color(box.look.accent ?? "#3a6ea5"), 0.6)
    }
    Text {
        id: label
        x: 18
        anchors.verticalCenter: parent.verticalCenter
        text: box.text
        color: ThemeValues.color(box.look.text_color ?? "#000000")
        font.family: box.font.family
        font.pixelSize: box.font.size
    }
    MouseArea {
        id: area
        anchors.fill: parent
        onClicked: {
            box.forceActiveFocus()
            box.press()
        }
    }
}
