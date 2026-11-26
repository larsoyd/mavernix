// session lock screen (ext-session-lock, PAM) in floating greeter style or classic card
// noted in a runtime file so a restarted shell relocks instead of leaving screens blank

import QtQuick
import QtQuick.Effects
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Services.Pam
import "MenuBarClock.js" as MenuBarClock
import "ThemeValues.js" as ThemeValues

Item {
    id: lockScreen

    IconSource {
        id: themeIcons
    }

    required property var settings
    property var choices: null
    // What the desktop shows behind its windows.
    property var desktopFill: settings.theme.desktop.background

    readonly property var theme: settings.theme
    readonly property var looks: theme.lock ?? ({})
    readonly property var wishes: settings.config.lock ?? ({})
    readonly property var user: settings.user ?? ({})
    readonly property string themeDirectory: settings.theme_directory

    readonly property string style: choices?.value("lock", "style") ?? wishes.style ?? "greeter"
    readonly property bool floatingStyle: style !== "card"
    readonly property int blurPx: {
        const mode = choices?.value("greeter", "background_mode") ?? "desktop"
        if (mode === "desktop_blurred")
            return Math.max(0, Math.min(64, choices?.value("greeter", "background_blur") ?? 28))
        return 0
    }
    readonly property bool instantMotion: (choices?.value("greeter", "handoff_style") ?? "choreographed") === "instant"

    property bool locked: false
    property bool unlocking: false
    property bool capsLock: false
    // True once the compositor shows nothing but the lock screen.
    readonly property bool secure: sessionLock.secure
    // "" (nothing yet), "wrong", or a message from PAM to show in the field.
    property string problem: ""
    // Counts wrong tries, each of which triggers the theme's shake.
    property int wrongTries: 0

    function lock() {
        unlockDeadline.stop()
        unlocking = false
        locked = true
        refreshCapsLock()
    }

    function unlock() {
        unlockDeadline.stop()
        unlocking = false
        problem = ""
        locked = false
    }

    function beginSmoothUnlock() {
        if (!locked || unlocking)
            return
        if (instantMotion) {
            unlock()
            return
        }
        problem = ""
        unlocking = true
        unlockDeadline.restart()
    }

    Timer {
        id: unlockDeadline
        interval: 220
        onTriggered: lockScreen.unlock()
    }

    function refreshCapsLock() {
        if (!capsProc.running)
            capsProc.running = true
    }

    Process {
        id: capsProc
        command: ["sh", "-c", "grep -q '^1$' /sys/class/leds/*::capslock/brightness 2>/dev/null && echo 1 || echo 0"]
        stdout: StdioCollector {
            onStreamFinished: lockScreen.capsLock = text.trim() === "1"
        }
    }

    // --- Remembering the lock across a restart of the shell --------------------

    FileView {
        id: lockedNote
        path: Quickshell.env("XDG_RUNTIME_DIR") + "/mavernix-locked"
        blockLoading: true
        printErrors: false
    }
    onLockedChanged: lockedNote.setText(locked ? "locked\n" : "")
    Component.onCompleted: {
        if (lockedNote.text().trim() === "locked") {
            locked = true
            refreshCapsLock()
        }
    }

    // --- Clock formatting matching the menu bar and greeter --------------------

    function clockText(date) {
        const panel = settings.config.panel ?? ({})
        const c = {
            format: choices?.value("panel", "clock_format") ?? panel.clock_format ?? "HH:mm",
            showDate: choices?.value("panel", "clock_shows_date") ?? panel.clock_shows_date ?? false,
            showTime: choices?.value("panel", "clock_shows_time") ?? panel.clock_shows_time ?? true,
            regionTime: choices?.value("panel", "clock_region_time") ?? panel.clock_region_time ?? false,
            regionDate: choices?.value("panel", "clock_region_date") ?? panel.clock_region_date ?? false,
            dateStyle: choices?.value("panel", "clock_date_style") ?? panel.clock_date_style ?? "long",
        }
        return MenuBarClock.text(date, c, Qt.locale(), Qt.locale("en_US"))
    }

    // --- Checking the password ------------------------------------------------

    PamContext {
        id: pam
        // A file in /etc/pam.d/; the config may point at another folder.
        config: lockScreen.wishes.pam_service ?? "login"
        configDirectory: lockScreen.wishes.pam_config_directory ?? "/etc/pam.d"

        // Kept only until PAM asks for it.
        property string typed: ""

        onPamMessage: {
            if (responseRequired) {
                respond(typed)
                typed = ""
            } else if (messageIsError) {
                lockScreen.problem = message
            }
        }
        onCompleted: result => {
            typed = ""
            if (result === PamResult.Success) {
                lockScreen.beginSmoothUnlock()
            } else {
                lockScreen.problem = "wrong"
                lockScreen.wrongTries += 1
            }
        }
        // No verdict counts as a wrong try (some PAM setups answer a wrong
        // password this way); the reason goes to the log.
        onError: error => {
            typed = ""
            console.warn("lock screen: the password could not be checked:", PamError.toString(error),
                         "- PAM service", config, "in", configDirectory)
            lockScreen.problem = "wrong"
            lockScreen.wrongTries += 1
        }
    }

    function tryPassword(password) {
        if (pam.active || lockScreen.unlocking || password === "") {
            return
        }
        problem = ""
        pam.typed = password
        pam.start()
    }

    // --- What is shown ---------------------------------------------------------

    WlSessionLock {
        id: sessionLock
        locked: lockScreen.locked

        WlSessionLockSurface {
            id: surface
            color: "black"

            property real reveal: lockScreen.instantMotion ? 1.0 : 0.0
            Component.onCompleted: {
                if (!lockScreen.instantMotion)
                    lockEnterAnim.start()
                if (lockScreen.floatingStyle)
                    floatingPassword.forceActiveFocus()
                else
                    cardPassword.forceActiveFocus()
            }

            NumberAnimation {
                id: lockEnterAnim
                target: surface
                property: "reveal"
                from: 0.0
                to: 1.0
                duration: 220
                easing.type: Easing.OutCubic
            }

            NumberAnimation {
                id: lockExitAnim
                target: surface
                property: "reveal"
                to: 0.0
                duration: 140
                easing.type: Easing.InQuad
                onFinished: lockScreen.unlock()
            }

            Connections {
                target: lockScreen
                function onUnlockingChanged() {
                    if (lockScreen.unlocking) {
                        lockEnterAnim.stop()
                        lockExitAnim.start()
                    }
                }
            }

            Item {
                id: backdropHost
                anchors.fill: parent
                opacity: surface.reveal

                Fill {
                    id: bgFill
                    anchors.fill: parent
                    visible: lockScreen.blurPx <= 0
                    fill: lockScreen.desktopFill
                    themeDirectory: lockScreen.themeDirectory
                }

                MultiEffect {
                    anchors.fill: parent
                    visible: lockScreen.blurPx > 0
                    source: bgFill
                    blurEnabled: true
                    blurMax: 64
                    blur: Math.min(1.0, lockScreen.blurPx / 64.0)
                    autoPaddingEnabled: false
                }

                Fill {
                    anchors.fill: parent
                    fill: lockScreen.looks.backdrop_veil ?? ({ color: lockScreen.floatingStyle ? "#00000059" : "#00000080" })
                    themeDirectory: lockScreen.themeDirectory
                }
            }

            // Top-right clock matching the menu bar and mavernix-greeter
            Text {
                readonly property var clockLooks: lockScreen.looks.clock ?? ({})
                visible: lockScreen.floatingStyle || lockScreen.looks.clock !== undefined
                opacity: surface.reveal
                anchors.right: parent.right
                anchors.rightMargin: clockLooks.right ?? 18
                y: (clockLooks.baseline ?? 18) - baselineOffset
                color: ThemeValues.color(clockLooks.color ?? "#ffffffb0")
                font.family: lockScreen.theme.fonts.interface.family
                font.pixelSize: lockScreen.floatingStyle ? 15 : lockScreen.theme.fonts.interface.size
                text: lockScreen.clockText(clock.date)
                SystemClock {
                    id: clock
                    precision: SystemClock.Seconds
                }
            }

            // --- Style A: Floating Mavericks layout matching mavernix-greeter ---
            Item {
                id: floatingStage
                anchors.fill: parent
                visible: lockScreen.floatingStyle
                opacity: surface.reveal
                scale: 0.985 + 0.015 * surface.reveal
                transformOrigin: Item.Center

                property real shakeOffset: 0

                Item {
                    id: floatingCenter
                    width: 240
                    height: 220
                    x: Math.round((surface.width - width) / 2)
                    y: Math.round(surface.height / 2 - 41)

                    // 84px circular avatar with Mavericks double rim
                    Item {
                        id: fAvatar
                        width: 88
                        height: 88
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 2

                        Rectangle {
                            anchors.fill: parent
                            radius: 44
                            color: "#22000000"
                        }
                        Image {
                            id: fPic
                            visible: false
                            width: 84
                            height: 84
                            sourceSize: Qt.size(width, height)
                            fillMode: Image.PreserveAspectCrop
                            source: lockScreen.user.picture
                                ? "file://" + lockScreen.user.picture
                                : themeIcons.path("avatar-default", "user-identity")
                        }
                        Rectangle {
                            id: fMask
                            visible: false
                            layer.enabled: true
                            width: 84
                            height: 84
                            radius: 42
                        }
                        MultiEffect {
                            anchors.centerIn: parent
                            width: 84
                            height: 84
                            source: fPic
                            maskEnabled: true
                            maskSource: fMask
                            maskThresholdMin: 0.5
                            maskSpreadAtMin: 1.0
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: 84
                            height: 84
                            radius: 42
                            color: "transparent"
                            border.width: 1
                            border.color: "#40ffffff"
                        }
                    }

                    // Shadowed user name
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 97
                        text: lockScreen.user.full_name || lockScreen.user.name || ""
                        color: "#ffffff"
                        font.family: lockScreen.theme.fonts.interface.family
                        font.pixelSize: 15
                        style: Text.Raised
                        styleColor: "#a3000000"
                    }

                    // Mavericks-style password field (164 x 30, 6px radius)
                    Item {
                        id: fField
                        width: 164
                        height: 30
                        x: Math.round((parent.width - width) / 2 + floatingStage.shakeOffset)
                        y: 143

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -2
                            radius: 8
                            visible: floatingPassword.activeFocus
                            gradient: Gradient {
                                GradientStop { position: 0; color: "#4a95e4" }
                                GradientStop { position: 1; color: "#3c8cdc" }
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            border.width: 1
                            border.color: "#33000000"
                            gradient: Gradient {
                                GradientStop { position: 0; color: "#eaeaea" }
                                GradientStop { position: 0.5; color: "#ffffff" }
                                GradientStop { position: 1; color: "#ffffff" }
                            }
                        }
                        Text {
                            visible: floatingPassword.text === ""
                            anchors.fill: floatingPassword
                            verticalAlignment: Text.AlignVCenter
                            text: lockScreen.problem !== "" && lockScreen.problem !== "wrong"
                                ? lockScreen.problem
                                : (lockScreen.wishes.password_placeholder ?? "Password")
                            color: "#808080"
                            font: floatingPassword.font
                            elide: Text.ElideRight
                        }
                        TextInput {
                            id: floatingPassword
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: (fGo.visible || fBusy.visible ? 28 : 8) + (fCaps.visible ? 20 : 0)
                            verticalAlignment: TextInput.AlignVCenter
                            clip: true
                            focus: lockScreen.floatingStyle
                            echoMode: TextInput.Password
                            passwordMaskDelay: 0
                            passwordCharacter: "•"
                            enabled: !pam.active && !lockScreen.unlocking
                            color: "#000000"
                            font.family: lockScreen.theme.fonts.interface.family
                            font.pixelSize: 14
                            onAccepted: {
                                lockScreen.tryPassword(text)
                                text = ""
                            }
                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_CapsLock)
                                    lockScreen.refreshCapsLock()
                            }
                        }

                        // Caps Lock indicator
                        Rectangle {
                            id: fCaps
                            visible: lockScreen.capsLock
                            width: 16
                            height: 16
                            radius: 3
                            anchors.right: parent.right
                            anchors.rightMargin: (fGo.visible || fBusy.visible) ? 26 : 7
                            anchors.verticalCenter: parent.verticalCenter
                            color: "#22000000"
                            border.width: 1
                            border.color: "#44000000"
                            Text {
                                anchors.centerIn: parent
                                text: "⇪"
                                color: "#555555"
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }

                        // Submit arrow pill
                        Rectangle {
                            id: fGo
                            visible: floatingPassword.text !== "" && !pam.active
                            width: 16
                            height: 16
                            radius: 8
                            anchors.right: parent.right
                            anchors.rightMargin: 7
                            anchors.verticalCenter: parent.verticalCenter
                            color: fGoArea.pressed ? "#8c8c8c" : "#a8a8a8"
                            Text {
                                anchors.centerIn: parent
                                text: "→"
                                color: "white"
                                font.pixelSize: 11
                                font.bold: true
                            }
                            MouseArea {
                                id: fGoArea
                                anchors.fill: parent
                                anchors.margins: -3
                                onClicked: {
                                    lockScreen.tryPassword(floatingPassword.text)
                                    floatingPassword.text = ""
                                }
                            }
                        }

                        // Checking indicator
                        Text {
                            id: fBusy
                            visible: pam.active
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: "…"
                            color: "#7a7a7a"
                            font.pixelSize: 14
                        }
                    }
                }

                // Subtle bottom Sleep action (safe while locked)
                Item {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 35
                    width: sleepLabel.implicitWidth + 16
                    height: sleepLabel.implicitHeight + 10

                    Text {
                        id: sleepLabel
                        anchors.centerIn: parent
                        text: "Sleep"
                        color: sleepArea.pressed ? "#f0f0f0" : "#99ffffff"
                        font.family: lockScreen.theme.fonts.interface.family
                        font.pixelSize: 14
                        style: Text.Raised
                        styleColor: "#66000000"
                    }
                    MouseArea {
                        id: sleepArea
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Quickshell.execDetached(["systemctl", "suspend"])
                    }
                }
            }

            // --- Style B: Classic themed card ([lock] box) ---------------------
            Item {
                id: box
                visible: !lockScreen.floatingStyle
                opacity: surface.reveal
                scale: 0.985 + 0.015 * surface.reveal
                transformOrigin: Item.Center

                readonly property var boxLooks: lockScreen.looks.box ?? ({})
                width: boxLooks.width ?? 260
                height: boxLooks.height ?? 240
                // Its centre is a share of the screen's height down.
                readonly property real restX: Math.round((surface.width - width) / 2)
                property real shakeOffset: 0
                x: restX + shakeOffset
                y: Math.round(surface.height * (boxLooks.center_y ?? 0.5) - height / 2)

                RectangularShadow {
                    visible: box.boxLooks.shadow !== undefined
                    anchors.fill: parent
                    radius: box.boxLooks.fill?.radius ?? 0
                    color: ThemeValues.color(box.boxLooks.shadow?.color)
                    blur: box.boxLooks.shadow?.blur ?? 0
                    offset: Qt.vector2d(box.boxLooks.shadow?.offset[0] ?? 0, box.boxLooks.shadow?.offset[1] ?? 0)
                }
                Fill {
                    anchors.fill: parent
                    fill: box.boxLooks.fill ?? ({ color: "#f0f0f0", radius: 6 })
                    themeDirectory: lockScreen.themeDirectory
                }

                readonly property real pictureSize: lockScreen.looks.picture?.size ?? 80
                Image {
                    id: picture
                    visible: false
                    width: box.pictureSize
                    height: box.pictureSize
                    sourceSize: Qt.size(width, height)
                    fillMode: Image.PreserveAspectCrop
                    source: lockScreen.user.picture
                        ? "file://" + lockScreen.user.picture
                        : themeIcons.path("avatar-default", "user-identity")
                }
                Rectangle {
                    id: pictureShape
                    visible: false
                    layer.enabled: true
                    width: box.pictureSize
                    height: box.pictureSize
                    radius: (lockScreen.looks.picture?.round ?? true) ? width / 2 : 0
                }
                MultiEffect {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: lockScreen.looks.picture?.top ?? 30
                    width: box.pictureSize
                    height: box.pictureSize
                    source: picture
                    maskEnabled: true
                    maskSource: pictureShape
                    // A one-pixel soft edge instead of a jagged one.
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1.0
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: (lockScreen.looks.name_baseline ?? 140) - baselineOffset
                    text: lockScreen.user.full_name || lockScreen.user.name || ""
                    color: ThemeValues.color(lockScreen.looks.text_color ?? "#000000")
                    font.family: lockScreen.theme.fonts.lock_name.family
                    font.pixelSize: lockScreen.theme.fonts.lock_name.size
                    font.bold: lockScreen.theme.fonts.lock_name.bold ?? false
                }

                Item {
                    id: field
                    readonly property var fieldLooks: lockScreen.looks.field ?? ({})
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: fieldLooks.top ?? 180
                    width: fieldLooks.width ?? 160
                    height: fieldLooks.height ?? 22

                    // Focus glow rings, from the field's edge outwards.
                    Repeater {
                        model: field.fieldLooks.focus_rings ?? []
                        Rectangle {
                            required property int index
                            required property string modelData
                            anchors.fill: parent
                            anchors.margins: -(index + 1)
                            color: "transparent"
                            border.width: 1
                            border.color: ThemeValues.color(modelData)
                            radius: (field.fieldLooks.fill?.radius ?? 0) + index + 1
                        }
                    }
                    Fill {
                        anchors.fill: parent
                        fill: field.fieldLooks.fill ?? ({ color: "#ffffff", border: { width: 1, color: "#909090" } })
                        themeDirectory: lockScreen.themeDirectory
                    }
                    Text {
                        visible: cardPassword.text === ""
                        x: field.fieldLooks.text_inset ?? 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: lockScreen.problem !== "" && lockScreen.problem !== "wrong"
                            ? lockScreen.problem
                            : (lockScreen.wishes.password_placeholder ?? "Password")
                        color: ThemeValues.color(field.fieldLooks.placeholder_color ?? "#808080")
                        font: cardPassword.font
                    }
                    TextInput {
                        id: cardPassword
                        x: field.fieldLooks.text_inset ?? 4
                        width: parent.width - 2 * x - (cardCaps.visible ? 18 : 0)
                        anchors.verticalCenter: parent.verticalCenter
                        clip: true
                        focus: !lockScreen.floatingStyle
                        echoMode: TextInput.Password
                        // Never show the last typed character.
                        passwordMaskDelay: 0
                        enabled: !pam.active && !lockScreen.unlocking
                        color: ThemeValues.color(lockScreen.looks.text_color ?? "#000000")
                        font.family: lockScreen.theme.fonts.interface.family
                        font.pixelSize: lockScreen.theme.fonts.interface.size
                        onAccepted: {
                            lockScreen.tryPassword(text)
                            text = ""
                        }
                        Keys.onPressed: event => {
                            if (event.key === Qt.Key_CapsLock)
                                lockScreen.refreshCapsLock()
                        }
                    }
                    Text {
                        id: cardCaps
                        visible: lockScreen.capsLock
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: "⇪"
                        color: "#666666"
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
            }

            // --- Shared wrong-password shake animation -------------------------
            readonly property var wrongMotion:
                (lockScreen.theme.animation ?? ({})).lock_wrong ?? ({ kind: "shake" })
            readonly property var swings: {
                if (wrongMotion.swings !== undefined) {
                    return wrongMotion.swings
                }
                const list = []
                for (let time = 0; time < (wrongMotion.times ?? 3); time++) {
                    list.push(-(wrongMotion.distance ?? 10), wrongMotion.distance ?? 10)
                }
                return list
            }
            property int swingNumber: -1
            function swingOn() {
                swingNumber += 1
                swing.to = swingNumber < swings.length ? swings[swingNumber] : 0
                swing.start()
            }
            NumberAnimation {
                id: swing
                target: lockScreen.floatingStyle ? floatingStage : box
                property: "shakeOffset"
                duration: surface.wrongMotion.swing_ms ?? 60
                easing.type: Easing.InOutSine
                onFinished: {
                    if (surface.swingNumber < surface.swings.length) {
                        surface.swingOn()
                    }
                }
            }
            Connections {
                target: lockScreen
                function onWrongTriesChanged() {
                    if (surface.wrongMotion.kind === "shake" || lockScreen.floatingStyle) {
                        swing.stop()
                        surface.swingNumber = -1
                        surface.swingOn()
                    }
                    if (lockScreen.floatingStyle)
                        floatingPassword.forceActiveFocus()
                    else
                        cardPassword.forceActiveFocus()
                }
            }
        }
    }
}
