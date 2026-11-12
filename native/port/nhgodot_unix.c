/*
 * nhgodot_unix.c - terminal functions the NetHack core expects on Unix
 * (normally provided by sys/share/unixtty.c).
 *
 * unixtty.c reprograms the controlling terminal and, on Linux, includes
 * <curses.h> and calls has_colors() from libcurses.  The Godot frontend runs
 * the game on a worker thread inside the Godot process and owns no terminal,
 * so these are no-ops and the build needs no curses headers or library.
 *
 * Only compiled on Linux and macOS.  Derived from sys/share/unixtty.c.
 * NetHack may be freely redistributed.  See license for details.
 */

#ifndef _WIN32

#include <fcntl.h>

#include "hack.h"
#include "nhbridge.h"

/* there is no terminal to save or restore */
void
gettty(void)
{
}

void
settty(const char *s)
{
    if (s)
        raw_print(s);
    iflags.echo = ON;
    iflags.cbreak = OFF;
}

void
setftty(void)
{
    iflags.cbreak = ON;
    iflags.echo = OFF;
}

/* no terminal: there is no interrupt character to enable or disable */
void
intron(void)
{
}

void
introff(void)
{
}

/* sysconf lives in the game directory, not the working directory; see
 * nhgodot_force.h */
int nhgodot_open_syscf(const char *name, int flags);

int
nhgodot_open_syscf(const char *name, int flags)
{
    return open(fqname(name, SYSCONFPREFIX, 0), flags);
}

#ifdef __linux__
/* console font mapping (ioctl.c, unixunix.c) only applies to a tty */
void linux_mapon(void);
void linux_mapoff(void);

void
linux_mapon(void)
{
}

void
linux_mapoff(void)
{
}
#endif /* __linux__ */

DISABLE_WARNING_FORMAT_NONLITERAL

/* fatal error: report it and end the game without ending the process */
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

RESTORE_WARNING_FORMAT_NONLITERAL

#endif /* !_WIN32 */
