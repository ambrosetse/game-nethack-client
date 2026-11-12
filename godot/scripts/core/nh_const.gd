extends RefCounted
## Constants shared with the native bridge.
##
## Keep the EV_* / IN_* values in sync with native/bridge/nhbridge.h (the
## NetHackGame class also exposes them as EV_* constants at runtime) and the
## status / condition tables with thirdparty/NetHack/include/botl.h.
## Scripts use this file through preload() so it works before the editor has
## built its global class cache (headless test runs, first import).

# --- events (worker -> frontend) -----------------------------------------
const EV_NONE := 0
const EV_INIT := 1
const EV_RAW_PRINT := 2
const EV_CREATE_WINDOW := 3
const EV_CLEAR_WINDOW := 4
const EV_DISPLAY_WINDOW := 5
const EV_DESTROY_WINDOW := 6
const EV_CURSOR := 7
const EV_PUTSTR := 8
const EV_DISPLAY_FILE := 9
const EV_PRINT_GLYPH := 10
const EV_CLIPAROUND := 11
const EV_BELL := 12
const EV_STATUS_INIT := 13
const EV_STATUS_ENABLE := 14
const EV_STATUS_UPDATE := 15
const EV_STATUS_FINISH := 16
const EV_INVENTORY := 17
const EV_PREF_UPDATE := 18
const EV_NUMBER_PAD := 19
const EV_DELAY := 20
const EV_SYNC := 21
const EV_MSG_HISTORY := 22
const EV_CHANGE_COLOR := 23
const EV_OUTRIP := 24
const EV_GAME_OVER := 25
const EV_EXIT_WINDOWS := 26
const EV_SUSPEND := 27
const EV_RESUME := 28
const EV_MESSAGE_MENU := 29
const EV_POSITIONBAR := 30
const EV_CTRL_WINDOW := 31
const EV_PLAYER_SELECTED := 32
const EV_PREV_MESSAGE := 33
const EV_GET_EVENT := 34
const EV_VISION := 35
const EV_REQ_KEY := 100
const EV_REQ_YN := 101
const EV_REQ_LINE := 102
const EV_REQ_EXTCMD := 103
const EV_REQ_MENU := 104

# --- NetHack window types (include/wintype.h) -----------------------------
const NHW_MESSAGE := 1
const NHW_STATUS := 2
const NHW_MAP := 3
const NHW_MENU := 4
const NHW_TEXT := 5
const NHW_PERMINVENT := 6

# --- menu selection modes -------------------------------------------------
const PICK_NONE := 0
const PICK_ONE := 1
const PICK_ANY := 2

# --- click modifiers for send_click ---------------------------------------
const CLICK_TRAVEL := 1
const CLICK_INSPECT := 2

# --- map size (include/global.h COLNO / ROWNO) ----------------------------
const COLNO := 80
const ROWNO := 21

# --- glyph categories (native/port/nhgodot_glyphs.c) ----------------------
enum Category {
	MONSTER, PET, DETECTED, RIDDEN, INVISIBLE, BODY, STATUE, OBJECT, CMAP,
	ZAP, SWALLOW, EXPLODE, WARNING, UNEXPLORED, NOTHING,
}

# --- status fields (botl.h enum statusfields) -----------------------------
enum Status {
	TITLE, STR, DX, CO, IN, WI, CH, ALIGN, SCORE, CAP, GOLD,
	ENE, ENEMAX, XP, AC, HD, TIME, HUNGER, HP, HPMAX,
	LEVELDESC, EXP, CONDITION, WEAPON, ARMOR, TERRAIN, VERS,
}
const MAXBLSTATS := 27

## botl.h enum blconditions, in bit order; values are translation keys.
const CONDITIONS := [
	"COND_BAREH", "COND_BLIND", "COND_BUSY", "COND_CONF", "COND_DEAF",
	"COND_ELF_IRON", "COND_FLY", "COND_FOODPOIS", "COND_GLOWHANDS",
	"COND_GRAB", "COND_HALLU", "COND_HELD", "COND_ICY", "COND_INLAVA",
	"COND_LEV", "COND_PARLYZ", "COND_RIDE", "COND_SLEEPING", "COND_SLIME",
	"COND_SLIPPERY", "COND_STONE", "COND_STRNGL", "COND_STUN",
	"COND_SUBMERGED", "COND_TERMILL", "COND_TETHERED", "COND_TRAPPED",
	"COND_UNCONSC", "COND_WOUNDEDL", "COND_HOLDING",
]

## NetHack CLR_* colours 0..15 as Godot colours (same palette as the tiles).
const COLORS := [
	Color8(70, 80, 110), Color8(200, 40, 40), Color8(40, 170, 60),
	Color8(150, 100, 40), Color8(50, 80, 210), Color8(170, 50, 170),
	Color8(40, 160, 170), Color8(180, 180, 180), Color8(200, 200, 200),
	Color8(255, 140, 30), Color8(90, 240, 90), Color8(250, 230, 60),
	Color8(100, 140, 255), Color8(250, 100, 250), Color8(100, 240, 250),
	Color8(255, 255, 255),
]

static func color_of(index: int) -> Color:
	return COLORS[clampi(index, 0, 15)]

## Character creation choices: option value, translation key, short code.
const ROLES := [
	["archeologist", "ROLE_ARC"], ["barbarian", "ROLE_BAR"],
	["caveman", "ROLE_CAV"], ["healer", "ROLE_HEA"], ["knight", "ROLE_KNI"],
	["monk", "ROLE_MON"], ["priest", "ROLE_PRI"], ["ranger", "ROLE_RAN"],
	["rogue", "ROLE_ROG"], ["samurai", "ROLE_SAM"], ["tourist", "ROLE_TOU"],
	["valkyrie", "ROLE_VAL"], ["wizard", "ROLE_WIZ"],
]
const RACES := [
	["human", "RACE_HUMAN"], ["elf", "RACE_ELF"], ["dwarf", "RACE_DWARF"],
	["gnome", "RACE_GNOME"], ["orc", "RACE_ORC"],
]
const GENDERS := [["male", "GENDER_MALE"], ["female", "GENDER_FEMALE"]]
const ALIGNS := [
	["lawful", "ALIGN_LAWFUL"], ["neutral", "ALIGN_NEUTRAL"],
	["chaotic", "ALIGN_CHAOTIC"],
]

## Valid combinations (NetHack role.c): role -> [races, aligns, genders].
const ROLE_RULES := {
	"archeologist": [["human", "dwarf", "gnome"], ["lawful", "neutral"], ["male", "female"]],
	"barbarian": [["human", "orc"], ["neutral", "chaotic"], ["male", "female"]],
	"caveman": [["human", "dwarf", "gnome"], ["lawful", "neutral"], ["male", "female"]],
	"healer": [["human", "gnome"], ["neutral"], ["male", "female"]],
	"knight": [["human"], ["lawful"], ["male", "female"]],
	"monk": [["human"], ["lawful", "neutral", "chaotic"], ["male", "female"]],
	"priest": [["human", "elf"], ["lawful", "neutral", "chaotic"], ["male", "female"]],
	"ranger": [["human", "elf", "gnome", "orc"], ["neutral", "chaotic"], ["male", "female"]],
	"rogue": [["human", "orc"], ["chaotic"], ["male", "female"]],
	"samurai": [["human"], ["lawful"], ["male", "female"]],
	"tourist": [["human"], ["neutral"], ["male", "female"]],
	"valkyrie": [["human", "dwarf"], ["lawful", "neutral"], ["female"]],
	"wizard": [["human", "elf", "gnome", "orc"], ["neutral", "chaotic"], ["male", "female"]],
}
const RACE_ALIGNS := {
	"human": ["lawful", "neutral", "chaotic"], "elf": ["chaotic"],
	"dwarf": ["lawful"], "gnome": ["neutral"], "orc": ["chaotic"],
}

## Return true when role/race/gender/align is a combination NetHack allows.
## "random" matches anything.
static func valid_combo(role: String, race: String, gender: String, align: String) -> bool:
	if role != "random":
		var r: Array = ROLE_RULES.get(role, [[], [], []])
		if race != "random" and not r[0].has(race):
			return false
		if align != "random" and not r[1].has(align):
			return false
		if gender != "random" and not r[2].has(gender):
			return false
	if race != "random" and align != "random":
		if not RACE_ALIGNS.get(race, []).has(align):
			return false
	return true
