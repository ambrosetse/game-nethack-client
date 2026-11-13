/*
 * nhbridge.cpp - thread-safe queues between the NetHack worker thread and the
 * frontend thread.  See nhbridge.h for the protocol.
 *
 * NetHack may be freely redistributed.  See license for details.
 */
#include "nhbridge_cpp.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

extern "C" {
/* implemented in port/nhgodot_glyphs.c (needs the NetHack headers) */
int nhgodot_max_glyph(void);
int nhgodot_glyph_info(int glyph, int *symidx, int *color, int *ttychar,
                       int *category, const char **symname,
                       const char **name);
int nhgodot_max_symidx(void);
const char *nhgodot_symbol_name(int symidx);
/* implemented in port/nhgodot_main.c */
void nhgodot_configure(const char *game_dir, unsigned long seed);
}

namespace nhb {
namespace {

std::mutex ev_mutex;
std::condition_variable ev_cv;
std::deque<Event> events;

std::mutex in_mutex;
std::condition_variable in_cv;
std::deque<nhb_input> inputs;

std::atomic<bool> quit_flag{false};
std::atomic<bool> worker_started{false};
std::atomic<bool> worker_done{false};
std::atomic<int> worker_status{0};

/* the event under construction; only the worker thread touches it */
Event current;

std::string g_game_dir;
std::string g_options_env;
unsigned long g_seed = 0;

#ifdef _WIN32
HANDLE worker_handle = nullptr;
#else
pthread_t worker_thread;
#endif

void push_event(Event &&ev) {
    {
        std::lock_guard<std::mutex> lk(ev_mutex);
        events.push_back(std::move(ev));
    }
    ev_cv.notify_all();
}

void push_input(const nhb_input &in) {
    {
        std::lock_guard<std::mutex> lk(in_mutex);
        inputs.push_back(in);
    }
    in_cv.notify_all();
}

void set_env(const char *name, const std::string &value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void worker_main() {
    static char arg0[] = "nethack";
    static char *argv[] = {arg0, nullptr};
    nhgodot_configure(g_game_dir.c_str(), g_seed);
    int status = nhgodot_run(1, argv);
    worker_status = status;
    Event ev;
    ev.type = NHB_EV_GAME_OVER;
    ev.i[0] = status;
    push_event(std::move(ev));
    worker_done = true;
}

#ifdef _WIN32
unsigned __stdcall worker_trampoline(void *) {
    worker_main();
    return 0;
}
#else
void *worker_trampoline(void *) {
    worker_main();
    return nullptr;
}
#endif

/* The NetHack core recurses deeply (level generation, Lua); give the worker
 * a generous stack on every platform (macOS secondary threads default to
 * 512 KiB, Windows to 1 MiB). */
const size_t kWorkerStack = 32u * 1024u * 1024u;

void append_opt(std::string &opts, const char *key, const std::string &v) {
    if (v.empty())
        return;
    if (!opts.empty())
        opts += ",";
    opts += key;
    opts += ":";
    opts += v;
}

} // namespace

std::string clean_player_name(const std::string &name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (c == ',' || c == ':' || c < 32 || c == 127)
            continue;
        out += (char) c;
    }
    size_t b = out.find_first_not_of(' ');
    if (b == std::string::npos)
        return std::string();
    size_t e = out.find_last_not_of(' ');
    return out.substr(b, e - b + 1);
}

bool start(const Config &cfg) {
    bool expected = false;
    if (!worker_started.compare_exchange_strong(expected, true))
        return false;
    g_game_dir = cfg.game_dir;
    g_seed = cfg.seed;

    std::string opts;
    append_opt(opts, "name", clean_player_name(cfg.name));
    append_opt(opts, "role", cfg.role);
    append_opt(opts, "race", cfg.race);
    append_opt(opts, "gender", cfg.gender);
    append_opt(opts, "alignment", cfg.align);
    if (!cfg.extra_options.empty()) {
        if (!opts.empty())
            opts += ",";
        opts += cfg.extra_options;
    }
    /* Never read the player's own ~/.nethackrc: the frontend owns options.
     * An explicit (possibly empty-ish) option string disables that. */
    if (opts.empty())
        opts = "color";
    g_options_env = opts;
    set_env("NETHACKOPTIONS", g_options_env);

#ifdef _WIN32
    uintptr_t h = _beginthreadex(nullptr, (unsigned) kWorkerStack,
                                 worker_trampoline, nullptr, 0, nullptr);
    if (!h) {
        worker_done = true;
        return false;
    }
    worker_handle = (HANDLE) h;
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kWorkerStack);
    int rc = pthread_create(&worker_thread, &attr, worker_trampoline, nullptr);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        worker_done = true;
        return false;
    }
#endif
    return true;
}

bool started() { return worker_started; }
bool finished() { return worker_done; }
int exit_status() { return worker_status; }

std::vector<Event> poll() {
    std::vector<Event> out;
    std::lock_guard<std::mutex> lk(ev_mutex);
    while (!events.empty()) {
        out.push_back(std::move(events.front()));
        events.pop_front();
    }
    return out;
}

std::vector<Event> wait(int timeout_ms) {
    {
        std::unique_lock<std::mutex> lk(ev_mutex);
        ev_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                       [] { return !events.empty(); });
    }
    return poll();
}

void send_key(int key) {
    nhb_input in{};
    in.kind = NHB_IN_KEY;
    in.key = key;
    push_input(in);
}

void send_click(int x, int y, int mod) {
    nhb_input in{};
    in.kind = NHB_IN_CLICK;
    in.x = x;
    in.y = y;
    in.mod = mod;
    push_input(in);
}

void send_text(const std::string &text, bool cancelled) {
    nhb_input in{};
    in.kind = NHB_IN_TEXT;
    in.key = cancelled ? 27 : 0;
    std::strncpy(in.text, text.c_str(), sizeof in.text - 1);
    push_input(in);
}

void send_menu(const std::vector<int> &sel, const std::vector<long> &counts,
               bool cancelled) {
    nhb_input in{};
    in.kind = NHB_IN_MENU;
    if (cancelled) {
        in.nsel = -1;
    } else {
        size_t n = sel.size();
        if (n > 512)
            n = 512;
        in.nsel = (int) n;
        for (size_t k = 0; k < n; ++k) {
            in.sel[k] = sel[k];
            in.count[k] = k < counts.size() ? counts[k] : -1L;
        }
    }
    push_input(in);
}

void send_extcmd(int index) {
    nhb_input in{};
    in.kind = NHB_IN_EXTCMD;
    in.key = index;
    push_input(in);
}

void request_save_quit() {
    quit_flag = true;
    nhb_input in{};
    in.kind = NHB_IN_QUIT;
    push_input(in);
}

void join(int timeout_ms) {
    if (!worker_started)
        return;
    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(timeout_ms);
    while (!worker_done && std::chrono::steady_clock::now() < deadline) {
        /* keep feeding QUIT so every pending prompt is cancelled */
        if (quit_flag) {
            nhb_input in{};
            in.kind = NHB_IN_QUIT;
            push_input(in);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
#ifdef _WIN32
    if (worker_done && worker_handle) {
        WaitForSingleObject(worker_handle, 1000);
        CloseHandle(worker_handle);
        worker_handle = nullptr;
    }
#else
    if (worker_done)
        pthread_join(worker_thread, nullptr);
    else
        pthread_detach(worker_thread);
#endif
}

std::vector<GlyphInfo> glyph_table() {
    std::vector<GlyphInfo> out;
    int maxg = nhgodot_max_glyph();
    out.reserve((size_t) maxg);
    for (int g = 0; g < maxg; ++g) {
        GlyphInfo gi;
        const char *sn = nullptr, *nm = nullptr;
        gi.glyph = g;
        nhgodot_glyph_info(g, &gi.symidx, &gi.color, &gi.ttychar,
                           &gi.category, &sn, &nm);
        gi.symname = sn ? sn : "";
        gi.name = nm ? nm : "";
        out.push_back(std::move(gi));
    }
    return out;
}

std::vector<std::string> symbol_names() {
    std::vector<std::string> out;
    int n = nhgodot_max_symidx();
    for (int k = 0; k < n; ++k) {
        const char *s = nhgodot_symbol_name(k);
        out.emplace_back(s ? s : "");
    }
    return out;
}

} // namespace nhb

/* ---------------- C API used by the window port ----------------------- */

extern "C" {

void nhb_begin(int type) {
    nhb::current = nhb::Event();
    nhb::current.type = type;
}

void nhb_int(int slot, long value) {
    if (slot >= 0 && slot < 9)
        nhb::current.i[slot] = value;
}

void nhb_str(const char *s) { nhb::current.s = s ? s : ""; }
void nhb_str2(const char *s) { nhb::current.s2 = s ? s : ""; }

void nhb_item(int glyph, int identifier, int ch, int gch, int attr, int color,
              unsigned itemflags, const char *text) {
    nhb::MenuItem it;
    it.glyph = glyph;
    it.identifier = identifier;
    it.ch = ch;
    it.gch = gch;
    it.attr = attr;
    it.color = color;
    it.flags = itemflags;
    it.text = text ? text : "";
    nhb::current.items.push_back(std::move(it));
}

void nhb_post(void) {
    nhb::push_event(std::move(nhb::current));
    nhb::current = nhb::Event();
}

int nhb_wait_input(struct nhb_input *out) {
    std::unique_lock<std::mutex> lk(nhb::in_mutex);
    nhb::in_cv.wait(lk, [] { return !nhb::inputs.empty(); });
    *out = nhb::inputs.front();
    nhb::inputs.pop_front();
    return out->kind == NHB_IN_QUIT ? -1 : 0;
}

int nhb_request(struct nhb_input *out) {
    if (nhb::quit_flag) {
        std::memset(out, 0, sizeof *out);
        out->kind = NHB_IN_QUIT;
        nhb::current = nhb::Event();
        return -1;
    }
    nhb_post();
    return nhb_wait_input(out);
}

int nhb_quit_requested(void) { return nhb::quit_flag ? 1 : 0; }

void nhb_sleep_ms(int ms) {
    if (!nhb::quit_flag && ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

unsigned long nhb_fixed_seed(void) { return nhb::g_seed; }

const char *nhb_core_text(const char *s) {
    /* Single translation hook for core-originated text.  Core messages are
     * deliberately kept in English (the NetHack core has no message
     * catalogue); frontends translate their own strings via gettext. */
    return s;
}

} // extern "C"
