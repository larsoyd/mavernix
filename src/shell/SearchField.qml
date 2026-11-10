// the rounded search field of [search] with a clear button while it holds text

import QtQuick
import "ThemeValues.js" as ThemeValues

FocusScope {
    id: fieldBox

    required property var looks
    required property var face
    required property string themeDirectory

    property alias text: field.text
    // shown in grey while the field is empty
    property string placeholder: ""
    readonly property alias input: field
    // a handler that acts on a key sets event.accepted
    signal keyPressed(var event)
    signal accepted()

    height: looks.field_height ?? 22

    Fill {
        anchors.fill: parent
        fill: fieldBox.looks.field ?? ({ color: "#ffffff", radius: 4 })
        themeDirectory: fieldBox.themeDirectory
    }
    TextInput {
        id: field
        focus: true
        x: fieldBox.looks.field_text_inset ?? 10
        width: clearButton.x - x
        // centred unless the theme gives the baseline row
        y: fieldBox.looks.field_text_baseline_row !== undefined
            ? Math.round(fieldBox.looks.field_text_baseline_row - baselineOffset)
            : Math.round((fieldBox.height - height) / 2)
        clip: true
        color: ThemeValues.color(fieldBox.looks.field_text_color ?? "#000000")
        font.family: fieldBox.face.family
        font.pixelSize: fieldBox.face.size

        Keys.onPressed: event => fieldBox.keyPressed(event)
        onAccepted: fieldBox.accepted()
    }

    Text {
        visible: field.text === "" && fieldBox.placeholder !== ""
        x: field.x
        y: field.y
        width: field.width
        elide: Text.ElideRight
        text: fieldBox.placeholder
        color: field.color
        opacity: 0.4
        font: field.font
    }

    // empties the field, shown while there is text
    Rectangle {
        id: clearButton
        readonly property var buttonLooks: fieldBox.looks.clear_button ?? ({})
        readonly property real crossLength: buttonLooks.cross_length ?? 7
        visible: field.text !== ""
        width: buttonLooks.size ?? 14
        height: width
        radius: width / 2
        x: parent.width - width - (buttonLooks.right_inset ?? 4)
        anchors.verticalCenter: parent.verticalCenter
        color: ThemeValues.color(buttonLooks.color ?? "#b0b0b0")

        Repeater {
            model: [45, -45]
            Rectangle {
                required property int modelData
                anchors.centerIn: parent
                width: clearButton.crossLength
                height: clearButton.buttonLooks.cross_thickness ?? 1.5
                rotation: modelData
                antialiasing: true
                color: ThemeValues.color(clearButton.buttonLooks.cross_color ?? "#ffffff")
            }
        }
        MouseArea {
            anchors.fill: parent
            onClicked: field.text = ""
        }
    }
}
