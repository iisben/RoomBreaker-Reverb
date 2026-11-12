# RoomBreaker

A reverb whose room falls apart. Loud, low and sustained audio puts stress on six surfaces (four
walls, ceiling, floor). They crack, loosen and breach. The tail gets rougher, then leaks into
whatever is outside. When things go quiet, the room heals, unless you turn healing off.

VST3 and Standalone on Windows and macOS, plus AU on macOS. Built with JUCE 8.

![RoomBreaker after a hit](docs/images/editor_snapshot.png)

## The interface

One console, read left to right like the signal: input meter and gain, the house, the sixteen sound
controls in four sections (Structure, Destruction, Space, Tone), then Mix, Ducking, Output and the
output meter. The house is the only colourful thing on screen; amber means "what you are touching",
red means damage or overload, and nothing else uses colour.

- **Knobs:** drag up or right (Shift for fine), scroll the wheel (2 % a notch, Shift for 0.2 %),
  double-click to type a value ("120 ms", "4.5k", "-6"), Alt- or Cmd-click to reset, right-click
  for the host's automation items. The value shows under every knob and lights up under the mouse.
- **Presets:** click the name to open the browser. Search by name, tag or description, filter by
  category and tags, and audition with the arrow keys. Save your own with tags; they are plain files
  in `~/Library/Application Support/RoomBreaker/Presets` (`%APPDATA%\RoomBreaker\Presets` on Windows).
  A dot before the name means you have changed something since loading it.
- **Meters:** RMS bar, falling peak and held peak on both sides, with a peak readout that turns red
  after a true over (click to reset).
- **Bypass:** the power button in the header, also the host's bypass; a 20 ms crossfade.
- **Size:** drag the corner or pick 75 % to 200 % from the header; the window keeps its shape and
  stays sharp on Retina and Windows scaling.
- The line along the bottom explains whatever the mouse is over.

![The controls](docs/images/detail_controls.png)

| Preset browser |
|---|
| ![](docs/images/editor_browser.png) |

## The house

The room is drawn as a small cottage on a patch of lawn, and the picture follows the sound:

- **Walls** (North, South, East, West) crack from the window and door corners. Then each material
  fails in its own way: render spalls to bare concrete, tiles and glass panes drop one by one, stone
  leaves stepped edges, timber strips back to its studs. Holes show the wall's thickness and the room
  inside. A wall that is nearly gone collapses from the top.
- **Roof** (the ceiling) slips tiles, shows its battens, opens to the rafters, and the chimney leans
  and breaks off.
- **Lawn** (the floor) splits into trenches, and the house settles off level.
- Fractures throw debris from wherever the wall is failing; rubble piles up against the walls and
  stays until the room heals.
- The light and scenery follow the Outside setting, from open sky to a burning city.

Drag to turn the house and double-click to face it again. Hover a wall to read its integrity and load.

| Intact | Detonated | From behind |
|---|---|---|
| ![](docs/images/editor_pristine.png) | ![](docs/images/editor_detonate.png) | ![](docs/images/editor_back.png) |

Four of the eleven materials, each shown new, part-broken and wrecked:

![Glass atrium and cathedral stone](docs/images/materials_glass_stone.jpg)
![Ceramic tile and corrugated sheet](docs/images/materials_ceramic_silo.jpg)

## Features

- 11 materials (concrete, ceramic, silo …) that set how the room sounds, how it breaks and what the
  walls look like. 7 outside spaces set what you hear and see through a breach.
- Detonate and Rebuild buttons, Freeze, and permadeath.
- Sidechain input, so another track can break the room.
- 1x / 2x / 4x oversampling, which switches without clicks.
- Every control can be automated without clicks.
- A safety limiter and ducking.
- 28 factory presets.
- Notes on the sheet that explain whatever the mouse is over.

## Download

Every push to `main` builds on GitHub Actions. Open the latest run under **Actions → Build** and
download:

- `RoomBreaker-Windows`: `RoomBreaker.vst3`, `RoomBreaker.exe` (standalone) and install notes.
- `RoomBreaker-macOS`: `RoomBreaker.vst3`, `RoomBreaker.component` (AU) and `RoomBreaker.app`.

### Windows install

Copy `RoomBreaker.vst3` to `C:\Program Files\Common Files\VST3\`, then rescan plugins in your DAW.
See [docs/INSTALL-Windows.txt](docs/INSTALL-Windows.txt).

### macOS install

Copy `RoomBreaker.component` to `~/Library/Audio/Plug-Ins/Components/` and `RoomBreaker.vst3` to
`~/Library/Audio/Plug-Ins/VST3/`. The builds are ad-hoc signed, not notarised, so you may need to
clear the quarantine flag:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/RoomBreaker.component ~/Library/Audio/Plug-Ins/VST3/RoomBreaker.vst3
```

## Build from source

Requires CMake 3.22+ and a C++20 compiler (Visual Studio 2022, Xcode 15+ or recent Clang/GCC). JUCE
8.0.4 is fetched automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Plugins land in `build/RoomBreaker_artefacts/Release/`. `-DRB_CORE_ONLY=ON` builds only the
JUCE-free DSP core and its test harness.

## Tests

- `rb_offline_render` drives the DSP core through scripted scenarios. It covers silence,
  fatigue, healing and permadeath, block-size independence and telemetry threading. It also jumps
  every control mid-signal to check for clicks and runs a 40 s parameter fuzz.
- `rb_plugin_smoke` loads the real plugin headless. It covers buses, state, every preset,
  Detonate, Freeze, the limiter, oversampling switches and the sidechain. It also renders the
  editor snapshots above.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the signal flow, engines, parameters and design rules.

## Status

Built and tested on macOS (Apple Silicon). The AU passes `auval -strict`. The Windows build is
verified by CI only. It has not been tested in a DAW on Windows yet, and the plugins are unsigned.
