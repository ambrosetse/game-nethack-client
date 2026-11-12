/*
 * nhgodot_force.h - force-included (-include / /FI) into every NetHack core C
 * file compiled into the GDExtension.
 *
 * The NetHack core ends the process with exit() when a game is over.  Inside
 * a Godot editor/game process that would kill the whole application, so every
 * exit() from core code is redirected to nhgodot_exit(), which unwinds the
 * worker thread back to nhgodot_run() (longjmp) and reports the status to the
 * frontend as a GAME_OVER event.
 *
 * NetHack may be freely redistributed.  See license for details.
 */
#ifndef NHGODOT_FORCE_H
#define NHGODOT_FORCE_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
__declspec(noreturn) void nhgodot_exit(int status);
#else
void nhgodot_exit(int status) __attribute__((noreturn));
#endif

#ifdef __cplusplus
}
#endif

/* Include the system header first so its own declaration of exit() is not
 * rewritten, then redirect every later use. */
#include <stdlib.h>
#define exit(status) nhgodot_exit(status)

/* The core opens data files (nhlib.lua, the level scripts, help text...)
 * by bare name, relying on the process having changed directory to the game
 * directory; without DLB, dlb_fopen() is plain fopen().  The Godot process
 * never changes directory, so relative names are resolved against the game
 * directory instead (nhgodot_main.c).  Absolute names, which is what fqname()
 * returns, are passed through unchanged. */
#include <stdio.h>
FILE *nhgodot_fopen(const char *name, const char *mode);
#define fopen(name, mode) nhgodot_fopen(name, mode)

/* MSVC's <stdlib.h> defines min()/max() for C code.  NetHack's win32api.h
 * refuses to be included after them ("should be included first"), and it is
 * the first include of several core files, so drop them here. */
#if defined(_MSC_VER) && !defined(__cplusplus)
#undef min
#undef max
#endif

/* cfgfiles.c's assure_syscf_file() checks that sysconf is readable with
 * open(SYSCF_FILE), relative to the working directory, unless the platform is
 * WIN32.  The Godot process never changes its working directory, so on Unix
 * that check fails although fopen_config_file() would find the file under the
 * SYSCONFPREFIX game directory.  SConstruct defines NHGODOT_CFGFILES for
 * cfgfiles.c only; that file's single open() call is the sysconf check. */
#if defined(NHGODOT_CFGFILES) && !defined(_WIN32)
#include <fcntl.h>
int nhgodot_open_syscf(const char *name, int flags);
#define open(name, flags) nhgodot_open_syscf(name, flags)
#endif

#endif /* NHGODOT_FORCE_H */
