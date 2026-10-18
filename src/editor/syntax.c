/*
   Editor syntax highlighting.

   Copyright (C) 1996-2026
   Free Software Foundation, Inc.

   Written by:
   Paul Sheer, 1998
   Leonard den Ottolander <leonard den ottolander nl>, 2005, 2006
   Egmont Koblinger <egmont@gmail.com>, 2010
   Slava Zanko <slavazanko@gmail.com>, 2013
   Andrew Borodin <aborodin@vmail.ru>, 2013, 2014, 2021

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

/** \file
 *  \brief Source: editor syntax highlighting
 *  \author Paul Sheer
 *  \date 1996, 1997
 *  \author Mikhail Pobolovets
 *  \date 2010
 *
 *  Misspelled words are flushed from the syntax highlighting rules
 *  when they have been around longer than
 *  TRANSIENT_WORD_TIME_OUT seconds. At a cursor rate of 30
 *  chars per second and say 3 chars + a space per word, we can
 *  accumulate 450 words absolute max with a value of 60. This is
 *  below this limit of 1024 words in a context.
 */

#include <config.h>

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>

#include "lib/global.h"
#include "lib/skin.h"
#include "lib/fileloc.h"  // EDIT_SYNTAX_DIR, EDIT_SYNTAX_FILE
#include "lib/strutil.h"  // utf string functions
#include "lib/util.h"
#include "lib/widget.h"  // Listbox, message()

#include "src/util.h"  // file_error_message()

#include "edit-impl.h"
#include "editwidget.h"
#include "src/syntax/syntax.h"
#include "src/syntax/syntax_scheme.h"

/*** global variables ****************************************************************************/

gboolean auto_syntax = TRUE;

/*** file scope macro definitions ****************************************************************/

#define MAX_ENTRY_LEN     40
#define LIST_LINES        14
#define N_DFLT_ENTRIES    2

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* state of the color scheme dialog */
static WGroup *scheme_dialog_editors = NULL;
static WListbox *scheme_dialog_list = NULL;
static WLabel *scheme_dialog_status = NULL;
/* scheme load errors stand out in the skin's error colors */
static const label_colors_t scheme_error_colors = {
    [LABEL_COLOR_MAIN] = ERROR_NORMAL_COLOR,
    [LABEL_COLOR_DISABLED] = CORE_DISABLED_COLOR,  // unused
};
static const GPtrArray *scheme_dialog_names = NULL;

/* state of the syntax highlighting dialog */
static WEdit *syntax_dialog_edit = NULL;
static WListbox *syntax_dialog_list = NULL;
static const GPtrArray *syntax_dialog_names = NULL;
static gboolean syntax_dialog_previewed = FALSE;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static int
syntax_get_editor_byte (const void *data, off_t byte_index)
{
    return edit_buffer_get_byte ((const edit_buffer_t *) data, byte_index);
}

/* --------------------------------------------------------------------------------------------- */

static const char *
get_first_editor_line (WEdit *edit)
{
    static char s[256];

    s[0] = '\0';

    if (edit != NULL)
    {
        size_t i;

        for (i = 0; i < sizeof (s) - 1; i++)
        {
            s[i] = edit_buffer_get_byte (&edit->buffer, i);
            if (s[i] == '\n')
            {
                s[i] = '\0';
                break;
            }
        }

        s[sizeof (s) - 1] = '\0';
    }

    return s;
}

/* --------------------------------------------------------------------------------------------- */

static int
pstrcmp (const void *p1, const void *p2)
{
    return strcmp (*(char *const *) p1, *(char *const *) p2);
}

/* --------------------------------------------------------------------------------------------- */

/* make syntax list entry @syntax current for @edit and reload its rules if they change */
static void
syntax_apply (WEdit *edit, const GPtrArray *names, int syntax)
{
    gboolean force_reload = FALSE;
    char *current_syntax;
    gboolean old_auto_syntax;

    current_syntax = g_strdup (edit->syntax_type);
    old_auto_syntax = auto_syntax;

    switch (syntax)
    {
    case 0:  // auto syntax
        auto_syntax = TRUE;
        break;
    case 1:  // reload current syntax
        force_reload = TRUE;
        break;
    default:
        auto_syntax = FALSE;
        g_free (edit->syntax_type);
        edit->syntax_type = g_strdup (g_ptr_array_index (names, syntax - N_DFLT_ENTRIES));
    }

    // Load or unload syntax rules if the option has changed
    if (force_reload || (auto_syntax && !old_auto_syntax) || old_auto_syntax
        || (current_syntax != NULL && edit->syntax_type != NULL
            && strcmp (current_syntax, edit->syntax_type) != 0))
        edit_load_syntax (edit, NULL, edit->syntax_type);

    g_free (current_syntax);
}

/* --------------------------------------------------------------------------------------------- */

/* preview the syntax as the selection moves */
static cb_ret_t
syntax_dialog_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    if (msg == MSG_NOTIFY && parm == 0 && sender != NULL && sender == WIDGET (syntax_dialog_list))
    {
        // reloading the syntax file on every move is pointless: leave it for Enter
        if (syntax_dialog_list->current != 1)
        {
            syntax_apply (syntax_dialog_edit, syntax_dialog_names, syntax_dialog_list->current);
            syntax_dialog_previewed = TRUE;
            do_refresh ();
        }
        return MSG_HANDLED;
    }

    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */

static int
exec_edit_syntax_dialog (WEdit *edit, const GPtrArray *names, const char *current_syntax)
{
    size_t i;
    Listbox *syntaxlist;
    int n;

    syntaxlist =
        listbox_window_new (LIST_LINES, MAX_ENTRY_LEN, _ ("Choose syntax highlighting"), NULL);
    LISTBOX_APPEND_TEXT (syntaxlist, 'A', _ ("< Auto >"), NULL, FALSE);
    LISTBOX_APPEND_TEXT (syntaxlist, 'R', _ ("< Reload Current Syntax >"), NULL, FALSE);

    for (i = 0; i < names->len; i++)
    {
        const char *name;

        name = g_ptr_array_index (names, i);
        LISTBOX_APPEND_TEXT (syntaxlist, 0, name, NULL, FALSE);
        if (current_syntax != NULL && strcmp (name, current_syntax) == 0)
            listbox_set_current (syntaxlist->list, i + N_DFLT_ENTRIES);
    }

    syntax_dialog_edit = edit;
    syntax_dialog_list = syntaxlist->list;
    syntax_dialog_names = names;
    syntax_dialog_previewed = FALSE;
    WIDGET (syntaxlist->dlg)->callback = syntax_dialog_callback;

    n = listbox_run (syntaxlist);

    syntax_dialog_edit = NULL;
    syntax_dialog_list = NULL;
    syntax_dialog_names = NULL;

    return n;
}

/* --------------------------------------------------------------------------------------------- */

static void
scheme_reload_editor (void *data, void *user_data)
{
    (void) user_data;

    if (edit_widget_is_editor (CONST_WIDGET (data)))
    {
        WEdit *edit = EDIT (data);

        edit_load_syntax (edit, NULL, edit->syntax_type);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* make the scheme of list entry @n current and repaint all editors */
static void
scheme_apply (const GPtrArray *names, int n)
{
    const char *name;

    name = n <= 0 ? "" : (const char *) g_ptr_array_index (names, n - 1);
    if (syntax_scheme_name != NULL && strcmp (syntax_scheme_name, name) == 0)
        return;

    g_free (syntax_scheme_name);
    syntax_scheme_name = g_strdup (name);

    g_list_foreach (GROUP (scheme_dialog_editors)->widgets, scheme_reload_editor, NULL);
    do_refresh ();
}

/* --------------------------------------------------------------------------------------------- */

/* preview the scheme as the selection moves */
/* show why the selected scheme is not used, under the dialog frame */
static void
scheme_status_update (void)
{
    const char *error;

    if (scheme_dialog_status == NULL)
        return;

    error = syntax_scheme_error ();
    if (error == NULL)
        label_set_text (scheme_dialog_status, "");
    else
    {
        char *text;

        text = g_strdup_printf (_ ("Error: %s"), error);
        label_set_text (scheme_dialog_status,
                        str_trunc (text, WIDGET (scheme_dialog_list)->rect.cols));
        g_free (text);
    }

    // the label shrinks with its text: repaint the dialog so no old message is left behind
    if (widget_get_state (WIDGET (WIDGET (scheme_dialog_status)->owner), WST_ACTIVE))
        widget_draw (WIDGET (WIDGET (scheme_dialog_status)->owner));
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
scheme_dialog_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    if (msg == MSG_NOTIFY && parm == 0 && sender != NULL && sender == WIDGET (scheme_dialog_list))
    {
        scheme_apply (scheme_dialog_names, scheme_dialog_list->current);
        scheme_status_update ();
        return MSG_HANDLED;
    }

    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

int
edit_get_syntax_color (WEdit *edit, off_t byte_index)
{
    if (!tty_use_colors ())
        return 0;

    if (edit_options.syntax_highlighting && edit->syntax.rules != NULL
        && byte_index < edit->buffer.size)
        return syntax_get_color (&edit->syntax, byte_index);

    return EDITOR_NORMAL_COLOR;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_free_syntax_rules (WEdit *edit)
{
    if (edit == NULL)
        return;

    if (edit->syntax.rules != NULL)
    {
        MC_PTR_FREE (edit->syntax_type);
        syntax_free (&edit->syntax);
        tty_color_free_temp ();
    }
    else
        syntax_free (&edit->syntax);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Load rules into edit struct.  Either edit or *pnames must be NULL.  If
 * edit is NULL, a list of types will be stored into names.  If type is
 * NULL, then the type will be selected according to the filename.
 * type must be edit->syntax_type or NULL
 */
void
edit_load_syntax (WEdit *edit, GPtrArray *pnames, const char *type)
{
    syntax_t names_only = { 0 };
    char *type_found, *error_file;
    int r;
    char *f = NULL;

    if (auto_syntax)
        type = NULL;

    if (edit != NULL)
    {
        char *saved_type;

        saved_type = g_strdup (type);  // save edit->syntax_type
        edit_free_syntax_rules (edit);
        edit->syntax_type = saved_type;  // restore edit->syntax_type
    }

    if (!tty_use_colors ())
        return;

    if (!edit_options.syntax_highlighting && (pnames == NULL || pnames->len == 0))
        return;

    if (edit != NULL && edit->filename_vpath == NULL)
        return;

    f = mc_config_get_full_path (EDIT_SYNTAX_FILE);
    if (edit != NULL)
    {
        edit->syntax.get_byte = syntax_get_editor_byte;
        edit->syntax.data = &edit->buffer;
        edit->syntax.normal_color = EDITOR_NORMAL_COLOR;
        edit->syntax.skin_group = "editor";
        r = syntax_load (&edit->syntax, pnames, f, vfs_path_as_str (edit->filename_vpath),
                         get_first_editor_line (edit), auto_syntax ? NULL : edit->syntax_type,
                         &type_found, &error_file);
        if (r != 0)
            edit_free_syntax_rules (edit);
        else if (type_found != NULL && edit->syntax.rules == NULL)
        {
            // a syntax without rules is turned off for speed
            MC_PTR_FREE (edit->syntax_type);
            g_free (type_found);
            tty_color_free_temp ();
        }
        else if (type_found != NULL)
        {
            g_free (edit->syntax_type);
            edit->syntax_type = type_found;
        }
    }
    else
    {
        r = syntax_load (&names_only, pnames, f, NULL, "", NULL, &type_found, &error_file);
        syntax_free (&names_only);
        g_free (type_found);
    }
    if (r == -1)
        file_error_message (_ ("Cannot open file\n%s"), f);
    else if (r != 0)
        message (D_ERROR, _ ("Load syntax file"), _ ("Error in file %s on line %d"),
                 error_file != NULL ? error_file : f, r);

    g_free (error_file);

    g_free (f);
}

/* --------------------------------------------------------------------------------------------- */

const char *
edit_get_syntax_type (const WEdit *edit)
{
    return edit->syntax_type;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_syntax_dialog (WEdit *edit)
{
    GPtrArray *names;
    int syntax;
    char *saved_syntax;
    gboolean saved_auto_syntax;

    names = g_ptr_array_new_with_free_func (g_free);

    /* We fill the list of syntax files every time the editor is invoked.
       Instead we could save the list to a file and update it once the syntax
       file gets updated (either by testing or by explicit user command). */
    edit_load_syntax (NULL, names, NULL);
    g_ptr_array_sort (names, pstrcmp);

    saved_syntax = g_strdup (edit->syntax_type);
    saved_auto_syntax = auto_syntax;

    syntax = exec_edit_syntax_dialog (edit, names, edit->syntax_type);
    if (syntax >= 0)
        syntax_apply (edit, names, syntax);
    else if (syntax_dialog_previewed)
    {
        // cancelled: back to the original syntax
        auto_syntax = saved_auto_syntax;
        g_free (edit->syntax_type);
        edit->syntax_type = saved_syntax;
        saved_syntax = NULL;
        edit_load_syntax (edit, NULL, edit->syntax_type);
    }

    g_free (saved_syntax);
    g_ptr_array_free (names, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/** Choose color scheme for scope tokens; the editors follow the selection live */

void
edit_syntax_scheme_dialog (WEdit *edit)
{
    GPtrArray *names;
    Listbox *list;
    char *saved;
    int i, n, width;

    names = syntax_scheme_list ();
    saved = g_strdup (syntax_scheme_name != NULL ? syntax_scheme_name : "");

    list = listbox_window_new (LIST_LINES, MAX_ENTRY_LEN, _ ("Choose color scheme"), NULL);
    // text area of the listbox: items are drawn with one column of margin on each side
    width = WIDGET (list->list)->rect.cols - 2;
    LISTBOX_APPEND_TEXT (list, 'D', _ ("< Default >"), NULL, FALSE);
    for (i = 0; i < (int) names->len; i++)
    {
        const char *name = g_ptr_array_index (names, i);
        const char *title = syntax_scheme_title (name);
        const char *type = syntax_scheme_type (name);
        char *label;
        int pad;

        if (type == NULL)
            type = "";
        // scheme format right-aligned
        pad = MAX (1, width - str_term_width1 (title) - str_term_width1 (type));
        label = g_strdup_printf ("%s%*s%s", title, pad, "", type);
        LISTBOX_APPEND_TEXT (list, 0, label, NULL, FALSE);
        g_free (label);
        if (strcmp (name, saved) == 0)
            listbox_set_current (list->list, i + 1);
    }

    scheme_dialog_editors = GROUP (WIDGET (edit)->owner);
    scheme_dialog_list = list->list;
    scheme_dialog_names = names;
    // the row under the bottom frame line, in the dialog margin
    scheme_dialog_status = label_new (WIDGET (list->list)->rect.lines + 3, 2, "");
    scheme_dialog_status->color = scheme_error_colors;
    group_add_widget (GROUP (list->dlg), scheme_dialog_status);
    scheme_status_update ();
    WIDGET (list->dlg)->callback = scheme_dialog_callback;

    n = listbox_run (list);

    if (n >= 0)
        scheme_apply (names, n);
    else
    {
        // cancelled: back to the original scheme
        GPtrArray *orig;

        orig = g_ptr_array_new ();
        g_ptr_array_add (orig, saved);
        scheme_apply (orig, *saved == '\0' ? 0 : 1);
        g_ptr_array_free (orig, TRUE);
    }

    scheme_dialog_editors = NULL;
    scheme_dialog_list = NULL;
    scheme_dialog_status = NULL;
    scheme_dialog_names = NULL;
    g_free (saved);
    g_ptr_array_free (names, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
