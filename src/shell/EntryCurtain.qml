// a login fades in from the compositor's empty colour once the desktop is drawn, not a restart

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "ThemeValues.js" as ThemeValues

Scope {
    id: entry

    required property var settings
    // every desktop picture is decoded and on screen
    property bool desktopReady: false

    // the same colour make_wayfire_config.py gives the compositor where nothing is drawn
    readonly property color cover: {
        const theme = settings.theme
        const pushed = ((theme.animation ?? ({})).notification_list ?? ({})).kind === "push"
        const fill = ((theme.notifications ?? ({})).list ?? ({})).background ?? ({})
        const stops = fill.color ? [fill.color] : (fill.gradient ?? [])
        return pushed && stops.length ? ThemeValues.color(stops[Math.floor(stops.length / 2)]) : "#1a1a1a"
    }
    property bool covering: false
    property bool lifting: false
    property real shade: 1
    property real coveredAt: 0
    // a picture that never loads must not keep the desktop hidden
    readonly property int deadline: 4000

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
        coveredAt = Date.now()
        covering = true
    }

    Timer {
        interval: entry.deadline
        running: entry.covering
        onTriggered: entry.lift("deadline")
    }

    function lift(reason) {
        if (!covering || lifting)
            return
        console.log("[entry] lift reason=" + reason + " waited_ms=" + (Date.now() - coveredAt))
        lifting = true
    }

    onDesktopReadyChanged: if (desktopReady) lift("ready")

    SequentialAnimation {
        running: entry.lifting
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
