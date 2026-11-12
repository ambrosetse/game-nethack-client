# Asset sources and licences

The project as a whole links the NetHack game core, so the combined program
is distributed under the **NetHack General Public License** (NGPL, see
`thirdparty/NetHack/dat/license`).  Every asset listed here is either
original to this project or under a licence that allows redistribution
together with NGPL code.

| Asset | Where it lives | Source | Licence |
|---|---|---|---|
| Original 2D tileset (`tiles.png`, `glyph_map.json`) | generated into `godot/assets/tiles/` | Drawn procedurally by `tools/make_tileset.py` from the glyph table dumped by `nethack_native_test --dump-glyphs`.  No third-party artwork is used. | CC0 1.0 (public domain dedication) |
| Classic 2D tileset (`tiles_classic.png`, `glyph_map_classic.json`) | generated into `godot/assets/tiles/` | Converted by `tools/convert_share_tiles.py` from NetHack's `win/share/monsters.txt`, `objects.txt` and `other.txt` (the pinned submodule). | NGPL (same as NetHack) |
| 3D materials (`wall`, `floor`, `corridor`, `door`, `door_open`, `stairs`, `water`, `lava`, `tree`) | `godot/assets/3d/*.tres` | Hand-written Godot `StandardMaterial3D` resources using flat colours only; meshes are Godot primitives created in `map_view_3d.gd`. | CC0 1.0 |
| Application icon | `godot/assets/icons/app_icon.svg` | Hand-written SVG, original to this project. | CC0 1.0 |
| UI theme | `godot/assets/ui/theme.tres` | Hand-written `StyleBoxFlat` theme. | CC0 1.0 |
| CJK UI font (Noto Sans CJK SC Regular) | fetched into `godot/assets/fonts/` by `tools/fetch_fonts.py` | `notofonts/noto-cjk`, tag `Sans2.004`, checksum-verified; the licence text is fetched next to it as `godot/assets/fonts/LICENSE`. | SIL Open Font License 1.1 |
| NetHack data files (`nhdat`, Lua level files, `sysconf`) | staged into `godot/nethack_data/` by SCons | NetHack submodule (`NetHack-5.0.0_Released`) and `native/data/sysconf`. | NGPL |

## Third-party code

| Component | Version | Licence |
|---|---|---|
| NetHack | `NetHack-5.0.0_Released` (`16ff591`) | NGPL |
| Lua (bundled with NetHack) | NetHack's `submodules/lua` | MIT |
| godot-cpp | 10.0.0-stable (`507ed9d`) | MIT |
| Godot Engine (runtime, export templates) | 4.7.2-stable | MIT |

## Regenerating the generated assets

```sh
build/<platform>/nethack_native_test --dump-glyphs build/GLYPHS.tsv
python3 tools/make_tileset.py build/GLYPHS.tsv godot/assets/tiles
python3 tools/convert_share_tiles.py build/GLYPHS.tsv thirdparty/NetHack/win/share godot/assets/tiles
python3 tools/check_glyph_coverage.py build/GLYPHS.tsv godot/assets/tiles
python3 tools/fetch_fonts.py
```

Anyone adding artwork must add a row above with its source and licence;
only CC0, CC-BY, OFL, MIT/BSD or NGPL material is accepted.
