extends Node2D
## 2D renderer: a TileMapLayer fed from the shared GameModel.
##
## Remembered-but-not-visible cells are darkened by a second TileMapLayer of
## translucent black squares; clicking sends nh_poskey clicks (left = travel,
## right = inspect) through `clicked`.

signal clicked(cell: Vector2i, button: int)

const NhConst = preload("res://scripts/core/nh_const.gd")

var model
var atlas
var _map: TileMapLayer
var _fog: TileMapLayer
var _camera: Camera2D
var _source_id := 0


func setup(p_model, p_atlas) -> void:
	model = p_model
	atlas = p_atlas
	_map = TileMapLayer.new()
	_map.tile_set = _make_tileset()
	add_child(_map)
	_fog = TileMapLayer.new()
	_fog.tile_set = _make_fog_tileset()
	add_child(_fog)
	_camera = Camera2D.new()
	_camera.position_smoothing_enabled = true
	_camera.zoom = Vector2(1.5, 1.5)
	add_child(_camera)
	model.cell_changed.connect(_on_cell)
	model.map_cleared.connect(redraw)
	model.vision_changed.connect(_redraw_fog)
	model.hero_moved.connect(func(_x, _y): _center())
	redraw()


func set_atlas(p_atlas) -> void:
	atlas = p_atlas
	_map.tile_set = _make_tileset()
	redraw()


func activate(on: bool) -> void:
	visible = on
	_camera.enabled = on
	if on:
		_camera.make_current()
		_center()


func _make_tileset() -> TileSet:
	var ts := TileSet.new()
	ts.tile_size = Vector2i(atlas.tile_size, atlas.tile_size)
	var src := TileSetAtlasSource.new()
	src.texture = atlas.texture
	src.texture_region_size = ts.tile_size
	var grid: Vector2i = atlas.grid_size()
	for y in grid.y:
		for x in grid.x:
			src.create_tile(Vector2i(x, y))
	_source_id = ts.add_source(src)
	return ts


func _make_fog_tileset() -> TileSet:
	var size: int = atlas.tile_size
	var img := Image.create_empty(size, size, false, Image.FORMAT_RGBA8)
	img.fill(Color(0.02, 0.02, 0.06, 0.55))
	var ts := TileSet.new()
	ts.tile_size = Vector2i(size, size)
	var src := TileSetAtlasSource.new()
	src.texture = ImageTexture.create_from_image(img)
	src.texture_region_size = ts.tile_size
	src.create_tile(Vector2i.ZERO)
	ts.add_source(src, 0)
	return ts


func redraw() -> void:
	_map.clear()
	for y in model.height:
		for x in model.width:
			_on_cell(x, y)
	_redraw_fog()


func _on_cell(x: int, y: int) -> void:
	var g: int = model.glyph_at(x, y)
	var coords: Vector2i = atlas.atlas_coords(g) if g >= 0 else Vector2i(-1, -1)
	if coords.x < 0:
		_map.erase_cell(Vector2i(x, y))
	else:
		_map.set_cell(Vector2i(x, y), _source_id, coords)


func _redraw_fog() -> void:
	_fog.clear()
	for y in model.height:
		for x in model.width:
			if model.is_remembered(x, y):
				_fog.set_cell(Vector2i(x, y), 0, Vector2i.ZERO)


func _center() -> void:
	var c: Vector2i = model.hero if model.hero.x >= 0 else model.cursor
	if c.x >= 0:
		_camera.position = (Vector2(c) + Vector2(0.5, 0.5)) * atlas.tile_size


func zoom_by(f: float) -> void:
	_camera.zoom = (_camera.zoom * f).clamp(Vector2(0.5, 0.5), Vector2(4, 4))


func _unhandled_input(event: InputEvent) -> void:
	if not visible:
		return
	if event is InputEventMouseButton and event.pressed:
		match event.button_index:
			MOUSE_BUTTON_WHEEL_UP:
				zoom_by(1.1)
			MOUSE_BUTTON_WHEEL_DOWN:
				zoom_by(1.0 / 1.1)
			MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT:
				var cell := _map.local_to_map(_map.get_local_mouse_position())
				if model.in_bounds(cell.x, cell.y):
					clicked.emit(cell, event.button_index)
					get_viewport().set_input_as_handled()
