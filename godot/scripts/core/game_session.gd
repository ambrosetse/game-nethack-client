extends Node
## Owns the running NetHack game (autoloaded as "Session").
##
## * stages the core's data files into user://nethack and hands NetHack the
##   resolved OS path, so saves, bones and the record live in user data;
## * creates the native NetHackGame node (looked up dynamically so scripts
##   still parse when the extension is missing);
## * feeds the shared GameModel that both map renderers and the HUD read;
## * re-emits blocking input requests for the dialogs.
##
## Every string the core produces for the player passes through `core_text()`
## - the single hook where core messages could later be translated.

const NhConst = preload("res://scripts/core/nh_const.gd")
const GameModel = preload("res://scripts/core/game_model.gd")

const DATA_SRC := "res://nethack_data"
const GAME_DIR := "user://nethack"

## A blocking request from the core (EV_REQ_*): show the matching dialog.
signal request(ev: Dictionary)
## A text window to show (help, discoveries, tombstone ...).  When
## `needs_key` is true, call `dismiss_text()` once the player closes it.
signal text_window(title: String, text: String, kind: String, needs_key: bool)
## The core asked to show the message history (^P).
signal show_history
signal game_ended(status: int)
signal started

var model := GameModel.new()
## Set by the main menu; the game scene starts it in _ready.
var next_config := {}
var game: Node = null
## Per-glyph tables from the core: {count, category, name, symname, ...}.
var glyph_table := {}
## The request currently waiting for an answer, or {} when none.
var pending_request := {}
var finished := false

## text windows shown that still wait for dismiss_text()
var _text_open := 0
## text windows closed before the core's key request arrived
var _text_dismiss_pending := 0
var _outrip_winid := -1
var _window_types := {}


static func extension_available() -> bool:
	return ClassDB.class_exists("NetHackGame")


func is_running() -> bool:
	return game != null and game.call("is_running")


## True when the process already ran a game (the core allows one per run).
func used() -> bool:
	return game != null and game.call("has_started")


## Absolute OS path of the game directory (saves, record, bones).
func game_dir_path() -> String:
	return ProjectSettings.globalize_path(GAME_DIR)


func has_save() -> bool:
	var dir := DirAccess.open(GAME_DIR + "/save")
	if dir == null:
		return false
	for f in dir.get_files():
		if not f.begins_with("."):
			return true
	return false


## Copy the core's data files (listed in nethack_data/manifest.txt) to
## user://nethack.  The score files are created empty and never replaced.
func stage_data() -> bool:
	DirAccess.make_dir_recursive_absolute(GAME_DIR + "/save")
	var manifest := FileAccess.open(DATA_SRC + "/manifest.txt", FileAccess.READ)
	if manifest == null:
		push_error("NetHack data missing: %s/manifest.txt" % DATA_SRC)
		return false
	while not manifest.eof_reached():
		var name := manifest.get_line().strip_edges()
		if name.is_empty() or name.begins_with("#"):
			continue
		var bytes := FileAccess.get_file_as_bytes(DATA_SRC + "/" + name)
		var out := FileAccess.open(GAME_DIR + "/" + name, FileAccess.WRITE)
		if out == null:
			push_error("cannot write %s/%s" % [GAME_DIR, name])
			return false
		out.store_buffer(bytes)
	for keep in ["record", "logfile", "xlogfile", "perm", "livelog"]:
		var p: String = GAME_DIR + "/" + keep
		if not FileAccess.file_exists(p):
			FileAccess.open(p, FileAccess.WRITE)
	return true


## Create the native game node (once).  Returns false without the extension.
func ensure_game() -> bool:
	if game != null:
		return true
	if not extension_available():
		push_error("NetHackGame extension not loaded (build native/ first)")
		return false
	game = ClassDB.instantiate("NetHackGame")
	game.name = "NetHackGame"
	add_child(game)
	# map_glyph/message/status_changed are typed shortcuts of nh_event; the
	# model reads nh_event because it also carries bkglyph and condmask.
	game.connect("nh_event", _on_event)
	game.connect("input_requested", _on_request)
	game.connect("game_over", _on_game_over)
	glyph_table = game.call("get_glyph_table")
	return true


## Start a game.  config: {name, role, race, gender, align, seed?}.
## If this process already ran a game, the process restarts and starts the
## new game on boot (see Settings.pending_start / main_menu.gd).
func start(config: Dictionary) -> bool:
	if used():
		restart_into(config)
		return false
	if not ensure_game() or not stage_data():
		return false
	model.reset()
	finished = false
	var cfg := config.duplicate()
	cfg["game_dir"] = game_dir_path()
	for k in ["role", "race", "gender", "align"]:
		if cfg.get(k, "random") == "random":
			cfg.erase(k)
	cfg["options"] = "color,!splash_screen,time,showexp,number_pad:%d" \
		% (1 if Settings.number_pad else 0)
	if not game.call("start_game", cfg):
		return false
	started.emit()
	return true


## Restart the application and start `config` right after boot.
func restart_into(config: Dictionary) -> void:
	Settings.set_value("pending_start", config)
	OS.set_restart_on_exit(true, OS.get_cmdline_args())
	get_tree().quit()


## Save the game and let the core end it (the save lands in user://).
func save_and_quit() -> void:
	if is_running():
		game.call("request_save_quit")


func _exit_tree() -> void:
	if is_running():
		game.call("shutdown", 3000)


# --- input helpers used by dialogs and the input mapper -----------------

func send_key(key: int) -> void:
	if is_running():
		pending_request = {}
		game.call("send_key", key)


func send_click(x: int, y: int, mod: int) -> void:
	if is_running():
		pending_request = {}
		game.call("send_click", x, y, mod)


func send_text(text: String, cancelled: bool) -> void:
	if is_running():
		pending_request = {}
		game.call("send_text", text, cancelled)


func send_menu(indices: PackedInt32Array, counts: PackedInt64Array, cancelled: bool) -> void:
	if is_running():
		pending_request = {}
		game.call("send_menu", indices, counts, cancelled)


func send_extcmd(index: int) -> void:
	if is_running():
		pending_request = {}
		game.call("send_extcmd", index)


## The player closed a text window that the core is waiting on.
func dismiss_text() -> void:
	_text_open = maxi(_text_open - 1, 0)
	if pending_request.get("_text", false):
		send_key(27)
	else:
		_text_dismiss_pending += 1


func waiting_for_command() -> bool:
	return not pending_request.is_empty() and pending_request.get("type") == NhConst.EV_REQ_KEY


## The single hook every core-produced string passes through.
func core_text(text: String) -> String:
	return game.call("core_text", text) if game != null else text


# --- event handling -------------------------------------------------------

func _on_event(ev: Dictionary) -> void:
	match int(ev.get("type", 0)):
		NhConst.EV_PRINT_GLYPH:
			model.set_glyph(ev.b, ev.c, ev.d, ev.e, ev.get("i", -1))
		NhConst.EV_VISION:
			model.set_vision(ev.a, ev.b, ev.c, ev.d, ev.get("s", ""))
		NhConst.EV_CLIPAROUND:
			model.cursor = Vector2i(ev.a, ev.b)
		NhConst.EV_CLEAR_WINDOW:
			if _is_map_window(ev.a):
				model.reset()
		NhConst.EV_STATUS_UPDATE:
			model.set_status(ev.a, core_text(ev.get("s", "")), ev.d, ev.c, ev.get("e", 0))
		NhConst.EV_INVENTORY:
			model.set_inventory(ev.get("items", []))
		NhConst.EV_NUMBER_PAD:
			model.number_pad = int(ev.a) != 0
		NhConst.EV_PUTSTR:
			if int(ev.get("c", 0)) == NhConst.NHW_MESSAGE:
				model.add_message(core_text(ev.get("s", "")), ev.b)
		NhConst.EV_RAW_PRINT:
			model.add_message(core_text(ev.get("s", "")), 1 if ev.a else 0)
		NhConst.EV_MSG_HISTORY:
			model.add_message(core_text(ev.get("s", "")), 0)
		NhConst.EV_CREATE_WINDOW:
			_window_types[int(ev.a)] = int(ev.b)
		NhConst.EV_OUTRIP:
			_outrip_winid = int(ev.a)
		NhConst.EV_DISPLAY_WINDOW:
			_display_window(ev)
		NhConst.EV_DISPLAY_FILE:
			var needs := int(ev.get("c", 0)) == 1
			_show_text(ev.get("s2", ""), ev.get("s", ""), "file", needs)
		NhConst.EV_PREV_MESSAGE:
			show_history.emit()


func _show_text(title: String, text: String, kind: String, needs: bool) -> void:
	if needs:
		_text_open += 1
	text_window.emit(title, core_text(text), kind, needs)


func _is_map_window(winid: int) -> bool:
	return _window_types.get(winid, -1) == NhConst.NHW_MAP


func _display_window(ev: Dictionary) -> void:
	var text: String = ev.get("s", "")
	var needs := int(ev.get("c", 0)) == 1
	if text.strip_edges().is_empty():
		if needs:
			_text_dismiss_pending += 1
		return
	var kind := "tombstone" if int(ev.a) == _outrip_winid else "text"
	_show_text("", text, kind, needs)


func _on_request(ev: Dictionary) -> void:
	pending_request = ev
	var plain_key := int(ev.type) == NhConst.EV_REQ_KEY and int(ev.get("a", 0)) == 0
	if plain_key and _text_dismiss_pending > 0:
		# The text window this key waits for was already closed.
		_text_dismiss_pending -= 1
		send_key(27)
		return
	if plain_key and _text_open > 0:
		# The key only dismisses an open text window; dismiss_text() answers.
		ev["_text"] = true
		return
	request.emit(ev)


func _on_game_over(status: int) -> void:
	finished = true
	pending_request = {}
	game_ended.emit(status)
