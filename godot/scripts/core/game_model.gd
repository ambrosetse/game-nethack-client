extends RefCounted
## The single model both map renderers (2D and 3D) and the HUD read from.
##
## Fed by GameSession from NetHackGame events; holds the map glyphs, what is
## currently in view versus only remembered, the hero position, status
## fields, inventory and the message history.

const NhConst = preload("res://scripts/core/nh_const.gd")

signal cell_changed(x: int, y: int)
signal map_cleared
signal vision_changed
signal hero_moved(x: int, y: int)
signal status_changed(field: int)
signal inventory_changed
signal message_added(text: String, attr: int)

const NO_GLYPH := -1
const MAX_MESSAGES := 2000

var width := NhConst.COLNO
var height := NhConst.ROWNO
## glyph id per cell (row major), NO_GLYPH when never seen
var glyphs := PackedInt32Array()
## background glyph per cell (floor under an object/monster), NO_GLYPH if none
var bk_glyphs := PackedInt32Array()
var colors := PackedByteArray()
## 1 if the hero can see the cell right now
var visible := PackedByteArray()
var hero := Vector2i(-1, -1)
var cursor := Vector2i(-1, -1)
## field -> {"value": String, "color": int, "percent": int}
var status := {}
var conditions := 0
## [{glyph, letter, text, attr, color}]
var inventory: Array = []
## [{text, attr}]
var messages: Array = []
var number_pad := false


func _init() -> void:
	reset()


func reset() -> void:
	var n := width * height
	glyphs.resize(n)
	glyphs.fill(NO_GLYPH)
	bk_glyphs.resize(n)
	bk_glyphs.fill(NO_GLYPH)
	colors.resize(n)
	colors.fill(0)
	visible.resize(n)
	visible.fill(0)
	map_cleared.emit()


func index(x: int, y: int) -> int:
	return y * width + x


func in_bounds(x: int, y: int) -> bool:
	return x >= 0 and y >= 0 and x < width and y < height


func glyph_at(x: int, y: int) -> int:
	return glyphs[index(x, y)] if in_bounds(x, y) else NO_GLYPH


func is_visible(x: int, y: int) -> bool:
	return in_bounds(x, y) and visible[index(x, y)] == 1


## True for cells the hero has seen before but cannot see now.
func is_remembered(x: int, y: int) -> bool:
	return in_bounds(x, y) and visible[index(x, y)] == 0 \
		and glyphs[index(x, y)] != NO_GLYPH


func set_glyph(x: int, y: int, glyph: int, color: int, bk_glyph: int) -> void:
	if not in_bounds(x, y):
		return
	var i := index(x, y)
	glyphs[i] = glyph
	bk_glyphs[i] = bk_glyph
	colors[i] = color & 0xFF
	cell_changed.emit(x, y)


func set_vision(cols: int, rows: int, hx: int, hy: int, bits: String) -> void:
	var n := mini(bits.length(), visible.size())
	if cols != width or rows != height:
		n = 0
	for i in n:
		visible[i] = 1 if bits.unicode_at(i) == 49 else 0  # '1'
	vision_changed.emit()
	if Vector2i(hx, hy) != hero:
		hero = Vector2i(hx, hy)
		hero_moved.emit(hx, hy)


func set_status(field: int, value: String, color: int, percent: int, condmask: int) -> void:
	status[field] = {"value": value.strip_edges(), "color": color, "percent": percent}
	if field == NhConst.Status.CONDITION:
		conditions = condmask
	status_changed.emit(field)


func status_value(field: int) -> String:
	return status.get(field, {}).get("value", "")


func status_int(field: int) -> int:
	var s := status_value(field)
	var digits := ""
	for ch in s:
		if ch.is_valid_int() or (ch == "-" and digits.is_empty()):
			digits += ch
		elif not digits.is_empty():
			break
	return int(digits) if digits.is_valid_int() else 0


## Translation keys of the active conditions (Blind, Stun, ...).
func condition_keys() -> PackedStringArray:
	var out := PackedStringArray()
	for bit in NhConst.CONDITIONS.size():
		if conditions & (1 << bit):
			out.append(NhConst.CONDITIONS[bit])
	return out


func set_inventory(items: Array) -> void:
	inventory = []
	for it in items:
		inventory.append({
			"glyph": it.get("glyph", NO_GLYPH),
			"letter": String.chr(int(it.get("ch", 32))) if int(it.get("ch", 0)) > 0 else "",
			"text": it.get("text", ""),
			"attr": it.get("attr", 0),
			"color": it.get("color", 8),
			"header": int(it.get("identifier", 0)) == 0,
		})
	inventory_changed.emit()


func add_message(text: String, attr: int) -> void:
	if text.strip_edges().is_empty():
		return
	messages.append({"text": text, "attr": attr})
	if messages.size() > MAX_MESSAGES:
		messages = messages.slice(messages.size() - MAX_MESSAGES)
	message_added.emit(text, attr)
