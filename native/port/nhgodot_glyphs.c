/*
 * nhgodot_glyphs.c - glyph metadata for the Godot frontend.
 *
 * Exposes, for every glyph 0..MAX_GLYPH-1, the symbol index/name, default
 * colour and character, a coarse category and a human-readable name.  The
 * frontend uses this to build its glyph -> tile map (tileset generator and
 * coverage test use the same data through `nethack_native_test
 * --dump-glyphs`).
 *
 * NetHack may be freely redistributed.  See license for details.
 */

#include "hack.h"

extern const struct symparse loadsyms[];
extern glyph_map glyphmap[MAX_GLYPH];

/* Keep in sync with NetHackGame.GLYPH_CAT_* and nh_const.gd */
enum nhgodot_glyph_category {
    NHG_CAT_MONSTER = 0,
    NHG_CAT_PET,
    NHG_CAT_DETECTED,
    NHG_CAT_RIDDEN,
    NHG_CAT_INVISIBLE,
    NHG_CAT_BODY,
    NHG_CAT_STATUE,
    NHG_CAT_OBJECT,
    NHG_CAT_CMAP,
    NHG_CAT_ZAP,
    NHG_CAT_SWALLOW,
    NHG_CAT_EXPLODE,
    NHG_CAT_WARNING,
    NHG_CAT_UNEXPLORED,
    NHG_CAT_NOTHING
};

static boolean glyphmap_ready = FALSE;
static const char *symnames[SYM_MAX];
static char namebuf[BUFSZ];

static void
nhg_prepare(void)
{
    int i;

    if (glyphmap_ready)
        return;
    for (i = 0; loadsyms[i].range != SYM_INVALID; ++i) {
        const struct symparse *sp = &loadsyms[i];

        if (sp->range == SYM_CONTROL)
            continue;
        if (sp->idx >= 0 && sp->idx < SYM_MAX && !symnames[sp->idx])
            symnames[sp->idx] = sp->name;
    }
    /* warnings have no loadsyms entries */
    for (i = 0; i < WARNCOUNT; ++i) {
        static char warnnames[WARNCOUNT][16];

        Sprintf(warnnames[i], "S_warn%d", i);
        symnames[SYM_OFF_W + i] = warnnames[i];
    }
    /* The game initialises glyphmap during startup; before that (tileset
     * tooling, glyph table built in Godot's _ready) build it with default
     * settings.  Colours are always wanted by the frontend. */
    if (!program_state.in_moveloop) {
        boolean save_color = iflags.use_color;

        iflags.use_color = TRUE;
        reset_glyphmap(gm_newgame);
        iflags.use_color = save_color;
    }
    glyphmap_ready = TRUE;
}

int
nhgodot_max_glyph(void)
{
    return MAX_GLYPH;
}

int
nhgodot_max_symidx(void)
{
    return SYM_MAX;
}

const char *
nhgodot_symbol_name(int symidx)
{
    nhg_prepare();
    if (symidx < 0 || symidx >= SYM_MAX || !symnames[symidx])
        return "S_unknown";
    return symnames[symidx];
}

static int
default_char(int symidx)
{
    if (symidx < SYM_OFF_O)
        return defsyms[symidx - SYM_OFF_P].sym;
    if (symidx < SYM_OFF_M)
        return def_oc_syms[symidx - SYM_OFF_O].sym;
    if (symidx < SYM_OFF_W)
        return def_monsyms[symidx - SYM_OFF_M].sym;
    if (symidx < SYM_OFF_X)
        return def_warnsyms[symidx - SYM_OFF_W].sym;
    switch (symidx - SYM_OFF_X) {
    case SYM_BOULDER:
        return '0';
    case SYM_INVISIBLE:
        return 'I';
    case SYM_PET_OVERRIDE:
    case SYM_HERO_OVERRIDE:
        return '@';
    default:
        return ' ';
    }
}

static const char *
mon_name(int mndx, boolean female)
{
    if (mndx < LOW_PM || mndx >= NUMMONS)
        return "unknown monster";
    return pmname(&mons[mndx], female ? FEMALE : MALE);
}

static const char *
obj_name(int otyp)
{
    const char *nm;

    if (otyp < 0 || otyp >= NUM_OBJECTS)
        return "unknown object";
    nm = OBJ_NAME(objects[otyp]);
    if (!nm || !*nm)
        nm = OBJ_DESCR(objects[otyp]);
    if (!nm || !*nm)
        nm = def_oc_syms[(int) objects[otyp].oc_class].name;
    return nm ? nm : "object";
}

/* Returns 0 on success, -1 for an out-of-range glyph.  *name points to a
 * static buffer valid until the next call. */
int
nhgodot_glyph_info(int glyph, int *symidx, int *color, int *ttychar,
                   int *category, const char **symname, const char **name)
{
    int idx, cat;
    const char *nm;

    nhg_prepare();
    if (glyph < 0 || glyph >= MAX_GLYPH)
        return -1;

    idx = glyphmap[glyph].sym.symidx;
    if (idx < 0 || idx >= SYM_MAX)
        idx = SYM_NOTHING + SYM_OFF_X;
    *symidx = idx;
    *color = glyphmap[glyph].sym.color;
    *ttychar = default_char(idx);
    *symname = nhgodot_symbol_name(idx);

    if (glyph_is_invisible(glyph)) {
        cat = NHG_CAT_INVISIBLE;
        nm = "remembered unseen monster";
    } else if (glyph_is_pet(glyph)) {
        cat = NHG_CAT_PET;
        Sprintf(namebuf, "tame %s",
                mon_name(glyph_to_mon(glyph), glyph_is_female_pet(glyph)));
        nm = namebuf;
    } else if (glyph_is_detected_monster(glyph)) {
        cat = NHG_CAT_DETECTED;
        Sprintf(namebuf, "detected %s",
                mon_name(glyph_to_mon(glyph),
                         glyph_is_detected_female_monster(glyph)));
        nm = namebuf;
    } else if (glyph_is_ridden_monster(glyph)) {
        cat = NHG_CAT_RIDDEN;
        Sprintf(namebuf, "ridden %s",
                mon_name(glyph_to_mon(glyph),
                         glyph_is_ridden_female_monster(glyph)));
        nm = namebuf;
    } else if (glyph_is_monster(glyph)) {
        cat = NHG_CAT_MONSTER;
        nm = mon_name(glyph_to_mon(glyph),
                      glyph_is_normal_female_monster(glyph));
    } else if (glyph_is_body(glyph)) {
        cat = NHG_CAT_BODY;
        Sprintf(namebuf, "%s corpse%s",
                mon_name(glyph_to_body_corpsenm(glyph), FALSE),
                glyph_is_body_piletop(glyph) ? " (pile)" : "");
        nm = namebuf;
    } else if (glyph_is_statue(glyph)) {
        cat = NHG_CAT_STATUE;
        Sprintf(namebuf, "statue of %s%s",
                mon_name(glyph_to_statue_corpsenm(glyph),
                         glyph_is_fem_statue(glyph)),
                glyph_is_piletop(glyph) ? " (pile)" : "");
        nm = namebuf;
    } else if (glyph_is_object(glyph)) {
        cat = NHG_CAT_OBJECT;
        Sprintf(namebuf, "%s%s", obj_name(glyph_to_obj(glyph)),
                glyph_is_piletop(glyph) ? " (pile)" : "");
        nm = namebuf;
    } else if (glyph_is_swallow(glyph)) {
        cat = NHG_CAT_SWALLOW;
        Sprintf(namebuf, "swallowed by %s (%d)",
                mon_name((glyph - GLYPH_SWALLOW_OFF) >> 3, FALSE),
                glyph_to_swallow(glyph));
        nm = namebuf;
    } else if (glyph_is_explosion(glyph)) {
        cat = NHG_CAT_EXPLODE;
        Sprintf(namebuf, "explosion %d/%d",
                (glyph - GLYPH_EXPLODE_OFF) / MAXEXPCHARS,
                (glyph - GLYPH_EXPLODE_OFF) % MAXEXPCHARS);
        nm = namebuf;
    } else if (glyph_is_warning(glyph)) {
        cat = NHG_CAT_WARNING;
        Sprintf(namebuf, "warning level %d", glyph_to_warning(glyph));
        nm = namebuf;
    } else if (glyph_is_cmap_zap(glyph)) {
        cat = NHG_CAT_ZAP;
        Sprintf(namebuf, "zap %d/%d", (glyph - GLYPH_ZAP_OFF) >> 2,
                (glyph - GLYPH_ZAP_OFF) & 3);
        nm = namebuf;
    } else if (glyph_is_unexplored(glyph)) {
        cat = NHG_CAT_UNEXPLORED;
        nm = "unexplored";
    } else if (glyph_is_nothing(glyph)) {
        cat = NHG_CAT_NOTHING;
        nm = "nothing";
    } else if (glyph_is_cmap(glyph)) {
        int cm = glyph_to_cmap(glyph);

        cat = NHG_CAT_CMAP;
        nm = (cm >= 0 && cm < MAXPCHARS && defsyms[cm].explanation
              && *defsyms[cm].explanation)
                 ? defsyms[cm].explanation
                 : *symname;
        if (glyph_is_cmap_mines(glyph) || glyph_is_cmap_gehennom(glyph)
            || glyph_is_cmap_knox(glyph) || glyph_is_cmap_sokoban(glyph)) {
            Sprintf(namebuf, "%s (%s)", nm,
                    glyph_is_cmap_mines(glyph)       ? "mines"
                    : glyph_is_cmap_gehennom(glyph) ? "gehennom"
                    : glyph_is_cmap_knox(glyph)     ? "knox"
                                                    : "sokoban");
            nm = namebuf;
        } else if (glyph_is_cmap_altar(glyph)) {
            Sprintf(namebuf, "%s %d", nm, glyph - GLYPH_ALTAR_OFF);
            nm = namebuf;
        }
    } else {
        cat = NHG_CAT_NOTHING;
        nm = *symname;
    }
    *category = cat;
    *name = nm;
    return 0;
}
