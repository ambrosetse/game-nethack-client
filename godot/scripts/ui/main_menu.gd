extends Control
## Main menu: continue, new game (character select), options, language, quit.

const NhConst = preload("res://scripts/core/nh_const.gd")
const OptionsPanel = preload("res://scripts/ui/options_panel.gd")
const GAME_SCENE := "res://scenes/game.tscn"

var _main: VBoxContainer
var _chargen: PanelContainer
var _options: PanelContainer
var _language: PanelContainer
var _error: AcceptDialog
var _continue: Button

var _name: LineEdit
var _pickers := {}
var _warning: Label
var _start: Button


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var bg := ColorRect.new()
	bg.color = Color(0.05, 0.045, 0.07)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)

	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)

	_main = _build_main()
	center.add_child(_main)
	_chargen = _build_chargen()
	center.add_child(_chargen)
	_options = OptionsPanel.new()
	_options.closed.connect(_show.bind("main"))
	center.add_child(_options)
	_language = _build_language()
	center.add_child(_language)
	_error = AcceptDialog.new()
	add_child(_error)
	_show("main")

	# A finished game in this process asked for a restart into a new game.
	if not Settings.pending_start.is_empty():
		var cfg: Dictionary = Settings.pending_start
		Settings.set_value("pending_start", {})
		_launch.call_deferred(cfg)


func _build_main() -> VBoxContainer:
	var box := VBoxContainer.new()
	box.custom_minimum_size = Vector2(320, 0)
	box.add_theme_constant_override("separation", 10)
	var title := Label.new()
	title.text = "GAME_TITLE"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 44)
	title.add_theme_color_override("font_color", Color(0.95, 0.75, 0.35))
	box.add_child(title)
	var sub := Label.new()
	sub.text = "GAME_SUBTITLE"
	sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	sub.modulate = Color(1, 1, 1, 0.6)
	box.add_child(sub)
	box.add_child(HSeparator.new())
	_continue = _button(box, "MENU_CONTINUE", _on_continue)
	_button(box, "MENU_NEW_GAME", _show.bind("chargen"))
	_button(box, "MENU_OPTIONS", _show.bind("options"))
	_button(box, "MENU_LANGUAGE", _show.bind("language"))
	_button(box, "MENU_QUIT", func(): get_tree().quit())
	return box


func _button(parent: Control, key: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = key
	b.pressed.connect(cb)
	parent.add_child(b)
	return b


func _build_chargen() -> PanelContainer:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(480, 0)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 10)
	panel.add_child(box)
	var title := Label.new()
	title.text = "CHARGEN_TITLE"
	title.add_theme_font_size_override("font_size", 24)
	box.add_child(title)
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 16)
	box.add_child(grid)

	var nl := Label.new()
	nl.text = "CHARGEN_NAME"
	grid.add_child(nl)
	_name = LineEdit.new()
	_name.max_length = 30
	_name.placeholder_text = "CHARGEN_NAME_HINT"
	_name.text = Settings.player_name
	_name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_name.text_changed.connect(_on_name_changed)
	grid.add_child(_name)

	for spec in [["role", "CHARGEN_ROLE", NhConst.ROLES], ["race", "CHARGEN_RACE", NhConst.RACES],
			["gender", "CHARGEN_GENDER", NhConst.GENDERS], ["align", "CHARGEN_ALIGN", NhConst.ALIGNS]]:
		var l := Label.new()
		l.text = spec[1]
		grid.add_child(l)
		var o := OptionButton.new()
		o.add_item("CHARGEN_RANDOM")
		o.set_item_metadata(0, "random")
		for pair in spec[2]:
			o.add_item(pair[1])
			o.set_item_metadata(o.item_count - 1, pair[0])
		var current: String = Settings.get(spec[0])
		for i in o.item_count:
			if o.get_item_metadata(i) == current:
				o.select(i)
		o.item_selected.connect(func(_i): _validate())
		grid.add_child(o)
		_pickers[spec[0]] = o

	_warning = Label.new()
	_warning.text = "CHARGEN_INVALID"
	_warning.add_theme_color_override("font_color", Color(1, 0.45, 0.35))
	box.add_child(_warning)
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_END
	box.add_child(row)
	_button(row, "UI_BACK", _show.bind("main"))
	_start = _button(row, "CHARGEN_START", _on_start)
	_validate()
	return panel


func _pick(key: String) -> String:
	var o: OptionButton = _pickers[key]
	return o.get_item_metadata(o.selected)


## Drop ',' and ':' as they are typed or pasted (see NhConst.NAME_FORBIDDEN).
func _on_name_changed(text: String) -> void:
	var kept := ""
	var caret := _name.caret_column
	for i in text.length():
		var ch := text[i]
		if NhConst.NAME_FORBIDDEN.has(ch):
			if i < _name.caret_column:
				caret -= 1
			continue
		kept += ch
	if kept != text:
		_name.text = kept
		_name.caret_column = maxi(caret, 0)
	_validate()


func _validate() -> void:
	var ok := NhConst.valid_combo(_pick("role"), _pick("race"), _pick("gender"), _pick("align"))
	_warning.visible = not ok
	_start.disabled = not ok or NhConst.clean_player_name(_name.text).is_empty()


func _build_language() -> PanelContainer:
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(320, 0)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	panel.add_child(box)
	var title := Label.new()
	title.text = "MENU_LANGUAGE"
	title.add_theme_font_size_override("font_size", 24)
	box.add_child(title)
	for code in Settings.LOCALES:
		var b := Button.new()
		b.text = Settings.LOCALE_NAMES[code]
		b.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
		b.pressed.connect(func():
			Settings.set_value("language", code)
			_show("main"))
		box.add_child(b)
	_button(box, "UI_BACK", _show.bind("main"))
	return panel


func _show(which: String) -> void:
	_main.visible = which == "main"
	_chargen.visible = which == "chargen"
	_options.visible = which == "options"
	_language.visible = which == "language"
	_continue.disabled = not Session.has_save() or Settings.player_name.is_empty()
	if which == "chargen":
		_name.grab_focus()


func _on_continue() -> void:
	# NetHack restores the save that belongs to this player name.
	_launch({"name": NhConst.clean_player_name(Settings.player_name)})


func _on_start() -> void:
	var cfg := {"name": NhConst.clean_player_name(_name.text)}
	Settings.set_value("player_name", cfg.name)
	for k in _pickers:
		cfg[k] = _pick(k)
		Settings.set_value(k, cfg[k])
	_launch(cfg)


func _launch(cfg: Dictionary) -> void:
	if not Session.extension_available():
		_error.title = tr("ERROR_TITLE")
		_error.dialog_text = tr("ERROR_NO_EXTENSION")
		_error.popup_centered()
		return
	if Session.used():
		Session.restart_into(cfg)
		return
	Session.next_config = cfg
	get_tree().change_scene_to_file(GAME_SCENE)


func _unhandled_key_input(event: InputEvent) -> void:
	if event.is_pressed() and event is InputEventKey and event.keycode == KEY_ESCAPE \
			and not _main.visible:
		get_viewport().set_input_as_handled()
		_show("main")
