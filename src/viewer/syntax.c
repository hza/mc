/*
   Internal file viewer for the Midnight Commander
   Syntax highlighting of the shown text

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
 *  \brief Source: syntax highlighting in the internal viewer
 *
 *  The viewer colors text with the rules of the editor.  It asks for the color of each byte
 *  it draws, jumping around the text, so the rules keep every state they walk through.
 *  The color at an offset depends on every byte before it; past mcview_syntax_sync_window
 *  bytes from a known state, the rules start afresh at a line start rather than read all
 *  of them, which may color a long comment or string wrong.
 */

#include <config.h>

#include "lib/global.h"
#include "lib/fileloc.h"  // EDIT_SYNTAX_FILE
#include "lib/mcconfig.h"
#include "lib/skin.h"
#include "lib/tty/color.h"  // tty_use_colors()
#include "lib/util.h"

#include "src/syntax/scheme_dialog.h"

#include "internal.h"

/*** global variables ****************************************************************************/

gboolean mcview_syntax_highlighting = TRUE;

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* the most bytes walked to color one: about a tenth of a second */
static const off_t mcview_syntax_sync_window = 1024 * 1024;
/* one saved state per this many bytes: 16 KiB of states per MiB of text read */
static const off_t mcview_syntax_checkpoint_density = 4096;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static int
mcview_syntax_get_byte (const void *data, off_t byte_index)
{
    int c;

    return mcview_get_byte ((WView *) data, byte_index, &c) ? c : '\n';
}

/* --------------------------------------------------------------------------------------------- */

/** The first line, which may tell the syntax as well as the file name does. */

static const char *
mcview_syntax_first_line (WView *view)
{
    static char s[256];
    size_t i;

    for (i = 0; i < sizeof (s) - 1; i++)
    {
        int c;

        if (!mcview_get_byte (view, (off_t) i, &c) || c == '\n')
            break;
        s[i] = (char) c;
    }
    s[i] = '\0';

    return s;
}

/* recolor the text with the scheme just chosen */
static void
mcview_syntax_scheme_apply (void *data)
{
    WView *view = (WView *) data;

    mcview_syntax_unload (view);
    view->dirty++;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/** Load the rules for the text, unless highlighting is off or does not fit how it is shown. */

void
mcview_syntax_load (WView *view)
{
    char *syntax_file, *type_found, *error_file;
    const char *file_name;
    int r;

    mcview_syntax_unload (view);
    view->syntax_loaded = TRUE;

    // nroff mode colors the text itself, from the backspaces the rules would trip over
    if (!mcview_syntax_highlighting || !tty_use_colors () || view->mode_flags.nroff
        || view->datasource == DS_NONE)
        return;

    view->syntax.get_byte = mcview_syntax_get_byte;
    view->syntax.data = view;
    view->syntax.normal_color = VIEWER_NORMAL_COLOR;
    view->syntax.skin_group = "viewer";
    view->syntax.fixed = TRUE;
    view->syntax.checkpoint_density = mcview_syntax_checkpoint_density;
    view->syntax.sync_window = mcview_syntax_sync_window;

    // the output of a command has no file name, but its first line may tell the syntax
    file_name = view->filename_vpath != NULL ? vfs_path_as_str (view->filename_vpath) : "";

    syntax_file = mc_config_get_full_path (EDIT_SYNTAX_FILE);
    r = syntax_load (&view->syntax, NULL, syntax_file, file_name, mcview_syntax_first_line (view),
                     NULL, &type_found, &error_file);
    // a broken syntax file is reported by the editor; the viewer just shows plain text
    if (r != 0)
        syntax_free (&view->syntax);
    else if (view->syntax.rules != NULL)
        (void) syntax_get_color (&view->syntax, -1);  // the state before the text

    g_free (type_found);
    g_free (error_file);
    g_free (syntax_file);
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_syntax_unload (WView *view)
{
    syntax_free (&view->syntax);
    view->syntax_loaded = FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/** Color of the byte at @offset, VIEWER_NORMAL_COLOR without rules. */

int
mcview_syntax_color (WView *view, off_t offset)
{
    if (view->syntax.rules == NULL)
        return VIEWER_NORMAL_COLOR;

    return syntax_get_color (&view->syntax, offset);
}

/* --------------------------------------------------------------------------------------------- */

/** Choose the color scheme; the text follows the selection live. */

void
mcview_syntax_scheme_dialog (WView *view)
{
    syntax_scheme_dialog (mcview_syntax_scheme_apply, view);
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_toggle_syntax_mode (WView *view)
{
    mcview_syntax_highlighting = !mcview_syntax_highlighting;
    mcview_syntax_unload (view);
    view->dpy_bbar_dirty = TRUE;
    view->dirty++;
}

/* --------------------------------------------------------------------------------------------- */
