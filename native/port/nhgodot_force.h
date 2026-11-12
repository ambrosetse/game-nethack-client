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

#endif /* NHGODOT_FORCE_H */
