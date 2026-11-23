// a small pop-up button whose menu opens with the chosen entry over the button, as appkit's does
import QtQuick
import "ThemeValues.js" as ThemeValues

FocusScope {
    id: button

    required property var look
    required property string themeDirectory
    required property var font
    required property var settings
    required property var screen
    // where the window holding it sits on its screen
    property point windowPlace: Qt.point(0, 0)
    // { value, label } each, or { separator: true }
    property var choices: []
    property var value

    signal chosen(var value)

    readonly property var pictures: look.popup ?? ({})
    readonly property int current: choices.findIndex(choice => choice.value === value)
    implicitWidth: 70
    implicitHeight: 22
    activeFocusOnTab: true
    opacity: enabled ? 1 : (look.disabled_opacity ?? 0.5)

    property bool open: false
    function openMenu() {
        forceActiveFocus()
        open = true
    }
    Keys.onSpacePressed: openMenu()
    Keys.onDownPressed: openMenu()
    Keys.onUpPressed: openMenu()

    readonly property var menuLooks: settings.theme.menu
    readonly property var items: choices.map((choice, number) => choice.separator
        ? { id: 0, label: "", separator: true, enabled: false, checkable: false, checked: false, shortcut: [], children: [] }
        : { id: number + 1, label: choice.label, separator: false, enabled: true, checkable: true,
            checked: number === current, shortcut: [], children: [] })
    // how far the chosen row lies below the menu's top
    readonly property real chosenRow: {
        let row = menuLooks.padding ?? 0
        for (let number = 0; number < Math.max(0, current); number++) {
            row += choices[number].separator ? (menuLooks.separator_height ?? 12) : (menuLooks.item_height ?? 19)
        }
        return row
    }

    Fill {
        id: art
        anchors.fill: parent
        visible: button.pictures.normal !== undefined
        themeDirectory: button.themeDirectory
        fill: ({ image: button.pictures[area.pressed || button.open ? "pressed" : "normal"] ?? button.pictures.normal ?? "",
                 slice: button.pictures.slice ?? [0, 0, 0, 0] })
    }
    Rectangle {
        visible: !art.visible || art.missing
        x: 1
        y: 1
        width: parent.width - 2
        height: parent.height - 4
        radius: 4
        color: area.pressed || button.open ? "#d8d8d8" : "#ffffff"
        border.color: ThemeValues.color(button.look.separator ?? "#a0a0a0")
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            text: "⇕"
            color: ThemeValues.color(button.look.text_color ?? "#000000")
            font.pixelSize: button.font.size
        }
    }
    Rectangle {
        visible: button.activeFocus && [Qt.TabFocusReason, Qt.BacktabFocusReason].includes(button.focusReason)
        x: -1
        y: -1
        width: parent.width + 2
        height: parent.height - 1
        radius: 5
        color: "transparent"
        border.width: 2
        border.color: Qt.alpha(ThemeValues.color(button.look.accent ?? "#3a6ea5"), 0.6)
    }
    Text {
        id: label
        x: button.pictures.text_inset ?? 9
        width: parent.width - x - 18
        y: Math.round((parent.height - 3 - height) / 2)
        elide: Text.ElideRight
        text: button.choices[button.current]?.label ?? ""
        color: ThemeValues.color(button.look.text_color ?? "#000000")
        font.family: button.font.family
        font.pixelSize: button.font.size
    }
    MouseArea {
        id: area
        anchors.fill: parent
        onPressed: button.openMenu()
    }

    MenuOverlay {
        id: menu
        screen: button.screen
        settings: button.settings
        open: button.open
        items: button.items
        readonly property point inWindow: button.open ? button.mapToItem(null, 0, 0) : Qt.point(0, 0)
        menuX: Math.max(0, Math.min(button.windowPlace.x + inWindow.x - (button.menuLooks.text_inset ?? 21) + label.x,
                                    width - menuWidth))
        menuY: Math.max(0, Math.min(button.windowPlace.y + inWindow.y - button.chosenRow, height - menuHeight))
        onChosen: id => {
            button.open = false
            button.chosen(button.choices[id - 1].value)
        }
        onDismissed: button.open = false
    }
}
