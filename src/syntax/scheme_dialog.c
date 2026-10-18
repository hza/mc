/*
   Syntax highlighting: the color scheme chooser.

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

/** \file
 *  \brief Source: the color scheme chooser of the editor and the viewer
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/skin.h"
#include "lib/strutil.h"  // str_term_width1(), str_trunc()
#include "lib/widget.h"   // Listbox

#include "syntax_scheme.h"
#include "scheme_dialog.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define MAX_ENTRY_LEN 40
#define LIST_LINES    14

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* state of the color scheme dialog */
static syntax_scheme_apply_fn scheme_dialog_apply = NULL;
static void *scheme_dialog_apply_data = NULL;
static WListbox *scheme_dialog_list = NULL;
static WLabel *scheme_dialog_status = NULL;
/* scheme load errors stand out in the skin's error colors */
static const label_colors_t scheme_error_colors = {
    [LABEL_COLOR_MAIN] = ERROR_NORMAL_COLOR,
    [LABEL_COLOR_DISABLED] = CORE_DISABLED_COLOR,  // unused
};
static const GPtrArray *scheme_dialog_names = NULL;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* make the scheme of list entry @n current and repaint what shows it */
static void
scheme_apply (const GPtrArray *names, int n)
{
    const char *name;

    name = n <= 0 ? "" : (const char *) g_ptr_array_index (names, n - 1);
    if (syntax_scheme_name != NULL && strcmp (syntax_scheme_name, name) == 0)
        return;

    g_free (syntax_scheme_name);
    syntax_scheme_name = g_strdup (name);

    scheme_dialog_apply (scheme_dialog_apply_data);
    do_refresh ();
}

/* --------------------------------------------------------------------------------------------- */

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

/* preview the scheme as the selection moves */
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

/**
 * Choose the color scheme for scope tokens.  @apply is called with @data whenever the scheme
 * changes, so that what is colored follows the selection live.
 */

void
syntax_scheme_dialog (syntax_scheme_apply_fn apply, void *data)
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

    scheme_dialog_apply = apply;
    scheme_dialog_apply_data = data;
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

    scheme_dialog_apply = NULL;
    scheme_dialog_apply_data = NULL;
    scheme_dialog_list = NULL;
    scheme_dialog_status = NULL;
    scheme_dialog_names = NULL;
    g_free (saved);
    g_ptr_array_free (names, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
