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
## 3. glyph -> tile coverage: when the GDExtension is loaded, every tile atlas
##    map every glyph of the core's glyph table to a cell inside the atlas;
## 4. player-name cleaning (see _check_player_name);
## 5. save-file matching for Continue (see _check_save_names);
## 6. core-text table: translated rows and the English fallback;
## 7. the game scene, with a stand-in for the native game (fake_game.gd):
##    dialogs stay centred (within 4px) before and after a window resize,
##    the map is visible with its camera current after the first map draw
##    (no F2), keys reach Session.send_key with the HUD present, and map
##    clicks step or travel;
## 8. y/n questions: buttons from the core's choices or the prompt, and the
##    keyboard only restricted by the core's own choices.
##
## Exit status 0 = pass.

const LOCALES := ["en", "zh_TW", "zh_CN", "ko", "ja"]
const CJK_FONT := "res://assets/fonts/NotoSansCJKsc-Regular.otf"
const TileAtlas = preload("res://scripts/game/tile_atlas.gd")
const NhConst = preload("res://scripts/core/nh_const.gd")
const CoreText = preload("res://scripts/core/core_text.gd")
const FakeGame = preload("res://tests/fake_game.gd")
# loaded at run time: dialogs.gd uses the Session autoload
const DIALOGS := "res://scripts/ui/dialogs.gd"
const GAME_ROOT := "res://scripts/game/game_root.gd"

var failures := 0


## Runs after the autoloads (Session, Settings) are in the tree; the scene
## checks need frames, so the run is a coroutine.
func _initialize() -> void:
	_run()


func _run() -> void:
	var translations := _check_translations()
	_check_font(translations)
	_check_glyph_coverage()
	_check_player_name()
	_check_save_names()
	_check_core_text()
	_check_yn_choices()
	await _check_game_scene()
	print("PASSED" if failures == 0 else "FAILED (%d)" % failures)
	quit(1 if failures else 0)


## 6. core text: table rows translate, everything else stays English.
func _check_core_text() -> void:
	var cases := [
		# [text, lang, expected]
		["Who are you?", "zh_TW", "你是誰？"],
		["Who are you?", "ja", "あなたは誰ですか？"],
		["female", "ko", "여성"],
		["Who are you?", "en", "Who are you?"],
		["Who are you?", "fr", "Who are you?"],
		["You hear the footsteps of a guard on patrol.", "zh_TW",
				"You hear the footsteps of a guard on patrol."],
		["", "ja", ""],
	]
	for c in cases:
		var got := CoreText.translate(c[0], c[1])
		if got != c[2]:
			_fail("CoreText.translate(%s, %s) = %s, want %s" % [c[0], c[1], got, c[2]])
	print("core text: %d cases" % cases.size())


## 8. y/n questions: the buttons offered and the keys the keyboard accepts.
func _check_yn_choices() -> void:
	var Dialogs := load(DIALOGS) as GDScript
	if Dialogs == null:
		_fail("cannot load %s" % DIALOGS)
		return
	var cases := [
		# [prompt, s2, expected buttons]
		["Really quit?", "ynq", "ynq"],
		["Shall I pick a character for you? [ynaq]", "", "ynaq"],
		["What do you want to throw? [a-c or ?*]", "", "abc?*"],
		["What do you want to eat? [fg or ?*]", "", "fg?*"],
		["What do you want to wield? [- ab or ?*]", "", ""],
		["Call it:", "", ""],
		["Continue?", "yn\u001bq", "yn"],
	]
	for c in cases:
		var got: String = Dialogs.yn_choices(c[0], c[1])
		if got != c[2]:
			_fail("yn_choices(%s, %s) = %s, want %s" % [c[0], c[1].c_escape(), got, c[2]])
	var keys := [
		# [s2, key, accepted]
		["ynq", "y", true],
		["ynq", "x", false],
		["ynq", "?", false],
		["", "?", true],
		["", "*", true],
		["", "3", true],
		["", "z", true],
	]
	for k in keys:
		if Dialogs.yn_accepts(k[0], k[1]) != k[2]:
			_fail("yn_accepts(%s, %s) != %s" % [k[0], k[1], k[2]])
	print("y/n choices: %d cases" % (cases.size() + keys.size()))


## 7. the game scene with a fake native game.
func _check_game_scene() -> void:
	var session: Node = root.get_node_or_null("Session")
	var settings: Node = root.get_node_or_null("Settings")
	if session == null or settings == null:
		_fail("Session/Settings autoloads missing")
		return
	var fake := FakeGame.new()
	var saved_game: Node = session.game
	session.game = fake
	settings.renderer = "2d"
	settings.number_pad = false
	var model = session.model
	model.number_pad = false
	model.reset()
	var gr: Node = (load(GAME_ROOT) as GDScript).new()
	root.add_child(gr)
	await process_frame
	# without the extension the scene reports it; the checks need it closed
	gr._error.visible = false
	await _check_dialog_centring(gr, session)
	await _check_first_draw(gr, model)
	await _check_keys(gr, session, fake)
	await _check_yn_keys(gr, session, fake)
	await _check_clicks(gr, session, fake)
	gr.queue_free()
	await process_frame
	session.game = saved_game
	session.pending_request = {}
	fake.free()


func _centre_error(c: Control) -> float:
	var r := c.get_global_rect()
	var vp := c.get_viewport_rect()
	return (r.get_center() - vp.get_center()).length()


func _check_dialog_centring(gr: Node, session: Node) -> void:
	session.request.emit({"type": NhConst.EV_REQ_YN, "s": "Really quit?", "s2": "ynq", "a": 0})
	await process_frame
	await process_frame
	var dlg = gr.dialogs
	if not dlg.is_open():
		_fail("y/n request did not open a dialog")
		return
	var buttons: Array = dlg.panel().find_children("*", "Button", true, false)
	if buttons.size() != 3:
		_fail("y/n dialog for [ynq] has %d buttons, want 3" % buttons.size())
	var panels: Array = [dlg.panel()]
	for p in gr.overlay_panels():
		if p != null:
			panels.append(p)
	for p in panels:
		p.visible = true
	await process_frame
	await process_frame
	for p in panels:
		var e := _centre_error(p)
		if e > 4.0:
			_fail("%s is %.1fpx off centre" % [p.name, e])
	var old := root.size
	root.size = Vector2i(1600, 700)
	await process_frame
	await process_frame
	await process_frame
	if root.size != Vector2i(1600, 700):
		print("dialog centring: window cannot be resized here; resize check skipped")
	else:
		for p in panels:
			var e := _centre_error(p)
			if e > 4.0:
				_fail("%s is %.1fpx off centre after a resize" % [p.name, e])
	root.size = old
	for p in panels:
		if p != dlg.panel():
			p.visible = false
	# answer the dialog so it closes (no Session request is pending)
	dlg._panel.visible = false
	await process_frame
	print("dialog centring: %d panels" % panels.size())


func _check_first_draw(gr: Node, model) -> void:
	var view: Node2D = gr.view_2d
	model.set_vision(model.width, model.height, 10, 5, "")
	# the first glyph the atlas can draw
	var g := 0
	while gr.atlas.cell_of(g) < 0 and g < gr.atlas.cells.size():
		g += 1
	model.set_glyph(10, 5, g, 7, g)
	model.set_glyph(11, 5, g, 7, g)
	await process_frame
	if not view.has_drawn():
		_fail("map view did not draw the first glyph")
	if not view.is_visible_in_tree():
		_fail("map not visible after the first draw")
	if not view.camera().is_current():
		_fail("map camera is not current after the first draw")
	var want: Vector2 = (Vector2(10, 5) + Vector2(0.5, 0.5)) * gr.atlas.tile_size
	if view.camera().position.distance_to(want) > 1.0:
		_fail("camera not centred on the hero: %s, want %s" % [view.camera().position, want])
	print("first draw: map visible, camera current")


func _key_event(keycode: int, unicode: int) -> InputEventKey:
	var k := InputEventKey.new()
	k.keycode = keycode
	k.unicode = unicode
	k.pressed = true
	return k


func _check_keys(gr: Node, session: Node, fake) -> void:
	if gr.hud == null or not gr.hud.is_inside_tree():
		_fail("HUD missing from the game scene")
	var cases := [
		# [keycode, unicode, expected key]
		[KEY_H, "h".unicode_at(0), "h".unicode_at(0)],
		[KEY_LEFT, 0, "h".unicode_at(0)],
		[KEY_KP_8, 0, "k".unicode_at(0)],
		[KEY_S, "s".unicode_at(0), "s".unicode_at(0)],
		[KEY_PERIOD, ".".unicode_at(0), ".".unicode_at(0)],
	]
	for c in cases:
		fake.keys.clear()
		session.pending_request = {"type": NhConst.EV_REQ_KEY, "a": 1}
		root.push_input(_key_event(c[0], c[1]))
		await process_frame
		if fake.keys != [c[2]]:
			_fail("key %s reached send_key as %s, want [%d]"
					% [OS.get_keycode_string(c[0]), fake.keys, c[2]])
	var focused := root.gui_get_focus_owner()
	if focused != null:
		_fail("%s keeps keyboard focus" % focused.name)
	session.pending_request = {}
	print("keys: %d cases reach Session.send_key" % cases.size())


## Keys typed into an open y/n dialog: getobj prompts (no choice string) take
## any key, a [yn] question only its own answers (Escape always cancels).
func _check_yn_keys(gr: Node, session: Node, fake) -> void:
	var cases := [
		# [prompt, s2, keycode, unicode, expected keys]
		["What do you want to eat? [fg or ?*]", "", KEY_QUESTION, "?".unicode_at(0), ["?".unicode_at(0)]],
		["What do you want to eat? [fg or ?*]", "", KEY_ASTERISK, "*".unicode_at(0), ["*".unicode_at(0)]],
		["What do you want to eat? [fg or ?*]", "", KEY_2, "2".unicode_at(0), ["2".unicode_at(0)]],
		["What do you want to eat? [fg or ?*]", "", KEY_Z, "z".unicode_at(0), ["z".unicode_at(0)]],
		["Really quit?", "yn", KEY_X, "x".unicode_at(0), []],
		["Really quit?", "yn", KEY_Y, "y".unicode_at(0), ["y".unicode_at(0)]],
	]
	for c in cases:
		fake.keys.clear()
		session.request.emit({"type": NhConst.EV_REQ_YN, "s": c[0], "s2": c[1], "a": 0})
		await process_frame
		if not gr.dialogs.is_open():
			_fail("y/n request %s did not open a dialog" % c[0])
			continue
		root.push_input(_key_event(c[2], c[3]))
		await process_frame
		if fake.keys != c[4]:
			_fail("key %s in y/n %s [%s] sent %s, want %s" % [String.chr(c[3]), c[0], c[1], fake.keys, c[4]])
		if gr.dialogs.is_open():
			root.push_input(_key_event(KEY_ESCAPE, 0))
			await process_frame
	session.pending_request = {}
	print("y/n keys: %d cases" % cases.size())


func _check_clicks(gr: Node, session: Node, fake) -> void:
	var view: Node2D = gr.view_2d
	var tile: float = gr.atlas.tile_size
	var cases := [
		# [cell, button, expected keys, expected clicks]
		[Vector2i(11, 5), MOUSE_BUTTON_LEFT, ["l".unicode_at(0)], []],
		[Vector2i(9, 6), MOUSE_BUTTON_LEFT, ["b".unicode_at(0)], []],
		[Vector2i(20, 8), MOUSE_BUTTON_LEFT, [], [[20, 8, NhConst.CLICK_TRAVEL]]],
		[Vector2i(20, 8), MOUSE_BUTTON_RIGHT, [], [[20, 8, NhConst.CLICK_INSPECT]]],
	]
	# The headless display server drops pushed mouse events before they reach
	# _unhandled_input, so the event goes to the map view's handler directly;
	# the screen -> cell conversion and the mapper are still exercised.
	await process_frame
	for c in cases:
		fake.keys.clear()
		fake.clicks.clear()
		session.pending_request = {"type": NhConst.EV_REQ_KEY, "a": 1}
		var local := (Vector2(c[0]) + Vector2(0.5, 0.5)) * tile
		var screen: Vector2 = view.get_global_transform_with_canvas() * local
		var m := InputEventMouseButton.new()
		m.button_index = c[1]
		m.pressed = true
		m.position = screen
		m.global_position = screen
		view._unhandled_input(m)
		await process_frame
		if fake.keys != c[2] or fake.clicks != c[3]:
			_fail("click %s on %s sent keys %s clicks %s, want %s %s"
					% [c[1], c[0], fake.keys, fake.clicks, c[2], c[3]])
	# without a position-capable key request the travel key goes first and the
	# click answers the position prompt it opens
	fake.keys.clear()
	fake.clicks.clear()
	session.pending_request = {"type": NhConst.EV_REQ_KEY, "a": 0}
	gr.mapper.on_map_clicked(Vector2i(20, 8), MOUSE_BUTTON_LEFT)
	session.request.emit({"type": NhConst.EV_REQ_KEY, "a": 1})
	if fake.keys != ["_".unicode_at(0)] or fake.clicks != [[20, 8, NhConst.CLICK_TRAVEL]]:
		_fail("travel via '_' sent keys %s clicks %s" % [fake.keys, fake.clicks])
	session.pending_request = {}
	print("clicks: %d cases" % (cases.size() + 1))


## 4. player names: ',' and ':' never reach NETHACKOPTIONS (same cases as
##    native_test.cpp's check of nhb::clean_player_name).
func _check_player_name() -> void:
	var cases := {
		"tester": "tester",
		"Bob,!autopickup": "Bob!autopickup",
		"x,windowtype:tty": "xwindowtypetty",
		"  a\tb\n ": "ab",
		",:": "",
	}
	for k in cases:
		var got := NhConst.clean_player_name(k)
		if got != cases[k]:
			_fail("clean_player_name(%s) = %s, want %s" % [k, got, cases[k]])


## 5. Continue: a save only counts for the player name NetHack restores it
##    for (set_savefile_name() in src/files.c, Unix and Windows forms).
func _check_save_names() -> void:
	var cases := [
		# [file, name, expected]
		["1000A", "A", true],
		["1000A", "B", false],
		["1000AB", "B", false],
		["1000B", "B", true],
		["A", "A", false],
		["x1000A", "A", false],
		["1000Mr_X", "Mr X", true],
		["1000a_b", "a.b", true],
		["1000Bob", "Bob-Val-hum", true],
		["1000Bob", "Bob,", true],
		["1000Bob", "", false],
		["A.NetHack-saved-game", "A", true],
		["A.NetHack-saved-game", "B", false],
		["Mr%20X.NetHack-saved-game", "Mr X", true],
		["p1.NetHack-saved-game", "p1", true],
		["%FF%FFFFFFA9.NetHack-saved-game", "é", true],
		["%FF%FFa.NetHack-saved-game", "éa", true],
		["%FFFFFFC3%FFFFFFA9.NetHack-saved-game", "é", false],
		["Bob.NetHack-saved-game", "Bob-Val", true],
		# ZLIB_COMP builds: docompress_file() appends ".gz"
		["1000A.gz", "A", true],
		["1000A.gz", "B", false],
		["1000Mr_X.gz", "Mr X", true],
		["1000a_gz.gz", "a.gz", true],
		["1000a_gz", "a.gz", true],
		["A.gz", "A", false],
		[".gz", "A", false],
		["A.NetHack-saved-game.gz", "A", true],
		["A.NetHack-saved-game.gz", "B", false],
		["a.gz.NetHack-saved-game.gz", "a.gz", true],
	]
	for c in cases:
		var got := NhConst.is_save_for(c[0], c[1])
		if got != c[2]:
			_fail("is_save_for(%s, %s) = %s, want %s" % [c[0], c[1], got, c[2]])
	var long := "abcdefghijklmnopqrstuvwxyz0123456789"
	if NhConst.core_player_name(long) != long.substr(0, NhConst.PL_NSIZ - 1):
		_fail("core_player_name does not cut at PL_NSIZ - 1 bytes")


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
	for set_name in TileAtlas.ORDER:
		var atlas = TileAtlas.new()
		if not atlas._load_set(set_name):
			_fail("tileset '%s' missing: run tools/make_tileset.py, " % set_name
					+ "tools/make_illustrated_tileset.py and tools/convert_share_tiles.py")
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
