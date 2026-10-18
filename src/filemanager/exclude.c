/*
   Hide panel entries matching patterns from the exclude file

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

/** \file src/filemanager/exclude.c
 *  \brief Source: hide panel entries matching patterns from the exclude file
 *
 *  The file holds one shell pattern per line. A pattern without '/' is matched
 *  against the entry name, a pattern with '/' against the full path, after a
 *  leading '~' is expanded to the home directory. Empty lines and lines
 *  starting with '#' are ignored. A leading "**" "/" is stripped, so patterns
 *  can be copied from VS Code's files.exclude.
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/fileloc.h"
#include "lib/mcconfig.h"
#include "lib/search.h"
#include "lib/util.h"  // exist_file()

#include "exclude.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

typedef struct
{
    mc_search_t *search;
    gboolean full_path;  // match against the full path instead of the name
} exclude_pattern_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static GPtrArray *patterns = NULL;  // of exclude_pattern_t

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean
exclude_read_file (const char *path, char **contents)
{
    return path != NULL && exist_file (path) && g_file_get_contents (path, contents, NULL, NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
exclude_pattern_free (exclude_pattern_t *p)
{
    mc_search_free (p->search);
    g_free (p);
}

/* --------------------------------------------------------------------------------------------- */

static void
exclude_add (const char *pattern)
{
    char *expanded;
    mc_search_t *search;

    expanded = pattern[0] == '~' ? tilde_expand (pattern) : g_strdup (pattern);

    // "dir/" means the same as "dir"
    while (strlen (expanded) > 1 && g_str_has_suffix (expanded, "/"))
        expanded[strlen (expanded) - 1] = '\0';

    search = mc_search_new (expanded, NULL);
    search->search_type = MC_SEARCH_T_GLOB;
    search->is_entire_line = TRUE;
    search->is_case_sensitive = TRUE;

    if (mc_search_prepare (search))
    {
        exclude_pattern_t *p;

        p = g_new (exclude_pattern_t, 1);
        p->search = search;
        p->full_path = strchr (expanded, '/') != NULL;
        g_ptr_array_add (patterns, p);
    }
    else
        mc_search_free (search);

    g_free (expanded);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
exclude_load (void)
{
    char *path;
    char *contents = NULL;
    gboolean ok;
    char **lines, **line;

    exclude_free ();
    patterns = g_ptr_array_new_with_free_func ((GDestroyNotify) exclude_pattern_free);

    path = mc_config_get_full_path (MC_EXCLUDE_FILE);
    ok = exclude_read_file (path, &contents);
    g_free (path);

    if (!ok)
    {
        path = g_build_filename (mc_global.sysconfig_dir, MC_EXCLUDE_FILE, (char *) NULL);
        ok = exclude_read_file (path, &contents);
        g_free (path);
    }

    if (!ok)
        return;

    lines = g_strsplit (contents, "\n", -1);
    g_free (contents);

    for (line = lines; *line != NULL; line++)
    {
        char *p;

        p = g_strstrip (*line);
        while (strncmp (p, "**/", 3) == 0)
            p += 3;
        if (*p != '\0' && *p != '#')
            exclude_add (p);
    }

    g_strfreev (lines);
}

/* --------------------------------------------------------------------------------------------- */

void
exclude_free (void)
{
    if (patterns != NULL)
    {
        g_ptr_array_free (patterns, TRUE);
        patterns = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Check whether the entry @name (of @len bytes) in directory @dir is excluded.
 * @dir may be NULL; then patterns with '/' are not checked.
 */

gboolean
exclude_match (const char *dir, const char *name, size_t len)
{
    char *path = NULL;
    gboolean found = FALSE;
    guint i;

    if (patterns == NULL)
        exclude_load ();

    for (i = 0; i < patterns->len && !found; i++)
    {
        exclude_pattern_t *p = g_ptr_array_index (patterns, i);

        if (!p->full_path)
            found = mc_search_run (p->search, name, 0, len, NULL);
        else if (dir != NULL)
        {
            if (path == NULL)
                path = g_build_filename (dir, name, (char *) NULL);
            found = mc_search_run (p->search, path, 0, strlen (path), NULL);
        }
    }

    g_free (path);
    return found;
}

/* --------------------------------------------------------------------------------------------- */
