// the clock's calendar: today marked, a day's events from almanac and a way into it

import QtQuick
import Quickshell
import Quickshell.Io
import "ThemeValues.js" as ThemeValues

FocusScope {
    id: box

    required property var panel
    required property var title

    readonly property var looks: panel.settings.theme.menu
    readonly property var face: panel.settings.theme.fonts.menu
    readonly property var choices: panel.choices
    readonly property color ink: ThemeValues.color(looks.text_color)
    readonly property color faint: ThemeValues.color(looks.disabled_text_color)
    readonly property color todayInk: "#cd6b69"
    readonly property int cell: 30
    readonly property int rowsShown: 6
    readonly property int mostEvents: 6

    property date today: new Date()
    property date picked: today
    property date page: new Date(today.getFullYear(), today.getMonth(), 1)
    // every event of the page's days, by day as 2026-11-23
    property var byDay: ({})
    property bool listed: false

    // the program that reads the calendars, by its desktop entry
    readonly property var entry: DesktopEntries.applications.values.find(one => one.id === "almanac") ?? null
    readonly property int firstWeekday: Qt.locale().firstDayOfWeek === 0 ? 7 : Qt.locale().firstDayOfWeek
    readonly property var days: {
        const first = new Date(page.getFullYear(), page.getMonth(), 1)
        const back = ((first.getDay() === 0 ? 7 : first.getDay()) - firstWeekday + 7) % 7
        const start = new Date(first.getFullYear(), first.getMonth(), 1 - back)
        return Array.from({ length: 7 * rowsShown }, (_, index) => new Date(start.getFullYear(), start.getMonth(), start.getDate() + index))
    }
    readonly property string region: (choices?.value("calendar", "holiday_region") ?? Qt.locale().name.split("_")[1]) ?? ""
    readonly property bool holidays: choices?.value("calendar", "holidays") ?? true
    readonly property var pickedEvents: byDay[stamp(picked)] ?? []

    function stamp(day) {
        return Qt.formatDate(day, "yyyy-MM-dd")
    }
    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate()
    }
    function pick(day) {
        picked = day
        if (day.getMonth() !== page.getMonth() || day.getFullYear() !== page.getFullYear()) {
            page = new Date(day.getFullYear(), day.getMonth(), 1)
        }
    }
    function turn(months) {
        page = new Date(page.getFullYear(), page.getMonth() + months, 1)
    }
    // asked for by a page turn and an opening at once, so it runs once after both
    function list() {
        Qt.callLater(startListing)
    }
    function startListing() {
        if (listing.running) {
            listing.running = false
            Qt.callLater(startListing)
            return
        }
        if (entry === null || (entry.command ?? []).length === 0 || days.length === 0) {
            byDay = {}
            listed = false
            return
        }
        listing.command = ["timeout", "5"].concat(entry.command, ["--list", stamp(days[0]), stamp(days[days.length - 1])],
                                                  holidays && region !== "" ? ["--holidays", region] : [])
        listing.running = true
    }
    // the picked day in Almanac, which takes over a window already open
    function openDay(day) {
        if (entry !== null) {
            Quickshell.execDetached(["mavernix-start-program", entry.id].concat(entry.command, ["--date", stamp(day)]))
        }
        panel.closeMenu()
    }

    onPageChanged: list()

    Connections {
        target: box.title
        function onOpening() {
            box.today = new Date()
            box.picked = box.today
            box.page = new Date(box.today.getFullYear(), box.today.getMonth(), 1)
            box.list()
            box.forceActiveFocus()
        }
    }

    Process {
        id: listing
        stdout: StdioCollector {
            onStreamFinished: {
                const grouped = {}
                try {
                    for (const item of JSON.parse(text || "[]")) {
                        if (item.kind === "event") {
                            (grouped[item.date] = grouped[item.date] ?? []).push(item)
                        }
                    }
                } catch (problem) {
                    console.warn("[calendar-popover] unreadable listing:", problem)
                }
                box.byDay = grouped
                box.listed = true
            }
        }
    }

    Keys.onLeftPressed: pick(new Date(picked.getFullYear(), picked.getMonth(), picked.getDate() - 1))
    Keys.onRightPressed: pick(new Date(picked.getFullYear(), picked.getMonth(), picked.getDate() + 1))
    Keys.onUpPressed: pick(new Date(picked.getFullYear(), picked.getMonth(), picked.getDate() - 7))
    Keys.onDownPressed: pick(new Date(picked.getFullYear(), picked.getMonth(), picked.getDate() + 7))
    Keys.onReturnPressed: openDay(picked)
    Keys.onEnterPressed: openDay(picked)

    implicitWidth: 7 * cell + 2 * looks.padding + 12
    implicitHeight: column.height + 2 * looks.padding

    component Small: Text {
        color: box.ink
        font.family: box.face.family
        font.pixelSize: box.face.size
        elide: Text.ElideRight
    }

    Column {
        id: column
        x: box.looks.padding + 6
        y: box.looks.padding
        width: 7 * box.cell
        spacing: 4
        // as much air under the last row as over the first
        topPadding: 4
        bottomPadding: 4

        // month and year with arrows either side
        Item {
            width: parent.width
            height: 22
            Small {
                anchors.centerIn: parent
                text: Qt.locale().standaloneMonthName(box.page.getMonth(), Locale.LongFormat) + " " + box.page.getFullYear()
                font.bold: true
            }
            Repeater {
                model: [-1, 1]
                Small {
                    required property int modelData
                    x: modelData < 0 ? 4 : parent.width - width - 4
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData < 0 ? "◀" : "▶"
                    color: arrowArea.containsMouse ? box.ink : box.faint
                    font.pixelSize: 10
                    MouseArea {
                        id: arrowArea
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        onClicked: box.turn(parent.modelData)
                    }
                }
            }
        }

        Row {
            Repeater {
                model: 7
                Small {
                    required property int index
                    width: box.cell
                    horizontalAlignment: Text.AlignHCenter
                    text: Qt.locale().dayName((box.firstWeekday + index) % 7, Locale.ShortFormat).slice(0, 2)
                    color: box.faint
                    font.pixelSize: box.face.size - 2
                }
            }
        }

        Grid {
            columns: 7
            Repeater {
                model: box.days
                Item {
                    id: day
                    required property date modelData
                    readonly property bool inMonth: modelData.getMonth() === box.page.getMonth()
                    readonly property bool isToday: box.sameDay(modelData, box.today)
                    readonly property bool isPicked: box.sameDay(modelData, box.picked)
                    readonly property bool busy: (box.byDay[box.stamp(modelData)] ?? []).length > 0
                    width: box.cell
                    height: 24

                    Rectangle {
                        anchors.centerIn: parent
                        width: 24
                        height: 22
                        radius: 4
                        visible: day.isToday || day.isPicked
                        color: day.isToday ? box.todayInk : Qt.rgba(box.ink.r, box.ink.g, box.ink.b, 0.12)
                    }
                    Small {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -1
                        text: day.modelData.getDate()
                        color: day.isToday ? "#ffffff" : day.inMonth ? box.ink : box.faint
                        font.bold: day.isToday || day.isPicked
                    }
                    Rectangle {
                        visible: day.busy
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 2
                        width: 4
                        height: 4
                        radius: 2
                        color: day.isToday ? "#ffffff" : box.faint
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: box.pick(day.modelData)
                        onDoubleClicked: box.openDay(day.modelData)
                    }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: ThemeValues.color(box.looks.separator_color)
        }

        // the picked day's events, a few at most
        Small {
            width: parent.width
            text: box.picked.toLocaleDateString(Qt.locale(), Locale.LongFormat)
            font.bold: true
        }
        Small {
            visible: box.pickedEvents.length === 0
            text: box.entry === null ? "" : "No Events"
            color: box.faint
        }
        Repeater {
            model: box.pickedEvents.slice(0, box.mostEvents)
            Item {
                id: event
                required property var modelData
                width: column.width
                height: box.face.size + 6
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 7
                    height: 7
                    radius: 3.5
                    color: event.modelData.color !== "" ? event.modelData.color : box.faint
                }
                Small {
                    id: when
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: String(event.modelData.span).split("\n")[0]
                    color: box.faint
                }
                Small {
                    x: 13
                    width: when.x - x - 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: event.modelData.title !== "" ? event.modelData.title : "New Event"
                }
            }
        }
        Small {
            visible: box.pickedEvents.length > box.mostEvents
            text: (box.pickedEvents.length - box.mostEvents) + " more…"
            color: box.faint
        }

        Rectangle {
            visible: box.entry !== null
            width: parent.width
            height: 1
            color: ThemeValues.color(box.looks.separator_color)
        }
        Item {
            visible: box.entry !== null
            width: parent.width
            height: box.looks.item_height
            Fill {
                anchors.fill: parent
                anchors.leftMargin: -box.looks.padding - 6
                anchors.rightMargin: -box.looks.padding - 6
                visible: openArea.containsMouse
                fill: box.looks.highlight ?? ({})
                themeDirectory: box.panel.themeDirectory
            }
            Small {
                anchors.verticalCenter: parent.verticalCenter
                text: "Open " + (box.entry?.name ?? "Almanac") + "…"
                color: openArea.containsMouse ? ThemeValues.color(box.looks.highlight_text_color) : box.ink
            }
            MouseArea {
                id: openArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: box.openDay(box.picked)
            }
        }
    }
}
