// The desktop background: a fill (colour, gradient or picture) behind every
// window. On one screen it also shows the files of the Desktop folder.

import QtQuick
import Quickshell
import Quickshell.Wayland

PanelWindow {
    id: background

    required property var settings
    property var fill: settings.theme.desktop.background
    // how much of the screen's right side a dock there keeps, which the files keep clear of
    property real dockOnRight: 0
    // The fill names a picture that cannot be read.
    signal pictureFailed()
    property bool showsFiles: false
    // The shell's UserChoices, which keep the order of the desktop's files.
    property var choices: null
    property var soundPlayer: null
    // subtle login bloom scale and icons entrance opacity from EntryCurtain
    property real backdropScale: 1.0
    property real iconsOpacity: 1.0
    // shown with its picture decoded
    readonly property bool drawn: backingWindowVisible && picture.ready
    // the files' icons on the screen that shows them, else null
    readonly property var icons: iconsLoader.item

    WlrLayershell.layer: WlrLayer.Background
    exclusionMode: ExclusionMode.Ignore
    anchors {
        top: true
        bottom: true
        left: true
        right: true
    }

    // Always takes the keyboard on a click, files or not: the compositor then
    // leaves no window active (src/compositor/desktop-focus).
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand
    WlrLayershell.namespace: "desktop-background"

    // Shown while a picture is still loading, or if a theme gives no fill.
    color: "black"

    Item {
        anchors.fill: parent
        clip: true

        Fill {
            id: picture
            anchors.fill: parent
            scale: background.backdropScale
            transformOrigin: Item.Center
            fill: background.fill
            themeDirectory: background.settings.theme_directory
            onFailedChanged: {
                if (failed) {
                    background.pictureFailed()
                }
            }
        }
    }

    Loader {
        id: iconsLoader
        anchors.fill: parent
        opacity: background.iconsOpacity
        active: background.showsFiles && background.choices !== null
        sourceComponent: DesktopIcons {
            dockOnRight: background.dockOnRight
            settings: background.settings
            userChoices: background.choices
            screen: background.screen
            sounds: background.soundPlayer
        }
    }
}
