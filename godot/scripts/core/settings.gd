extends Node
## Persistent frontend settings (user://settings.cfg), language and fonts.
##
## Autoloaded as "Settings".  Everything the player can change in Options
## lives here; other scripts read the properties and listen to `changed`.

signal changed(key: String)

const PATH := "user://settings.cfg"
const LOCALES := ["en", "zh_TW", "zh_CN", "ko", "ja"]
## Native names, shown untranslated in the language picker.
const LOCALE_NAMES := {
	"en": "English", "zh_TW": "繁體中文", "zh_CN": "简体中文",
	"ko": "한국어", "ja": "日本語",
}
const CJK_FONT := "res://assets/fonts/NotoSansCJKsc-Regular.otf"

var language := "en"
var renderer := "2d"          # "2d" or "3d"
var number_pad := false       # false = vi-keys, true = numpad digits
var tileset := "original"     # "original" or "classic" (win/share tiles)
var player_name := ""
var role := "random"
var race := "random"
var gender := "random"
var align := "random"
var music_volume := 0.8
## Set just before a process restart so the next boot starts this game.
var pending_start := {}

var _cfg := ConfigFile.new()


func _ready() -> void:
	load_settings()
	_install_fonts()
	apply_language()


func load_settings() -> void:
	if _cfg.load(PATH) != OK:
		language = _default_language()
		return
	language = _cfg.get_value("ui", "language", _default_language())
	if not LOCALES.has(language):
		language = "en"
	renderer = _cfg.get_value("ui", "renderer", "2d")
	tileset = _cfg.get_value("ui", "tileset", "original")
	number_pad = _cfg.get_value("input", "number_pad", false)
	player_name = _cfg.get_value("player", "name", "")
	role = _cfg.get_value("player", "role", "random")
	race = _cfg.get_value("player", "race", "random")
	gender = _cfg.get_value("player", "gender", "random")
	align = _cfg.get_value("player", "align", "random")
	pending_start = _cfg.get_value("boot", "pending_start", {})


func save_settings() -> void:
	_cfg.set_value("ui", "language", language)
	_cfg.set_value("ui", "renderer", renderer)
	_cfg.set_value("ui", "tileset", tileset)
	_cfg.set_value("input", "number_pad", number_pad)
	_cfg.set_value("player", "name", player_name)
	_cfg.set_value("player", "role", role)
	_cfg.set_value("player", "race", race)
	_cfg.set_value("player", "gender", gender)
	_cfg.set_value("player", "align", align)
	_cfg.set_value("boot", "pending_start", pending_start)
	_cfg.save(PATH)


## Change one setting, persist it and notify listeners.
func set_value(key: String, value: Variant) -> void:
	if get(key) == value:
		return
	set(key, value)
	save_settings()
	if key == "language":
		apply_language()
	changed.emit(key)


func apply_language() -> void:
	TranslationServer.set_locale(language)


func _default_language() -> String:
	var os_locale := OS.get_locale()
	if os_locale.begins_with("zh"):
		if os_locale.contains("TW") or os_locale.contains("HK") or os_locale.contains("Hant"):
			return "zh_TW"
		return "zh_CN"
	for l in ["ko", "ja"]:
		if os_locale.begins_with(l):
			return l
	return "en"


## Use Godot's default font for Latin text with the bundled Noto Sans CJK as
## fallback, so every UI language renders without tofu.  The CJK font is
## fetched by tools/fetch_fonts.py; without it the UI still works but CJK
## text depends on system fonts.
func _install_fonts() -> void:
	var base: Font = ThemeDB.fallback_font
	var fallbacks: Array[Font] = []
	if ResourceLoader.exists(CJK_FONT):
		var cjk := load(CJK_FONT) as Font
		if cjk:
			fallbacks.append(cjk)
	if fallbacks.is_empty():
		var sys := SystemFont.new()
		sys.font_names = PackedStringArray([
			"Noto Sans CJK SC", "Microsoft YaHei", "PingFang SC",
			"Malgun Gothic", "Apple SD Gothic Neo", "Yu Gothic",
			"Hiragino Sans", "Noto Sans CJK JP",
		])
		fallbacks.append(sys)
	var font := base.duplicate() as Font
	font.fallbacks = fallbacks
	var theme: Theme = ThemeDB.get_project_theme()
	if theme == null:
		theme = ThemeDB.get_default_theme()
	theme.default_font = font
	ThemeDB.fallback_font = font
