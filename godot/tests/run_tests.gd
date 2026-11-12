extends SceneTree
## Headless GDScript checks, run by CI after the assets are generated:
##
##   godot --headless --path godot --import
##   godot --headless --path godot --script res://tests/run_tests.gd
##
## 1. translation coverage: every key in en.po has a non-empty entry in every
##    other locale, and the project lists every locale file;
## 2. CJK font: the bundled font draws every CJK/Hangul/Kana character used
##    by the translations;
## 3. glyph -> tile coverage: when the GDExtension is loaded, both tile atlases
##    map every glyph of the core's glyph table to a cell inside the atlas.
##
## Exit status 0 = pass.

const LOCALES := ["en", "zh_TW", "zh_CN", "ko", "ja"]
const CJK_FONT := "res://assets/fonts/NotoSansCJKsc-Regular.otf"
const TileAtlas = preload("res://scripts/game/tile_atlas.gd")

var failures := 0


func _init() -> void:
	var translations := _check_translations()
	_check_font(translations)
	_check_glyph_coverage()
	print("PASSED" if failures == 0 else "FAILED (%d)" % failures)
	quit(1 if failures else 0)


func _fail(msg: String) -> void:
	failures += 1
	printerr("FAIL: " + msg)


## Minimal .po reader (single-line msgid/msgstr, as tools/build_locale.py
## writes them).  The imported resources are hashed OptimizedTranslations
## that cannot list their keys, so the source files are read instead.
static func _read_po(path: String) -> Dictionary:
	var out := {}
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return out
	var key := ""
	while not f.eof_reached():
		var line := f.get_line().strip_edges()
		if line.begins_with("msgid "):
			key = line.substr(6).trim_prefix("\"").trim_suffix("\"").c_unescape()
		elif line.begins_with("msgstr ") and not key.is_empty():
			out[key] = line.substr(7).trim_prefix("\"").trim_suffix("\"").c_unescape()
			key = ""
	return out


func _check_translations() -> Array:
	var out := []
	var listed: PackedStringArray = ProjectSettings.get_setting(
			"internationalization/locale/translations", PackedStringArray())
	var by_locale := {}
	for loc in LOCALES:
		var path := "res://locale/%s.po" % loc
		if not path in listed:
			_fail("%s is not listed in project.godot translations" % path)
		if load(path) == null:
			_fail("Godot cannot load " + path)
		var entries := _read_po(path)
		if entries.is_empty():
			_fail("cannot read " + path)
			continue
		by_locale[loc] = entries
		out.append(entries)
	if not by_locale.has("en"):
		return out
	var keys: Array = by_locale["en"].keys()
	if keys.size() < 50:
		_fail("en.po has only %d keys" % keys.size())
	for loc in by_locale:
		var entries: Dictionary = by_locale[loc]
		for key in keys:
			if String(entries.get(key, "")).strip_edges().is_empty():
				_fail("%s: missing translation for %s" % [loc, key])
	print("translations: %d keys x %d locales" % [keys.size(), by_locale.size()])
	return out


func _check_font(translations: Array) -> void:
	var font: Font = null
	if ResourceLoader.exists(CJK_FONT):
		font = load(CJK_FONT) as Font
	elif FileAccess.file_exists(CJK_FONT):
		var ff := FontFile.new()
		if ff.load_dynamic_font(CJK_FONT) == OK:
			font = ff
	if font == null:
		_fail("CJK font missing: run tools/fetch_fonts.py")
		return
	var missing := {}
	var checked := 0
	for entries in translations:
		for text in (entries as Dictionary).values():
			for i in String(text).length():
				var c: int = String(text).unicode_at(i)
				if c < 0x2E80:
					continue
				checked += 1
				if not font.has_char(c):
					missing[c] = true
	for c in missing:
		_fail("CJK font has no glyph for U+%04X" % c)
	print("font: %d CJK characters checked" % checked)


func _check_glyph_coverage() -> void:
	if not ClassDB.class_exists("NetHackGame"):
		print("glyph coverage: GDExtension not loaded, skipped "
				+ "(tools/check_glyph_coverage.py covers the atlases)")
		return
	var game = ClassDB.instantiate("NetHackGame")
	var table: Dictionary = game.get_glyph_table()
	game.free()
	var count := int(table.get("count", 0))
	if count <= 0:
		_fail("empty glyph table")
		return
	for set_name in ["original", "classic"]:
		var atlas = TileAtlas.new()
		if not atlas._load_set(set_name):
			_fail("tileset '%s' missing: run tools/make_tileset.py and "
					% set_name + "tools/convert_share_tiles.py")
			continue
		if atlas.cells.size() < count:
			_fail("%s: %d cells for %d glyphs" % [set_name, atlas.cells.size(), count])
		var grid: Vector2i = atlas.grid_size()
		var bad := 0
		for g in count:
			var a: Vector2i = atlas.atlas_coords(g)
			if a.x < 0 or a.x >= grid.x or a.y < 0 or a.y >= grid.y:
				bad += 1
		if bad:
			_fail("%s: %d glyphs map outside the atlas" % [set_name, bad])
		print("glyph coverage (%s): %d glyphs" % [set_name, count])
