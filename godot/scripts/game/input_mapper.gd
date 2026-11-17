extends Node
## Translates Godot key and mouse events into NetHack input.
##
## * Printable characters go through unchanged (vi-keys hjklyubn work as-is).
## * Ctrl+letter becomes the matching control code (^A = 1 ... ^Z = 26).
## * Arrow keys, Home/End/PgUp/PgDn and the keypad become movement in the
##   scheme the core currently uses: digits when number_pad is on, vi-keys
##   otherwise.  Shift runs (uppercase vi-key or Shift+digit -> rush prefix).
## * Mouse clicks while the core waits for a key: left on a neighbouring cell
##   steps there, left farther away travels, right inspects (see click_action).

const NhConst = preload("res://scripts/core/nh_const.gd")

## Direction -> [vi-key, numpad digit]
const DIRS := {
	"w": ["h", "4"], "e": ["l", "6"], "n": ["k", "8"], "s": ["j", "2"],
	"nw": ["y", "7"], "ne": ["u", "9"], "sw": ["b", "1"], "se": ["n", "3"],
}
const KEY_DIRS := {
	KEY_LEFT: "w", KEY_RIGHT: "e", KEY_UP: "n", KEY_DOWN: "s",
	KEY_HOME: "nw", KEY_PAGEUP: "ne", KEY_END: "sw", KEY_PAGEDOWN: "se",
	KEY_KP_4: "w", KEY_KP_6: "e", KEY_KP_8: "n", KEY_KP_2: "s",
	KEY_KP_7: "nw", KEY_KP_9: "ne", KEY_KP_1: "sw", KEY_KP_3: "se",
}

var model
## Callable returning true while a modal UI (dialog, in-game menu) is open.
var blocked: Callable = func(): return false
## {cell, mod} of a click waiting for the position prompt it opened
var _pending_click := {}


func setup(p_model) -> void:
	model = p_model
	if not Session.request.is_connected(_on_request):
		Session.request.connect(_on_request)


## Pure mapping used by the input handler and the tests.
## Returns the NetHack key code, or -1 when the event is not game input.
static func key_for(keycode: int, unicode: int, ctrl: bool, shift: bool, number_pad: bool) -> int:
	if KEY_DIRS.has(keycode):
		var d: Array = DIRS[KEY_DIRS[keycode]]
		if number_pad:
			# Shift+digit has no run meaning in number_pad mode; keep the digit.
			return d[1].unicode_at(0)
		var vi: String = d[0]
		return (vi.to_upper() if shift else vi).unicode_at(0)
	match keycode:
		KEY_ESCAPE:
			return 27
		KEY_ENTER, KEY_KP_ENTER:
			return 13
		KEY_BACKSPACE:
			return 8
		KEY_TAB:
			return 9
		KEY_KP_5:
			return ("5" if number_pad else ".").unicode_at(0)
		KEY_KP_PERIOD, KEY_DELETE:
			return ".".unicode_at(0)
	if ctrl and keycode >= KEY_A and keycode <= KEY_Z:
		return keycode - KEY_A + 1
	if unicode > 0 and unicode < 0x110000:
		return unicode
	return -1


func _unhandled_key_input(event: InputEvent) -> void:
	var k := event as InputEventKey
	if k == null or not k.pressed or blocked.call():
		return
	if not Session.waiting_for_command():
		return
	# At the command prompt Esc does nothing in NetHack; the game root uses it
	# for the in-game menu.  Function keys are frontend hotkeys.
	if k.keycode == KEY_ESCAPE and int(Session.pending_request.get("a", 0)) == 1:
		return
	if k.keycode >= KEY_F1 and k.keycode <= KEY_F12:
		return
	var np: bool = model.number_pad if model else Settings.number_pad
	var code := key_for(k.keycode, k.unicode, k.ctrl_pressed, k.shift_pressed, np)
	if code < 0:
		return
	get_viewport().set_input_as_handled()
	Session.send_key(code)


## Pure mapping of a map click, used by on_map_clicked and the tests.
##   left on a neighbouring cell  -> {"key": direction key}  (one step)
##   left on a farther cell       -> travel there
##   right on any cell            -> inspect it (farlook)
## When the core reads a position-capable key (poskey) the travel/inspect is
## a direct click; otherwise the command key (_ travel, ; farlook) is sent
## first and the click answers the position prompt it opens.
## Returns {} when the click does nothing (left click on the hero).
static func click_action(hero: Vector2i, cell: Vector2i, button: int, poskey: bool,
		number_pad: bool) -> Dictionary:
	if button == MOUSE_BUTTON_RIGHT:
		if poskey:
			return {"click": NhConst.CLICK_INSPECT}
		return {"key": ";".unicode_at(0), "click": NhConst.CLICK_INSPECT}
	if button != MOUSE_BUTTON_LEFT:
		return {}
	if hero.x >= 0:
		var d := cell - hero
		if d == Vector2i.ZERO:
			return {}
		if absi(d.x) <= 1 and absi(d.y) <= 1:
			return {"key": direction_key(d, number_pad)}
	if poskey:
		return {"click": NhConst.CLICK_TRAVEL}
	return {"key": "_".unicode_at(0), "click": NhConst.CLICK_TRAVEL}


## The movement key for a one-cell step (dx, dy in -1..1).
static func direction_key(d: Vector2i, number_pad: bool) -> int:
	var name := ""
	match d:
		Vector2i(-1, 0): name = "w"
		Vector2i(1, 0): name = "e"
		Vector2i(0, -1): name = "n"
		Vector2i(0, 1): name = "s"
		Vector2i(-1, -1): name = "nw"
		Vector2i(1, -1): name = "ne"
		Vector2i(-1, 1): name = "sw"
		Vector2i(1, 1): name = "se"
	if name == "":
		return -1
	return DIRS[name][1 if number_pad else 0].unicode_at(0)


## Called by the map views with a map cell and the mouse button.  Works
## whenever the core waits for a key, not only for position-capable keys.
func on_map_clicked(cell: Vector2i, button: int) -> void:
	if blocked.call() or not Session.waiting_for_command():
		return
	if not model.in_bounds(cell.x, cell.y):
		return
	var poskey := int(Session.pending_request.get("a", 0)) == 1
	var act := click_action(model.hero, cell, button, poskey, model.number_pad)
	if act.is_empty():
		return
	if act.has("key"):
		if act.has("click"):
			# answered by the position prompt the command key opens
			_pending_click = {"cell": cell, "mod": act.click}
		Session.send_key(act.key)
	else:
		Session.send_click(cell.x, cell.y, act.click)


## The core's next request after a travel/farlook key: answer its position
## prompt with the clicked cell, or drop the click if it asked for something
## else.
func _on_request(ev: Dictionary) -> void:
	if _pending_click.is_empty():
		return
	var pc := _pending_click
	_pending_click = {}
	if int(ev.get("type", 0)) == NhConst.EV_REQ_KEY and int(ev.get("a", 0)) == 1:
		Session.send_click(pc.cell.x, pc.cell.y, pc.mod)
