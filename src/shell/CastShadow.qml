// layer effect for white text, the letters in ink over the blurred shadow they cast
// painted more than once the shadow grows darker and the letters heavier

import QtQuick
import "ThemeValues.js" as ThemeValues

ShaderEffect {
    required property var look
    required property color ink

    readonly property real ratio: Screen.devicePixelRatio
    readonly property color shadowColor: ThemeValues.color(look?.shadow_color ?? "#00000000")
    readonly property vector2d texel: Qt.vector2d(1 / (width * ratio), 1 / (height * ratio))
    readonly property real reach: Math.ceil((look?.shadow_blur ?? 0) * ratio)
    readonly property real drop: Math.round((look?.shadow_drop ?? 1) * ratio)
    readonly property real passes: look?.shadow_passes ?? 1

    fragmentShader: "cast-shadow.frag.qsb"
}
