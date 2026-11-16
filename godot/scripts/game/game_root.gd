extends Node
## Root of the game scene: builds the tile atlas, both map renderers (2D and
## 3D, sharing Session.model), the HUD, the dialogs and the input mapper, then
## starts the game chosen in the main menu.
##
## Frontend hotkeys (function keys, so they never clash with NetHack commands):
##   F1 help (NetHack's '?')   F2 switch 2D/3D   F3 3D camera mode
##   F4 inventory panel        F5 message history
##   Esc at the command prompt opens the in-game menu.

const TileAtlas = preload("res://scripts/game/tile_atlas.gd")
const MapView2D = preload("res://scripts/game/map_view_2d.gd")
const MapView3D = preload("res://scripts/game/map_view_3d.gd")
const Hud = preload("res://scripts/ui/hud.gd")
const Dialogs = preload("res://scripts/ui/dialogs.gd")
const InputMapper = preload("res://scripts/game/input_mapper.gd")
const OptionsPanel = preload("res://scripts/ui/options_panel.gd")
const MENU_SCENE := "res://scenes/main_menu.tscn"

var atlas
var view_2d: Node2D
var view_3d: Node3D
var hud: Control
var dialogs: Control
var mapper: Node

var _ui: CanvasLayer
var _overlay: CanvasLayer
var _pause: PanelContainer
var _options: PanelContainer
var _ended: PanelContainer
var _error: AcceptDialog


func _ready() -> void:
	var model = Session.model
	atlas = TileAtlas.load_preferred(Settings.tileset, Session.glyph_table)

	view_2d = MapView2D.new()
	add_child(view_2d)
	view_2d.setup(model, atlas)
	view_3d = MapView3D.new()
	add_child(view_3d)
	view_3d.setup(model, atlas, Session.glyph_table)

	_ui = CanvasLayer.new()
	add_child(_ui)
	hud = Hud.new()
	_ui.add_child(hud)
	hud.setup(model, atlas)

	_overlay = CanvasLayer.new()
	_overlay.layer = 2
	add_child(_overlay)
	dialogs = Dialogs.new()
	_overlay.add_child(dialogs)
	dialogs.setup(atlas)
	_build_pause()
	_build_ended()
	_error = AcceptDialog.new()
	_error.confirmed.connect(_to_menu)
	_error.canceled.connect(_to_menu)
	_overlay.add_child(_error)

	mapper = InputMapper.new()
	add_child(mapper)
	mapper.setup(model)
	mapper.blocked = func(): return _modal_open()
	view_2d.clicked.connect(mapper.on_map_clicked)
	view_3d.clicked.connect(mapper.on_map_clicked)

	Settings.changed.connect(_on_setting)
	Session.show_history.connect(hud.show_history)
	Session.game_ended.connect(_on_game_ended)
	_apply_renderer()

	if not Session.extension_available():
		_fail("ERROR_NO_EXTENSION")
		return
	if not Session.is_running() and not Session.start(Session.next_config):
		if not Session.used():
			_fail("ERROR_START_FAILED")


func _fail(key: String) -> void:
	_error.title = tr("ERROR_TITLE")
	_error.dialog_text = tr(key)
	_error.popup_centered.call_deferred()


func _to_menu() -> void:
	get_tree().change_scene_to_file(MENU_SCENE)


func _modal_open() -> bool:
	return dialogs.is_open() or _pause.visible or _options.visible or _ended.visible


# --- renderers ------------------------------------------------------------

func _apply_renderer() -> void:
	var three := Settings.renderer == "3d"
	view_3d.activate(three)
	view_2d.activate(not three)


func _on_setting(key: String) -> void:
	match key:
		"renderer":
			_apply_renderer()
		"tileset":
			atlas = TileAtlas.load_preferred(Settings.tileset, Session.glyph_table)
			view_2d.set_atlas(atlas)
			view_3d.set_atlas(atlas)
			hud.set_atlas(atlas)
			dialogs.atlas = atlas


# --- overlays -------------------------------------------------------------

func _panel(parent: Node) -> Array:
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	center.mouse_filter = Control.MOUSE_FILTER_IGNORE
	parent.add_child(center)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(320, 0)
	panel.visible = false
	center.add_child(panel)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	panel.add_child(box)
	return [panel, box]


func _add_button(box: Control, key: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = key
	b.pressed.connect(cb)
	box.add_child(b)
	return b


func _build_pause() -> void:
	var p := _panel(_overlay)
	_pause = p[0]
	var box: VBoxContainer = p[1]
	var t := Label.new()
	t.text = "GAME_PAUSED"
	t.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(t)
	_add_button(box, "GAME_RESUME", func(): _pause.visible = false)
	_add_button(box, "MENU_OPTIONS", _open_options)
	_add_button(box, "GAME_HELP", _pause_help)
	_add_button(box, "GAME_SAVE_QUIT", _save_quit)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	center.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_overlay.add_child(center)
	_options = OptionsPanel.new()
	_options.visible = false
	_options.closed.connect(_close_options)
	center.add_child(_options)


func _pause_help() -> void:
	_pause.visible = false
	_send_help()


func _close_options() -> void:
	_options.visible = false
	_pause.visible = true


func _open_options() -> void:
	_pause.visible = false
	_options.visible = true


func _save_quit() -> void:
	_pause.visible = false
	Session.save_and_quit()


func _build_ended() -> void:
	var p := _panel(_overlay)
	_ended = p[0]
	var box: VBoxContainer = p[1]
	var t := Label.new()
	t.text = "GAME_OVER"
	t.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(t)
	_add_button(box, "GAME_BACK_TO_MENU", _to_menu)
	_add_button(box, "MENU_QUIT", func(): get_tree().quit())


func _on_game_ended(_status: int) -> void:
	_pause.visible = false
	_show_ended_when_ready()


## Wait until the tombstone / final text windows are closed.
func _show_ended_when_ready() -> void:
	while dialogs.is_open():
		await get_tree().process_frame
	_ended.visible = true


## True at the top-level command prompt (the core reads commands with
## nh_poskey; direction and other single-key prompts use nhgetch).
static func at_command_prompt() -> bool:
	return Session.waiting_for_command() and int(Session.pending_request.get("a", 0)) == 1 \
		and not Session.pending_request.get("_text", false)


func _send_help() -> void:
	if Session.waiting_for_command():
		Session.send_key("?".unicode_at(0))


# --- hotkeys --------------------------------------------------------------

func _unhandled_key_input(event: InputEvent) -> void:
	var k := event as InputEventKey
	if k == null or not k.pressed or k.echo:
		return
	var handled := true
	match k.keycode:
		KEY_F1:
			if not _modal_open():
				_send_help()
		KEY_F2:
			Settings.set_value("renderer", "2d" if Settings.renderer == "3d" else "3d")
		KEY_F3:
			view_3d.toggle_camera()
		KEY_F4:
			hud.toggle_inventory()
		KEY_F5:
			hud.toggle_history()
		KEY_ESCAPE, KEY_F10:
			if _options.visible:
				handled = false  # the options panel closes itself
			elif _pause.visible:
				_pause.visible = false
			elif not dialogs.is_open() and not _ended.visible and at_command_prompt():
				_pause.visible = true
			else:
				handled = false
		_:
			handled = false
	if handled:
		get_viewport().set_input_as_handled()
