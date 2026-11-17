extends Node
## Stand-in for the native NetHackGame node in the headless tests: reports a
## running game and records the input Session forwards to it.

var keys: Array = []
var clicks: Array = []


func is_running() -> bool:
	return true


func has_started() -> bool:
	return true


func send_key(key: int) -> void:
	keys.append(key)


func send_click(x: int, y: int, mod: int) -> void:
	clicks.append([x, y, mod])


func shutdown(_ms: int) -> void:
	pass
