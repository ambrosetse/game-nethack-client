extends Control
## Modal dialogs answering the core's blocking requests:
## menus (PICK_NONE / PICK_ONE / PICK_ANY), y/n questions, getlin / askname,
## extended commands, and text windows (help, discoveries, tombstone).

const NhConst = preload("res://scripts/core/nh_const.gd")
const MENU_ITEMFLAGS_SELECTED := 1

var atlas
var _panel: PanelContainer
var _box: VBoxContainer
var _kind := ""
var _request := {}
var _text_queue: Array = []
var _text_needs_key := false
# menu state
var _menu_items: Array = []
var _checks := {}  # item index -> CheckBox / Button
var _line: LineEdit
var _ext_list: ItemList
var _ext_ids: Array = []


func setup(p_atlas) -> void:
	atlas = p_atlas
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	center.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(center)
	_panel = PanelContainer.new()
	_panel.custom_minimum_size = Vector2(520, 0)
	center.add_child(_panel)
	_box = VBoxContainer.new()
	_box.add_theme_constant_override("separation", 8)
	_panel.add_child(_box)
	_panel.visible = false
	Session.request.connect(_on_request)
	Session.text_window.connect(_on_text_window)


func is_open() -> bool:
	return _panel.visible


func _clear() -> void:
	for c in _box.get_children():
		c.queue_free()
	_checks.clear()
	_menu_items = []
	_line = null
	_ext_list = null


func _open(kind: String) -> void:
	_kind = kind
	_panel.visible = true


func _close() -> void:
	_panel.visible = false
	_kind = ""
	_clear()
	if not _text_queue.is_empty():
		var t: Array = _text_queue.pop_front()
		_show_text(t[0], t[1], t[2], t[3])


func _label(text: String, translate := false) -> Label:
	var l := Label.new()
	l.text = text
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	if not translate:
		l.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	_box.add_child(l)
	return l


func _buttons(specs: Array) -> void:
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_END
	_box.add_child(row)
	for s in specs:
		var b := Button.new()
		b.text = s[0]
		b.pressed.connect(s[1])
		row.add_child(b)


# --- requests ---------------------------------------------------------------

func _on_request(ev: Dictionary) -> void:
	match int(ev.type):
		NhConst.EV_REQ_YN:
			_show_yn(ev)
		NhConst.EV_REQ_LINE:
			_show_line(ev)
		NhConst.EV_REQ_MENU:
			_show_menu(ev)
		NhConst.EV_REQ_EXTCMD:
			_show_extcmd(ev)
		_:
			return
	_request = ev


func _show_yn(ev: Dictionary) -> void:
	_clear()
	_open("yn")
	_label(Session.core_text(ev.get("s", "")))
	var choices: String = ev.get("s2", "")
	var specs := []
	if choices.is_empty():
		_label("DIALOG_ANY_KEY", true)
		specs.append(["UI_OK", func(): _answer_key(int(ev.get("a", 27)) if int(ev.get("a", 0)) else 32)])
	else:
		for ch in choices:
			if ch == "\u001b":
				break
			specs.append([_choice_label(ch), _answer_key.bind(ch.unicode_at(0))])
	specs.append(["UI_CANCEL", _answer_key.bind(27)])
	_buttons(specs)


static func _choice_label(ch: String) -> String:
	match ch:
		"y":
			return "DIALOG_YES"
		"n":
			return "DIALOG_NO"
		"q":
			return "DIALOG_QUIT"
		"a":
			return "DIALOG_ALL"
	return ch


func _answer_key(key: int) -> void:
	_close()
	Session.send_key(key)


func _show_line(ev: Dictionary) -> void:
	_clear()
	_open("line")
	var askname := int(ev.get("a", 0)) == 1
	_label("CHARGEN_NAME" if askname else Session.core_text(ev.get("s", "")), askname)
	_line = LineEdit.new()
	_line.max_length = 255
	_line.text_submitted.connect(func(_t): _submit_line(false))
	_box.add_child(_line)
	_buttons([["UI_CANCEL", _submit_line.bind(true)], ["UI_OK", _submit_line.bind(false)]])
	_line.grab_focus.call_deferred()


func _submit_line(cancelled: bool) -> void:
	var t := _line.text if _line else ""
	_close()
	Session.send_text(t, cancelled)


func _show_menu(ev: Dictionary) -> void:
	_clear()
	_open("menu")
	var how := int(ev.get("b", NhConst.PICK_NONE))
	var prompt: String = ev.get("s", "")
	if prompt != "":
		_label(Session.core_text(prompt))
	_menu_items = ev.get("items", [])
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(500, mini(36 * _menu_items.size() + 8, 480))
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_box.add_child(scroll)
	var list := VBoxContainer.new()
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(list)
	var group := ButtonGroup.new()
	var first: Control = null
	for i in _menu_items.size():
		var it: Dictionary = _menu_items[i]
		var text: String = Session.core_text(it.get("text", ""))
		var selectable := int(it.get("identifier", 0)) != 0 and how != NhConst.PICK_NONE
		if not selectable:
			var l := Label.new()
			l.text = text
			l.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
			if int(it.get("identifier", 0)) == 0:
				l.add_theme_color_override("font_color", Color(0.95, 0.75, 0.35))
			list.add_child(l)
			continue
		var ch := int(it.get("ch", 0))
		var b := CheckBox.new()
		b.text = ("%s - %s" % [String.chr(ch), text]) if ch > 0 else text
		b.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
		b.icon = _icon(int(it.get("glyph", -1)))
		b.expand_icon = false
		b.button_pressed = int(it.get("flags", 0)) & MENU_ITEMFLAGS_SELECTED != 0
		if how == NhConst.PICK_ONE:
			b.button_group = group
			b.pressed.connect(_finish_menu.bind(false))
		list.add_child(b)
		_checks[i] = b
		if first == null:
			first = b
	if how == NhConst.PICK_ANY:
		_buttons([["UI_CANCEL", _finish_menu.bind(true)], ["DIALOG_ALL", _select_all],
			["UI_OK", _finish_menu.bind(false)]])
	elif how == NhConst.PICK_ONE:
		_buttons([["UI_CANCEL", _finish_menu.bind(true)]])
	else:
		_buttons([["UI_OK", _finish_menu.bind(true)]])
	if first:
		first.grab_focus.call_deferred()


func _icon(glyph: int) -> Texture2D:
	if atlas == null or glyph < 0 or atlas.cell_of(glyph) < 0:
		return null
	var t := AtlasTexture.new()
	t.atlas = atlas.texture
	t.region = atlas.region(glyph)
	return t


func _select_all() -> void:
	var all_on := true
	for b in _checks.values():
		all_on = all_on and b.button_pressed
	for b in _checks.values():
		b.button_pressed = not all_on


func _finish_menu(cancelled: bool) -> void:
	var sel := PackedInt32Array()
	var counts := PackedInt64Array()
	if not cancelled:
		for i in _checks:
			if _checks[i].button_pressed:
				sel.append(i)
				counts.append(-1)
	_close()
	Session.send_menu(sel, counts, cancelled)


## Menu accelerators: typing an item's letter toggles (or picks) it.
func _menu_key(unicode: int) -> bool:
	var how := int(_request.get("b", 0))
	for i in _checks:
		if int(_menu_items[i].get("ch", 0)) == unicode:
			var b: CheckBox = _checks[i]
			if how == NhConst.PICK_ONE:
				b.button_pressed = true
				_finish_menu(false)
			else:
				b.button_pressed = not b.button_pressed
			return true
	return false


func _show_extcmd(ev: Dictionary) -> void:
	_clear()
	_open("extcmd")
	_label("DIALOG_EXTCMD", true)
	_line = LineEdit.new()
	_line.placeholder_text = "DIALOG_EXTCMD_HINT"
	_box.add_child(_line)
	_ext_list = ItemList.new()
	_ext_list.custom_minimum_size = Vector2(500, 300)
	_ext_list.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	_box.add_child(_ext_list)
	var items: Array = ev.get("items", [])
	var fill := func(filter: String):
		_ext_list.clear()
		_ext_ids = []
		for it in items:
			var t: String = it.get("text", "")
			if filter.is_empty() or t.begins_with(filter):
				_ext_list.add_item(t)
				_ext_ids.append(int(it.get("identifier", -1)))
		if _ext_list.item_count > 0:
			_ext_list.select(0)
	fill.call("")
	_line.text_changed.connect(func(t): fill.call(t.strip_edges().to_lower()))
	_line.text_submitted.connect(func(_t): _finish_ext(false))
	_ext_list.item_activated.connect(func(_i): _finish_ext(false))
	_buttons([["UI_CANCEL", _finish_ext.bind(true)], ["UI_OK", _finish_ext.bind(false)]])
	_line.grab_focus.call_deferred()


func _finish_ext(cancelled: bool) -> void:
	var sel := _ext_list.get_selected_items() if _ext_list else PackedInt32Array()
	var id := -1
	if not cancelled and sel.size() > 0:
		id = _ext_ids[sel[0]]
	_close()
	Session.send_extcmd(id)


# --- text windows -------------------------------------------------------------

func _on_text_window(title: String, text: String, kind: String, needs_key: bool) -> void:
	if _panel.visible:
		_text_queue.append([title, text, kind, needs_key])
		return
	_show_text(title, text, kind, needs_key)


func _show_text(title: String, text: String, kind: String, needs_key: bool) -> void:
	_clear()
	_open("text")
	_text_needs_key = needs_key
	if kind == "tombstone":
		_label("DIALOG_TOMBSTONE", true).add_theme_font_size_override("font_size", 24)
	elif title != "":
		_label(title)
	var body := RichTextLabel.new()
	body.custom_minimum_size = Vector2(640, 420)
	body.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	body.selection_enabled = true
	body.add_theme_font_override("normal_font", _mono())
	body.add_text(text)
	_box.add_child(body)
	_buttons([["UI_CLOSE", _close_text]])


func _close_text() -> void:
	var needs := _text_needs_key
	_text_needs_key = false
	_close()
	if needs:
		Session.dismiss_text()


static func _mono() -> Font:
	var f := SystemFont.new()
	f.font_names = PackedStringArray(["DejaVu Sans Mono", "Consolas", "Menlo", "monospace"])
	return f


func _unhandled_key_input(event: InputEvent) -> void:
	if not _panel.visible or not event.is_pressed():
		return
	var k := event as InputEventKey
	var handled := true
	if k.keycode == KEY_ESCAPE:
		match _kind:
			"yn":
				_answer_key(27)
			"line":
				_submit_line(true)
			"menu":
				_finish_menu(true)
			"extcmd":
				_finish_ext(true)
			"text":
				_close_text()
	elif _kind == "text" and (k.keycode == KEY_ENTER or k.keycode == KEY_SPACE):
		_close_text()
	elif _kind == "yn" and k.unicode > 0:
		var choices: String = _request.get("s2", "")
		var ch := String.chr(k.unicode)
		if choices.is_empty() or choices.contains(ch):
			_answer_key(k.unicode)
	elif _kind == "menu" and (k.keycode == KEY_ENTER or k.keycode == KEY_KP_ENTER):
		_finish_menu(int(_request.get("b", 0)) == NhConst.PICK_NONE)
	elif _kind == "menu" and k.unicode > 32:
		handled = _menu_key(k.unicode)
	else:
		handled = false
	if handled:
		get_viewport().set_input_as_handled()
