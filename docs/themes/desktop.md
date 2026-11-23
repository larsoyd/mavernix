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
desktop_icon = { family = "DejaVu Sans", size = 12 }   # the names under the icons

[desktop.icons]
icon_size = 48
# Icons stand in columns, starting at the far side of the screen. Each icon
# starts row_pitch below the one above it, and each column sits column_pitch
# beside the one before. The first icon starts `top` below the panel. The
# middle of the first column is edge_to_icon_middle from the screen's edge.
# bottom_room is kept free at the bottom (for a dock).
row_pitch = 92
column_pitch = 100
top = 12
edge_to_icon_middle = 58
bottom_room = 90
picked = { color = "#ffffff30", radius = 4 }    # any fill: the box around a picked file's icon
picked_reach = 4                                # how far it reaches past the icon
# The name under the icon: its colour, its line height, how far below the
# icon it starts, and how wide a line can get. With lines = 2 a long name
# goes on to a second line at a word, or between letters when one word is
# too long, and that line is cut short in the middle. Left out, it's 1.
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
name = { color = "#ffffff", shadow_color = "#000000c0", height = 18, padding = 5, gap = 4, most_width = 96, lines = 2, picked_background = { color = "#3a6ea5", radius = 4 } }
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
```
