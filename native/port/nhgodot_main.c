/*
 * nhgodot_main.c - NetHack entry point for the Godot frontend.
 *
 * Replaces sys/unix/unixmain.c / sys/libnh/libnhmain.c: the game runs on a
 * worker thread (see bridge/nhbridge.cpp), never changes the process working
 * directory, and every file prefix points at the "playground" directory the
 * frontend passes in (user:// resolved to an OS path).
 *
 * exit() inside the core is redirected (port/nhgodot_force.h) to
 * nhgodot_exit(), which longjmps back to nhgodot_run() so the host process
 * (the Godot editor/game) survives the end of a NetHack game.
 *
 * Derived from NetHack's sys/libnh/libnhmain.c.
 * Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985.
 * NetHack may be freely redistributed.  See license for details.
 */

#include "hack.h"
#include "dlb.h"
#include "nhbridge.h"

#include <setjmp.h>
#include <sys/stat.h>

#ifndef WIN32
#include <pwd.h>
#include <signal.h>
#include <unistd.h>
#endif

#undef exit /* we need the real one nowhere, but keep the macro out of here */

/* Only the Windows and DOS-like config headers define PATHLEN. */
#ifndef PATHLEN
#define PATHLEN BUFSZ
#endif

static char nhg_game_dir[PATHLEN];
static jmp_buf nhg_exit_jmp;
static int nhg_exit_status;
static boolean nhg_running;

void nhgodot_configure(const char *game_dir, unsigned long seed);
ATTRNORETURN void nhgodot_exit(int status) NORETURN;
int nhgodot_run(int argc, char **argv);

#ifndef WIN32
boolean whoami(void);
static struct passwd *get_unix_pw(void);
#endif
static void wd_message(void);

void
nhgodot_configure(const char *game_dir, unsigned long seed)
{
    size_t n;

    nhUse(seed); /* the seed is read back through nhb_fixed_seed() */
    (void) strncpy(nhg_game_dir, game_dir ? game_dir : "", PATHLEN - 2);
    nhg_game_dir[PATHLEN - 2] = '\0';
    n = strlen(nhg_game_dir);
    if (n > 0 && nhg_game_dir[n - 1] != '/' && nhg_game_dir[n - 1] != '\\') {
        nhg_game_dir[n] = '/';
        nhg_game_dir[n + 1] = '\0';
    }
}

/* Every fopen() in the core ends up here (see nhgodot_force.h): a relative
 * name is taken to be relative to the game directory, as if the process had
 * changed directory there the way the Unix and Windows ports do. */
#undef fopen
FILE *
nhgodot_fopen(const char *name, const char *mode)
{
    char path[PATHLEN + BUFSZ];
    boolean absolute;

    if (!name || !*nhg_game_dir)
        return fopen(name, mode);
    absolute = (name[0] == '/' || name[0] == '\\'
                || (name[0] && name[1] == ':'));
    /* fqname() results already carry the game directory, which is itself
     * relative when the caller passed a relative one */
    if (absolute || !strncmp(name, nhg_game_dir, strlen(nhg_game_dir)))
        return fopen(name, mode);
    if (strlen(nhg_game_dir) + strlen(name) >= sizeof path)
        return (FILE *) 0;
    Strcpy(path, nhg_game_dir);
    Strcat(path, name);
    return fopen(path, mode);
}

/* Every exit() in the core ends up here (see nhgodot_force.h). */
void
nhgodot_exit(int status)
{
    nhg_exit_status = status;
    if (nhg_running)
        longjmp(nhg_exit_jmp, 1);
    /* not inside nhgodot_run(): nothing sensible left to do */
    abort();
}

static void
nhg_set_prefixes(void)
{
    int i;

    for (i = 0; i < PREFIX_COUNT; i++) {
        if (gf.fqn_prefix[i])
            free((genericptr_t) gf.fqn_prefix[i]);
        gf.fqn_prefix[i] = dupstr(nhg_game_dir);
    }
}

int
nhgodot_run(int argc, char **argv)
{
    NHFILE *nhfp;
    boolean exact_username;
    volatile boolean resuming = FALSE; /* assume new game */
    volatile boolean plsel_once = FALSE;

    nhg_exit_status = EXIT_SUCCESS;
    if (setjmp(nhg_exit_jmp)) {
        nhg_running = FALSE;
        return nhg_exit_status;
    }
    nhg_running = TRUE;

    early_init(argc, argv);

    gh.hname = argv[0];
#ifdef WIN32
    svh.hackpid = (unsigned) _getpid();
#else
    svh.hackpid = getpid();
    (void) umask(0777 & ~FCMASK);
#endif

    choose_windows("shim");

    nhg_set_prefixes();

    initoptions();
#ifdef WIN32
    exact_username = FALSE;
    if (!*svp.plname) {
        const char *s = nh_getenv("USERNAME");

        if (s && *s)
            (void) strncpy(svp.plname, s, sizeof svp.plname - 1);
    }
#else
    exact_username = whoami();
#endif

    /* hero is not alive yet, but hangup/end-of-input paths look at u.uhp */
    u.uhp = 1;
    program_state.preserve_locks = 1;
#if !defined(NO_SIGNAL) && !defined(WIN32)
    sethanguphandler((SIG_RET_TYPE) hangup);
#endif

    init_nhwindows(&argc, argv);

    set_playmode(); /* sets plname to "wizard" for wizard mode */
    gp.plnamelen = exact_username ? (int) strlen(svp.plname) : 0;
    /* strip role,race,&c suffix; calls askname() if plname[] is empty
       or holds a generic user name like "player" or "games" */
    plnamesuffix();

#if defined(UNIX) || defined(VMS) /* decl.h only has locknum there */
    if (wizard) {
        /* use character name rather than lock letter for file names */
        gl.locknum = 0;
    }
#endif

    dlb_init(); /* must be before newgame() */

    /*
     * Initialize the vision system.  This must be before mklev() on a
     * new game or before a level restore on a saved game.
     */
    vision_init();

    init_sound_disp_gamewindows();

 attempt_restore:
    /*
     * getlock() complains and quits if there is already a game
     * in progress for current character name (when locknum == 0)
     * or if there are too many active games (when locknum > 0).
     * When proceeding, it creates an empty <lockname>.0 file to
     * designate the current game.
     */
    if (*svp.plname) {
        getlock();
        program_state.preserve_locks = 0; /* after getlock() */
    }

    if (*svp.plname && (nhfp = restore_saved_game()) != 0) {
        const char *fq_save = fqname(gs.SAVEF, SAVEPREFIX, 1);

        (void) chmod(fq_save, 0); /* disallow parallel restores */
        pline("Restoring save file...");
        mark_synch(); /* flush output */
        if (dorecover(nhfp)) {
            resuming = TRUE; /* not starting new game */
            wd_message();
            if (discover || wizard) {
                /* this seems like a candidate for paranoid_confirmation... */
                if (y_n("Do you want to keep the save file?") == 'n') {
                    (void) delete_savefile();
                } else {
                    (void) chmod(fq_save, FCMASK); /* back to readable */
                    nh_compress(fq_save);
                }
            }
        }
    }

    if (!resuming) {
        boolean neednewlock = (!*svp.plname);
        /* new game:  start by choosing role, race, etc;
           player might change the hero's name while doing that,
           in which case we try to restore under the new name
           and skip selection this time if that didn't succeed */
        if (!iflags.renameinprogress || iflags.defer_plname || neednewlock) {
            if (!plsel_once)
                player_selection();
            plsel_once = TRUE;
            nhb_begin(NHB_EV_PLAYER_SELECTED);
            nhb_int(0, flags.initrole);
            nhb_int(1, flags.initrace);
            nhb_int(2, flags.initgend);
            nhb_int(3, flags.initalign);
            nhb_str(svp.plname);
            nhb_post();
            if (neednewlock && *svp.plname)
                goto attempt_restore;
            if (iflags.renameinprogress) {
                /* player has renamed the hero while selecting role;
                   if locking alphabetically, the existing lock file
                   can still be used; otherwise, discard current one
                   and create another for the new character name */
#if defined(UNIX) || defined(VMS)
                if (!gl.locknum)
#endif
                {
                    /* without locknum (Windows) locks are always per
                       character name, so the lock must be replaced */
                    delete_levelfile(0); /* remove empty lock file */
                    getlock();
                }
                goto attempt_restore;
            }
        }
        newgame();
        wd_message();
    }

    /* moveloop() never returns but isn't flagged NORETURN */
    moveloop(resuming);

    nhgodot_exit(EXIT_SUCCESS);
    /*NOTREACHED*/
    return 0;
}

static void
wd_message(void)
{
    if (iflags.wiz_error_flag) {
        if (sysopt.wizards && sysopt.wizards[0]) {
            char *tmp = build_english_list(sysopt.wizards);
            pline("Only user%s %s may access debug (wizard) mode.",
                  strchr(sysopt.wizards, ' ') ? "s" : "", tmp);
            free(tmp);
        } else {
            You("cannot access debug (wizard) mode.");
        }
        wizard = FALSE; /* (paranoia) */
        if (!iflags.explore_error_flag)
            pline("Entering explore/discovery mode instead.");
    } else if (iflags.explore_error_flag) {
        You("cannot access explore mode."); /* same as enter_explore_mode */
        discover = iflags.deferred_X = FALSE; /* (more paranoia) */
    } else if (discover)
        You("are in non-scoring explore/discovery mode.");
}

/* ------------------------------------------------------------------ */
/* Port functions expected by the core (Unix flavour)                 */
/* ------------------------------------------------------------------ */

#ifndef WIN32

/* returns True iff we set plname[] to username which contains a hyphen;
 * also called by earlyarg.c, so it is not static */
boolean
whoami(void)
{
    if (!*svp.plname) {
        const char *s;

        s = nh_getenv("USER");
        if (!s || !*s)
            s = nh_getenv("LOGNAME");
        if (!s || !*s)
            s = getlogin();

        if (s && *s) {
            (void) strncpy(svp.plname, s, sizeof svp.plname - 1);
            if (strchr(svp.plname, '-'))
                return TRUE;
        }
    }
    return FALSE;
}

#ifndef NO_SIGNAL
void
sethanguphandler(void (*handler)(int))
{
    /* The game runs on a worker thread inside the Godot process; it must not
     * take over process-wide signal handling.  Hangups are delivered through
     * the bridge (NHB_IN_QUIT -> hangup()) instead, so SIGHUP keeps whatever
     * disposition the host process installed. */
    nhUse(handler);
}
#endif /* !NO_SIGNAL */

#ifdef PORT_HELP
void
port_help(void)
{
    display_file(PORT_HELP, TRUE);
}
#endif

/* validate wizard mode if player has requested access to it */
boolean
authorize_wizard_mode(void)
{
    if (sysopt.wizards && sysopt.wizards[0]) {
        if (check_user_string(sysopt.wizards))
            return TRUE;
    }
    iflags.wiz_error_flag = TRUE; /* not being allowed into wizard mode */
    return FALSE;
}

/* similar to above, validate explore mode access */
boolean
authorize_explore_mode(void)
{
#ifdef SYSCF
    if (sysopt.explorers && sysopt.explorers[0]) {
        if (check_user_string(sysopt.explorers))
            return TRUE;
    }
    iflags.explore_error_flag = TRUE; /* not allowed into explore mode */
    return FALSE;
#else
    return TRUE;
#endif
}

void
append_slash(char *name)
{
    char *ptr;

    if (!*name)
        return;
    ptr = name + (strlen(name) - 1);
    if (*ptr != '/') {
        *++ptr = '/';
        *++ptr = '\0';
    }
}

boolean
check_user_string(const char *optstr)
{
    struct passwd *pw;
    int pwlen;
    const char *eop, *w;
    char *pwname = 0;

    if (optstr[0] == '*')
        return TRUE; /* allow any user */
    if (sysopt.check_plname)
        pwname = svp.plname;
    else if ((pw = get_unix_pw()) != 0)
        pwname = pw->pw_name;
    if (!pwname || !*pwname)
        return FALSE;
    pwlen = (int) strlen(pwname);
    eop = eos((char *) optstr);
    w = optstr;
    while (w + pwlen <= eop) {
        if (!*w)
            break;
        if (isspace((uchar) *w)) {
            w++;
            continue;
        }
        if (!strncmp(w, pwname, pwlen)) {
            if (!w[pwlen] || isspace((uchar) w[pwlen]))
                return TRUE;
        }
        while (*w && !isspace((uchar) *w))
            w++;
    }
    return FALSE;
}

static struct passwd *
get_unix_pw(void)
{
    char *user;
    unsigned uid;
    static struct passwd *pw = (struct passwd *) 0;

    if (pw)
        return pw; /* cache answer */

    uid = (unsigned) getuid();
    user = getlogin();
    if (user) {
        pw = getpwnam(user);
        if (pw && ((unsigned) pw->pw_uid != uid))
            pw = 0;
    }
    if (pw == 0) {
        user = nh_getenv("USER");
        if (user) {
            pw = getpwnam(user);
            if (pw && ((unsigned) pw->pw_uid != uid))
                pw = 0;
        }
        if (pw == 0) {
            pw = getpwuid(uid);
        }
    }
    return pw;
}

char *
get_login_name(void)
{
    static char buf[BUFSZ];
    struct passwd *pw = get_unix_pw();

    buf[0] = '\0';
    if (pw)
        (void) strncpy(buf, pw->pw_name, BUFSZ - 1);
    return buf;
}

#endif /* !WIN32 */

unsigned long sys_random_seed(void);

unsigned long
sys_random_seed(void)
{
    unsigned long seed = nhb_fixed_seed();
    unsigned long pid;
    boolean no_seed = TRUE;

    if (seed) /* deterministic run requested by the frontend / test */
        return seed;
#ifdef WIN32
    pid = (unsigned long) _getpid();
#else
    pid = (unsigned long) getpid();
#endif
#if defined(DEV_RANDOM) && !defined(WIN32)
    {
        FILE *fptr = fopen(DEV_RANDOM, "r");

        if (fptr) {
            if (fread(&seed, sizeof (long), 1, fptr) == 1) {
                has_strong_rngseed = TRUE; /* decl.c */
                no_seed = FALSE;
            }
            (void) fclose(fptr);
        }
    }
#endif
    if (no_seed) {
        seed = (unsigned long) getnow();
        if (pid) {
            if (!(pid & 3L))
                pid -= 1L;
            seed *= pid;
        }
    }
    return seed;
}

/* No unique game id: NHUUID is not defined for this port, and the Unix and
 * Windows sources that normally provide these cannot be linked in. */
void
get_nhuuid(void)
{
}

void
free_nhuuid(void)
{
    (void) memset(svn.nhuuid, 0, sizeof svn.nhuuid);
}
