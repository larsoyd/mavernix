pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Wayland
import Shell.Native

Singleton {
    id: programs

    // these windows carry no hint of their own
    readonly property var knownWindows: [
        { appId: /^(com\.mojang\.minecraft|Minecraft\*? .+)$/, name: "Minecraft", icon: "minecraft-launcher" }
    ]

    // entry ids by the steam id in their command and by their name
    property var steamEntries: ({})
    property var namedEntries: ({})

    WindowGames {
        id: games
    }

    IconSource {
        id: icons
    }

    function index() {
        const steam = {}
        const named = {}
        for (const entry of DesktopEntries.applications.values) {
            named[entry.name] = entry.id
            for (const word of entry.command) {
                const found = /^steam:\/\/rungameid\/(\d+)$/.exec(word)
                if (found !== null) {
                    steam[found[1]] = entry.id
                }
            }
        }
        steamEntries = steam
        namedEntries = named
    }

    Component.onCompleted: index()

    Connections {
        target: DesktopEntries
        function onApplicationsChanged() {
            programs.index()
        }
    }

    function known(appId) {
        return knownWindows.find(window => window.appId.test(appId)) ?? null
    }

    function steamId(appId) {
        return games.steamId(appId, games.revision)
    }

    function gameName(appId) {
        return games.gameName(appId, games.revision)
    }

    function entry(appId) {
        const own = DesktopEntries.heuristicLookup(appId)
        if (own !== null) {
            return own
        }
        const id = steamEntries[steamId(appId)] ?? namedEntries[gameName(appId)]
        return id !== undefined ? DesktopEntries.byId(id) : null
    }

    function name(appId) {
        const id = steamId(appId)
        const title = ToplevelManager.toplevels.values.find(window => window.appId === appId)?.title ?? ""
        return (id !== "" ? games.steamName(id) : "") || gameName(appId) || (known(appId)?.name ?? "")
            || title || appId
    }

    function icon(appId) {
        const id = steamId(appId)
        const wishes = id !== "" ? ["steam_icon_" + id, games.steamIcon(id)] : []
        const candidates = wishes.concat(games.lutrisIcon(gameName(appId)), known(appId)?.icon ?? "")
        return candidates.find(candidate => icons.find(candidate) !== "") ?? appId
    }
}
