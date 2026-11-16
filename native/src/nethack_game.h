/*
 * nethack_game.h - NetHackGame, the Godot node that owns the NetHack worker
 * thread.  Every window-port call arrives as an `nh_event` signal carrying a
 * Dictionary (plus a few typed convenience signals); input goes back through
 * the send_* methods, which push into the bridge's thread-safe queue.
 *
 * NetHack may be freely redistributed.  See license for details.
 */
#ifndef NETHACK_GAME_H
#define NETHACK_GAME_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

class NetHackGame : public Node {
    GDCLASS(NetHackGame, Node)

    bool auto_poll = true;
    int max_events_per_frame = 4000;

protected:
    static void _bind_methods();

public:
    NetHackGame();
    ~NetHackGame() override;

    void _process(double delta) override;
    void _notification(int what);

    /* config keys: game_dir (OS path), name, role, race, gender, align,
     * options (extra NETHACKOPTIONS), seed (int) */
    bool start_game(const Dictionary &config);
    bool is_running() const;
    bool is_finished() const;
    bool has_started() const;
    int get_exit_status() const;

    void send_key(int key);
    void send_click(int x, int y, int mod);
    void send_text(const String &text, bool cancelled);
    void send_menu(const PackedInt32Array &selection,
                   const PackedInt64Array &counts, bool cancelled);
    void send_extcmd(int index);
    void request_save_quit();
    void shutdown(int timeout_ms);

    /* Pops pending events and returns them (also emitted when auto_poll). */
    Array poll_events();
    int dispatch_events();

    Dictionary get_glyph_table() const;
    PackedStringArray get_symbol_names() const;
    String core_text(const String &s) const;

    void set_auto_poll(bool v) { auto_poll = v; }
    bool get_auto_poll() const { return auto_poll; }
    void set_max_events_per_frame(int v) { max_events_per_frame = v; }
    int get_max_events_per_frame() const { return max_events_per_frame; }
};

} // namespace godot

#endif
