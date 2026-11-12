/*
 * nethack_game.cpp - see nethack_game.h.
 *
 * NetHack may be freely redistributed.  See license for details.
 */
#include "nethack_game.h"

#include <deque>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "nhbridge_cpp.h"

namespace godot {

namespace {

/* Events popped from the bridge but not yet dispatched (frame budget). */
std::deque<nhb::Event> pending;

const char *const kSlotNames[9] = {"a", "b", "c", "d", "e",
                                   "f", "g", "h", "i"};

String utf8(const std::string &s) { return String::utf8(s.c_str()); }

Dictionary to_dict(const nhb::Event &ev) {
    Dictionary d;
    d["type"] = ev.type;
    for (int k = 0; k < 9; ++k)
        d[kSlotNames[k]] = (int64_t) ev.i[k];
    /* core text already went through nhb_core_text() in the window port */
    d["s"] = utf8(ev.s);
    d["s2"] = utf8(ev.s2);
    if (!ev.items.empty()) {
        Array items;
        for (const nhb::MenuItem &it : ev.items) {
            Dictionary m;
            m["glyph"] = it.glyph;
            m["identifier"] = it.identifier;
            m["ch"] = it.ch;
            m["gch"] = it.gch;
            m["attr"] = it.attr;
            m["color"] = it.color;
            m["flags"] = (int64_t) it.flags;
            m["text"] = utf8(it.text);
            items.push_back(m);
        }
        d["items"] = items;
    }
    return d;
}

} // namespace

NetHackGame::NetHackGame() {}

NetHackGame::~NetHackGame() {}

void NetHackGame::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start_game", "config"),
                         &NetHackGame::start_game);
    ClassDB::bind_method(D_METHOD("is_running"), &NetHackGame::is_running);
    ClassDB::bind_method(D_METHOD("is_finished"), &NetHackGame::is_finished);
    ClassDB::bind_method(D_METHOD("has_started"), &NetHackGame::has_started);
    ClassDB::bind_method(D_METHOD("get_exit_status"),
                         &NetHackGame::get_exit_status);
    ClassDB::bind_method(D_METHOD("send_key", "key"), &NetHackGame::send_key);
    ClassDB::bind_method(D_METHOD("send_click", "x", "y", "mod"),
                         &NetHackGame::send_click);
    ClassDB::bind_method(D_METHOD("send_text", "text", "cancelled"),
                         &NetHackGame::send_text);
    ClassDB::bind_method(
        D_METHOD("send_menu", "selection", "counts", "cancelled"),
        &NetHackGame::send_menu);
    ClassDB::bind_method(D_METHOD("send_extcmd", "index"),
                         &NetHackGame::send_extcmd);
    ClassDB::bind_method(D_METHOD("request_save_quit"),
                         &NetHackGame::request_save_quit);
    ClassDB::bind_method(D_METHOD("shutdown", "timeout_ms"),
                         &NetHackGame::shutdown);
    ClassDB::bind_method(D_METHOD("poll_events"), &NetHackGame::poll_events);
    ClassDB::bind_method(D_METHOD("dispatch_events"),
                         &NetHackGame::dispatch_events);
    ClassDB::bind_method(D_METHOD("get_glyph_table"),
                         &NetHackGame::get_glyph_table);
    ClassDB::bind_method(D_METHOD("get_symbol_names"),
                         &NetHackGame::get_symbol_names);
    ClassDB::bind_method(D_METHOD("core_text", "text"),
                         &NetHackGame::core_text);
    ClassDB::bind_method(D_METHOD("set_auto_poll", "enabled"),
                         &NetHackGame::set_auto_poll);
    ClassDB::bind_method(D_METHOD("get_auto_poll"),
                         &NetHackGame::get_auto_poll);
    ClassDB::bind_method(D_METHOD("set_max_events_per_frame", "count"),
                         &NetHackGame::set_max_events_per_frame);
    ClassDB::bind_method(D_METHOD("get_max_events_per_frame"),
                         &NetHackGame::get_max_events_per_frame);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_poll"), "set_auto_poll",
                 "get_auto_poll");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_events_per_frame"),
                 "set_max_events_per_frame", "get_max_events_per_frame");

    /* every window-port event, as a Dictionary (see nhbridge.h) */
    ADD_SIGNAL(MethodInfo("nh_event", PropertyInfo(Variant::DICTIONARY,
                                                   "event")));
    /* typed conveniences */
    ADD_SIGNAL(MethodInfo("map_glyph", PropertyInfo(Variant::INT, "x"),
                          PropertyInfo(Variant::INT, "y"),
                          PropertyInfo(Variant::INT, "glyph"),
                          PropertyInfo(Variant::INT, "color"),
                          PropertyInfo(Variant::INT, "glyphflags")));
    ADD_SIGNAL(MethodInfo("message", PropertyInfo(Variant::STRING, "text"),
                          PropertyInfo(Variant::INT, "attr")));
    ADD_SIGNAL(MethodInfo("status_changed", PropertyInfo(Variant::INT, "field"),
                          PropertyInfo(Variant::STRING, "value"),
                          PropertyInfo(Variant::INT, "color"),
                          PropertyInfo(Variant::INT, "percent")));
    ADD_SIGNAL(MethodInfo("input_requested",
                          PropertyInfo(Variant::DICTIONARY, "event")));
    ADD_SIGNAL(MethodInfo("game_over", PropertyInfo(Variant::INT, "status")));

#define NHB_CONST(name, value) \
    ClassDB::bind_integer_constant(get_class_static(), "", name, value)
    NHB_CONST("EV_INIT", NHB_EV_INIT);
    NHB_CONST("EV_RAW_PRINT", NHB_EV_RAW_PRINT);
    NHB_CONST("EV_CREATE_WINDOW", NHB_EV_CREATE_WINDOW);
    NHB_CONST("EV_CLEAR_WINDOW", NHB_EV_CLEAR_WINDOW);
    NHB_CONST("EV_DISPLAY_WINDOW", NHB_EV_DISPLAY_WINDOW);
    NHB_CONST("EV_DESTROY_WINDOW", NHB_EV_DESTROY_WINDOW);
    NHB_CONST("EV_CURSOR", NHB_EV_CURSOR);
    NHB_CONST("EV_PUTSTR", NHB_EV_PUTSTR);
    NHB_CONST("EV_DISPLAY_FILE", NHB_EV_DISPLAY_FILE);
    NHB_CONST("EV_PRINT_GLYPH", NHB_EV_PRINT_GLYPH);
    NHB_CONST("EV_CLIPAROUND", NHB_EV_CLIPAROUND);
    NHB_CONST("EV_BELL", NHB_EV_BELL);
    NHB_CONST("EV_STATUS_INIT", NHB_EV_STATUS_INIT);
    NHB_CONST("EV_STATUS_ENABLE", NHB_EV_STATUS_ENABLE);
    NHB_CONST("EV_STATUS_UPDATE", NHB_EV_STATUS_UPDATE);
    NHB_CONST("EV_STATUS_FINISH", NHB_EV_STATUS_FINISH);
    NHB_CONST("EV_INVENTORY", NHB_EV_INVENTORY);
    NHB_CONST("EV_PREF_UPDATE", NHB_EV_PREF_UPDATE);
    NHB_CONST("EV_NUMBER_PAD", NHB_EV_NUMBER_PAD);
    NHB_CONST("EV_DELAY", NHB_EV_DELAY);
    NHB_CONST("EV_SYNC", NHB_EV_SYNC);
    NHB_CONST("EV_MSG_HISTORY", NHB_EV_MSG_HISTORY);
    NHB_CONST("EV_CHANGE_COLOR", NHB_EV_CHANGE_COLOR);
    NHB_CONST("EV_OUTRIP", NHB_EV_OUTRIP);
    NHB_CONST("EV_GAME_OVER", NHB_EV_GAME_OVER);
    NHB_CONST("EV_EXIT_WINDOWS", NHB_EV_EXIT_WINDOWS);
    NHB_CONST("EV_SUSPEND", NHB_EV_SUSPEND);
    NHB_CONST("EV_RESUME", NHB_EV_RESUME);
    NHB_CONST("EV_MESSAGE_MENU", NHB_EV_MESSAGE_MENU);
    NHB_CONST("EV_POSITIONBAR", NHB_EV_POSITIONBAR);
    NHB_CONST("EV_CTRL_WINDOW", NHB_EV_CTRL_WINDOW);
    NHB_CONST("EV_PLAYER_SELECTED", NHB_EV_PLAYER_SELECTED);
    NHB_CONST("EV_PREV_MESSAGE", NHB_EV_PREV_MESSAGE);
    NHB_CONST("EV_GET_EVENT", NHB_EV_GET_EVENT);
    NHB_CONST("EV_VISION", NHB_EV_VISION);
    NHB_CONST("EV_REQ_KEY", NHB_EV_REQ_KEY);
    NHB_CONST("EV_REQ_YN", NHB_EV_REQ_YN);
    NHB_CONST("EV_REQ_LINE", NHB_EV_REQ_LINE);
    NHB_CONST("EV_REQ_EXTCMD", NHB_EV_REQ_EXTCMD);
    NHB_CONST("EV_REQ_MENU", NHB_EV_REQ_MENU);
#undef NHB_CONST
}

void NetHackGame::_notification(int what) {
    if (what == NOTIFICATION_WM_CLOSE_REQUEST || what == NOTIFICATION_PREDELETE)
        shutdown(3000);
}

void NetHackGame::_process(double) {
    if (auto_poll)
        dispatch_events();
}

bool NetHackGame::start_game(const Dictionary &config) {
    nhb::Config cfg;
    cfg.game_dir = String(config.get("game_dir", "")).utf8().get_data();
    cfg.name = String(config.get("name", "")).utf8().get_data();
    cfg.role = String(config.get("role", "")).utf8().get_data();
    cfg.race = String(config.get("race", "")).utf8().get_data();
    cfg.gender = String(config.get("gender", "")).utf8().get_data();
    cfg.align = String(config.get("align", "")).utf8().get_data();
    cfg.extra_options = String(config.get("options", "")).utf8().get_data();
    cfg.seed = (unsigned long) (int64_t) config.get("seed", 0);
    if (cfg.game_dir.empty()) {
        UtilityFunctions::push_error("NetHackGame.start_game: game_dir is required");
        return false;
    }
    if (cfg.game_dir.back() != '/' && cfg.game_dir.back() != '\\')
        cfg.game_dir += '/';
    if (!nhb::start(cfg)) {
        UtilityFunctions::push_error(
            "NetHackGame.start_game: a game already ran in this process "
            "(the NetHack core supports one game per process)");
        return false;
    }
    return true;
}

bool NetHackGame::is_running() const {
    return nhb::started() && !nhb::finished();
}
bool NetHackGame::is_finished() const { return nhb::finished(); }
bool NetHackGame::has_started() const { return nhb::started(); }
int NetHackGame::get_exit_status() const { return nhb::exit_status(); }

void NetHackGame::send_key(int key) { nhb::send_key(key); }
void NetHackGame::send_click(int x, int y, int mod) {
    nhb::send_click(x, y, mod);
}
void NetHackGame::send_text(const String &text, bool cancelled) {
    nhb::send_text(text.utf8().get_data(), cancelled);
}
void NetHackGame::send_menu(const PackedInt32Array &selection,
                            const PackedInt64Array &counts, bool cancelled) {
    std::vector<int> sel;
    std::vector<long> cnt;
    for (int64_t k = 0; k < selection.size(); ++k)
        sel.push_back(selection[k]);
    for (int64_t k = 0; k < counts.size(); ++k)
        cnt.push_back((long) counts[k]);
    nhb::send_menu(sel, cnt, cancelled);
}
void NetHackGame::send_extcmd(int index) { nhb::send_extcmd(index); }
void NetHackGame::request_save_quit() {
    if (is_running())
        nhb::request_save_quit();
}
void NetHackGame::shutdown(int timeout_ms) {
    if (!is_running())
        return;
    nhb::request_save_quit();
    nhb::join(timeout_ms);
}

Array NetHackGame::poll_events() {
    Array out;
    for (const nhb::Event &ev : pending)
        out.push_back(to_dict(ev));
    pending.clear();
    for (const nhb::Event &ev : nhb::poll())
        out.push_back(to_dict(ev));
    return out;
}

int NetHackGame::dispatch_events() {
    for (nhb::Event &ev : nhb::poll())
        pending.push_back(std::move(ev));
    int n = 0;
    while (!pending.empty() && n < max_events_per_frame) {
        nhb::Event ev = std::move(pending.front());
        pending.pop_front();
        ++n;
        Dictionary d = to_dict(ev);
        switch (ev.type) {
        case NHB_EV_PRINT_GLYPH:
            emit_signal("map_glyph", (int) ev.i[1], (int) ev.i[2],
                        (int) ev.i[3], (int) ev.i[4], (int) ev.i[7]);
            break;
        case NHB_EV_PUTSTR:
            if (ev.i[2] == 1 /* window type NHW_MESSAGE */)
                emit_signal("message", d["s"], (int) ev.i[1]);
            break;
        case NHB_EV_STATUS_UPDATE:
            emit_signal("status_changed", (int) ev.i[0], d["s"],
                        (int) ev.i[3], (int) ev.i[2]);
            break;
        default:
            break;
        }
        emit_signal("nh_event", d);
        if (ev.type >= NHB_EV_REQ_KEY)
            emit_signal("input_requested", d);
        if (ev.type == NHB_EV_GAME_OVER)
            emit_signal("game_over", (int) ev.i[0]);
    }
    return n;
}

Dictionary NetHackGame::get_glyph_table() const {
    std::vector<nhb::GlyphInfo> table = nhb::glyph_table();
    PackedInt32Array symidx, color, ttychar, category;
    PackedStringArray symname, name;
    for (const nhb::GlyphInfo &g : table) {
        symidx.push_back(g.symidx);
        color.push_back(g.color);
        ttychar.push_back(g.ttychar);
        category.push_back(g.category);
        symname.push_back(utf8(g.symname));
        name.push_back(utf8(g.name));
    }
    Dictionary d;
    d["count"] = (int64_t) table.size();
    d["symidx"] = symidx;
    d["color"] = color;
    d["ttychar"] = ttychar;
    d["category"] = category;
    d["symname"] = symname;
    d["name"] = name;
    return d;
}

PackedStringArray NetHackGame::get_symbol_names() const {
    PackedStringArray out;
    for (const std::string &s : nhb::symbol_names())
        out.push_back(utf8(s));
    return out;
}

String NetHackGame::core_text(const String &s) const {
    return utf8(nhb_core_text(s.utf8().get_data()));
}

} // namespace godot
