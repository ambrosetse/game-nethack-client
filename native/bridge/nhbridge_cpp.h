/*
 * nhbridge_cpp.h - C++ side of the bridge (used by the Godot wrapper and the
 * headless test).  The worker thread runs the NetHack core; the frontend
 * thread polls events and pushes input.
 *
 * NetHack may be freely redistributed.  See license for details.
 */
#ifndef NHBRIDGE_CPP_H
#define NHBRIDGE_CPP_H

#include <string>
#include <vector>

#include "nhbridge.h"

namespace nhb {

struct MenuItem {
    int glyph = -1;
    int identifier = 0;
    int ch = 0;
    int gch = 0;
    int attr = 0;
    int color = 0;
    unsigned flags = 0;
    std::string text;
};

struct Event {
    int type = NHB_EV_NONE;
    long i[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::string s;
    std::string s2;
    std::vector<MenuItem> items;
};

struct GlyphInfo {
    int glyph;
    int symidx;
    int color;
    int ttychar;
    int category;      /* see nhb_glyph_category in nhgodot_glyphs.c */
    std::string symname;
    std::string name;
};

struct Config {
    std::string game_dir;      /* playground: save files, data files */
    std::string name;          /* character name ("" = ask) */
    std::string role, race, gender, align; /* "" = ask / random */
    std::string extra_options; /* appended to NETHACKOPTIONS */
    unsigned long seed = 0;    /* 0 = system entropy */
};

/* Start the game on a worker thread.  Only one game may run per process
 * (the NetHack core is full of globals); returns false if one already ran. */
bool start(const Config &cfg);

/* Has the worker finished (game over, saved, or failed)? */
bool finished();
int exit_status();

/* Has the worker thread been started in this process? */
bool started();

/* Pop every pending event (non-blocking). */
std::vector<Event> poll();

/* Wait up to timeout_ms for at least one event; returns them. */
std::vector<Event> wait(int timeout_ms);

/* Input from the frontend. */
void send_key(int key);
void send_click(int x, int y, int mod);
void send_text(const std::string &text, bool cancelled);
void send_menu(const std::vector<int> &sel, const std::vector<long> &counts,
               bool cancelled);
void send_extcmd(int index);

/* Ask the game to save and exit; returns immediately. */
void request_save_quit();

/* Block until the worker exits (used at shutdown). */
void join(int timeout_ms);

/* Glyph table (valid once the core initialised its glyph map, i.e. after
 * the first event of type NHB_EV_INIT has been seen). */
std::vector<GlyphInfo> glyph_table();
std::vector<std::string> symbol_names(); /* index = symidx */

} // namespace nhb

#endif
