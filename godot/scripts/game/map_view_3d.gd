extends Node3D
## 3D renderer over the same GameModel as the 2D view.
##
## Map features (walls, floors, doors, corridors, stairs, water, lava) become
## simple meshes with the materials in res://assets/3d/; monsters, objects
## and the player are billboarded Sprite3Ds cut from the tile atlas.
## Remembered cells use darkened material copies and hide their sprites'
## monsters (fog of war); distance fog comes from the WorldEnvironment.
## F3 toggles between the orbit and top-down cameras.

signal clicked(cell: Vector2i, button: int)

const NhConst = preload("res://scripts/core/nh_const.gd")
const MAT_DIR := "res://assets/3d/"
const KINDS := ["wall", "floor", "corridor", "door", "door_open", "stairs", "water", "lava", "tree"]

var model
var atlas
var glyph_table := {}

var _meshes := {}
var _mats := {}
var _mats_dim := {}
var _features := {}  # cell index -> MeshInstance3D
var _sprites := {}   # cell index -> Sprite3D
var _kind_cache := {}
var _camera: Camera3D
var _pivot: Node3D
var _top_down := false
var _yaw := -0.6
var _pitch := -0.85
var _distance := 14.0
var _hero_light: OmniLight3D
var _dragging := false


func setup(p_model, p_atlas, p_glyph_table: Dictionary) -> void:
	model = p_model
	atlas = p_atlas
	glyph_table = p_glyph_table
	_build_resources()
	_build_scene()
	model.cell_changed.connect(_on_cell)
	model.map_cleared.connect(redraw)
	model.vision_changed.connect(_refresh_fog)
	model.hero_moved.connect(func(_x, _y): _follow())
	redraw()


func set_atlas(p_atlas) -> void:
	atlas = p_atlas
	redraw()


func activate(on: bool) -> void:
	visible = on
	_camera.current = on
	if on:
		_follow()


func _build_resources() -> void:
	for k in KINDS:
		var path: String = MAT_DIR + k + ".tres"
		var m: StandardMaterial3D = load(path) if ResourceLoader.exists(path) else StandardMaterial3D.new()
		_mats[k] = m
		var dim := m.duplicate() as StandardMaterial3D
		dim.albedo_color = m.albedo_color.darkened(0.6)
		dim.emission_enabled = false
		_mats_dim[k] = dim
	var wall := BoxMesh.new()
	wall.size = Vector3(1, 1.6, 1)
	_meshes["wall"] = wall
	var floor_mesh := BoxMesh.new()
	floor_mesh.size = Vector3(1, 0.1, 1)
	_meshes["floor"] = floor_mesh
	_meshes["corridor"] = floor_mesh
	var door := BoxMesh.new()
	door.size = Vector3(1, 1.4, 0.2)
	_meshes["door"] = door
	var door_open := BoxMesh.new()
	door_open.size = Vector3(0.15, 1.4, 0.9)
	_meshes["door_open"] = door_open
	var stairs := PrismMesh.new()
	stairs.size = Vector3(0.9, 0.5, 0.9)
	_meshes["stairs"] = stairs
	var liquid := BoxMesh.new()
	liquid.size = Vector3(1, 0.05, 1)
	_meshes["water"] = liquid
	_meshes["lava"] = liquid
	var tree := CylinderMesh.new()
	tree.top_radius = 0.05
	tree.bottom_radius = 0.4
	tree.height = 1.5
	_meshes["tree"] = tree


func _build_scene() -> void:
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.02, 0.02, 0.04)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.35, 0.35, 0.45)
	e.ambient_light_energy = 0.6
	e.fog_enabled = true
	e.fog_light_color = Color(0.05, 0.05, 0.09)
	e.fog_density = 0.035
	e.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.environment = e
	add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation = Vector3(deg_to_rad(-60), deg_to_rad(30), 0)
	sun.light_energy = 0.35
	add_child(sun)
	_hero_light = OmniLight3D.new()
	_hero_light.light_color = Color(1.0, 0.85, 0.6)
	_hero_light.omni_range = 7.0
	_hero_light.light_energy = 1.6
	_hero_light.shadow_enabled = true
	add_child(_hero_light)
	_pivot = Node3D.new()
	add_child(_pivot)
	_camera = Camera3D.new()
	_camera.fov = 55
	_pivot.add_child(_camera)
	_update_camera()


## Map a glyph to a feature kind ("" = no mesh).
func kind_of(glyph: int) -> String:
	if glyph < 0:
		return ""
	if _kind_cache.has(glyph):
		return _kind_cache[glyph]
	var k := ""
	var cats: PackedInt32Array = glyph_table.get("category", PackedInt32Array())
	var names: PackedStringArray = glyph_table.get("symname", PackedStringArray())
	if glyph < cats.size() and cats[glyph] == NhConst.Category.CMAP and glyph < names.size():
		k = feature_kind(names[glyph])
	_kind_cache[glyph] = k
	return k


## Feature kind for a NetHack map symbol name (S_vwall, S_room, ...).
static func feature_kind(sym: String) -> String:
	match sym:
		"S_vwall", "S_hwall", "S_tlcorn", "S_trcorn", "S_blcorn", "S_brcorn", \
		"S_crwall", "S_tuwall", "S_tdwall", "S_tlwall", "S_trwall", "S_lavawall":
			return "lava" if sym == "S_lavawall" else "wall"
		"S_room", "S_darkroom", "S_ndoor", "S_altar", "S_grave", "S_throne", \
		"S_sink", "S_fountain", "S_ice", "S_air", "S_cloud", "S_engroom":
			return "floor"
		"S_corr", "S_litcorr", "S_engrcorr":
			return "corridor"
		"S_vcdoor", "S_hcdoor", "S_vcdbridge", "S_hcdbridge":
			return "door"
		"S_vodoor", "S_hodoor", "S_vodbridge", "S_hodbridge":
			return "door_open"
		"S_upstair", "S_dnstair", "S_upladder", "S_dnladder", "S_brupstair", \
		"S_brdnstair", "S_brupladder", "S_brdnladder":
			return "stairs"
		"S_pool", "S_water":
			return "water"
		"S_lava":
			return "lava"
		"S_tree":
			return "tree"
		"S_bars":
			return "wall"
	return ""


const PLAIN := ["S_stone", "S_room", "S_darkroom", "S_air", "S_ndoor", "S_corr", "S_litcorr"]


## True for glyphs drawn as a billboard: monsters, objects, the player, and
## map decorations without a mesh of their own (traps, altars, fountains...).
func _is_sprite_glyph(glyph: int) -> bool:
	if glyph < 0:
		return false
	var cats: PackedInt32Array = glyph_table.get("category", PackedInt32Array())
	if glyph >= cats.size():
		return true
	var c := cats[glyph]
	if c == NhConst.Category.UNEXPLORED or c == NhConst.Category.NOTHING:
		return false
	if c != NhConst.Category.CMAP:
		return true
	var names: PackedStringArray = glyph_table.get("symname", PackedStringArray())
	var sym: String = names[glyph] if glyph < names.size() else ""
	var k := feature_kind(sym)
	return (k == "" or k == "floor") and not PLAIN.has(sym)


func redraw() -> void:
	for n in _features.values():
		n.queue_free()
	for n in _sprites.values():
		n.queue_free()
	_features.clear()
	_sprites.clear()
	for y in model.height:
		for x in model.width:
			_on_cell(x, y)


func _cell_pos(x: int, y: int) -> Vector3:
	return Vector3(x, 0, y)


func _on_cell(x: int, y: int) -> void:
	var i: int = model.index(x, y)
	var g: int = model.glyphs[i]
	var bk: int = model.bk_glyphs[i]
	# The feature under an object/monster comes from the background glyph.
	var sprite := _is_sprite_glyph(g)
	var kind := kind_of(bk if sprite and bk >= 0 else g)
	if kind == "" and sprite:
		kind = "floor"
	_set_feature(i, x, y, kind)
	_set_sprite(i, x, y, g if sprite else -1)


func _set_feature(i: int, x: int, y: int, kind: String) -> void:
	var node: MeshInstance3D = _features.get(i)
	if kind == "":
		if node:
			node.queue_free()
			_features.erase(i)
		return
	if node == null:
		node = MeshInstance3D.new()
		add_child(node)
		_features[i] = node
	node.set_meta("kind", kind)
	node.mesh = _meshes[kind]
	var p := _cell_pos(x, y)
	match kind:
		"wall":
			p.y = 0.8
		"door", "door_open":
			p.y = 0.7
			var vertical := _neighbour_is_wall(x, y - 1) or _neighbour_is_wall(x, y + 1)
			node.rotation.y = PI / 2 if vertical else 0.0
		"stairs":
			p.y = 0.25
		"tree":
			p.y = 0.75
		_:
			p.y = -0.05
	node.position = p
	node.material_override = _mats[kind] if model.is_visible(x, y) else _mats_dim[kind]


func _neighbour_is_wall(x: int, y: int) -> bool:
	return kind_of(model.glyph_at(x, y)) == "wall"


func _set_sprite(i: int, x: int, y: int, glyph: int) -> void:
	var s: Sprite3D = _sprites.get(i)
	if glyph < 0:
		if s:
			s.queue_free()
			_sprites.erase(i)
		return
	if s == null:
		s = Sprite3D.new()
		s.billboard = BaseMaterial3D.BILLBOARD_FIXED_Y
		s.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
		s.alpha_cut = SpriteBase3D.ALPHA_CUT_DISCARD
		s.shaded = false
		s.region_enabled = true
		add_child(s)
		_sprites[i] = s
	s.texture = atlas.texture
	s.region_rect = atlas.region(glyph)
	s.pixel_size = 0.85 / atlas.tile_size
	s.position = _cell_pos(x, y) + Vector3(0, 0.45, 0)
	s.modulate = Color.WHITE if model.is_visible(x, y) else Color(0.45, 0.45, 0.55)


func _refresh_fog() -> void:
	for i in _features:
		var node: MeshInstance3D = _features[i]
		var lit: bool = model.visible[i] == 1
		var kind: String = node.get_meta("kind")
		node.material_override = _mats[kind] if lit else _mats_dim[kind]
	for i in _sprites:
		_sprites[i].modulate = Color.WHITE if model.visible[i] == 1 else Color(0.45, 0.45, 0.55)


func _follow() -> void:
	var c: Vector2i = model.hero if model.hero.x >= 0 else model.cursor
	if c.x < 0:
		return
	var p := _cell_pos(c.x, c.y)
	_pivot.position = p
	_hero_light.position = p + Vector3(0, 1.5, 0)


func toggle_camera() -> void:
	_top_down = not _top_down
	_update_camera()


func zoom_by(f: float) -> void:
	_distance = clampf(_distance / f, 5.0, 40.0)
	_update_camera()


func _update_camera() -> void:
	if _top_down:
		_pivot.rotation = Vector3(-PI / 2 + 0.001, 0, 0)
	else:
		_pivot.rotation = Vector3(_pitch, _yaw, 0)
	_camera.position = Vector3(0, 0, _distance)


func _unhandled_input(event: InputEvent) -> void:
	if not visible:
		return
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_MIDDLE:
			_dragging = event.pressed
		elif event.pressed:
			match event.button_index:
				MOUSE_BUTTON_WHEEL_UP:
					zoom_by(1.1)
				MOUSE_BUTTON_WHEEL_DOWN:
					zoom_by(1.0 / 1.1)
				MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT:
					var cell := _pick_cell(event.position)
					if model.in_bounds(cell.x, cell.y):
						clicked.emit(cell, event.button_index)
						get_viewport().set_input_as_handled()
	elif event is InputEventMouseMotion and _dragging and not _top_down:
		_yaw -= event.relative.x * 0.01
		_pitch = clampf(_pitch - event.relative.y * 0.01, -1.45, -0.2)
		_update_camera()


## Intersect the mouse ray with the floor plane (y = 0).
func _pick_cell(screen: Vector2) -> Vector2i:
	var from := _camera.project_ray_origin(screen)
	var dir := _camera.project_ray_normal(screen)
	if absf(dir.y) < 0.0001:
		return Vector2i(-1, -1)
	var t := -from.y / dir.y
	var hit := from + dir * t
	return Vector2i(roundi(hit.x), roundi(hit.z))
