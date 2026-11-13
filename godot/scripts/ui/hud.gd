extends Control
## In-game HUD: message log with scroll-back, status panel (HP/Pw bars, AC,
## level, gold, hunger, encumbrance, conditions) and the inventory panel.
## Everything is read from the shared GameModel.

const NhConst = preload("res://scripts/core/nh_const.gd")
const S = NhConst.Status

var model
var atlas

var _log: RichTextLabel
var _log_panel: PanelContainer
var _expanded := false
var _hp_bar: ProgressBar
var _hp_label: Label
var _pw_bar: ProgressBar
var _pw_label: Label
var _fields := {}
var _conditions: HFlowContainer
var _title: Label
var _inventory_panel: PanelContainer
var _inventory: ItemList


func setup(p_model, p_atlas) -> void:
	model = p_model
	atlas = p_atlas
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_build_log()
	_build_status()
	_build_inventory()
	model.message_added.connect(_on_message)
	model.status_changed.connect(func(_f): _refresh_status())
	model.inventory_changed.connect(_refresh_inventory)
	for m in model.messages:
		_on_message(m.text, m.attr)
	_refresh_status()
	_refresh_inventory()


func set_atlas(p_atlas) -> void:
	atlas = p_atlas
	_refresh_inventory()


# --- message log ----------------------------------------------------------

func _build_log() -> void:
	_log_panel = PanelContainer.new()
	_log_panel.set_anchors_preset(Control.PRESET_TOP_WIDE)
	_log_panel.offset_left = 8
	_log_panel.offset_right = -268
	_log_panel.offset_top = 8
	_log_panel.offset_bottom = 104
	_log_panel.self_modulate = Color(1, 1, 1, 0.85)
	add_child(_log_panel)
	var box := VBoxContainer.new()
	_log_panel.add_child(box)
	var head := HBoxContainer.new()
	box.add_child(head)
	var l := Label.new()
	l.text = "HUD_MESSAGES"
	l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	l.modulate = Color(1, 1, 1, 0.6)
	head.add_child(l)
	var more := Button.new()
	more.text = "HUD_HISTORY"
	more.tooltip_text = "HUD_HISTORY_HINT"
	more.flat = true
	more.pressed.connect(toggle_history)
	head.add_child(more)
	_log = RichTextLabel.new()
	_log.scroll_following = true
	_log.selection_enabled = true
	_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_log.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	box.add_child(_log)


func _on_message(text: String, attr: int) -> void:
	var t := text.replace("[", "[lb]")
	if attr & 1 or attr == 1:
		t = "[b]%s[/b]" % t
	_log.append_text(t + "\n")


## Expand the log to show the full scroll-back (also bound to ^P via the core).
func toggle_history() -> void:
	_expanded = not _expanded
	_log_panel.offset_bottom = 460 if _expanded else 104
	_log.scroll_to_line(_log.get_line_count())


func show_history() -> void:
	if not _expanded:
		toggle_history()


func history_open() -> bool:
	return _expanded


# --- status ---------------------------------------------------------------

func _build_status() -> void:
	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	panel.offset_left = 8
	panel.offset_right = -268
	panel.offset_top = -112
	panel.offset_bottom = -8
	panel.self_modulate = Color(1, 1, 1, 0.9)
	add_child(panel)
	var box := VBoxContainer.new()
	panel.add_child(box)
	_title = Label.new()
	_title.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	box.add_child(_title)
	var bars := HBoxContainer.new()
	bars.add_theme_constant_override("separation", 12)
	box.add_child(bars)
	var hp := _bar(bars, "HUD_HP", Color(0.8, 0.15, 0.15))
	_hp_bar = hp[0]
	_hp_label = hp[1]
	var pw := _bar(bars, "HUD_PW", Color(0.2, 0.4, 0.9))
	_pw_bar = pw[0]
	_pw_label = pw[1]
	var row := HFlowContainer.new()
	row.add_theme_constant_override("h_separation", 18)
	box.add_child(row)
	for spec in [[S.LEVELDESC, "HUD_DLVL"], [S.AC, "HUD_AC"], [S.XP, "HUD_LEVEL"],
			[S.GOLD, "HUD_GOLD"], [S.TIME, "HUD_TIME"], [S.STR, "HUD_STR"],
			[S.DX, "HUD_DEX"], [S.CO, "HUD_CON"], [S.IN, "HUD_INT"], [S.WI, "HUD_WIS"],
			[S.CH, "HUD_CHA"], [S.ALIGN, "HUD_ALIGN"], [S.HUNGER, "HUD_HUNGER"],
			[S.CAP, "HUD_ENCUMBRANCE"]]:
		var h := HBoxContainer.new()
		var k := Label.new()
		k.text = spec[1]
		k.modulate = Color(1, 1, 1, 0.6)
		h.add_child(k)
		var v := Label.new()
		v.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
		h.add_child(v)
		row.add_child(h)
		_fields[spec[0]] = [h, v]
	_conditions = HFlowContainer.new()
	box.add_child(_conditions)


func _bar(parent: Control, key: String, color: Color) -> Array:
	var h := HBoxContainer.new()
	h.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(h)
	var k := Label.new()
	k.text = key
	h.add_child(k)
	var bar := ProgressBar.new()
	bar.custom_minimum_size = Vector2(160, 18)
	bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.show_percentage = false
	var fill := StyleBoxFlat.new()
	fill.bg_color = color
	bar.add_theme_stylebox_override("fill", fill)
	h.add_child(bar)
	var v := Label.new()
	v.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	v.custom_minimum_size = Vector2(80, 0)
	h.add_child(v)
	return [bar, v]


func _refresh_status() -> void:
	_title.text = model.status_value(S.TITLE)
	_set_bar(_hp_bar, _hp_label, model.status_int(S.HP), model.status_int(S.HPMAX))
	_set_bar(_pw_bar, _pw_label, model.status_int(S.ENE), model.status_int(S.ENEMAX))
	for f in _fields:
		var v: String = model.status_value(f)
		if f == S.XP and model.status_value(S.EXP) != "":
			v += "/" + model.status_value(S.EXP)
		_fields[f][1].text = v
		# status colour: low bits are CLR_*, 8 (NO_COLOR) and 0 mean default
		var col := int(model.status.get(f, {}).get("color", 8)) & 0xF
		_fields[f][1].modulate = Color.WHITE if col == 0 or col == 8 else NhConst.color_of(col)
		# hunger/encumbrance are blank when normal
		_fields[f][0].visible = v != ""
	for c in _conditions.get_children():
		c.queue_free()
	for key in model.condition_keys():
		var l := Label.new()
		l.text = key
		l.add_theme_color_override("font_color", Color(1, 0.75, 0.3))
		_conditions.add_child(l)


func _set_bar(bar: ProgressBar, label: Label, value: int, maximum: int) -> void:
	bar.max_value = maxi(maximum, 1)
	bar.value = clampi(value, 0, maxi(maximum, 1))
	label.text = "%d(%d)" % [value, maximum]


# --- inventory ------------------------------------------------------------

func _build_inventory() -> void:
	_inventory_panel = PanelContainer.new()
	_inventory_panel.set_anchors_preset(Control.PRESET_RIGHT_WIDE)
	_inventory_panel.offset_left = -260
	_inventory_panel.offset_right = -8
	_inventory_panel.offset_top = 8
	_inventory_panel.offset_bottom = -8
	_inventory_panel.self_modulate = Color(1, 1, 1, 0.9)
	add_child(_inventory_panel)
	var box := VBoxContainer.new()
	_inventory_panel.add_child(box)
	var t := Label.new()
	t.text = "HUD_INVENTORY"
	box.add_child(t)
	_inventory = ItemList.new()
	_inventory.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_inventory.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	_inventory.focus_mode = Control.FOCUS_NONE
	box.add_child(_inventory)


func toggle_inventory() -> void:
	_inventory_panel.visible = not _inventory_panel.visible


func _refresh_inventory() -> void:
	_inventory.clear()
	for it in model.inventory:
		var text: String = it.text if it.header else "%s - %s" % [it.letter, it.text]
		var idx := _inventory.add_item(Session.core_text(text), _icon(it.glyph), not it.header)
		if it.header:
			_inventory.set_item_custom_fg_color(idx, Color(0.95, 0.75, 0.35))


func _icon(glyph: int) -> Texture2D:
	if atlas == null or glyph < 0 or atlas.cell_of(glyph) < 0:
		return null
	var t := AtlasTexture.new()
	t.atlas = atlas.texture
	t.region = atlas.region(glyph)
	return t
