.pragma library

// qml's Locale.LongFormat and ShortFormat
const longForm = 0
const shortForm = 1
// a day of the week leading the clock format leads the date too
const leadingWeekday = /^(dddd?)[ ,]*/

// the clock format reads in english as it always has, only the region's own formats follow LC_TIME
function text(date, choices, timeLocale, wordsLocale) {
    const format = choices.format ?? "HH:mm"
    if (!choices.showDate && !choices.regionTime) {
        return Qt.formatDateTime(date, format)
    }
    const weekday = format.match(leadingWeekday)?.[1] ?? ""
    const style = choices.showDate ? choices.dateStyle : ""
    const parts = []
    if (weekday !== "") {
        parts.push(style === "full" || style === "short" ? date.toLocaleString(wordsLocale, weekday)
                   : style === "region" || choices.regionTime ? date.toLocaleString(timeLocale, weekday)
                   : Qt.formatDateTime(date, weekday))
    }
    if (choices.showDate) {
        parts.push(dateText(date, style, timeLocale, wordsLocale))
    }
    if (choices.showTime ?? true) {
        parts.push(choices.regionTime ? date.toLocaleString(timeLocale, regionTimeFormat(timeLocale, format))
                                      : Qt.formatDateTime(date, format.replace(leadingWeekday, "")))
    }
    return parts.join(" ")
}

function dateText(date, style, timeLocale, wordsLocale) {
    switch (style) {
    case "region":
        return date.toLocaleDateString(timeLocale, shortForm)
    case "numeric":
        return date.toLocaleString(timeLocale, numericFormat(timeLocale))
    default:
        return wordsDate(date, style === "short", wordsLocale)
    }
}

// the region's short time, with seconds when the clock's own format has them
function regionTimeFormat(locale, format) {
    const region = locale.timeFormat(shortForm)
    const withSeconds = /s/.test(format.replace(/'[^']*'/g, ""))
    return withSeconds && !/s/.test(region) ? region.replace(/mm/, "mm:ss") : region
}

// day, month and year in the region's order, two digits each and the whole year
function numericFormat(locale) {
    const order = locale.dateFormat(shortForm).replace(/'[^']*'/g, "").match(/d+|M+|y+/g) ?? ["MM", "dd", "yyyy"]
    return order.map(part => part[0] === "d" ? "dd" : part[0] === "M" ? "MM" : "yyyy").join("/")
}

function wordsDate(date, short, locale) {
    if (!locale.name.startsWith("en")) {
        const dayAndMonth = dayAndMonthFormat(locale)
        return date.toLocaleString(locale, short ? dayAndMonth.replace("MMMM", "MMM") : dayAndMonth)
    }
    const whole = date.toLocaleString(locale, "MMMM")
    const cut = date.toLocaleString(locale, "MMM")
    const month = !short ? whole : cut === whole ? cut : cut + "."
    return month + " " + ordinal(date.getDate())
}

// the long date without its year and weekday, quoted words between kept
function dayAndMonthFormat(locale) {
    const long = locale.dateFormat(longForm)
    const tokens = long.match(/'[^']*'|d+|M+|y+|[^'dMy]+/g) ?? []
    const isDayOrMonth = token => /^(d{1,2}|M{3,4})$/.test(token)
    const first = tokens.findIndex(isDayOrMonth)
    const last = tokens.length - 1 - tokens.slice().reverse().findIndex(isDayOrMonth)
    return first < 0 ? "d MMMM" : tokens.slice(first, last + 1).join("")
}

function ordinal(day) {
    const tens = Math.floor(day / 10) % 10
    const suffix = tens === 1 ? "th" : ["th", "st", "nd", "rd"][day % 10] ?? "th"
    return day + suffix
}

// the clock taken from wherever the config lists it, to the chosen end or the middle
function placed(left, right, position) {
    const listed = left.includes("clock") || right.includes("clock")
    const without = names => names.filter(name => name !== "clock")
    if (!listed || position === undefined || position === "right") {
        return { left: left, middle: [], right: right }
    }
    if (position === "middle") {
        return { left: without(left), middle: ["clock"], right: without(right) }
    }
    const leftNames = without(left)
    const after = leftNames.indexOf("logo") + 1
    leftNames.splice(after, 0, "clock")
    return { left: leftNames, middle: [], right: without(right) }
}
