/*
 * nhgodot_winprocs.c - the "godot" NetHack window port.
 *
 * Every window_procs entry is implemented here.  Each call that produces
 * output is turned into one bridge event (see bridge/nhbridge.h); calls that
 * need an answer post a request event and block the worker thread until the
 * frontend replies through the input queue.  Nothing is silently dropped:
 * procs without a natural visual counterpart (curs, mark_synch, number_pad,
 * ...) still emit an event so the frontend can observe them.
 *
 * The port occupies NetHack's SHIM_GRAPHICS slot ("shim" window system) so
 * no core file has to change; win/shim/winshim.c is not compiled.
 *
 * NetHack may be freely redistributed.  See license for details.
 */
#include "hack.h"
#include "func_tab.h"
#include "dlb.h"

#include "nhbridge.h"

#ifndef SHIM_GRAPHICS
#error "nhgodot_winprocs.c must be compiled with -DSHIM_GRAPHICS"
#endif

/* ------------------------------------------------------------------ */
/* window bookkeeping                                                  */

#define NHG_MAXWIN 64
#define NHG_MSGHIST 100

struct nhg_item {
    anything id;
    int glyph;
    int ch, gch, attr, color;
    unsigned itemflags;
    char *text;
};

struct nhg_window {
    int used;
    int type;
    /* text accumulated by putstr (text windows, menus used as text) */
    char *text;
    size_t textlen, textcap;
    /* menu */
    struct nhg_item *items;
    int nitems, capitems;
    char *prompt;
    unsigned long mbehavior;
};

static struct nhg_window nhg_wins[NHG_MAXWIN];
static int nhg_hung_up = 0;

static char *nhg_msghist[NHG_MSGHIST];
static int nhg_msgh_start = 0, nhg_msgh_count = 0, nhg_msgh_iter = 0;

static char *
nhg_strdup(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    char *r = (char *) alloc((unsigned) n + 1);

    if (n)
        memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static struct nhg_window *
nhg_win(winid w)
{
    if (w < 0 || w >= NHG_MAXWIN || !nhg_wins[w].used)
        return (struct nhg_window *) 0;
    return &nhg_wins[w];
}

static void
nhg_clear_text(struct nhg_window *w)
{
    if (w->text)
        free(w->text);
    w->text = 0;
    w->textlen = w->textcap = 0;
}

static void
nhg_clear_menu(struct nhg_window *w)
{
    int k;

    for (k = 0; k < w->nitems; ++k)
        if (w->items[k].text)
            free(w->items[k].text);
    if (w->items)
        free(w->items);
    w->items = 0;
    w->nitems = w->capitems = 0;
    if (w->prompt)
        free(w->prompt);
    w->prompt = 0;
}

static void
nhg_append_text(struct nhg_window *w, const char *s)
{
    size_t n = strlen(s);

    if (w->textlen + n + 2 > w->textcap) {
        size_t cap = w->textcap ? w->textcap * 2 : 256;
        char *nt;

        while (cap < w->textlen + n + 2)
            cap *= 2;
        nt = (char *) alloc((unsigned) cap);
        if (w->textlen)
            memcpy(nt, w->text, w->textlen);
        if (w->text)
            free(w->text);
        w->text = nt;
        w->textcap = cap;
    }
    memcpy(w->text + w->textlen, s, n);
    w->textlen += n;
    w->text[w->textlen++] = '\n';
    w->text[w->textlen] = '\0';
}

static void
nhg_remember_message(const char *msg)
{
    int slot;

    if (!msg || !*msg)
        return;
    if (nhg_msgh_count < NHG_MSGHIST) {
        slot = (nhg_msgh_start + nhg_msgh_count) % NHG_MSGHIST;
        nhg_msgh_count++;
    } else {
        slot = nhg_msgh_start;
        nhg_msgh_start = (nhg_msgh_start + 1) % NHG_MSGHIST;
    }
    if (nhg_msghist[slot])
        free(nhg_msghist[slot]);
    nhg_msghist[slot] = nhg_strdup(msg);
}

/* The frontend asked to save and quit.  Hang up once: the core then saves
 * (if a game is in progress) and exits, which unwinds the worker thread.
 * Until that happens every prompt is answered with ESC. */
static int
nhg_quit_answer(void)
{
    if (!nhg_hung_up) {
        nhg_hung_up = 1;
        hangup(0);
    }
    return '\033';
}

/* Wait for a key-like answer after a request event has been posted. */
static int
nhg_wait_key(struct nhb_input *in)
{
    for (;;) {
        if (nhb_wait_input(in) < 0)
            return -1;
        if (in->kind == NHB_IN_KEY || in->kind == NHB_IN_CLICK)
            return 0;
        /* TEXT/MENU/EXTCMD answers to a key request are stale; drop them */
    }
}

/* Post the current event as a request expecting a key; returns the key. */
static int
nhg_request_key(void)
{
    struct nhb_input in;

    if (nhb_request(&in) < 0)
        return nhg_quit_answer();
    if (in.kind != NHB_IN_KEY && in.kind != NHB_IN_CLICK
        && nhg_wait_key(&in) < 0)
        return nhg_quit_answer();
    return in.kind == NHB_IN_KEY ? in.key : '\n';
}

/* ------------------------------------------------------------------ */
/* window_procs implementation                                         */

static void
godot_init_nhwindows(int *argcp UNUSED, char **argv UNUSED)
{
    iflags.window_inited = TRUE;
    iflags.cbreak = ON;
    iflags.echo = OFF;
    nhb_begin(NHB_EV_INIT);
    nhb_post();
}

static void
godot_player_selection(void)
{
    genl_player_selection();
    nhb_begin(NHB_EV_PLAYER_SELECTED);
    nhb_int(0, flags.initrole);
    nhb_int(1, flags.initrace);
    nhb_int(2, flags.initgend);
    nhb_int(3, flags.initalign);
    nhb_str(svp.plname);
    nhb_post();
}

static void
godot_askname(void)
{
    struct nhb_input in;

    for (;;) {
        nhb_begin(NHB_EV_REQ_LINE);
        nhb_str("Who are you?");
        nhb_int(0, 1);
        if (nhb_request(&in) < 0) {
            (void) nhg_quit_answer();
            nh_terminate(EXIT_SUCCESS);
        }
        if (in.kind != NHB_IN_TEXT)
            continue;
        if (in.key == 27) {
            /* cancelled: leave the game like tty does */
            nh_terminate(EXIT_SUCCESS);
        }
        if (in.text[0]) {
            (void) strncpy(svp.plname, in.text, PL_NSIZ - 1);
            svp.plname[PL_NSIZ - 1] = '\0';
            return;
        }
    }
}

static void
godot_get_nh_event(void)
{
    nhb_begin(NHB_EV_GET_EVENT);
    nhb_post();
}

static void
godot_exit_nhwindows(const char *str)
{
    int k;

    nhb_begin(NHB_EV_EXIT_WINDOWS);
    nhb_str(str);
    nhb_post();
    for (k = 0; k < NHG_MAXWIN; ++k)
        if (nhg_wins[k].used) {
            nhg_clear_text(&nhg_wins[k]);
            nhg_clear_menu(&nhg_wins[k]);
            nhg_wins[k].used = 0;
        }
    iflags.window_inited = FALSE;
}

static void
godot_suspend_nhwindows(const char *str)
{
    nhb_begin(NHB_EV_SUSPEND);
    nhb_str(str);
    nhb_post();
}

static void
godot_resume_nhwindows(void)
{
    nhb_begin(NHB_EV_RESUME);
    nhb_post();
}

static winid
godot_create_nhwindow(int type)
{
    int k;

    for (k = 0; k < NHG_MAXWIN; ++k)
        if (!nhg_wins[k].used)
            break;
    if (k == NHG_MAXWIN) {
        panic("godot_create_nhwindow: too many windows");
        /*NOTREACHED*/
        return WIN_ERR;
    }
    (void) memset(&nhg_wins[k], 0, sizeof nhg_wins[k]);
    nhg_wins[k].used = 1;
    nhg_wins[k].type = type;
    nhb_begin(NHB_EV_CREATE_WINDOW);
    nhb_int(0, k);
    nhb_int(1, type);
    nhb_post();
    return (winid) k;
}

static void
godot_clear_nhwindow(winid window)
{
    struct nhg_window *w = nhg_win(window);

    if (w) {
        nhg_clear_text(w);
        if (w->type != NHW_MENU)
            nhg_clear_menu(w);
    }
    nhb_begin(NHB_EV_CLEAR_WINDOW);
    nhb_int(0, window);
    nhb_post();
}

static void
godot_display_nhwindow(winid window, boolean blocking)
{
    struct nhg_window *w = nhg_win(window);
    int needs_answer = 0;

    nhb_begin(NHB_EV_DISPLAY_WINDOW);
    nhb_int(0, window);
    nhb_int(1, blocking);
    nhb_int(3, w ? w->type : -1);
    if (w && (w->type == NHW_TEXT || w->type == NHW_MENU) && w->text) {
        nhb_str(w->text);
        needs_answer = 1;
    }
    nhb_int(2, needs_answer);
    if (needs_answer)
        (void) nhg_request_key();
    else
        nhb_post();
}

static void
godot_destroy_nhwindow(winid window)
{
    struct nhg_window *w = nhg_win(window);

    nhb_begin(NHB_EV_DESTROY_WINDOW);
    nhb_int(0, window);
    nhb_post();
    if (w) {
        nhg_clear_text(w);
        nhg_clear_menu(w);
        w->used = 0;
    }
}

static void
godot_curs(winid window, int x, int y)
{
    nhb_begin(NHB_EV_CURSOR);
    nhb_int(0, window);
    nhb_int(1, x);
    nhb_int(2, y);
    nhb_post();
}

static void
godot_putstr(winid window, int attr, const char *str)
{
    struct nhg_window *w = nhg_win(window);
    const char *text = nhb_core_text(str ? str : "");

    if (w && (w->type == NHW_TEXT || w->type == NHW_MENU))
        nhg_append_text(w, text);
    if (w && w->type == NHW_MESSAGE)
        nhg_remember_message(text);
    nhb_begin(NHB_EV_PUTSTR);
    nhb_int(0, window);
    nhb_int(1, attr);
    nhb_int(2, w ? w->type : -1);
    nhb_str(text);
    nhb_post();
}

static void
godot_display_file(const char *fname, boolean complain)
{
    dlb *f = dlb_fopen(fname, "r");
    char line[BUFSZ];
    struct nhg_window tmp;

    (void) memset(&tmp, 0, sizeof tmp);
    if (!f) {
        if (complain) {
            nhb_begin(NHB_EV_RAW_PRINT);
            nhb_str("Cannot open help file.");
            nhb_str2(fname);
            nhb_post();
        }
        return;
    }
    while (dlb_fgets(line, (int) sizeof line, f)) {
        size_t n = strlen(line);

        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        nhg_append_text(&tmp, line);
    }
    (void) dlb_fclose(f);
    nhb_begin(NHB_EV_DISPLAY_FILE);
    nhb_str(tmp.text ? tmp.text : "");
    nhb_str2(fname);
    nhb_int(0, complain);
    nhb_int(2, 1);
    nhg_clear_text(&tmp);
    (void) nhg_request_key();
}

static void
godot_start_menu(winid window, unsigned long mbehavior)
{
    struct nhg_window *w = nhg_win(window);

    if (w) {
        nhg_clear_menu(w);
        nhg_clear_text(w);
        w->mbehavior = mbehavior;
    }
}

static void
godot_add_menu(winid window, const glyph_info *glyphinfo,
               const ANY_P *identifier, char ch, char gch, int attr, int clr,
               const char *str, unsigned int itemflags)
{
    struct nhg_window *w = nhg_win(window);
    struct nhg_item *it;

    if (!w)
        return;
    if (w->nitems == w->capitems) {
        int cap = w->capitems ? w->capitems * 2 : 32;
        struct nhg_item *ni =
            (struct nhg_item *) alloc((unsigned) (cap * sizeof *ni));

        if (w->nitems)
            memcpy(ni, w->items, (size_t) w->nitems * sizeof *ni);
        if (w->items)
            free(w->items);
        w->items = ni;
        w->capitems = cap;
    }
    it = &w->items[w->nitems++];
    it->id = *identifier;
    it->glyph = glyphinfo ? glyphinfo->glyph : NO_GLYPH;
    it->ch = (uchar) ch;
    it->gch = (uchar) gch;
    it->attr = attr;
    it->color = clr;
    it->itemflags = itemflags;
    it->text = nhg_strdup(nhb_core_text(str ? str : ""));
}

static void
godot_end_menu(winid window, const char *prompt)
{
    struct nhg_window *w = nhg_win(window);

    if (!w)
        return;
    if (w->prompt)
        free(w->prompt);
    w->prompt = prompt ? nhg_strdup(nhb_core_text(prompt)) : 0;
}

static int
godot_select_menu(winid window, int how, MENU_ITEM_P **menu_list)
{
    struct nhg_window *w = nhg_win(window);
    struct nhb_input in;
    int k, n = 0, nsel;

    *menu_list = (MENU_ITEM_P *) 0;
    nhb_begin(NHB_EV_REQ_MENU);
    nhb_int(0, window);
    nhb_int(1, how);
    nhb_str(w && w->prompt ? w->prompt : "");
    if (w) {
        for (k = 0; k < w->nitems; ++k) {
            struct nhg_item *it = &w->items[k];

            nhb_item(it->glyph, it->id.a_void != 0 ? 1 : 0, it->ch, it->gch,
                     it->attr, it->color, it->itemflags, it->text);
        }
        /* a menu window used as a text window: show the lines */
        if (!w->nitems && w->text)
            nhb_str2(w->text);
    }
    for (;;) {
        if (nhb_request(&in) < 0) {
            (void) nhg_quit_answer();
            return -1;
        }
        if (in.kind == NHB_IN_MENU)
            break;
        if (in.kind == NHB_IN_KEY && in.key == 27)
            return -1;
        /* anything else is not an answer to a menu; ask again */
        nhb_begin(NHB_EV_REQ_MENU);
        nhb_int(0, window);
        nhb_int(1, how);
        nhb_str(w && w->prompt ? w->prompt : "");
        if (w)
            for (k = 0; k < w->nitems; ++k) {
                struct nhg_item *it = &w->items[k];

                nhb_item(it->glyph, it->id.a_void != 0 ? 1 : 0, it->ch,
                         it->gch, it->attr, it->color, it->itemflags,
                         it->text);
            }
    }
    if (in.nsel < 0)
        return -1;
    if (how == PICK_NONE || !w)
        return 0;
    nsel = in.nsel;
    if (how == PICK_ONE && nsel > 1)
        nsel = 1;
    if (nsel <= 0)
        return 0;
    *menu_list = (MENU_ITEM_P *) alloc((unsigned) (nsel * sizeof (MENU_ITEM_P)));
    for (k = 0; k < nsel; ++k) {
        int idx = in.sel[k];

        if (idx < 0 || idx >= w->nitems || w->items[idx].id.a_void == 0)
            continue;
        (*menu_list)[n].item = w->items[idx].id;
        (*menu_list)[n].count = in.count[k];
        (*menu_list)[n].itemflags = w->items[idx].itemflags;
        n++;
    }
    if (!n) {
        free((genericptr_t) *menu_list);
        *menu_list = (MENU_ITEM_P *) 0;
    }
    return n;
}

static char
godot_message_menu(char let, int how, const char *mesg)
{
    nhb_begin(NHB_EV_MESSAGE_MENU);
    nhb_int(0, (uchar) let);
    nhb_int(1, how);
    nhb_str(mesg);
    nhb_post();
    return genl_message_menu(let, how, mesg);
}

static void
godot_mark_synch(void)
{
    nhb_begin(NHB_EV_SYNC);
    nhb_int(0, 0);
    nhb_post();
}

static void
godot_wait_synch(void)
{
    nhb_begin(NHB_EV_SYNC);
    nhb_int(0, 1);
    nhb_post();
}

#ifdef CLIPPING
static void
godot_cliparound(int x, int y)
{
    nhb_begin(NHB_EV_CLIPAROUND);
    nhb_int(0, x);
    nhb_int(1, y);
    nhb_post();
}
#endif

#ifdef POSITIONBAR
static void
godot_update_positionbar(char *features)
{
    nhb_begin(NHB_EV_POSITIONBAR);
    nhb_str(features);
    nhb_post();
}
#endif

static void
godot_print_glyph(winid window, coordxy x, coordxy y,
                  const glyph_info *glyphinfo, const glyph_info *bkglyphinfo)
{
    nhb_begin(NHB_EV_PRINT_GLYPH);
    nhb_int(0, window);
    nhb_int(1, x);
    nhb_int(2, y);
    nhb_int(3, glyphinfo->glyph);
    nhb_int(4, glyphinfo->gm.sym.color);
    nhb_int(5, glyphinfo->ttychar);
    nhb_int(6, glyphinfo->gm.sym.symidx);
    nhb_int(7, (long) glyphinfo->gm.glyphflags);
    nhb_int(8, bkglyphinfo ? bkglyphinfo->glyph : NO_GLYPH);
    nhb_post();
}

static void
godot_raw_print(const char *str)
{
    nhb_begin(NHB_EV_RAW_PRINT);
    nhb_str(nhb_core_text(str ? str : ""));
    nhb_int(0, 0);
    nhb_post();
}

static void
godot_raw_print_bold(const char *str)
{
    nhb_begin(NHB_EV_RAW_PRINT);
    nhb_str(nhb_core_text(str ? str : ""));
    nhb_int(0, 1);
    nhb_post();
}

static int
godot_nhgetch(void)
{
    nhb_begin(NHB_EV_REQ_KEY);
    nhb_int(0, 0);
    return nhg_request_key();
}

/* Before each command prompt, tell the frontend which map squares the hero
 * can currently see, so renderers can fog remembered-but-unseen squares.
 * Reads the core's vision array (cansee) only; no game state changes. */
static void
nhg_post_vision(void)
{
    static char vis[COLNO * ROWNO + 1];
    coordxy x, y;

    if (!program_state.in_moveloop)
        return;
    for (y = 0; y < ROWNO; ++y)
        for (x = 0; x < COLNO; ++x)
            vis[y * COLNO + x] = (isok(x, y) && cansee(x, y)) ? '1' : '0';
    vis[COLNO * ROWNO] = '\0';
    nhb_begin(NHB_EV_VISION);
    nhb_int(0, COLNO);
    nhb_int(1, ROWNO);
    nhb_int(2, u.ux);
    nhb_int(3, u.uy);
    nhb_str(vis);
    nhb_post();
}

static int
godot_nh_poskey(coordxy *x, coordxy *y, int *mod)
{
    struct nhb_input in;

    nhg_post_vision();
    nhb_begin(NHB_EV_REQ_KEY);
    nhb_int(0, 1);
    if (nhb_request(&in) < 0)
        return nhg_quit_answer();
    while (in.kind != NHB_IN_KEY && in.kind != NHB_IN_CLICK)
        if (nhb_wait_input(&in) < 0)
            return nhg_quit_answer();
    if (in.kind == NHB_IN_CLICK) {
        *x = (coordxy) in.x;
        *y = (coordxy) in.y;
        *mod = (in.mod == 2) ? CLICK_2 : CLICK_1;
        return 0;
    }
    return in.key;
}

static void
godot_nhbell(void)
{
    nhb_begin(NHB_EV_BELL);
    nhb_post();
}

static int
godot_doprev_message(void)
{
    nhb_begin(NHB_EV_PREV_MESSAGE);
    nhb_post();
    return 0;
}

static char
godot_yn_function(const char *query, const char *resp, char def)
{
    struct nhb_input in;
    int key;

    for (;;) {
        nhb_begin(NHB_EV_REQ_YN);
        nhb_str(nhb_core_text(query ? query : ""));
        nhb_str2(resp ? resp : "");
        nhb_int(0, (uchar) def);
        if (nhb_request(&in) < 0)
            return (char) nhg_quit_answer();
        if (in.kind != NHB_IN_KEY)
            continue;
        key = in.key;
        if (!resp || !*resp)
            return (char) key; /* any key is acceptable */
        if (key == '\033') {
            if (strchr(resp, 'q'))
                return 'q';
            if (strchr(resp, 'n'))
                return 'n';
            return def;
        }
        if (key == '\n' || key == '\r' || key == ' ')
            return def;
        if (key > 0 && key < 256 && strchr(resp, (char) key))
            return (char) key;
        /* digits are allowed when the caller accepts a count */
        if (strchr(resp, '#') && key >= '0' && key <= '9')
            return '#';
    }
}

static void
godot_getlin(const char *query, char *bufp)
{
    struct nhb_input in;

    for (;;) {
        nhb_begin(NHB_EV_REQ_LINE);
        nhb_str(nhb_core_text(query ? query : ""));
        nhb_int(0, 0);
        if (nhb_request(&in) < 0) {
            (void) nhg_quit_answer();
            Strcpy(bufp, "\033");
            return;
        }
        if (in.kind == NHB_IN_TEXT)
            break;
        if (in.kind == NHB_IN_KEY && in.key == 27) {
            Strcpy(bufp, "\033");
            return;
        }
    }
    if (in.key == 27) {
        Strcpy(bufp, "\033");
        return;
    }
    (void) strncpy(bufp, in.text, BUFSZ - 1);
    bufp[BUFSZ - 1] = '\0';
}

static int
godot_get_ext_cmd(void)
{
    struct nhb_input in;
    int k;

    nhb_begin(NHB_EV_REQ_EXTCMD);
    for (k = 0; extcmdlist[k].ef_txt; ++k) {
        const struct ext_func_tab *ec = &extcmdlist[k];

        if ((ec->flags & (INTERNALCMD | CMD_NOT_AVAILABLE)) != 0)
            continue;
        if ((ec->flags & WIZMODECMD) && !wizard)
            continue;
        nhb_item(NO_GLYPH, k, ec->key, 0, 0, NO_COLOR, ec->flags,
                 ec->ef_txt);
    }
    for (;;) {
        if (nhb_request(&in) < 0) {
            (void) nhg_quit_answer();
            return -1;
        }
        if (in.kind == NHB_IN_EXTCMD)
            break;
        if (in.kind == NHB_IN_KEY && in.key == 27)
            return -1;
        nhb_begin(NHB_EV_REQ_EXTCMD);
        for (k = 0; extcmdlist[k].ef_txt; ++k) {
            const struct ext_func_tab *ec = &extcmdlist[k];

            if ((ec->flags & (INTERNALCMD | CMD_NOT_AVAILABLE)) != 0)
                continue;
            if ((ec->flags & WIZMODECMD) && !wizard)
                continue;
            nhb_item(NO_GLYPH, k, ec->key, 0, 0, NO_COLOR, ec->flags,
                     ec->ef_txt);
        }
    }
    if (in.key < 0)
        return -1;
    for (k = 0; extcmdlist[k].ef_txt; ++k)
        if (k == in.key)
            return k;
    return -1;
}

static void
godot_number_pad(int state)
{
    nhb_begin(NHB_EV_NUMBER_PAD);
    nhb_int(0, state);
    nhb_post();
}

static void
godot_delay_output(void)
{
    nhb_begin(NHB_EV_DELAY);
    nhb_post();
    nhb_sleep_ms(20);
}

#ifdef CHANGE_COLOR
static void
godot_change_color(int color, long rgb, int reverse)
{
    nhb_begin(NHB_EV_CHANGE_COLOR);
    nhb_int(0, color);
    nhb_int(1, rgb);
    nhb_int(2, reverse);
    nhb_post();
}

#ifdef MACOS9
static void
godot_change_background(int white_or_black UNUSED)
{
}

static short
set_godot_font_name(winid window UNUSED, char *fontname UNUSED)
{
    return 0;
}
#endif

static char *
godot_get_color_string(void)
{
    return (char *) "";
}
#endif

static void
godot_outrip(winid window, int how, time_t when)
{
    nhb_begin(NHB_EV_OUTRIP);
    nhb_int(0, window);
    nhb_int(1, how);
    nhb_str(svp.plname);
    nhb_post();
    /* the tombstone text itself goes into the window via putstr */
    genl_outrip(window, how, when);
}

static void
godot_preference_update(const char *pref)
{
    nhb_begin(NHB_EV_PREF_UPDATE);
    nhb_str(pref);
    nhb_post();
    genl_preference_update(pref);
}

static char *
godot_getmsghistory(boolean init)
{
    if (init)
        nhg_msgh_iter = 0;
    if (nhg_msgh_iter >= nhg_msgh_count)
        return (char *) 0;
    return nhg_msghist[(nhg_msgh_start + nhg_msgh_iter++) % NHG_MSGHIST];
}

static void
godot_putmsghistory(const char *msg, boolean restoring)
{
    if (!msg)
        return;
    nhg_remember_message(msg);
    nhb_begin(NHB_EV_MSG_HISTORY);
    nhb_str(nhb_core_text(msg));
    nhb_int(0, restoring);
    nhb_post();
}

static void
godot_status_init(void)
{
    genl_status_init();
    nhb_begin(NHB_EV_STATUS_INIT);
    nhb_post();
}

static void
godot_status_finish(void)
{
    nhb_begin(NHB_EV_STATUS_FINISH);
    nhb_post();
    genl_status_finish();
}

static void
godot_status_enablefield(int fieldidx, const char *nm, const char *fmt,
                         boolean enable)
{
    genl_status_enablefield(fieldidx, nm, fmt, enable);
    nhb_begin(NHB_EV_STATUS_ENABLE);
    nhb_int(0, fieldidx);
    nhb_int(1, enable);
    nhb_str(nm);
    nhb_str2(fmt);
    nhb_post();
}

static void
godot_status_update(int fldidx, genericptr_t ptr, int chg, int percent,
                    int color, unsigned long *colormasks UNUSED)
{
    char buf[BUFSZ];

    nhb_begin(NHB_EV_STATUS_UPDATE);
    nhb_int(0, fldidx);
    nhb_int(1, chg);
    nhb_int(2, percent);
    nhb_int(3, color);
    if (fldidx == BL_CONDITION) {
        nhb_int(4, ptr ? (long) *(unsigned long *) ptr : 0L);
    } else if (fldidx >= 0 && ptr) {
        /* decode "\GXXXXNNNN" glyph escapes (the gold symbol) to text */
        (void) decode_mixed(buf, (const char *) ptr);
        nhb_str(buf);
    }
    nhb_post();
}

static void
godot_update_inventory(int arg)
{
    struct obj *otmp;

    nhb_begin(NHB_EV_INVENTORY);
    nhb_int(0, arg);
    for (otmp = gi.invent; otmp; otmp = otmp->nobj) {
        nhb_item(obj_to_glyph(otmp, rn2_on_display_rng), otmp->invlet,
                 otmp->invlet, 0, 0, NO_COLOR,
                 (otmp->owornmask ? 1U : 0U), doname(otmp));
    }
    nhb_post();
}

static win_request_info *
godot_ctrl_nhwindow(winid window, int request, win_request_info *wri)
{
    nhb_begin(NHB_EV_CTRL_WINDOW);
    nhb_int(0, window);
    nhb_int(1, request);
    nhb_post();
    return wri;
}

/* The port lives in the SHIM_GRAPHICS slot of windows.c. */
struct window_procs shim_procs = {
    WPID(shim),
    (0 | WC_COLOR | WC_HILITE_PET | WC_INVERSE | WC_EIGHT_BIT_IN
     | WC_MOUSE_SUPPORT),
    (0
#if defined(SELECTSAVED)
     | WC2_SELECTSAVED
#endif
#if defined(STATUS_HILITES)
     | WC2_HILITE_STATUS | WC2_HITPOINTBAR | WC2_FLUSH_STATUS
     | WC2_RESET_STATUS
#endif
     | WC2_DARKGRAY | WC2_SUPPRESS_HIST | WC2_STATUSLINES),
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    godot_init_nhwindows, godot_player_selection, godot_askname,
    godot_get_nh_event, godot_exit_nhwindows, godot_suspend_nhwindows,
    godot_resume_nhwindows, godot_create_nhwindow, godot_clear_nhwindow,
    godot_display_nhwindow, godot_destroy_nhwindow, godot_curs, godot_putstr,
    genl_putmixed, godot_display_file, godot_start_menu, godot_add_menu,
    godot_end_menu, godot_select_menu, godot_message_menu, godot_mark_synch,
    godot_wait_synch,
#ifdef CLIPPING
    godot_cliparound,
#endif
#ifdef POSITIONBAR
    godot_update_positionbar,
#endif
    godot_print_glyph, godot_raw_print, godot_raw_print_bold, godot_nhgetch,
    godot_nh_poskey, godot_nhbell, godot_doprev_message, godot_yn_function,
    godot_getlin, godot_get_ext_cmd, godot_number_pad, godot_delay_output,
#ifdef CHANGE_COLOR
    godot_change_color,
#ifdef MACOS9
    godot_change_background, set_godot_font_name,
#endif
    godot_get_color_string,
#endif
    godot_outrip, godot_preference_update, godot_getmsghistory,
    godot_putmsghistory, godot_status_init, godot_status_finish,
    godot_status_enablefield, godot_status_update, genl_can_suspend_no,
    godot_update_inventory, godot_ctrl_nhwindow,
};
