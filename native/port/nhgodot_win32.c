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

/* -- file name wildcards (files.c: get_saved_games) -- */

static HANDLE ffhandle = (HANDLE) 0;
static WIN32_FIND_DATAA ffd;

int
findfirst(char *path)
{
    if (ffhandle && ffhandle != INVALID_HANDLE_VALUE)
        FindClose(ffhandle);
    ffhandle = FindFirstFileA(path, &ffd);
    return (ffhandle == INVALID_HANDLE_VALUE) ? 0 : 1;
}

int
findnext(void)
{
    if (!ffhandle || ffhandle == INVALID_HANDLE_VALUE)
        return 0;
    return FindNextFileA(ffhandle, &ffd) ? 1 : 0;
}

char *
foundfile_buffer(void)
{
    return &ffd.cFileName[0];
}

void
Delay(int ms)
{
    Sleep((DWORD) ms);
}

/* -- paths (files.c: fqname, reveal_paths, make_converted_name) -- */

/* windsys.c asks the user profile API (userenv.lib); %USERPROFILE% holds
   the same directory and needs no extra library. */
boolean
get_user_home_folder(char *homebuf, size_t sz)
{
    const char *home = nh_getenv("USERPROFILE");

    if (!home || !*home || !homebuf || !sz)
        return FALSE;
    Snprintf(homebuf, sz, "%s", home);
    return TRUE;
}

/* The frontend always sets the game directories itself; there is no
   portable-device install, and an empty string tells files.c so. */
const char *
get_portable_device(void)
{
    return "";
}

/* Expand %VAR% environment references in a path (from windsys.c). */
char *
translate_path_variables(const char *str, char *buf)
{
    const char *src;
    char evar[BUFSZ], *dest, *envp, *eptr = evar;
    boolean in_evar = FALSE;
    size_t ccount, ecount = 0, destcount = 0, slen = str ? strlen(str) : 0;

    if (!slen || !buf) {
        if (buf)
            *buf = '\0';
        return buf;
    }
    dest = buf;
    src = str;
    for (ccount = 0;
         ccount < slen && destcount < (BUFSZ - 1) && ecount < (BUFSZ - 1);
         ++ccount, ++src) {
        if (*src == '%') {
            if (in_evar) {
                *eptr = '\0';
                envp = nh_getenv(evar);
                if (envp) {
                    size_t elen = strlen(envp);

                    if (elen + destcount < (size_t) (BUFSZ - 1)) {
                        Strcpy(dest, envp);
                        dest += elen;
                        destcount += elen;
                    }
                }
            } else {
                eptr = evar;
                ecount = 0;
            }
            in_evar = !in_evar;
            continue;
        }
        if (in_evar) {
            *eptr++ = *src;
            ecount++;
        } else {
            *dest++ = *src;
            destcount++;
        }
    }
    *dest = '\0';
    return buf;
}

/* -- version.c -- */

char *
get_port_id(char *buf)
{
#if defined(_M_X64) || defined(_M_AMD64)
    Strcpy(buf, "x64");
#elif defined(_M_ARM64)
    Strcpy(buf, "ARM64");
#else
    Strcpy(buf, "x86");
#endif
    return buf;
}

/* #version's "copy to paste buffer": the Godot frontend owns the
   clipboard, so the core has nothing to do here. */
void
port_insert_pastebuf(char *buf)
{
    nhUse(buf);
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
