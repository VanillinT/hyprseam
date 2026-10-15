# hyprseam

Seamless pointer transfer across partially shared monitor edges for Hyprland.

## The problem

When monitors of different sizes share an edge, a strip of that edge is a
*dead end*: pixels where the pushed-toward neighbor simply does not exist on
the other side. Hyprland clamps the pointer to the layout there, so pushing
toward the neighbor does nothing — the pointer hits a hard wall.

```
┌────────────────────┬──────────┐
│                    │ HDMI-A-1 │
│        DP-1        │ 1980x1080│
│      3440x1440     ├──────────┘
│                    │ dead end ✗
└────────────────────┘
```

## What it does

hyprseam hooks `Pointer::CPointerManager::move`. When a *real* pointer motion
is blocked because it ran off the layout, and the pointer is pinned against
an edge that a neighbor partially shares, the pointer is warped to the closest
point on that neighbor — just inside the edge, clamped into the neighbor's
extent. The push then continues naturally on the other side.

- Only regular pointer motion is affected. Programmatic warps (`hyprctl
  movecursor`, tablet input, …) pass through untouched.
- Interception only happens on a dead end of the pushed edge: crossing at a
  spot where the neighbor exists behaves exactly as before.
- Skipped while a pointer constraint (locked/confined pointer, e.g. games) or
  a client input capture session is active.

## Monitor topologies

Any number of monitors of any sizes works — only the local topology at the
pushed edge matters:

- a tall monitor's edge shared by several smaller monitors works: the
  neighbor whose landing point is closest to the push wins;
- monitors left of / above the origin (negative coordinates), mixed scales and
  rotated monitors are fine; everything is logical geometry;
- edges must genuinely align (within half a logical pixel) and overlap: light
  gaps in the layout and monitors that only touch corner-to-corner stay hard
  walls, as does an edge with no neighbor behind it;
- mirrored, disabled and headless monitors are ignored, matching the set
  Hyprland clamps the pointer layout to;
- a non-default `cursor:hotspot_padding` is accounted for: the trigger
  tolerance and the landing inset grow with it, so dead ends still transfer.

## Requirements

Hyprland 0.56.x. The plugin uses a raw trampoline hook, so it must be rebuilt
whenever Hyprland is updated. It refuses to load on an ABI mismatch, and the
helper below rebuilds it with one click when that happens.

## Install

```sh
hyprpm update
hyprpm add https://github.com/VanillinT/hyprseam   # or a local clone of it
hyprpm enable hyprseam
```

## Keeping it loaded at login (Omarchy)

`~/.local/bin/hyprpm-reload-notify` runs `hyprpm reload` at login. If the
plugins cannot load — usually after a Hyprland update — it posts a clickable
notification that opens a terminal and runs `hyprpm update`.

Start it from `~/.config/hypr/autostart.lua`:

```lua
o.exec_on_start(os.getenv("HOME") .. "/.local/bin/hyprpm-reload-notify")
```

## Build

```sh
cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -S . -B ./build
cmake --build ./build --config Release --target all -j"$(nproc)"
```

## Tests

The transfer geometry is pure math and has synthetic-layout tests (stacked
neighbors, dead strips, gaps, corner-only touches, negative coordinates and
padded cursors):

```sh
cmake -S . -B build-tests -DHYPRSEAM_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Tested with

- Hyprland 0.56.2 (`efb50993780079460b0cbed1363e2166a2de1d9f`)
- DP-1 3440x1440@144 at 0x0, HDMI-A-1 1920x1080@60 at 3440x0
