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
#include "src/syntax/scheme_dialog.h"

/*** global variables ****************************************************************************/

gboolean auto_syntax = TRUE;

/*** file scope macro definitions ****************************************************************/

#define MAX_ENTRY_LEN  40
#define LIST_LINES     14
#define N_DFLT_ENTRIES 2

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* state of the syntax highlighting dialog */
static WEdit *syntax_dialog_edit = NULL;
static WListbox *syntax_dialog_list = NULL;
static const GPtrArray *syntax_dialog_names = NULL;
static gboolean syntax_dialog_previewed = FALSE;
static gboolean syntax_dialog_edit_file = FALSE;

/* the colors of a listbox window, with hotkeys on the buttons that show */
static const dlg_colors_t syntax_dialog_colors = {
    [DLG_COLOR_NORMAL] = PMENU_ENTRY_COLOR,
    [DLG_COLOR_FOCUS] = PMENU_SELECTED_COLOR,
    [DLG_COLOR_HOT_NORMAL] = MENU_HOT_COLOR,
    [DLG_COLOR_HOT_FOCUS] = MENU_HOTSEL_COLOR,
    [DLG_COLOR_SELECTED_NORMAL] = PMENU_SELECTED_COLOR,
    [DLG_COLOR_SELECTED_FOCUS] = PMENU_SELECTED_COLOR,
    [DLG_COLOR_TITLE] = PMENU_TITLE_COLOR,
    [DLG_COLOR_FRAME] = PMENU_FRAME_COLOR,
};
static gboolean syntax_dialog_associated = FALSE;

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

    if (msg == MSG_UNHANDLED_KEY && parm == KEY_F (4))
    {
        syntax_dialog_edit_file = TRUE;
        DIALOG (w)->ret_value = B_CANCEL;
        dlg_close (DIALOG (w));
        return MSG_HANDLED;
    }

    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */

/* F4 in the Associate dialog: Edit Syntax file */
static cb_ret_t
syntax_associate_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    if (msg == MSG_UNHANDLED_KEY && parm == KEY_F (4))
    {
        DIALOG (w)->ret_value = B_USER;
        dlg_close (DIALOG (w));
        return MSG_HANDLED;
    }

    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Ask for the entry of the syntax @name to change, or for a new entry with its rules that makes
 * files like the edited one use it, and save it.
 *
 * @return TRUE when it was saved, or when the Syntax file is to be edited instead
 */

static gboolean
syntax_entry_dialog (WEdit *edit, const char *name, gboolean edit_entry)
{
    const char *file_name;
    char *file_pattern, *entry_pattern, *include, *def, *def_name;
    char *pattern = NULL, *new_name = NULL, *file_label, *include_label, *title;
    gboolean done = FALSE;

    if (edit->filename_vpath == NULL)
    {
        message (D_ERROR, MSG_ERROR, _ ("Save the file first"));
        return FALSE;
    }
    if (!syntax_entry_get (name, &entry_pattern, &include))
    {
        message (D_ERROR, MSG_ERROR, _ ("No syntax \"%s\" in the Syntax file"), name);
        return FALSE;
    }

    file_name = vfs_path_as_str (edit->filename_vpath);
    file_pattern = syntax_name_pattern (file_name);
    if (!edit_entry)
    {
        def = file_pattern;
        def_name = g_strdup_printf (_ ("%s Files"), x_basename (file_name));
        title = g_strdup_printf (_ ("Add entry like \"%s\""), name);
    }
    else
    {
        // offer this file too, unless the pattern has it already
        def = strstr (entry_pattern, file_pattern) != NULL
            ? g_strdup (entry_pattern)
            : g_strdup_printf ("%s|%s", entry_pattern, file_pattern);
        g_free (file_pattern);
        def_name = g_strdup (name);
        title = g_strdup_printf (_ ("Edit entry \"%s\""), name);
    }
    file_label = g_strdup_printf (_ ("File: %s"), str_trunc (file_name, 50));
    include_label =
        g_strdup_printf (_ ("Syntax file: %s"), include != NULL ? include : _ ("(inline rules)"));

    while (!done)
    {
        int result;
        char *error;

        {
            quick_widget_t quick_widgets[] = {
                // clang-format off
                QUICK_LABEL (file_label, NULL),
                QUICK_LABEL (include_label, NULL),
                QUICK_LABELED_INPUT (edit_entry ? _ ("Name:") : _ ("Name of the new entry:"),
                                     input_label_above, def_name, "mc.edit.syntax-entry-name",
                                     &new_name, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
                QUICK_LABELED_INPUT (_ ("File name pattern, as written in the Syntax file:"),
                                     input_label_above, def, "mc.edit.syntax-entry-pattern",
                                     &pattern, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
                QUICK_START_BUTTONS (TRUE, TRUE),
                    QUICK_BUTTON (_ ("&OK"), B_ENTER, NULL, NULL),
                    QUICK_BUTTON (_ ("&Edit Syntax file (F4)"), B_USER, NULL, NULL),
                    QUICK_BUTTON (_ ("&Cancel"), B_CANCEL, NULL, NULL),
                QUICK_END,
                // clang-format on
            };

            WRect r = { -1, -1, 0, 64 };

            quick_dialog_t qdlg = {
                .rect = r,
                .title = title,
                .help = "[Input Line Keys]",
                .widgets = quick_widgets,
                .callback = syntax_associate_callback,
                .mouse_callback = NULL,
            };

            result = quick_dialog (&qdlg);
        }

        if (result == B_CANCEL)
            break;

        if (result == B_USER)
        {
            syntax_dialog_edit_file = TRUE;
            done = TRUE;
            break;
        }

        g_strstrip (new_name);
        error = edit_entry ? syntax_entry_edit (name, pattern, new_name)
                           : syntax_associate (name, pattern, new_name);
        if (error == NULL)
        {
            syntax_dialog_associated = TRUE;
            done = TRUE;
        }
        else
        {
            message (D_ERROR, title, "%s", error);
            g_free (error);
            // offer what was typed again
            g_free (def);
            def = pattern;
            pattern = NULL;
            g_free (def_name);
            def_name = new_name;
            new_name = NULL;
        }
    }

    g_free (pattern);
    g_free (new_name);
    g_free (def);
    g_free (def_name);
    g_free (entry_pattern);
    g_free (include);
    g_free (title);
    g_free (file_label);
    g_free (include_label);

    return done;
}

/* --------------------------------------------------------------------------------------------- */

/* the Add entry and Edit entry buttons: close the dialog when the Syntax file was changed */
static int
syntax_entry_button_cb (WButton *button, int action)
{
    int current = syntax_dialog_list->current;

    (void) button;

    if (current < N_DFLT_ENTRIES)
    {
        message (D_ERROR, MSG_ERROR, _ ("Choose a syntax first"));
        return 0;
    }

    return syntax_entry_dialog (syntax_dialog_edit,
                                g_ptr_array_index (syntax_dialog_names, current - N_DFLT_ENTRIES),
                                action == B_USER + 1)
        ? 1
        : 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
exec_edit_syntax_dialog (WEdit *edit, const GPtrArray *names, const char *current_syntax)
{
    size_t i;
    Listbox *syntaxlist;
    WButton *add, *change;
    WRect r;
    int n, x, y;

    syntaxlist =
        listbox_window_new (LIST_LINES + 2, MAX_ENTRY_LEN, _ ("Choose syntax highlighting"), NULL);
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

    // make room for the buttons under the list
    r = WIDGET (syntaxlist->list)->rect;
    r.lines -= 2;
    widget_set_size_rect (WIDGET (syntaxlist->list), &r);
    add = button_new (0, 0, B_USER, NORMAL_BUTTON, _ ("&Add entry..."), syntax_entry_button_cb);
    change =
        button_new (0, 0, B_USER + 1, NORMAL_BUTTON, _ ("&Edit entry..."), syntax_entry_button_cb);
    y = r.y - WIDGET (syntaxlist->dlg)->rect.y + r.lines + 1;
    x = (WIDGET (syntaxlist->dlg)->rect.cols - WIDGET (add)->rect.cols - 1
         - WIDGET (change)->rect.cols)
        / 2;
    WIDGET (add)->rect.y = WIDGET (change)->rect.y = y;
    WIDGET (add)->rect.x = x;
    WIDGET (change)->rect.x = x + WIDGET (add)->rect.cols + 1;
    // before the list, so that their hotkeys win over the one of < Auto >
    group_add_widget_before (GROUP (syntaxlist->dlg), add, syntaxlist->list);
    group_add_widget_before (GROUP (syntaxlist->dlg), change, syntaxlist->list);
    group_add_widget (GROUP (syntaxlist->dlg), hline_new (y - 1, -1, -1));
    syntaxlist->dlg->colors = syntax_dialog_colors;
    widget_select (WIDGET (syntaxlist->list));

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

/* repaint all editors in the colors of the new scheme */
static void
scheme_reload_editors (void *data)
{
    g_list_foreach (GROUP (data)->widgets, scheme_reload_editor, NULL);
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

    syntax_dialog_edit_file = FALSE;
    syntax_dialog_associated = FALSE;
    syntax = exec_edit_syntax_dialog (edit, names, edit->syntax_type);
    if (syntax_dialog_associated)
        syntax_apply (edit, names, 0);  // the new entry is found as Auto
    else if (syntax >= 0 && !syntax_dialog_edit_file)
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

    if (syntax_dialog_edit_file)
        edit_load_syntax_file (DIALOG (WIDGET (edit)->owner));
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/** Choose color scheme for scope tokens; the editors follow the selection live */

void
edit_syntax_scheme_dialog (WEdit *edit)
{
    syntax_scheme_dialog (scheme_reload_editors, GROUP (WIDGET (edit)->owner));
}

/* --------------------------------------------------------------------------------------------- */
