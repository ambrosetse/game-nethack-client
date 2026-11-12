extends PanelContainer
## Options panel, used from the main menu and in-game (Esc menu / F10).
##
## Every change goes straight to Settings, which persists it and emits
## `changed`; the game scene reacts (renderer swap, tileset reload).

signal closed

var _lang: OptionButton
var _renderer: OptionButton
var _input: OptionButton
var _tiles: OptionButton


func _ready() -> void:
	custom_minimum_size = Vector2(460, 0)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 10)
	add_child(box)

	var title := Label.new()
	title.text = "OPTIONS_TITLE"
	title.add_theme_font_size_override("font_size", 24)
	box.add_child(title)

	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 16)
	box.add_child(grid)

	_lang = _row(grid, "OPTIONS_LANGUAGE")
	for code in Settings.LOCALES:
		_lang.add_item(Settings.LOCALE_NAMES[code])
	_lang.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	_lang.select(Settings.LOCALES.find(Settings.language))
	_lang.item_selected.connect(func(i): Settings.set_value("language", Settings.LOCALES[i]))

	_renderer = _row(grid, "OPTIONS_RENDERER")
	_renderer.add_item("OPTIONS_RENDERER_2D")
	_renderer.add_item("OPTIONS_RENDERER_3D")
	_renderer.select(1 if Settings.renderer == "3d" else 0)
	_renderer.item_selected.connect(func(i): Settings.set_value("renderer", "3d" if i == 1 else "2d"))

	_input = _row(grid, "OPTIONS_MOVEMENT")
	_input.add_item("OPTIONS_MOVEMENT_VI")
	_input.add_item("OPTIONS_MOVEMENT_NUMPAD")
	_input.select(1 if Settings.number_pad else 0)
	_input.item_selected.connect(func(i): Settings.set_value("number_pad", i == 1))

	_tiles = _row(grid, "OPTIONS_TILESET")
	_tiles.add_item("OPTIONS_TILESET_ORIGINAL")
	_tiles.add_item("OPTIONS_TILESET_CLASSIC")
	_tiles.select(1 if Settings.tileset == "classic" else 0)
	_tiles.item_selected.connect(func(i): Settings.set_value("tileset", "classic" if i == 1 else "original"))

	var hint := Label.new()
	hint.text = "OPTIONS_HINT"
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	hint.modulate = Color(1, 1, 1, 0.7)
	box.add_child(hint)

	var close := Button.new()
	close.text = "UI_CLOSE"
	close.pressed.connect(func(): closed.emit())
	box.add_child(close)
	Settings.changed.connect(_on_settings_changed)


func _row(grid: GridContainer, key: String) -> OptionButton:
	var l := Label.new()
	l.text = key
	grid.add_child(l)
	var o := OptionButton.new()
	o.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	grid.add_child(o)
	return o


## Keep the widgets in sync when a hotkey changes a setting elsewhere.
func _on_settings_changed(key: String) -> void:
	match key:
		"renderer":
			_renderer.select(1 if Settings.renderer == "3d" else 0)
		"tileset":
			_tiles.select(1 if Settings.tileset == "classic" else 0)


func _unhandled_key_input(event: InputEvent) -> void:
	if visible and event.is_pressed() and event is InputEventKey \
			and event.keycode == KEY_ESCAPE:
		get_viewport().set_input_as_handled()
		closed.emit()
