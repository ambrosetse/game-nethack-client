/*
 * native_test.cpp - headless test harness for the NetHack bridge.
 *
 *   nethack_native_test --dump-glyphs OUT.tsv
 *       Writes the glyph table (one line per glyph) for tools/check_glyph_
 *       coverage.py and tools/make_tileset.py.
 *
 *   nethack_native_test --play GAMEDIR [--seed N] [--expect-restore]
 *       Starts a game with a fixed seed, plays a scripted key sequence,
 *       answers every prompt, asserts that map, status and message events
 *       arrived, then saves (S, y) and checks the game exited cleanly and a
 *       save file exists.  With --expect-restore, asserts that the run
 *       restored the previous save instead of starting a new character.
 *
 * Exit status 0 = pass.  Uses only the bridge (no Godot).
 *
 * NetHack may be freely redistributed.  See license for details.
 */
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "nhbridge_cpp.h"

namespace fs = std::filesystem;

static int dump_glyphs(const char *path) {
    std::vector<nhb::GlyphInfo> t = nhb::glyph_table();
    FILE *f = std::fopen(path, "wb");
    if (!f) {
        std::perror(path);
        return 2;
    }
    std::fprintf(f, "glyph\tsymidx\tcolor\tttychar\tcategory\tsymname\tname\n");
    for (const nhb::GlyphInfo &g : t)
        std::fprintf(f, "%d\t%d\t%d\t%d\t%d\t%s\t%s\n", g.glyph, g.symidx,
                     g.color, g.ttychar, g.category, g.symname.c_str(),
                     g.name.c_str());
    std::fclose(f);
    std::printf("wrote %zu glyphs to %s\n", t.size(), path);
    return t.empty() ? 1 : 0;
}

static bool contains(const std::string &hay, const char *needle) {
    return hay.find(needle) != std::string::npos;
}

static int play(const std::string &game_dir, unsigned long seed,
                bool expect_restore) {
    nhb::Config cfg;
    cfg.game_dir = game_dir;
    if (cfg.game_dir.back() != '/' && cfg.game_dir.back() != '\\')
        cfg.game_dir += '/';
    cfg.name = "tester";
    cfg.role = "Valkyrie";
    cfg.race = "human";
    cfg.gender = "female";
    cfg.align = "neutral";
    cfg.extra_options = "color,!autopickup,!legacy,!splash_screen,"
                        "pettype:none,time,showexp";
    cfg.seed = seed;

    /* glyph metadata must be usable before and during a game */
    if (nhb::glyph_table().empty()) {
        std::fprintf(stderr, "FAIL: empty glyph table\n");
        return 1;
    }
    if (!nhb::start(cfg)) {
        std::fprintf(stderr, "FAIL: could not start the worker\n");
        return 1;
    }

    /* scripted keys: search, walk around, look at inventory, then save */
    const char *script = "s" "l" "h" "j" "k" "i" "5s" ":" "\x10";
    size_t pos = 0;
    bool saving = false;
    int n_glyph = 0, n_status = 0, n_msg = 0, n_req = 0, n_menu = 0;
    bool restored = false, game_over = false;
    int status = -1;
    std::string all_msgs;
    auto t0 = std::chrono::steady_clock::now();

    while (!game_over) {
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(120)) {
            std::fprintf(stderr, "FAIL: timed out\n");
            nhb::request_save_quit();
            nhb::join(5000);
            return 1;
        }
        for (const nhb::Event &ev : nhb::wait(200)) {
            switch (ev.type) {
            case NHB_EV_PRINT_GLYPH:
                ++n_glyph;
                break;
            case NHB_EV_STATUS_UPDATE:
                ++n_status;
                break;
            case NHB_EV_PUTSTR:
                if (ev.i[2] == 1 /* NHW_MESSAGE */) {
                    ++n_msg;
                    all_msgs += ev.s;
                    all_msgs += '\n';
                    if (contains(ev.s, "welcome back")
                        || contains(ev.s, "Restoring"))
                        restored = true;
                }
                break;
            case NHB_EV_RAW_PRINT:
                std::printf("raw: %s\n", ev.s.c_str());
                break;
            case NHB_EV_DISPLAY_WINDOW:
            case NHB_EV_DISPLAY_FILE:
                if (ev.i[2])
                    nhb::send_key(27);
                break;
            case NHB_EV_REQ_KEY:
                ++n_req;
                if (script[pos]) {
                    nhb::send_key((unsigned char) script[pos++]);
                } else if (!saving) {
                    saving = true;
                    nhb::send_key('S');
                } else {
                    nhb::send_key(27);
                }
                break;
            case NHB_EV_REQ_YN:
                ++n_req;
                /* "Really save?" and any other confirmation */
                if (saving || contains(ev.s, "save"))
                    nhb::send_key('y');
                else
                    nhb::send_key(ev.i[0] ? (int) ev.i[0] : 'n');
                break;
            case NHB_EV_REQ_LINE:
                ++n_req;
                nhb::send_text(ev.i[0] == 1 ? "tester" : "", ev.i[0] != 1);
                break;
            case NHB_EV_REQ_EXTCMD:
                ++n_req;
                nhb::send_extcmd(-1);
                break;
            case NHB_EV_REQ_MENU:
                ++n_req;
                ++n_menu;
                nhb::send_menu({}, {}, true);
                break;
            case NHB_EV_GAME_OVER:
                game_over = true;
                status = (int) ev.i[0];
                break;
            default:
                break;
            }
        }
    }
    nhb::join(5000);

    std::printf("events: glyphs=%d status=%d messages=%d requests=%d "
                "menus=%d exit=%d restored=%d\n",
                n_glyph, n_status, n_msg, n_req, n_menu, status,
                restored ? 1 : 0);
    std::printf("--- messages ---\n%s----------------\n", all_msgs.c_str());

    int fails = 0;
    if (n_glyph == 0) {
        std::fprintf(stderr, "FAIL: no map (print_glyph) events\n");
        ++fails;
    }
    if (n_status == 0) {
        std::fprintf(stderr, "FAIL: no status_update events\n");
        ++fails;
    }
    if (n_msg == 0) {
        std::fprintf(stderr, "FAIL: no message window output\n");
        ++fails;
    }
    if (!saving) {
        std::fprintf(stderr, "FAIL: the scripted keys were not consumed\n");
        ++fails;
    }
    if (status != 0) {
        std::fprintf(stderr, "FAIL: exit status %d\n", status);
        ++fails;
    }
    bool have_save = false;
    std::error_code ec;
    fs::path save_dir = fs::path(game_dir) / "save";
    if (fs::is_directory(save_dir, ec))
        for (const auto &e : fs::directory_iterator(save_dir, ec))
            if (e.is_regular_file())
                have_save = true;
    if (!have_save) {
        std::fprintf(stderr, "FAIL: no save file in %s\n",
                     save_dir.string().c_str());
        ++fails;
    }
    if (expect_restore && !restored) {
        std::fprintf(stderr, "FAIL: the saved game was not restored\n");
        ++fails;
    }
    std::printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    std::string game_dir;
    unsigned long seed = 42;
    bool expect_restore = false;
    for (int k = 1; k < argc; ++k) {
        if (!std::strcmp(argv[k], "--dump-glyphs") && k + 1 < argc)
            return dump_glyphs(argv[++k]);
        if (!std::strcmp(argv[k], "--play") && k + 1 < argc)
            game_dir = argv[++k];
        else if (!std::strcmp(argv[k], "--seed") && k + 1 < argc)
            seed = std::strtoul(argv[++k], nullptr, 10);
        else if (!std::strcmp(argv[k], "--expect-restore"))
            expect_restore = true;
    }
    if (game_dir.empty()) {
        std::fprintf(stderr,
                     "usage: %s --dump-glyphs OUT.tsv | --play GAMEDIR "
                     "[--seed N] [--expect-restore]\n",
                     argv[0]);
        return 2;
    }
    return play(game_dir, seed, expect_restore);
}
