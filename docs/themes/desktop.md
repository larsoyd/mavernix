# Desktop

The desktop is what sits behind every window. It's a single fill.

```toml
[desktop]
background = { gradient = ["#5b6f82", "#34424f"] }
# or a wallpaper:
# background = { image = "images/desktop.jpg", fit = "cover" }
# Optional: a folder in your theme with pictures that the settings window
# offers for the desktop, next to any files of the user's own.
pictures = "wallpapers"
```

Your theme doesn't decide which picture the user picks. That choice is kept with the user's other choices (`[desktop] picture` in the config). If they haven't picked one, the desktop shows `background`.

`fit = "cover"` scales a picture until it covers the whole area without distorting it, and trims the overhang evenly. Without it, the picture is stretched to the shape of the area. That's fine for a strip or a frame, but not for a photo.

## Files on the desktop

This table shows the files in the user's Desktop folder as icons (`src/shell/DesktopIcons.qml`). Whether they're shown at all is up to the config (`[desktop_files] show`). If your theme leaves the table out, no files are shown.

```toml
[fonts]
desktop_icon = { family = "DejaVu Sans", size = 12 }   # the names under the icons, at the user's text size
view_options = { family = "DejaVu Sans", size = 11 }   # the View Options panel's words

[desktop.icons]
# The icon size, grid spacing, text size and where the name goes are the
# user's choices ([desktop_files] in the config, set in the View Options
# panel). Each icon takes a cell worked out from them the way Finder does it,
# and cells stand in columns from the screen's far top corner, margin from
# its edges. bottom_room is kept free at the bottom (for a dock).
margin = 8
bottom_room = 90
picked = { color = "#ffffff30", radius = 4 }    # any fill: the box around a picked file's icon
picked_reach = 4                                # how far it reaches past the icon
# The name under the icon: its colour, and its line height at the font's
# size, which grows with the user's text size. How wide a line gets and how
# many lines there are follow the grid spacing: a long name goes on to a second
# line at a word, or between letters when one word is too long, and that line
# is cut short in the middle. Item info is a line in smaller letters under it.
# picked_background is the plate behind each line of a picked file's name, and
# of a folder's name while files are dragged over it (any fill, `padding`
# wider than the line at each end).
#
# Optional, a shadow the letters cast on the desktop. shadow_color is what
# turns it on, shadow_drop how far down it falls (1 if left out) and
# shadow_blur how soft it is, 0 for a hard edge. shadow_passes paints name and
# shadow that many times, so the dark builds up and the letters get heavier.
# A picked name has none.
#
# Also optional: picked_edge is a darker copy of the plate, drop lower, showing
# as an edge under it. away_background and away_color are the plate and the
# letters of a picked name while a window is in front of the desktop.
name = { color = "#ffffff", shadow_color = "#000000c0", height = 18, padding = 5, picked_background = { color = "#3a6ea5", radius = 4 } }
# Optional: a frame for a picture that's shown as its own icon. It has an
# edge of one colour and width, and a soft shadow behind it. `share` is how
# much of the icon's square the framed picture may take. Without it, the
# bare picture fills the square.
preview_frame = { color = "#ffffff", width = 3, share = 0.86, shadow_color = "#00000080", shadow_blur = 4, shadow_drop = 1 }
# The band you drag over the empty desktop to pick files. Optional: fade_ms,
# how long it takes to fade away where you let it go (it's gone at once if
# left out).
band = { color = "#3a6ea540", border_color = "#3a6ea5", border_width = 1, fade_ms = 200 }
# Optional: how long icons take to glide to a place you give them by a drop
# onto the grid, Clean Up or a new order, eased in and out. Left out, they
# jump there.
move_ms = 350
# Optional: a program opened from its icon leaves a copy of the icon that
# grows to twice its size as it fades away, over this long.
launch_ms = 150
# Optional: dragged icons. All picked icons follow the pointer with their names
# lit, drag_opacity see-through. With several, a badge with their count sits at
# the lower right of the one held: size is its height, fill any fill (rounded to
# a circle), text_color its digits. A drop nothing takes slides back to the
# icons over slide_back_ms, slowing down. Left out, the picture just vanishes.
drag_opacity = 0.85
slide_back_ms = 250
drag_badge = { size = 18, fill = { color = "#d0312d" }, text_color = "#ffffff" }
# Optional: a drag that rests on a folder for spring_ms makes it blink three
# times, spring_blink_ms apart, and then opens it, so files can be dropped
# inside. Left out, folders never open by themselves.
spring_ms = 700
spring_blink_ms = 90
# Optional: an icon theme emblem laid over a link's icon from its lower left,
# share of the icon's size. Left out, or when the icon theme lacks it, links
# look like what they point to.
link_emblem = { icon = "emblem-symbolic-link", share = 0.4 }
```

## The View Options panel

The panel that the desktop's menu, the View menu and Super+J show: a small window with the icon size and grid spacing sliders, the text size and Sort By pop-ups, the label position, and the item info and preview boxes (`src/shell/DesktopViewOptions.qml`). Without this table the panel isn't offered.

```toml
[desktop.view_options]
width = 201
title_height = 16                     # the title bar, with the panel's name and a close button
title_fill = { color = "#e4e4e4" }    # any fill
title_line = "#c0c0c0"                # the line under it
title_color = "#505050"
fill = "#efefef"
text_color = "#202020"
separator = "#c0c0c0"
shadow = { color = "#00000055", offset = [0, 4], blur = 12 }
# Without the pictures below, the controls are drawn in these colours, with
# accent for what's switched on.
accent = "#3a6ea5"
# Optional pictures. close has normal, hover and pressed. grid_small and
# grid_large stand at the ends of the spacing slider. checkbox and radio have
# off, off_pressed, on and on_pressed. popup has normal and pressed, with the
# slice its corners keep (top, right, bottom, left) and where its text starts.
# slider has a track and its slice, a thumb that points down, thumb_pressed
# and a tick.
close = { normal = "images/close.png", hover = "images/close-hover.png", pressed = "images/close-pressed.png" }
grid_small = "images/grid-small.png"
grid_large = "images/grid-large.png"
checkbox = { off = "images/checkbox-off.png", off_pressed = "images/checkbox-off-pressed.png", on = "images/checkbox-on.png", on_pressed = "images/checkbox-on-pressed.png" }
radio = { off = "images/radio-off.png", off_pressed = "images/radio-off-pressed.png", on = "images/radio-on.png", on_pressed = "images/radio-on-pressed.png" }
popup = { normal = "images/popup.png", pressed = "images/popup-pressed.png", slice = [6, 15, 6, 8], text_inset = 10 }
slider = { track = "images/slider-track.png", track_slice = [2, 3, 2, 3], thumb = "images/slider-thumb.png", thumb_pressed = "images/slider-thumb-pressed.png", tick = "images/slider-tick.png" }
```
