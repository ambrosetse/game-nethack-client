/*
 * zlib.h - minimal stand-in for zlib used by the Godot frontend build.
 *
 * NetHack's Unix configuration otherwise defaults to COMPRESS, which forks
 * /usr/bin/compress after every save; forking from inside a Godot process is
 * not acceptable and the program is missing on most systems.  Building with
 * ZLIB_COMP and this header keeps the save/restore code path in-process.
 * The "compressed" file is a plain byte-for-byte copy (only the .gz suffix
 * is cosmetic), so save files stay portable and readable by this frontend.
 *
 * Only the four calls src/files.c uses are provided.
 *
 * NetHack may be freely redistributed.  See license for details.
 */
#ifndef NHGODOT_ZLIB_SHIM_H
#define NHGODOT_ZLIB_SHIM_H

#include <stdio.h>

typedef FILE *gzFile;

static inline gzFile
gzopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

static inline int
gzread(gzFile f, void *buf, unsigned len)
{
    size_t n = fread(buf, 1, len, f);
    return (n == 0 && ferror(f)) ? -1 : (int) n;
}

static inline int
gzwrite(gzFile f, const void *buf, unsigned len)
{
    return (int) fwrite(buf, 1, len, f);
}

static inline int
gzclose(gzFile f)
{
    return fclose(f);
}

#endif /* NHGODOT_ZLIB_SHIM_H */
