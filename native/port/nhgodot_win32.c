/*
 * nhgodot_win32.c - the handful of Windows port functions the NetHack core
 * expects (normally provided by sys/windows/windmain.c, windsys.c and
 * consoletty.c, which are tied to the console/mswin interfaces and cannot
 * be linked into a GDExtension).
 *
 * Only compiled on Windows.  Derived from sys/windows/windmain.c and
 * sys/windows/windsys.c.
 * Copyright (c) Derek S. Ray, 2015.
 * NetHack may be freely redistributed.  See license for details.
 */

#ifdef _WIN32

#include "win32api.h"
#include "hack.h"
#include "nhbridge.h"

#include <errno.h>
#include <io.h>
#include <sys/stat.h>

/* globals other port files would otherwise define */
int GUILaunched = 0;
boolean getreturn_enabled = FALSE;
int redirect_stdout = 0;
char orgdir[PATHLEN];
HANDLE hStdOut;
int windows_startup_state = 0;

/* extern.h only declares this for UNIX and VMS */
boolean check_user_string(const char *optstr);

boolean
authorize_wizard_mode(void)
{
    if (sysopt.wizards && sysopt.wizards[0]
        && check_user_string(sysopt.wizards))
        return TRUE;
    iflags.wiz_error_flag = TRUE;
    return FALSE;
}

boolean
authorize_explore_mode(void)
{
    if (sysopt.explorers && sysopt.explorers[0]
        && check_user_string(sysopt.explorers))
        return TRUE;
    iflags.explore_error_flag = TRUE;
    return FALSE;
}

boolean
check_user_string(const char *optstr)
{
    return (optstr && optstr[0] == '*') ? TRUE : FALSE;
}

char *
get_login_name(void)
{
    static char buf[BUFSZ];
    const char *s = nh_getenv("USERNAME");

    buf[0] = '\0';
    if (s)
        (void) strncpy(buf, s, BUFSZ - 1);
    return buf;
}

void
append_slash(char *name)
{
    char *ptr;

    if (!*name)
        return;
    ptr = name + (strlen(name) - 1);
    if (*ptr != '\\' && *ptr != '/') {
        *++ptr = '/';
        *++ptr = '\0';
    }
}

/* Replace characters that are illegal in Windows file names. */
void
nt_regularize(char *s)
{
    unsigned char *lp;

    for (lp = (unsigned char *) s; *lp; lp++)
        if (*lp == '?' || *lp == '"' || *lp == '\\' || *lp == '/'
            || *lp == '>' || *lp == '<' || *lp == '*' || *lp == '|'
            || *lp == ':' || *lp < 32 || *lp > 126)
            *lp = '_';
}

boolean
file_exists(const char *path)
{
    struct stat sb;

    return (path && stat(path, &sb) == 0) ? TRUE : FALSE;
}

boolean
file_newer(const char *a_path, const char *b_path)
{
    struct stat a_sb, b_sb;

    if (stat(a_path, &a_sb))
        return FALSE;
    if (stat(b_path, &b_sb))
        return TRUE;
    return difftime(a_sb.st_mtime, b_sb.st_mtime) > 0 ? TRUE : FALSE;
}

/* Simplified PC_LOCKING getlock(): one lock file per character name; an
   existing lock means an interrupted game, which we try to recover. */
int
getlock(void)
{
    int fd;
    const char *fq_lock;

    if (!lock_file(HLOCK, LOCKPREFIX, 10)) {
        raw_print("Cannot lock the playground; quitting.");
        nh_terminate(EXIT_FAILURE);
    }
    set_levelfile_name(gl.lock, 0);
    fq_lock = fqname(gl.lock, LEVELPREFIX, 1);
    if ((fd = open(fq_lock, 0)) != -1) {
        (void) nhclose(fd);
        if (recover_savefile())
            raw_print("Recovered an interrupted game.");
    }
    fd = creat(fq_lock, FCMASK);
    unlock_file(HLOCK);
    if (fd == -1) {
        raw_printf("Cannot create lock file %s (%s).", fq_lock,
                   strerror(errno));
        return 0;
    }
    if (write(fd, (char *) &svh.hackpid, sizeof svh.hackpid)
        != sizeof svh.hackpid)
        raw_printf("Cannot write lock file %s.", fq_lock);
    (void) nhclose(fd);
    return 1;
}

void
interject_assistance(int num, int interjection_type, genericptr_t ptr1,
                     genericptr_t ptr2)
{
    nhUse(num);
    nhUse(interjection_type);
    nhUse(ptr1);
    nhUse(ptr2);
}

void
interject(int interjection_type)
{
    nhUse(interjection_type);
}

ATTRNORETURN void
nethack_exit(int code)
{
    exit(code); /* -> nhgodot_exit() via nhgodot_force.h */
}

void
getreturn(const char *str)
{
    nhUse(str); /* nothing to wait for: the frontend owns the screen */
}

void
win32_abort(void)
{
    nhgodot_exit(EXIT_FAILURE);
}

void
nt_assert_failed(const char *expression, const char *filepath, int line)
{
    impossible("NHASSERT(%s) in '%s' at line %d", expression, filepath,
               line);
}

char *
windows_exepath(void)
{
    static char path[MAX_PATH];

    if (!GetModuleFileNameA(NULL, path, MAX_PATH))
        path[0] = '\0';
    return path;
}

int
set_keyhandling_via_option(void)
{
    return 0; /* keyboard handling belongs to the frontend */
}

int
windows_early_options(const char *window_opt)
{
    nhUse(window_opt);
    return 0;
}

void
consoletty_preference_update(const char *pref)
{
    nhUse(pref);
}

void
toggle_mouse_support(void)
{
}

void
map_subkeyvalue(char *op)
{
    nhUse(op);
}

void
set_altkeyhandling(const char *handler)
{
    nhUse(handler);
}

void
raw_clear_screen(void)
{
}

int
set_win32_option(const char *name, const char *val)
{
    nhUse(name);
    nhUse(val);
    return 0;
}

int
alternative_palette(char *op)
{
    nhUse(op);
    return 0;
}

#ifdef PORT_HELP
void
port_help(void)
{
    display_file(PORT_HELP, TRUE);
}
#endif

ATTRNORETURN void
error(const char *s, ...)
{
    char buf[BUFSZ];
    va_list the_args;

    va_start(the_args, s);
    (void) vsnprintf(buf, sizeof buf, s, the_args);
    va_end(the_args);
    raw_print(buf);
    nhgodot_exit(EXIT_FAILURE);
}

#endif /* _WIN32 */
