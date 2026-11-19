// how loud each program playing sound is, to read and to change

import QtQuick
import Quickshell.Services.Pipewire
import Quickshell.Wayland
import "ApplicationName.js" as ApplicationName
import "ProgramStreams.js" as ProgramStreams

Item {
    id: volumes

    // the shell's UserChoices, for names and icons the user gave programs
    property var choices: null

    IconSource {
        id: themeIcons
        choices: volumes.choices
    }

    // a stream that plays into a sink counts as a sink itself
    readonly property var streams: (Pipewire.nodes?.values ?? [])
        .filter(node => node.isStream && node.isSink && node.audio !== null)

    // pipewire reports an object's values only while it is tracked
    PwObjectTracker {
        objects: volumes.streams
    }

    readonly property var groups: ProgramStreams.groups(streams.map(
        node => ({ properties: node.properties, volume: node.audio.volume, muted: node.audio.muted })))

    // one { key, name, icon, loudness, muted } for each program, by name
    readonly property var programs: groups.map(group => {
        const windows = ToplevelManager.toplevels.values.map(window => window.appId)
        const appId = ProgramStreams.windowFor(group.hints, windows)
        const named = appId !== "" ? [appId].concat(group.hints) : group.hints
        const entry = named.map(hint => WindowPrograms.entry(hint)).find(found => found !== null) ?? null
        const picture = entry !== null ? themeIcons.nameFor(entry)
            : (appId !== "" ? WindowPrograms.icon(appId) : (group.hints.find(hint => themeIcons.find(hint) !== "") ?? ""))
        return { key: group.key,
                 name: ApplicationName.of(entry, volumes.choices) ?? (appId !== "" ? WindowPrograms.name(appId) : group.label),
                 icon: themeIcons.path(picture, "application-x-executable"),
                 loudness: group.loudness, muted: group.muted }
    })

    // sets every stream of the program and ends its mute
    function setTo(key, share) {
        const group = groups.find(candidate => candidate.key === key)
        for (const index of group?.members ?? []) {
            const sound = streams[index].audio
            sound.muted = false
            sound.volume = Math.max(0, Math.min(1, share))
        }
    }
}
