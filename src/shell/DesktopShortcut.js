// a launcher for a program on the desktop
// it carries the name and icon the user sees elsewhere

.pragma library
.import "ApplicationName.js" as ApplicationName

// the folder the desktop shows, with a leading tilde for the home folder
// quickshell is the Quickshell global, a library cannot name it itself
function folder(quickshell, settings) {
    const wanted = (settings.config.desktop_files ?? ({})).folder ?? "~/Desktop"
    return wanted.startsWith("~") ? quickshell.env("HOME") + wanted.substring(1) : wanted
}

// only where the desktop shows its files, or the launcher would go unseen
function offered(settings, choices) {
    return ((settings.theme.desktop ?? ({})).icons ?? null) !== null
        && ((choices?.value("desktop_files", "show") ?? false) === true)
}

// icons is an IconSource that knows the user's choices
function create(quickshell, settings, choices, entry, icons) {
    const name = ApplicationName.of(entry, choices)
    const icon = icons.nameFor(entry)
    quickshell.execDetached(["mavernix-file-action", "shortcut", folder(quickshell, settings), entry.id,
                             name !== entry.name ? name : "", icon !== entry.icon ? icon : ""])
}
