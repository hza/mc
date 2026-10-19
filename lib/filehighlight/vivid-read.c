/*
   File highlight plugin.
   Colors from a vivid filetypes.yml database and a vivid theme.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>

#include "lib/global.h"
#include "lib/fileloc.h"
#include "lib/mcconfig.h"   // mc_config_get_full_path()
#include "lib/skin.h"       // mc_skin_color_get_fg_bg_names()
#include "lib/tty/color.h"  // tty_try_alloc_color_pair(), tty_use_truecolors()
#include "lib/util.h"       // exist_file()

#include "lib/filehighlight.h"

#include "internal.h"

/*** file scope macro definitions ****************************************************************/

// Keys of a vivid theme style
#define VIVID_KEY_FOREGROUND "foreground"
#define VIVID_KEY_BACKGROUND "background"  // only checked for inheritance, the color is ignored
#define VIVID_KEY_FONT_STYLE "font-style"
#define VIVID_KEY_COLORS     "colors"

// Skin entry with the panel's own colors, used for colors the theme leaves out
#define VIVID_SKIN_GROUP "core"
#define VIVID_SKIN_ENTRY "_default_"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    gchar *fg;  // theme color name, NULL when the theme does not set one
    gchar *bg;
    gchar *attrs;  // mc attribute list such as "bold+underline"
} mc_fhl_vivid_style_t;

typedef struct
{
    const mc_fhl_yaml_node_t *theme;
    const mc_fhl_yaml_node_t *colors;
    GHashTable *codes;     // type code (without "$") -> gint color pair index
    GHashTable *suffixes;  // ".ext" -> gint color pair index, in database order
    gsize suffix_max_len;
    gboolean truecolor;
    gboolean color256;
} mc_fhl_vivid_ctx_t;

/*** file scope variables ************************************************************************/

// Basic colors in the order of mc's color names, with their xterm RGB values
static const struct
{
    const char *name;
    int r, g, b;
} vivid_basic_colors[] = {
    { "black", 0, 0, 0 },     { "red", 205, 0, 0 },           { "green", 0, 205, 0 },
    { "brown", 205, 205, 0 }, { "blue", 0, 0, 238 },          { "magenta", 205, 0, 205 },
    { "cyan", 0, 205, 205 },  { "lightgray", 229, 229, 229 },
};

// The 16 ANSI colors of a vivid "ansi:" name in mc names
static const struct
{
    const char *vivid;
    const char *mc;
} vivid_ansi_names[] = {
    { "ansi:black", "black" },
    { "ansi:red", "red" },
    { "ansi:green", "green" },
    { "ansi:yellow", "brown" },
    { "ansi:blue", "blue" },
    { "ansi:magenta", "magenta" },
    { "ansi:cyan", "cyan" },
    { "ansi:white", "lightgray" },
    { "ansi:bright_black", "gray" },
    { "ansi:bright_red", "brightred" },
    { "ansi:bright_green", "brightgreen" },
    { "ansi:bright_yellow", "yellow" },
    { "ansi:bright_blue", "brightblue" },
    { "ansi:bright_magenta", "brightmagenta" },
    { "ansi:bright_cyan", "brightcyan" },
    { "ansi:bright_white", "white" },
};

// Font styles of vivid that have an mc attribute; faint, overline and rapid-blink have none
static const struct
{
    const char *vivid;
    const char *mc;
} vivid_font_styles[] = {
    { "bold", "bold" },
    { "italic", "italic" },
    { "underline", "underline" },
    { "blink", "blink" },
};

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
mc_fhl_vivid_style_clear (mc_fhl_vivid_style_t *style)
{
    g_free (style->fg);
    g_free (style->bg);
    g_free (style->attrs);
    memset (style, 0, sizeof (*style));
}

/* --------------------------------------------------------------------------------------------- */
/** Hex "rrggbb" or "rgb" to components. */

static gboolean
mc_fhl_vivid_parse_hex (const gchar *s, int *r, int *g, int *b)
{
    gsize len = strlen (s), i;
    int v[3];

    if (len != 3 && len != 6)
        return FALSE;

    for (i = 0; i < len; i++)
        if (!g_ascii_isxdigit (s[i]))
            return FALSE;

    for (i = 0; i < 3; i++)
    {
        // "abc" is "aabbcc"
        if (len == 3)
            v[i] = g_ascii_xdigit_value (s[i]) * 17;
        else
            v[i] = g_ascii_xdigit_value (s[2 * i]) * 16 + g_ascii_xdigit_value (s[2 * i + 1]);
    }

    *r = v[0];
    *g = v[1];
    *b = v[2];

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/** Nearest color of the xterm 256 palette: the 6x6x6 cube or the gray ramp. */

static int
mc_fhl_vivid_nearest_256 (int r, int g, int b)
{
    static const int levels[6] = { 0, 95, 135, 175, 215, 255 };
    int ci[3], i, k, n;
    int cube_dist, gray_dist = -1, gray_best = 0;

    // nearest level of each channel of the 6x6x6 cube
    for (i = 0; i < 3; i++)
    {
        int c = (i == 0) ? r : (i == 1) ? g : b, best_k = 0, best_d = -1;

        for (k = 0; k < 6; k++)
        {
            int d = (c - levels[k]) * (c - levels[k]);

            if (best_d < 0 || d < best_d)
            {
                best_d = d;
                best_k = k;
            }
        }
        ci[i] = best_k;
    }
    cube_dist = (r - levels[ci[0]]) * (r - levels[ci[0]])
        + (g - levels[ci[1]]) * (g - levels[ci[1]]) + (b - levels[ci[2]]) * (b - levels[ci[2]]);

    // gray ramp: 8 + 10 * n for n in 0..23
    for (n = 0; n < 24; n++)
    {
        int v = 8 + 10 * n, d = (r - v) * (r - v) + (g - v) * (g - v) + (b - v) * (b - v);

        if (gray_dist < 0 || d < gray_dist)
        {
            gray_dist = d;
            gray_best = n;
        }
    }

    if (gray_dist < cube_dist)
        return 232 + gray_best;

    return 16 + 36 * ci[0] + 6 * ci[1] + ci[2];
}

/* --------------------------------------------------------------------------------------------- */
/** Nearest of the 8 basic colors, for terminals without 256 colors. */

static const char *
mc_fhl_vivid_nearest_basic (int r, int g, int b)
{
    const char *name = vivid_basic_colors[0].name;
    int best_d = -1, i;

    for (i = 0; i < (int) G_N_ELEMENTS (vivid_basic_colors); i++)
    {
        int d = (r - vivid_basic_colors[i].r) * (r - vivid_basic_colors[i].r)
            + (g - vivid_basic_colors[i].g) * (g - vivid_basic_colors[i].g)
            + (b - vivid_basic_colors[i].b) * (b - vivid_basic_colors[i].b);

        if (best_d < 0 || d < best_d)
        {
            best_d = d;
            name = vivid_basic_colors[i].name;
        }
    }

    return name;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Convert a theme color (a name from "colors:", "ansi:..." or hex) to an mc color name.
 * Returns a newly allocated string or NULL.
 */

static gchar *
mc_fhl_vivid_color_to_mc (const mc_fhl_vivid_ctx_t *ctx, const gchar *value)
{
    const mc_fhl_yaml_node_t *named;
    gsize i;
    int r, g, b;

    named = mc_fhl_yaml_get (ctx->colors, value);
    if (named != NULL && named->kind == MC_FHL_YAML_SCALAR)
        value = named->scalar;

    for (i = 0; i < G_N_ELEMENTS (vivid_ansi_names); i++)
        if (strcmp (value, vivid_ansi_names[i].vivid) == 0)
            return g_strdup (vivid_ansi_names[i].mc);

    if (!mc_fhl_vivid_parse_hex (value, &r, &g, &b))
        return NULL;

    if (ctx->truecolor)
        return g_strdup_printf ("#%02x%02x%02x", (unsigned int) r, (unsigned int) g,
                                (unsigned int) b);

    if (ctx->color256)
        return g_strdup_printf ("color%d", mc_fhl_vivid_nearest_256 (r, g, b));

    return g_strdup (mc_fhl_vivid_nearest_basic (r, g, b));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Style of a category path, the way vivid's theme.rs resolves it: a node that sets a style
 * applies to its children that the theme does not name.
 */

static gboolean
mc_fhl_vivid_resolve_style (const mc_fhl_vivid_ctx_t *ctx, GStrv path, mc_fhl_vivid_style_t *style)
{
    const mc_fhl_yaml_node_t *item = ctx->theme, *fg, *font;
    gsize i;
    GString *attrs;

    memset (style, 0, sizeof (*style));

    for (i = 0; path[i] != NULL; i++)
    {
        const mc_fhl_yaml_node_t *child;

        if (item->kind != MC_FHL_YAML_MAP)
            return FALSE;

        child = mc_fhl_yaml_get (item, path[i]);
        if (child == NULL)
        {
            if (mc_fhl_yaml_get (item, VIVID_KEY_FOREGROUND) != NULL
                || mc_fhl_yaml_get (item, VIVID_KEY_BACKGROUND) != NULL
                || mc_fhl_yaml_get (item, VIVID_KEY_FONT_STYLE) != NULL)
                break;
            return FALSE;
        }
        item = child;
    }

    if (item->kind != MC_FHL_YAML_MAP)
        return FALSE;

    fg = mc_fhl_yaml_get (item, VIVID_KEY_FOREGROUND);
    font = mc_fhl_yaml_get (item, VIVID_KEY_FONT_STYLE);

    if (fg != NULL && fg->kind == MC_FHL_YAML_SCALAR)
        style->fg = g_strdup (fg->scalar);
    // theme backgrounds are ignored: the panel background of the skin is always used

    // font-style is one name or a list of names
    attrs = g_string_new (NULL);
    if (font != NULL)
    {
        gsize j;

        for (j = 0; j < (font->kind == MC_FHL_YAML_SEQ ? font->items->len : 1); j++)
        {
            const mc_fhl_yaml_node_t *name_node =
                (font->kind == MC_FHL_YAML_SEQ) ? g_ptr_array_index (font->items, j) : font;
            gsize k;

            if (name_node->kind != MC_FHL_YAML_SCALAR)
                continue;

            for (k = 0; k < G_N_ELEMENTS (vivid_font_styles); k++)
                if (strcmp (name_node->scalar, vivid_font_styles[k].vivid) == 0)
                {
                    if (attrs->len > 0)
                        g_string_append_c (attrs, '+');
                    g_string_append (attrs, vivid_font_styles[k].mc);
                }
        }
    }

    if (attrs->len > 0)
        style->attrs = g_string_free (attrs, FALSE);
    else
        g_string_free (attrs, TRUE);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/** Color pair of a theme style. Missing colors come from the skin's panel colors. */

static int
mc_fhl_vivid_style_to_pair (const mc_fhl_vivid_ctx_t *ctx, const mc_fhl_vivid_style_t *style)
{
    const gchar *skin_fg = NULL, *skin_bg = NULL;
    tty_color_pair_t color;
    gchar *fg = NULL, *bg = NULL;
    int pair;

    mc_skin_color_get_fg_bg_names (VIVID_SKIN_GROUP, VIVID_SKIN_ENTRY, &skin_fg, &skin_bg);

    fg = (style->fg != NULL) ? mc_fhl_vivid_color_to_mc (ctx, style->fg) : NULL;
    bg = (style->bg != NULL) ? mc_fhl_vivid_color_to_mc (ctx, style->bg) : NULL;

    color.fg = (fg != NULL) ? fg : (skin_fg != NULL ? g_strdup (skin_fg) : NULL);
    color.bg = (bg != NULL) ? bg : (skin_bg != NULL ? g_strdup (skin_bg) : NULL);
    color.attrs = style->attrs;
    color.pair_index = 0;

    pair = tty_try_alloc_color_pair (&color, FALSE);

    if (fg == NULL)
        g_free (color.fg);
    if (bg == NULL)
        g_free (color.bg);
    g_free (fg);
    g_free (bg);

    return pair;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Walk the database. Lists are either "$code" type codes or literal suffixes, both below a
 * category path that selects the theme style.
 */

static void
mc_fhl_vivid_add_entry (mc_fhl_vivid_ctx_t *ctx, const gchar *entry, GStrv path)
{
    mc_fhl_vivid_style_t style;
    gint pair;

    if (!mc_fhl_vivid_resolve_style (ctx, path, &style))
        return;

    if (style.fg == NULL && style.bg == NULL && style.attrs == NULL)
    {
        mc_fhl_vivid_style_clear (&style);
        return;
    }

    pair = mc_fhl_vivid_style_to_pair (ctx, &style);
    mc_fhl_vivid_style_clear (&style);

    // values are pair + 1, so that pair 0 is not NULL
    if (entry[0] == '$')
        g_hash_table_replace (ctx->codes, g_strdup (entry + 1), GINT_TO_POINTER (pair + 1));
    else if (strlen (entry) <= VIVID_SUFFIX_KEY_MAX)
    {
        // "*" + entry is the glob: ".tar.gz" and "README" are matched as name suffixes
        ctx->suffix_max_len = MAX (ctx->suffix_max_len, strlen (entry));
        g_hash_table_replace (ctx->suffixes, g_strdup (entry), GINT_TO_POINTER (pair + 1));
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_fhl_vivid_walk (mc_fhl_vivid_ctx_t *ctx, const mc_fhl_yaml_node_t *node, GPtrArray *path)
{
    gsize i;

    if (node == NULL)
        return;

    if (node->kind == MC_FHL_YAML_MAP)
    {
        for (i = 0; i < node->keys->len; i++)
        {
            g_ptr_array_add (path, g_ptr_array_index (node->keys, i));
            mc_fhl_vivid_walk (ctx, g_ptr_array_index (node->items, i), path);
            g_ptr_array_remove_index (path, path->len - 1);
        }
    }
    else if (node->kind == MC_FHL_YAML_SEQ)
    {
        GStrv names = g_new0 (gchar *, path->len + 1);

        for (i = 0; i < path->len; i++)
            names[i] = g_ptr_array_index (path, i);

        for (i = 0; i < node->items->len; i++)
        {
            const mc_fhl_yaml_node_t *item = g_ptr_array_index (node->items, i);

            if (item->kind == MC_FHL_YAML_SCALAR)
                mc_fhl_vivid_add_entry (ctx, item->scalar, names);
        }
        g_free (names);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gchar *
mc_fhl_vivid_read_file (const gchar *path)
{
    gchar *text = NULL;

    if (!g_file_get_contents (path, &text, NULL, NULL))
        return NULL;

    return text;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_fhl_vivid_add_filter (mc_fhl_t *fhl, mc_flhgh_ftype_type type, int color_pair_index)
{
    mc_fhl_filter_t *filter;

    filter = g_new0 (mc_fhl_filter_t, 1);
    filter->type = MC_FLHGH_T_FTYPE;
    filter->file_type = type;
    filter->color_pair_index = color_pair_index;
    g_ptr_array_add (fhl->filters, (gpointer) filter);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_fhl_vivid_code_pair (GHashTable *codes, const gchar *code, int *pair)
{
    gpointer value = g_hash_table_lookup (codes, code);

    if (value == NULL)
        return FALSE;

    *pair = GPOINTER_TO_INT (value) - 1;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/** Directories searched for a vivid file, in order: user config, sysconfdir, datadir. */

static GPtrArray *
mc_fhl_vivid_dirs (void)
{
    GPtrArray *dirs = g_ptr_array_new_with_free_func (g_free);
    gchar *user_db;

    user_db = mc_config_get_full_path (MC_FHL_VIVID_DB_FILE);
    if (user_db != NULL)
    {
        g_ptr_array_add (dirs, g_path_get_dirname (user_db));
        g_free (user_db);
    }
    g_ptr_array_add (dirs, g_build_filename (mc_global.sysconfig_dir, (char *) NULL));
    g_ptr_array_add (dirs, g_build_filename (mc_global.share_data_dir, (char *) NULL));

    return dirs;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gchar *
mc_fhl_vivid_theme_path (const gchar *theme_name)
{
    GPtrArray *dirs;
    gchar *file_name, *result = NULL;
    guint i;

    if (theme_name == NULL || theme_name[0] == '\0' || strchr (theme_name, '/') != NULL)
        return NULL;

    file_name = g_strconcat (theme_name, ".yml", (char *) NULL);
    dirs = mc_fhl_vivid_dirs ();

    for (i = 0; i < dirs->len && result == NULL; i++)
    {
        gchar *path = g_build_filename (g_ptr_array_index (dirs, i), MC_FHL_VIVID_THEMES_DIR,
                                        file_name, (char *) NULL);

        if (exist_file (path))
            result = path;
        else
            g_free (path);
    }

    g_free (file_name);
    g_ptr_array_free (dirs, TRUE);

    return result;
}

/* --------------------------------------------------------------------------------------------- */

static gint
mc_fhl_vivid_name_compare (gconstpointer a, gconstpointer b)
{
    return strcmp (*(gchar *const *) a, *(gchar *const *) b);
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
mc_fhl_vivid_list_themes (void)
{
    GPtrArray *dirs, *names;
    guint i;

    names = g_ptr_array_new_with_free_func (g_free);
    dirs = mc_fhl_vivid_dirs ();

    for (i = 0; i < dirs->len; i++)
    {
        gchar *theme_dir =
            g_build_filename (g_ptr_array_index (dirs, i), MC_FHL_VIVID_THEMES_DIR, (char *) NULL);
        GDir *dir = g_dir_open (theme_dir, 0, NULL);
        const gchar *entry;

        if (dir != NULL)
        {
            while ((entry = g_dir_read_name (dir)) != NULL)
            {
                gchar *name;
                gsize j;
                gboolean seen = FALSE;

                if (!g_str_has_suffix (entry, ".yml"))
                    continue;

                name = g_strndup (entry, strlen (entry) - strlen (".yml"));
                for (j = 0; j < names->len && !seen; j++)
                    seen = (strcmp (g_ptr_array_index (names, j), name) == 0);

                if (seen)
                    g_free (name);
                else
                    g_ptr_array_add (names, name);
            }
            g_dir_close (dir);
        }
        g_free (theme_dir);
    }

    g_ptr_array_free (dirs, TRUE);
    g_ptr_array_sort (names, mc_fhl_vivid_name_compare);

    return names;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_fhl_vivid_load (mc_fhl_t *fhl, const gchar *theme_name)
{
    gchar *db_path = NULL, *theme_path, *db_text = NULL, *theme_text = NULL;
    GPtrArray *dirs;
    mc_fhl_yaml_node_t *db = NULL, *theme = NULL;
    mc_fhl_vivid_ctx_t ctx;
    gboolean ok = FALSE;
    guint i;
    gsize n;

    theme_path = mc_fhl_vivid_theme_path (theme_name);
    if (theme_path == NULL)
        return FALSE;

    dirs = mc_fhl_vivid_dirs ();
    for (i = 0; i < dirs->len && db_path == NULL; i++)
    {
        gchar *candidate =
            g_build_filename (g_ptr_array_index (dirs, i), MC_FHL_VIVID_DB_FILE, (char *) NULL);

        if (exist_file (candidate))
            db_path = candidate;
        else
            g_free (candidate);
    }
    g_ptr_array_free (dirs, TRUE);

    if (db_path == NULL)
        goto out;

    db_text = mc_fhl_vivid_read_file (db_path);
    theme_text = mc_fhl_vivid_read_file (theme_path);
    if (db_text == NULL || theme_text == NULL)
        goto out;

    db = mc_fhl_yaml_parse (db_text, NULL);
    if (db == NULL)
        goto out;
    theme = mc_fhl_yaml_parse (theme_text, NULL);
    if (theme == NULL)
        goto out;

    ctx.theme = theme;
    ctx.colors = mc_fhl_yaml_get (theme, VIVID_KEY_COLORS);
    ctx.codes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    ctx.suffixes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    ctx.suffix_max_len = 0;
    ctx.truecolor = tty_use_truecolors (NULL);
    ctx.color256 = !ctx.truecolor && tty_use_256colors (NULL);

    {
        GPtrArray *path = g_ptr_array_new ();

        mc_fhl_vivid_walk (&ctx, db, path);
        g_ptr_array_free (path, TRUE);
    }

    mc_fhl_array_free (fhl);
    fhl->filters = g_ptr_array_new_with_free_func (mc_fhl_filter_free);

    // the type codes of the ini file's type groups, in the same order
    {
        static const struct
        {
            const char *code;
            mc_flhgh_ftype_type type;
        } order[] = {
            // broken links before links: a broken link is also a symlink
            { "di", MC_FLHGH_FTYPE_T_DIR },          { "or", MC_FLHGH_FTYPE_T_STALE_LINK },
            { "ln", MC_FLHGH_FTYPE_T_SYMLINK },      { "ex", MC_FLHGH_FTYPE_T_FILE_EXE },
            { "pi", MC_FLHGH_FTYPE_T_SPECIAL_FIFO }, { "so", MC_FLHGH_FTYPE_T_SPECIAL_SOCKET },
            { "do", MC_FLHGH_FTYPE_T_SPECIAL_DOOR }, { "bd", MC_FLHGH_FTYPE_T_DEVICE_BLOCK },
            { "cd", MC_FLHGH_FTYPE_T_DEVICE_CHAR },
        };
        int pair;

        for (n = 0; n < G_N_ELEMENTS (order); n++)
            if (mc_fhl_vivid_code_pair (ctx.codes, order[n].code, &pair))
                mc_fhl_vivid_add_filter (fhl, order[n].type, pair);
    }

    if (g_hash_table_size (ctx.suffixes) > 0)
    {
        mc_fhl_filter_t *filter = g_new0 (mc_fhl_filter_t, 1);

        filter->type = MC_FLHGH_T_SUFFIX;
        filter->suffixes = ctx.suffixes;
        filter->suffix_max_len = ctx.suffix_max_len;
        ctx.suffixes = NULL;
        g_ptr_array_add (fhl->filters, (gpointer) filter);
    }

    {
        int pair;

        if (mc_fhl_vivid_code_pair (ctx.codes, "mh", &pair))
            mc_fhl_vivid_add_filter (fhl, MC_FLHGH_FTYPE_T_HARDLINK, pair);
        if (mc_fhl_vivid_code_pair (ctx.codes, "fi", &pair))
            mc_fhl_vivid_add_filter (fhl, MC_FLHGH_FTYPE_T_FILE, pair);
    }

    if (ctx.suffixes != NULL)
        g_hash_table_destroy (ctx.suffixes);
    g_hash_table_destroy (ctx.codes);

    if (fhl->filters->len == 0)
    {
        mc_fhl_array_free (fhl);
        goto out;
    }

    fhl->vivid_theme_path = g_strdup (theme_path);
    ok = TRUE;

out:
    g_free (db_path);
    g_free (theme_path);
    g_free (db_text);
    g_free (theme_text);
    mc_fhl_yaml_free (db);
    mc_fhl_yaml_free (theme);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */
