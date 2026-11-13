extends Node
## Translates Godot key and mouse events into NetHack input.
##
## * Printable characters go through unchanged (vi-keys hjklyubn work as-is).
## * Ctrl+letter becomes the matching control code (^A = 1 ... ^Z = 26).
## * Arrow keys, Home/End/PgUp/PgDn and the keypad become movement in the
##   scheme the core currently uses: digits when number_pad is on, vi-keys
##   otherwise.  Shift runs (uppercase vi-key or Shift+digit -> rush prefix).
## * Mouse clicks become nh_poskey clicks (left = travel, right = inspect),
##   but only while the core asked for a position-capable key.

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


func setup(p_model) -> void:
	model = p_model


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


## Called by the map views with a map cell and the mouse button.
func on_map_clicked(cell: Vector2i, button: int) -> void:
	if blocked.call() or not Session.waiting_for_command():
		return
	if int(Session.pending_request.get("a", 0)) != 1:
		return
	if not model.in_bounds(cell.x, cell.y):
		return
	var mod := NhConst.CLICK_TRAVEL if button == MOUSE_BUTTON_LEFT else NhConst.CLICK_INSPECT
	Session.send_click(cell.x, cell.y, mod)
