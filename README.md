# NetHack Godot

A cross-platform Godot 4 frontend for NetHack. The game core runs unchanged
inside a GDExtension. Godot draws the map in 2D (TileMap) or 3D, and provides
the HUD, menus and localisation.

| Pinned component | Version |
|---|---|
| NetHack (submodule `thirdparty/NetHack`) | `NetHack-5.0.0_Released` (`16ff591`) |
| godot-cpp (submodule `thirdparty/godot-cpp`) | `10.0.0-stable` (`507ed9d`) |
| Godot Engine / export templates | 4.7.2-stable |
| SCons | 4.11.1 |
| Noto Sans CJK (fetched) | `Sans2.004` |

## Building

You need Python 3, SCons (`pip install scons==4.11.1`), a C/C++ compiler and
Godot 4.7.2. Clone the repository with submodules:

```sh
git clone --recurse-submodules <repo-url>
# or, in an existing clone:
git submodule update --init --recursive
```

### Linux (x86_64)

```sh
sudo apt-get install build-essential
scons platform=linux target=template_debug tests=yes
scons platform=linux target=template_release
```

### Windows (x86_64)

Install Visual Studio 2022 Build Tools (the "Desktop development with C++"
workload). Then run this from a "x64 Native Tools" prompt:

```bat
scons platform=windows target=template_debug tests=yes
scons platform=windows target=template_release
```

### macOS (universal: x86_64 + arm64)

```sh
xcode-select --install
scons platform=macos arch=universal target=template_debug tests=yes
scons platform=macos arch=universal target=template_release
```

### What each build produces

| Output | Path |
|---|---|
| GDExtension library | `godot/bin/` |
| Native test program | `build/<platform>/nethack_native_test` |
| Staged NetHack data files and `manifest.txt` | `godot/nethack_data/` |

### Generated assets

The tilesets and the CJK font are generated or fetched, not committed:

```sh
build/<platform>/nethack_native_test --dump-glyphs build/GLYPHS.tsv
python3 tools/make_tileset.py build/GLYPHS.tsv godot/assets/tiles
python3 tools/convert_share_tiles.py build/GLYPHS.tsv thirdparty/NetHack/win/share godot/assets/tiles
python3 tools/fetch_fonts.py
```

Open `godot/project.godot` in Godot 4.7.2 and press Play. To export, install
the 4.7.2 export templates and use the presets in `godot/export_presets.cfg`
("Windows Desktop", "macOS", "Linux"):

```sh
godot --headless --path godot --export-release "Linux" export/linux/NetHackGodot.x86_64
```

CI (`.github/workflows/ci.yml`) runs all of these steps on each OS. It
uploads the Windows x86_64, macOS universal and Linux x86_64 builds as
workflow artifacts.

## Architecture

```
NetHack core (C, unchanged)
   │ window_procs
native/port/        nhgodot windowport: every window_procs entry
   │                 (map glyphs, messages, menus, yn, getlin, status, ...)
native/bridge/      worker thread running the game loop, a thread-safe input
   │                 queue (keys, mouse clicks via nh_poskey) and an event queue
native/src/         NetHackGame (GDExtension Node): start/stop, send_key,
   │                 send_click, get_glyph_table, events polled per frame
godot/scripts/core/ GameSession: one shared model (map cells, messages,
   │                 status, inventory), save data staged under user://
godot/scripts/game/ MapView2D (TileMap) and MapView3D, which both read the
   │                 same model and can be switched at runtime; InputMapper
godot/scripts/ui/   HUD (message log, status, conditions, inventory),
                     dialogs (menus, yes/no, text entry), main menu, options
```

- **Threading.** NetHack blocks waiting for input on its own worker thread.
  Godot never blocks: it pushes input into the queue and drains events on
  every frame.
- **Saving.** Saves, bones, the score files and options live in
  `user://nethack/`. The data files are copied there from
  `res://nethack_data/` on first run.
- **Translation.** Every message from the core passes through a single hook,
  `GameSession.core_text()`, which calls the native `core_text`. UI strings
  use `tr()`.

## Controls

| Input | Action |
|---|---|
| `h j k l y u b n` (shift to run) | vi-keys movement |
| Numpad 1–9 | Movement (numpad layout) |
| Arrow keys | Orthogonal movement |
| Left-click on the map | Travel to a position (`nh_poskey`) |
| Right-click on the map | Inspect a position (`nh_poskey`) |
| Mouse wheel | Zoom |
| F1 | Help |
| F2 | Toggle the 2D/3D renderer (also under Options) |
| F3 | Switch the 3D camera mode |
| F4 | Inventory panel |
| F5 | Message history (scroll-back) |
| Esc / F10 | Pause menu (save, options, quit) |

All other keys go to NetHack unchanged, so its normal command set works.

## Languages

English, Traditional Chinese (`zh_TW`), Simplified Chinese (`zh_CN`), Korean
(`ko`) and Japanese (`ja`) are included. You can change the language from the
main menu or Options. The change applies straight away and is saved.
CJK text falls back to Noto Sans CJK.

### Adding a language

1. Add a column for the new locale code to `tools/locale_table.txt`.
2. Run `python3 tools/build_locale.py`. It writes `godot/locale/<code>.po`.
3. Add `res://locale/<code>.po` to `internationalization/locale/translations`
   in `godot/project.godot`.
4. Add the code to `Settings.LOCALES` in `godot/scripts/core/settings.gd`.
5. Run `python3 tools/check_translations.py`. It fails if any key is missing.

If the new script is not covered by Noto Sans CJK, add a fallback font with
a compatible licence and record it in `assets/LICENSES.md`.

## Tests

| Test | Command |
|---|---|
| Native, headless, fixed seed and scripted keys (plays, saves, then restores) | `python3 tools/stage_gamedir.py godot/nethack_data build/nhtest`<br>`build/<platform>/nethack_native_test --play build/nhtest --seed 42`<br>`build/<platform>/nethack_native_test --play build/nhtest --seed 42 --expect-restore` |
| Glyph → tile coverage | `python3 tools/check_glyph_coverage.py build/GLYPHS.tsv godot/assets/tiles` |
| Translation coverage | `python3 tools/check_translations.py` |
| Godot-side checks (translations, CJK font, atlases) | `godot --headless --path godot --import`<br>`godot --headless --path godot --script res://tests/run_tests.gd` |

## Licensing

The combined program links the NetHack core, so it is distributed under the
NetHack General Public License (`thirdparty/NetHack/dat/license`). godot-cpp
and Godot are MIT. Every asset, with its source and licence, is listed in
[`assets/LICENSES.md`](assets/LICENSES.md). The original tileset and 3D
materials are CC0. The classic tileset is converted from NetHack's
`win/share` tiles (NGPL). The CJK font is under the SIL OFL 1.1.
