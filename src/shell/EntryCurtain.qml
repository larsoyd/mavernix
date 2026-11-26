// login reveal and logout fade curtain across all screens
// waits for wallpapers, panel and dock, then drives the entrance choreography

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Shell.Native

Scope {
    id: entry

    required property var settings
    property var choices: null
    // desktop pictures, panel and dock are decoded and mapped
    property bool desktopReady: false

    readonly property color cover: "#000000"
    readonly property string handoffStyle: {
        const chosen = choices?.value("greeter", "handoff_style")
        if (typeof chosen === "string" && chosen !== "")
            return chosen
        const fromGreeter = Quickshell.env("MAVERNIX_GREETER_HANDOFF")
        if (typeof fromGreeter === "string" && fromGreeter !== "")
            return fromGreeter
        return "choreographed"
    }
    readonly property bool choreographed: handoffStyle === "choreographed"
    readonly property bool instant: handoffStyle === "instant"

    property bool covering: false
    property bool lifting: false
    property bool exiting: false
    property real shade: 1
    property real coveredAt: 0

    // reveal values read by DesktopBackground, Panel, Dock and DesktopIcons
    property real backdropScale: 1.0
    property real panelProgress: 1.0
    property real dockSunk: 0.0
    property real iconsOpacity: 1.0

    signal revealed()

    // a picture or surface that never loads must not keep the desktop hidden
    readonly property int deadline: 2500

    property var exitCallback: null

    EntryCursor {
        id: entryCursor
    }

    FileView {
        id: note
        path: Quickshell.env("XDG_RUNTIME_DIR") + "/mavernix-entered"
        blockLoading: true
        printErrors: false
    }

    Component.onCompleted: {
        // names the login, the runtime folder can outlive a logout
        const mark = "entered " + (Quickshell.env("XDG_SESSION_ID") ?? "") + "\n"
        if (note.text() === mark) {
            entryCursor.show()
            return
        }
        note.setText(mark)
        coveredAt = Date.now()
        console.log("[entry] covered at_ms=" + coveredAt + " style=" + handoffStyle)
        if (choreographed) {
            backdropScale = 1.028
            panelProgress = 0.0
            dockSunk = 1.0
            iconsOpacity = 0.0
        }
        shade = 1.0
        covering = true
    }

    Timer {
        interval: entry.deadline
        running: entry.covering && !entry.lifting && !entry.exiting
        onTriggered: entry.lift("deadline")
    }

    function lift(reason) {
        if (!covering || lifting || exiting)
            return
        console.log("[entry] lift reason=" + reason + " style=" + handoffStyle + " waited_ms=" + (Date.now() - coveredAt))
        lifting = true
        entryCursor.show()
        revealed()
        if (instant) {
            instantLift.restart()
        } else if (choreographed) {
            choreographedLift.restart()
        } else {
            simpleLift.restart()
        }
    }

    function beginExit(callback) {
        if (exiting)
            return
        exitCallback = callback
        lifting = false
        choreographedLift.stop()
        simpleLift.stop()
        instantLift.stop()
        exiting = true
        entryCursor.hide(3000)
        shade = 0.0
        covering = true
        exitFade.restart()
        exitDeadline.restart()
    }

    function finishExit() {
        exitDeadline.stop()
        const cb = exitCallback
        exitCallback = null
        if (cb)
            cb()
    }

    onDesktopReadyChanged: if (desktopReady) lift("ready")

    SequentialAnimation {
        id: choreographedLift
        ParallelAnimation {
            NumberAnimation { target: entry; property: "shade"; to: 0.0; duration: 420; easing.type: Easing.OutCubic }
            NumberAnimation { target: entry; property: "backdropScale"; to: 1.0; duration: 520; easing.type: Easing.OutCubic }
            SequentialAnimation {
                PauseAnimation { duration: 50 }
                NumberAnimation { target: entry; property: "panelProgress"; to: 1.0; duration: 380; easing.type: Easing.OutCubic }
            }
            SequentialAnimation {
                PauseAnimation { duration: 90 }
                NumberAnimation { target: entry; property: "dockSunk"; to: 0.0; duration: 440; easing.type: Easing.OutCubic }
            }
            SequentialAnimation {
                PauseAnimation { duration: 110 }
                NumberAnimation { target: entry; property: "iconsOpacity"; to: 1.0; duration: 340; easing.type: Easing.OutQuad }
            }
        }
        ScriptAction {
            script: {
                entry.covering = false
                entry.lifting = false
            }
        }
    }

    SequentialAnimation {
        id: simpleLift
        NumberAnimation { target: entry; property: "shade"; to: 0.0; duration: 360; easing.type: Easing.InOutQuad }
        ScriptAction {
            script: {
                entry.covering = false
                entry.lifting = false
            }
        }
    }

    SequentialAnimation {
        id: instantLift
        NumberAnimation { target: entry; property: "shade"; to: 0.0; duration: 60; easing.type: Easing.OutQuad }
        ScriptAction {
            script: {
                entry.covering = false
                entry.lifting = false
            }
        }
    }

    SequentialAnimation {
        id: exitFade
        NumberAnimation {
            target: entry
            property: "shade"
            to: 1.0
            duration: entry.instant ? 60 : 220
            easing.type: Easing.InOutQuad
        }
        ScriptAction { script: entry.finishExit() }
    }

    Timer {
        id: exitDeadline
        interval: 450
        onTriggered: entry.finishExit()
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
