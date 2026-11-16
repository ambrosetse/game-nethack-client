/*
 * nhbridge.h - thread bridge between the NetHack core (worker thread) and a
 * frontend (the Godot main thread, or the headless test harness).
 *
 * The NetHack window port (port/nhgodot_winprocs.c) is plain C and only sees
 * this C API.  The implementation (bridge/nhbridge.cpp) is C++ and owns two
 * thread-safe queues:
 *
 *   events  : worker -> frontend.  Every window_procs call that produces
 *             output becomes one event; nothing is dropped.
 *   input   : frontend -> worker.  Key presses, mouse clicks, text lines and
 *             menu selections.  The worker blocks on this queue whenever the
 *             core asks for input.
 *
 * Copyright (c) the NetHack Godot frontend contributors.
 * NetHack may be freely redistributed.  See license for details.
 */
#ifndef NHBRIDGE_H
#define NHBRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Event types (worker -> frontend).  Keep in sync with NetHackGame
 * constants exposed to GDScript and with godot/scripts/core/nh_const.gd. */
enum nhb_event_type {
    NHB_EV_NONE = 0,
    NHB_EV_INIT,            /* window port initialised */
    NHB_EV_RAW_PRINT,       /* s=text, a=bold */
    NHB_EV_CREATE_WINDOW,   /* a=winid, b=type */
    NHB_EV_CLEAR_WINDOW,    /* a=winid */
    NHB_EV_DISPLAY_WINDOW,  /* a=winid, b=blocking, c=1 if the frontend must
                               answer with a key to dismiss (text windows),
                               d=window type, s=accumulated text */
    NHB_EV_DESTROY_WINDOW,  /* a=winid */
    NHB_EV_CURSOR,          /* a=winid, b=x, c=y */
    NHB_EV_PUTSTR,          /* a=winid, b=attr, s=text */
    NHB_EV_DISPLAY_FILE,    /* s=file contents, s2=file name, a=complain,
                               c=1: answer with a key to dismiss */
    NHB_EV_PRINT_GLYPH,     /* a=winid b=x c=y d=glyph e=color f=ttychar
                               g=symidx h=glyphflags i=bkglyph */
    NHB_EV_CLIPAROUND,      /* a=x, b=y */
    NHB_EV_BELL,
    NHB_EV_STATUS_INIT,
    NHB_EV_STATUS_ENABLE,   /* a=field, b=enable, s=name, s2=format */
    NHB_EV_STATUS_UPDATE,   /* a=field, b=chg, c=percent, d=color,
                               e=condmask (BL_CONDITION), s=value */
    NHB_EV_STATUS_FINISH,
    NHB_EV_INVENTORY,       /* items = inventory, a=in-use flag */
    NHB_EV_PREF_UPDATE,     /* s=option name */
    NHB_EV_NUMBER_PAD,      /* a=mode */
    NHB_EV_DELAY,
    NHB_EV_SYNC,
    NHB_EV_MSG_HISTORY,     /* s=message, a=is_restoring */
    NHB_EV_CHANGE_COLOR,    /* a=color, b=rgb, c=reverse */
    NHB_EV_OUTRIP,          /* a=winid, b=how, s=killer text */
    NHB_EV_GAME_OVER,       /* a=exit status */
    NHB_EV_EXIT_WINDOWS,    /* s=message */
    NHB_EV_SUSPEND,         /* s=message */
    NHB_EV_RESUME,
    NHB_EV_MESSAGE_MENU,    /* a=let, b=how, s=mesg */
    NHB_EV_POSITIONBAR,     /* s=features */
    NHB_EV_CTRL_WINDOW,     /* a=winid, b=request */
    NHB_EV_PLAYER_SELECTED, /* a=role b=race c=gender d=align s=plname */
    NHB_EV_PREV_MESSAGE,    /* ^P: frontend shows its message history */
    NHB_EV_GET_EVENT,       /* get_nh_event poll (no payload) */
    NHB_EV_VISION,          /* a=COLNO b=ROWNO c=hero x d=hero y,
                               s=COLNO*ROWNO chars, '1' = currently seen */
    /* blocking requests: the worker waits for a matching input */
    NHB_EV_REQ_KEY = 100,   /* a=1 if a map position (poskey) is acceptable */
    NHB_EV_REQ_YN,          /* s=prompt, s2=choices, a=default */
    NHB_EV_REQ_LINE,        /* s=prompt, a=purpose (0 getlin, 1 askname) */
    NHB_EV_REQ_EXTCMD,      /* items = extended commands */
    NHB_EV_REQ_MENU         /* a=winid b=how s=prompt items=menu */
};

/* Input kinds (frontend -> worker) */
enum nhb_input_kind {
    NHB_IN_KEY = 1,   /* key */
    NHB_IN_CLICK,     /* x, y, mod (1 = travel/left, 2 = inspect/right) */
    NHB_IN_TEXT,      /* text (getlin / askname); cancelled => key 27 */
    NHB_IN_MENU,      /* selection count + indices/counts; -1 = cancelled */
    NHB_IN_EXTCMD,    /* key = index into extended command list, -1 cancel */
    NHB_IN_QUIT       /* save and quit as soon as possible */
};

struct nhb_input {
    int kind;
    int key;
    int x, y, mod;
    char text[256];
    int nsel;          /* NHB_IN_MENU: number of selected entries, -1 cancel */
    int sel[512];      /* indices into the menu item list */
    long count[512];   /* per-selection count (-1 = all) */
};

/* --- worker side ------------------------------------------------------- */

/* Open a new event; fill it in with the setters, then post it. */
void nhb_begin(int type);
void nhb_int(int slot, long value);       /* slot 0..8 = a..i */
void nhb_str(const char *s);
void nhb_str2(const char *s);
void nhb_item(int glyph, int identifier, int ch, int gch, int attr,
              int color, unsigned itemflags, const char *text);
void nhb_post(void);

/* Post the event currently being built as a request and block until the
 * frontend answers.  Returns 0 on success, -1 if the bridge is shutting
 * down. */
int nhb_request(struct nhb_input *out);

/* Wait for the next input of any kind without posting a request event.
 * Used by nhgetch/nh_poskey after a REQ_KEY event has been posted. */
int nhb_wait_input(struct nhb_input *out);

/* True once the frontend asked the game to save and quit. */
int nhb_quit_requested(void);

/* Small sleep used for delay_output (milliseconds). */
void nhb_sleep_ms(int ms);

/* Fixed RNG seed (0 = use system entropy). */
unsigned long nhb_fixed_seed(void);

/* Translation hook for core messages: the single place where core strings
 * reach the frontend.  Currently identity (core messages stay English) but
 * the frontend may install a catalogue later; see README "Localisation". */
const char *nhb_core_text(const char *s);

/* --- process / thread control (used by the main entry point) ---------- */

/* Run NetHack on the calling thread.  argv[0] is the program name. */
int nhgodot_run(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* NHBRIDGE_H */
