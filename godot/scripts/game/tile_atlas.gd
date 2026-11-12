extends RefCounted
## Loads a glyph atlas: tiles*.png plus glyph_map*.json (tools/make_tileset.py
## or tools/convert_share_tiles.py).
##
## The preferred set is Settings.tileset; when it is missing the other set is
## used automatically, and when both are missing a flat-colour atlas is built
## from the core's glyph table so the game stays playable.

const NhConst = preload("res://scripts/core/nh_const.gd")
const DIR := "res://assets/tiles/"
const SETS := {
	"original": ["tiles.png", "glyph_map.json"],
	"classic": ["tiles_classic.png", "glyph_map_classic.json"],
}

var texture: Texture2D
var image: Image
var tile_size := 32
var columns := 100
var cells := PackedInt32Array()
var source := ""


static func load_preferred(preferred: String, glyph_table: Dictionary) -> RefCounted:
	var atlas = new()
	var order := [preferred, "classic" if preferred == "original" else "original"]
	for name in order:
		if atlas._load_set(name):
			return atlas
	atlas._build_fallback(glyph_table)
	return atlas


func _load_set(name: String) -> bool:
	var files: Array = SETS.get(name, [])
	if files.is_empty():
		return false
	var map_path: String = DIR + files[1]
	if not FileAccess.file_exists(map_path):
		return false
	var data = JSON.parse_string(FileAccess.get_file_as_string(map_path))
	if typeof(data) != TYPE_DICTIONARY:
		return false
	var img := _load_image(DIR + files[0])
	if img == null:
		return false
	image = img
	tile_size = int(data.get("tile_size", 32))
	columns = int(data.get("columns", 100))
	cells = PackedInt32Array(data.get("cells", []))
	texture = ImageTexture.create_from_image(image)
	source = name
	return true


static func _load_image(path: String) -> Image:
	if ResourceLoader.exists(path):
		var tex := load(path) as Texture2D
		if tex:
			return tex.get_image()
	if FileAccess.file_exists(path):
		var img := Image.new()
		if img.load(path) == OK:
			return img
	return null


func _build_fallback(glyph_table: Dictionary) -> void:
	var cats: PackedInt32Array = glyph_table.get("category", PackedInt32Array())
	var cols: PackedInt32Array = glyph_table.get("color", PackedInt32Array())
	var count := maxi(cats.size(), 1)
	tile_size = 16
	columns = 64
	var rows := int(ceil(float(count) / columns))
	image = Image.create_empty(columns * tile_size, rows * tile_size, false, Image.FORMAT_RGBA8)
	cells.resize(count)
	for g in count:
		cells[g] = g
		var c := Color(0, 0, 0, 0)
		if g < cats.size() and cats[g] != NhConst.Category.UNEXPLORED \
				and cats[g] != NhConst.Category.NOTHING:
			c = NhConst.color_of(cols[g] if g < cols.size() else 7)
		var r := Rect2i((g % columns) * tile_size + 2, (g / columns) * tile_size + 2,
				tile_size - 4, tile_size - 4)
		image.fill_rect(r, c)
	texture = ImageTexture.create_from_image(image)
	source = "fallback"


func cell_of(glyph: int) -> int:
	return cells[glyph] if glyph >= 0 and glyph < cells.size() else -1


func atlas_coords(glyph: int) -> Vector2i:
	var c := cell_of(glyph)
	return Vector2i(-1, -1) if c < 0 else Vector2i(c % columns, c / columns)


func region(glyph: int) -> Rect2:
	var a := atlas_coords(glyph)
	return Rect2(a * tile_size, Vector2(tile_size, tile_size))


func grid_size() -> Vector2i:
	return Vector2i(columns, image.get_height() / tile_size)
