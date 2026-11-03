// the first start after a login fades in from the compositor's own empty colour, a restart does not

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "ThemeValues.js" as ThemeValues

Scope {
    id: entry

    required property var settings

    // the same colour make_wayfire_config.py gives the compositor where nothing is drawn
    readonly property color cover: {
        const theme = settings.theme
        const pushed = ((theme.animation ?? ({})).notification_list ?? ({})).kind === "push"
        const fill = ((theme.notifications ?? ({})).list ?? ({})).background ?? ({})
        const stops = fill.color ? [fill.color] : (fill.gradient ?? [])
        return pushed && stops.length ? ThemeValues.color(stops[Math.floor(stops.length / 2)]) : "#1a1a1a"
    }
    property bool covering: false
    property real shade: 1

    FileView {
        id: note
        path: Quickshell.env("XDG_RUNTIME_DIR") + "/mavernix-entered"
        blockLoading: true
        printErrors: false
    }

    Component.onCompleted: {
        if (note.text() !== "")
            return
        note.setText("entered\n")
        covering = true
    }

    SequentialAnimation {
        running: entry.covering
        PauseAnimation { duration: 250 }
        NumberAnimation { target: entry; property: "shade"; to: 0; duration: 450; easing.type: Easing.InOutQuad }
        ScriptAction { script: entry.covering = false }
    }

    Variants {
        model: entry.covering ? Quickshell.screens : []

        PanelWindow {
            required property var modelData
            screen: modelData
            anchors {
                left: true
                right: true
                top: true
                bottom: true
            }
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.namespace: "mavernix-entry"
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
            color: "transparent"
            mask: Region {}

            Rectangle {
                anchors.fill: parent
                color: entry.cover
                opacity: entry.shade
            }
        }
    }
}
