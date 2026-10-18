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
#include "lib/util.h"     // exist_file()
#include "lib/tty/tty.h"  // COLS, LINES
#include "lib/widget.h"
#include "lib/strutil.h"

#include "src/setup.h"  // panels_options

#include "filemanager.h"  // current_panel
#include "layout.h"       // get_current_type()
#include "panel.h"        // update_panels()

#include "exclude.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

typedef struct
{
    mc_search_t *search;
    gboolean full_path;  // match against the full path instead of the name
} exclude_pattern_t;

typedef struct
{
    const char *text;
    bcback_fn cb;
    int ret;
    button_flags_t flags;
} exclude_button_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static GPtrArray *patterns = NULL;  // of exclude_pattern_t

static WListbox *exclude_list;
static Widget *exclude_ok_button;
static Widget *exclude_add_current_button;
static Widget *exclude_remove_current_button;

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

/** Read the exclude file, the user one or else the system wide one. NULL if none. */

static char *
exclude_read (void)
{
    char *path;
    char *contents = NULL;
    gboolean ok;

    path = mc_config_get_full_path (MC_EXCLUDE_FILE);
    ok = exclude_read_file (path, &contents);
    g_free (path);

    if (!ok)
    {
        path = g_build_filename (mc_global.sysconfig_dir, MC_EXCLUDE_FILE, (char *) NULL);
        ok = exclude_read_file (path, &contents);
        g_free (path);
    }

    return ok ? contents : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
exclude_is_pattern (const char *line)
{
    while (*line == ' ' || *line == '\t')
        line++;
    return *line != '\0' && *line != '#';
}

/* --------------------------------------------------------------------------------------------- */

static char *
exclude_ask (const char *def_text)
{
    char *text;

    text = input_dialog (_ ("Exclude pattern"),
                         _ ("Pattern (name, or path with '/'; '~' is the home directory):"),
                         "mc.exclude.pattern", def_text, INPUT_COMPLETE_FILENAMES);
    if (text != NULL)
    {
        g_strstrip (text);
        if (*text == '\0')
        {
            g_free (text);
            text = NULL;
        }
    }

    return text;
}

/* --------------------------------------------------------------------------------------------- */

static int
exclude_add_cb (WButton *button, int action)
{
    char *text;

    (void) button;
    (void) action;

    text = exclude_ask ("");
    if (text != NULL)
    {
        listbox_add_item_take (exclude_list, LISTBOX_APPEND_AT_END, 0, text, NULL, FALSE);
        listbox_select_last (exclude_list);
        widget_draw (WIDGET (exclude_list));
    }

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/** @name in the current panel directory as a full path, with the home directory as '~'. */

static char *
exclude_panel_path (const char *name)
{
    const char *home;
    char *path;
    size_t home_len;

    path = g_build_filename (vfs_path_as_str (current_panel->cwd_vpath), name, (char *) NULL);

    home = mc_config_get_home_dir ();
    home_len = home != NULL ? strlen (home) : 0;
    if (home_len > 1 && strncmp (path, home, home_len) == 0 && path[home_len] == PATH_SEP)
    {
        char *tilde;

        tilde = g_strconcat ("~", path + home_len, (char *) NULL);
        g_free (path);
        path = tilde;
    }

    return path;
}

/* --------------------------------------------------------------------------------------------- */

/** Paths of the marked files, or of the file under the cursor if none is marked. */

static GPtrArray *
exclude_current_paths (void)
{
    GPtrArray *paths;

    paths = g_ptr_array_new_with_free_func (g_free);

    if (get_current_type () != view_listing)
        return paths;

    if (current_panel->marked > 0)
    {
        int i;

        for (i = 0; i < current_panel->dir.len; i++)
            if (current_panel->dir.list[i].f.marked != 0
                && !DIR_IS_DOTDOT (current_panel->dir.list[i].fname->str))
                g_ptr_array_add (paths, exclude_panel_path (current_panel->dir.list[i].fname->str));
    }
    else
    {
        const file_entry_t *fe;

        fe = panel_current_entry (current_panel);
        if (fe != NULL && !DIR_IS_DOTDOT (fe->fname->str))
            g_ptr_array_add (paths, exclude_panel_path (fe->fname->str));
    }

    return paths;
}

/* --------------------------------------------------------------------------------------------- */

static int
exclude_add_current_cb (WButton *button, int action)
{
    GPtrArray *paths;
    guint i;

    (void) button;
    (void) action;

    paths = exclude_current_paths ();
    for (i = 0; i < paths->len; i++)
    {
        const char *path = g_ptr_array_index (paths, i);

        if (listbox_search_text (exclude_list, path) < 0)
            listbox_add_item (exclude_list, LISTBOX_APPEND_AT_END, 0, path, NULL, FALSE);
    }
    g_ptr_array_free (paths, TRUE);

    listbox_select_last (exclude_list);
    widget_draw (WIDGET (exclude_list));

    // a second Enter saves
    widget_select (exclude_ok_button);

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
exclude_remove_current_cb (WButton *button, int action)
{
    GPtrArray *paths;
    guint i;

    (void) button;
    (void) action;

    paths = exclude_current_paths ();
    for (i = 0; i < paths->len; i++)
    {
        int pos;

        pos = listbox_search_text (exclude_list, g_ptr_array_index (paths, i));
        if (pos >= 0)
        {
            listbox_set_current (exclude_list, pos);
            listbox_remove_current (exclude_list);
        }
    }
    g_ptr_array_free (paths, TRUE);

    widget_draw (WIDGET (exclude_list));
    widget_select (exclude_ok_button);

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/** Text for the "Current selected" box. */

static char *
exclude_current_text (void)
{
    GPtrArray *paths;
    char *text;

    paths = exclude_current_paths ();
    if (paths->len == 0)
        text = g_strdup (_ ("(nothing)"));
    else if (paths->len == 1)
        text = g_strdup (g_ptr_array_index (paths, 0));
    else
        text = g_strdup_printf (_ ("%s and %u more"), (char *) g_ptr_array_index (paths, 0),
                                paths->len - 1);
    g_ptr_array_free (paths, TRUE);

    return text;
}

/* --------------------------------------------------------------------------------------------- */

static int
exclude_edit_cb (WButton *button, int action)
{
    char *old;
    char *text;
    int pos;

    (void) button;
    (void) action;

    if (listbox_is_empty (exclude_list))
        return 0;

    listbox_get_current (exclude_list, &old, NULL);
    text = exclude_ask (old);
    if (text != NULL)
    {
        pos = exclude_list->current;
        listbox_remove_current (exclude_list);
        listbox_set_current (exclude_list, pos);
        listbox_add_item_take (exclude_list,
                               pos < listbox_get_length (exclude_list) ? LISTBOX_APPEND_BEFORE
                                                                       : LISTBOX_APPEND_AT_END,
                               0, text, NULL, FALSE);
        listbox_set_current (exclude_list, pos);
        widget_draw (WIDGET (exclude_list));
    }

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
exclude_delete_cb (WButton *button, int action)
{
    (void) button;
    (void) action;

    if (!listbox_is_empty (exclude_list))
    {
        listbox_remove_current (exclude_list);
        widget_draw (WIDGET (exclude_list));
    }

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/** Write the patterns back, keeping the comment lines at the top of the old file. */

static void
exclude_save (const char *old_contents)
{
    GString *out;
    GList *link;
    char *path;
    GError *error = NULL;

    out = g_string_new ("");

    if (old_contents != NULL)
    {
        char **lines, **line;

        lines = g_strsplit (old_contents, "\n", -1);
        for (line = lines; *line != NULL && !exclude_is_pattern (*line); line++)
            if (**line != '\0' || line[1] != NULL)
                g_string_append_printf (out, "%s\n", *line);
        g_strfreev (lines);
    }

    for (link = listbox_get_first_link (exclude_list); link != NULL; link = g_list_next (link))
        g_string_append_printf (out, "%s\n", ((WLEntry *) link->data)->text);

    path = mc_config_get_full_path (MC_EXCLUDE_FILE);
    if (!g_file_set_contents (path, out->str, out->len, &error))
    {
        message (D_ERROR, MSG_ERROR, _ ("Cannot save %s:\n%s"), path, error->message);
        g_error_free (error);
    }
    g_free (path);
    g_string_free (out, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
exclude_dialog_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    // A button run by a hotkey or by Enter as the default button gets the focus afterwards.
    // Give it back to OK after "Add current" and "Remove current", so a second Enter saves.
    if (msg == MSG_HOTKEY_HANDLED
        && (sender == exclude_add_current_button || sender == exclude_remove_current_button))
    {
        widget_select (exclude_ok_button);
        return MSG_HANDLED;
    }

    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */
/** Add a centered row of buttons at line @y, between columns @x1 and @x2. */

static void
exclude_add_buttons (WGroup *g, int y, int x1, int x2, const exclude_button_t *buttons, size_t n)
{
    size_t i;
    int x = 0;

    for (i = 0; i < n; i++)
        x += str_term_width1 (buttons[i].text) + 4 + 1
            + (buttons[i].flags == DEFPUSH_BUTTON ? 2 : 0);
    x = x1 + (x2 - x1 - x) / 2;

    for (i = 0; i < n; i++)
    {
        WButton *b;

        b = button_new (y, x, buttons[i].ret, buttons[i].flags, buttons[i].text, buttons[i].cb);
        group_add_widget (g, b);
        if (buttons[i].ret == B_ENTER)
            exclude_ok_button = WIDGET (b);
        else if (buttons[i].cb == exclude_add_current_cb)
            exclude_add_current_button = WIDGET (b);
        else if (buttons[i].cb == exclude_remove_current_cb)
            exclude_remove_current_button = WIDGET (b);
        x += widget_get_columns (WIDGET (b)) + 1;
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
exclude_load (void)
{
    char *contents;
    char **lines, **line;

    exclude_free ();
    patterns = g_ptr_array_new_with_free_func ((GDestroyNotify) exclude_pattern_free);

    contents = exclude_read ();
    if (contents == NULL)
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

/*
 * Rows of the dialog, L = list_lines:
 *
 *   0          frame
 *   1          (empty)
 *   2          "Files and directories matching..." label
 *   3          list box frame, top
 *   4..L+3     pattern list, L rows
 *   L+4        list box frame, bottom
 *   L+5        "Current selected in file panel" box, top with its title
 *   L+6        name of the current file
 *   L+7        [Add current] [Remove current]
 *   L+8        "Current selected" box, bottom
 *   L+9        horizontal line
 *   L+10       [Add] [Edit] [Delete] [OK] [Cancel]
 *   L+11       (empty)
 *   L+12       frame
 *
 * so the dialog is L + 13 rows high.
 */

void
exclude_dialog (void)
{
    const int cols = MAX (70, COLS * 2 / 3);
    // the dialog is list_lines + 13 high: this leaves 4 screen rows (menu bar, command line,
    // key bar and one more) on a tall screen; on a short one the list keeps 5 rows
    const int list_lines = MAX (5, LINES - 17);
    char *contents;
    char *current;
    WDialog *dlg;
    WGroup *g;
    int y = 2;  // the row the next widget goes to

    const exclude_button_t current_buttons[] = {
        { _ ("Add c&urrent"), exclude_add_current_cb, 0, DEFPUSH_BUTTON },
        { _ ("&Remove current"), exclude_remove_current_cb, 0, NORMAL_BUTTON },
    };

    const exclude_button_t buttons[] = {
        { _ ("&Add"), exclude_add_cb, 0, NORMAL_BUTTON },
        { _ ("&Edit"), exclude_edit_cb, 0, NORMAL_BUTTON },
        { _ ("&Delete"), exclude_delete_cb, 0, NORMAL_BUTTON },
        { _ ("&OK"), NULL, B_ENTER, NORMAL_BUTTON },
        { _ ("&Cancel"), NULL, B_CANCEL, NORMAL_BUTTON },
    };

    contents = exclude_read ();
    current = exclude_current_text ();

    dlg = dlg_create (TRUE, 0, 0, list_lines + 13, cols, WPOS_CENTER, FALSE, dialog_colors,
                      exclude_dialog_callback, NULL, "[Exclude patterns]", _ ("Exclude patterns"));
    g = GROUP (dlg);

    group_add_widget (
        g, label_new (y++, 3, _ ("Files and directories matching these patterns are hidden.")));
    // a frame around the list: one row above it, L rows of list, one row below
    group_add_widget (g, groupbox_new (y++, 2, list_lines + 2, cols - 4, ""));
    exclude_list = listbox_new (y, 3, list_lines, cols - 6, FALSE, NULL);
    if (contents != NULL)
    {
        char **lines, **line;

        lines = g_strsplit (contents, "\n", -1);
        for (line = lines; *line != NULL; line++)
            if (exclude_is_pattern (*line))
                listbox_add_item (exclude_list, LISTBOX_APPEND_AT_END, 0, g_strstrip (*line), NULL,
                                  FALSE);
        g_strfreev (lines);
    }
    group_add_widget (g, exclude_list);
    // skip the L list rows and the bottom of its frame: y = L + 5
    y += list_lines + 1;

    // 4 rows: top with title, file name, buttons, bottom
    group_add_widget (g, groupbox_new (y++, 2, 4, cols - 4, _ ("Current selected in file panel")));
    group_add_widget (g, label_new (y++, 4, str_trunc (current, cols - 8)));
    exclude_add_buttons (g, y++, 2, cols - 2, current_buttons, G_N_ELEMENTS (current_buttons));
    // skip the bottom of the box (L + 8)
    y += 1;

    // y = L + 9; the buttons go on L + 10, then an empty row and the frame on L + 12
    group_add_widget (g, hline_new (y++, -1, -1));
    exclude_add_buttons (g, y, 0, cols, buttons, G_N_ELEMENTS (buttons));

    widget_select (WIDGET (exclude_list));

    if (dlg_run (dlg) == B_ENTER)
    {
        exclude_save (contents);
        exclude_load ();
        update_panels (UP_RELOAD, UP_KEEPSEL);
    }

    widget_destroy (WIDGET (dlg));
    exclude_list = NULL;
    exclude_ok_button = NULL;
    exclude_add_current_button = NULL;
    exclude_remove_current_button = NULL;
    g_free (current);
    g_free (contents);
}

/* --------------------------------------------------------------------------------------------- */
