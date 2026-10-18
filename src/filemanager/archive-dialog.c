/*
   Dialog to pack the selected files into an archive

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

/** \file src/filemanager/archive-dialog.c
 *  \brief Source: dialog to pack the selected files into an archive
 *
 *  Opened from the Copy dialog with F5. Only the dialog exists so far:
 *  OK does not pack anything yet.
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/tty/tty.h"  // KEY_F(), COLS
#include "lib/strutil.h"
#include "lib/util.h"
#include "lib/vfs/vfs.h"
#include "lib/widget.h"

#include "dir.h"          // DIR_IS_DOTDOT
#include "filemanager.h"  // current_panel
#include "layout.h"       // get_other_panel()
#include "panel.h"

#include "archive-pack.h"
#include "archive-dialog.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define B_ARCHIVE_BG (B_USER + 1)  // the Background button

/*** file scope type declarations ****************************************************************/

/** A button that shows one value of a list and picks another one from a popup */
typedef struct
{
    WButton *button;
    WRadio *radio;  // switched to its last item ("Other:") on a pick
    const char **items;
    int count;
    int sel;
} archive_dropdown_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

#define DROPDOWN_COLS 14

static const char *format_items[] = { "zip", "7z", N_ ("Other:") };
static const char *format_other[] = { "tar", "tar.gz", "tar.bz2", "tar.xz", "tar.zst" };
static const char *compression_items[] = { N_ ("Normal"), N_ ("Maximum"), N_ ("Other:") };
static const char *compression_other[] = { N_ ("Store"), N_ ("Fastest"), N_ ("Fast"),
                                           N_ ("Ultra") };
static const char *method_items[] = { "ZipCrypto", "AES-256" };

static archive_dropdown_t format_dropdown = { NULL, NULL, format_other, G_N_ELEMENTS (format_other),
                                              1 };
static archive_dropdown_t compression_dropdown = { NULL, NULL, compression_other,
                                                   G_N_ELEMENTS (compression_other), 1 };

static WInput *archive_name;
static WRadio *archive_format;
static WRadio *archive_compression;
static WCheck *archive_encrypt;
static WButton *archive_encryption;

// encryption options, kept while the dialog is open
static char *enc_password = NULL;
static int enc_method = 1;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/** The suffix of the chosen format */

static const char *
archive_suffix (void)
{
    if (archive_format->sel < 2)
        return format_items[archive_format->sel];
    return format_other[format_dropdown.sel];
}

/* --------------------------------------------------------------------------------------------- */
/** Only zip can be encrypted: libarchive writes 7z without encryption */

static gboolean
archive_can_encrypt (void)
{
    return archive_format->sel == 0;
}

/* --------------------------------------------------------------------------------------------- */
/** Replace the archive suffix at the end of the name with the one of the chosen format */

static void
archive_set_suffix (void)
{
    const char *text;
    size_t len, cut = 0, i;
    char *name;

    text = input_get_ctext (archive_name);
    len = strlen (text);

    // the longest known suffix, so "tar.gz" wins over a shorter one
    for (i = 0; i < G_N_ELEMENTS (format_items) + G_N_ELEMENTS (format_other); i++)
    {
        const char *t;
        size_t slen;

        if (i < 2)
            t = format_items[i];
        else if (i == 2)
            continue;
        else
            t = format_other[i - 3];
        slen = strlen (t) + 1;

        if (slen > cut && len > slen && text[len - slen] == '.'
            && strcmp (text + len - slen + 1, t) == 0)
            cut = slen;
    }

    name = g_strdup_printf ("%.*s.%s", (int) (len - cut), text, archive_suffix ());
    input_assign_text (archive_name, name);
    g_free (name);
}

/* --------------------------------------------------------------------------------------------- */

static void
archive_format_changed (void)
{
    archive_set_suffix ();
    widget_disable (WIDGET (archive_encrypt), !archive_can_encrypt ());
    widget_disable (WIDGET (archive_encryption), !archive_can_encrypt ());
}

/* --------------------------------------------------------------------------------------------- */
/** The button text of a dropdown: the value padded to a fixed width */

static char *
archive_dropdown_text (const archive_dropdown_t *d)
{
    return g_strdup_printf ("%-*s", DROPDOWN_COLS, _ (d->items[d->sel]));
}

/* --------------------------------------------------------------------------------------------- */

static int
archive_dropdown_cb (WButton *button, int action)
{
    archive_dropdown_t *d;
    const Widget *w = WIDGET (button);
    Listbox *lb;
    int i, pick;

    (void) action;

    d = button == format_dropdown.button ? &format_dropdown : &compression_dropdown;

    lb = listbox_window_centered_new (w->rect.y + d->count / 2 + 2, w->rect.x + w->rect.cols / 2,
                                      d->count, w->rect.cols - 4, NULL, NULL);
    for (i = 0; i < d->count; i++)
        listbox_add_item (lb->list, LISTBOX_APPEND_AT_END, 0, _ (d->items[i]), NULL, FALSE);
    listbox_set_current (lb->list, d->sel);

    pick = listbox_run (lb);
    if (pick >= 0)
    {
        char *text;

        d->sel = pick;
        text = archive_dropdown_text (d);
        button_set_text (button, text);
        g_free (text);

        d->radio->sel = d->radio->count - 1;
        widget_draw (WIDGET (d->radio));
        if (d == &format_dropdown)
            archive_format_changed ();
    }

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
archive_advanced_cb (WButton *button, int action)
{
    int ret;

    (void) button;
    (void) action;

    while (TRUE)
    {
        char *pass = NULL, *confirm = NULL;
        // labels padded to one width, so the inputs start in the same column
        const char *pass_text = _ ("Password:"), *confirm_text = _ ("Confirm:");
        const int label_cols = MAX (str_term_width1 (pass_text), str_term_width1 (confirm_text));
        char *pass_label =
            g_strdup_printf ("%s%*s", pass_text, label_cols - str_term_width1 (pass_text), "");
        char *confirm_label = g_strdup_printf ("%s%*s", confirm_text,
                                               label_cols - str_term_width1 (confirm_text), "");
        int method = enc_method;

        quick_widget_t quick_widgets[] = {
            // clang-format off
            QUICK_LABELED_INPUT (pass_label, input_label_left, enc_password != NULL ? enc_password : "",
                                 NULL, &pass, NULL, TRUE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (confirm_label, input_label_left, enc_password != NULL ? enc_password : "",
                                 NULL, &confirm, NULL, TRUE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_SEPARATOR (TRUE),
            QUICK_RADIO (G_N_ELEMENTS (method_items), method_items, &method, NULL),
            QUICK_BUTTONS_OK_CANCEL,
            QUICK_END,
            // clang-format on
        };

        WRect r = { -1, -1, 0, 50 };

        quick_dialog_t qdlg = {
            .rect = r,
            .title = _ ("Encrypt options"),
            .help = "[Pack files]",
            .widgets = quick_widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };

        ret = quick_dialog (&qdlg);
        g_free (pass_label);
        g_free (confirm_label);
        if (ret != B_ENTER)
            return 0;

        if (strcmp (pass, confirm) != 0)
        {
            message (D_ERROR, MSG_ERROR, _ ("The passwords do not match."));
            g_free (pass);
            g_free (confirm);
            continue;
        }

        g_free (enc_password);
        enc_password = pass;
        g_free (confirm);
        enc_method = method;

        archive_encrypt->state = TRUE;
        widget_draw (WIDGET (archive_encrypt));
        return 0;
    }
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
archive_dialog_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    WDialog *h = DIALOG (w);

    switch (msg)
    {
    case MSG_NOTIFY:
        if (sender == WIDGET (archive_format))
        {
            archive_format_changed ();
            return MSG_HANDLED;
        }
        break;

    case MSG_UNHANDLED_KEY:
        // F5 goes back to the Copy dialog
        if (parm == KEY_F (5))
        {
            h->ret_value = B_USER;
            dlg_close (h);
            return MSG_HANDLED;
        }
#ifdef ENABLE_BACKGROUND
        if (parm == KEY_F (2))
        {
            h->ret_value = B_ARCHIVE_BG;
            dlg_close (h);
            return MSG_HANDLED;
        }
#endif
        break;

    default:
        break;
    }

    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */
/** The archive name without a suffix: the current entry, or the directory for many files */

static char *
archive_base_name (const WPanel *panel)
{
    const file_entry_t *fe;
    const char *name, *dot;

    fe = panel_current_entry (panel);
    if (panel->marked > 1 || fe == NULL || DIR_IS_DOTDOT (fe->fname->str))
    {
        const char *cwd = vfs_path_as_str (panel->cwd_vpath);

        name = x_basename (cwd);
        return g_strdup (*name != '\0' ? name : "archive");
    }

    name = fe->fname->str;
    if (panel->marked == 1)
    {
        int i;

        for (i = 0; i < panel->dir.len; i++)
            if (panel->dir.list[i].f.marked != 0)
            {
                fe = &panel->dir.list[i];
                name = fe->fname->str;
                break;
            }
    }

    dot = strrchr (name, '.');
    if (S_ISDIR (fe->st.st_mode) || dot == NULL || dot == name)
        return g_strdup (name);
    return g_strndup (name, (gsize) (dot - name));
}

/* --------------------------------------------------------------------------------------------- */
/** Add a radio in a box at @y, @x with a dropdown under it */

static WRadio *
archive_add_choice (WGroup *g, int y, int x, int cols, const char *title, const char **items,
                    int count, int sel, archive_dropdown_t *d)
{
    WRadio *radio;
    char *text;
    int i;

    for (i = 0; i < count; i++)
        items[i] = _ (items[i]);

    group_add_widget (g, groupbox_new (y, x, count + 3, cols, title));
    radio = radio_new (y + 1, x + 2, count, items);
    radio->sel = sel;
    group_add_widget (g, radio);

    text = archive_dropdown_text (d);
    d->button = button_new (y + 1 + count, x + 2, 0, NORMAL_BUTTON, text, archive_dropdown_cb);
    g_free (text);
    d->radio = radio;
    group_add_widget (g, d->button);

    return radio;
}

/* --------------------------------------------------------------------------------------------- */
/** The compression level of the dialog: Normal, Maximum, or the Other: dropdown */

static int
archive_level (void)
{
    static const int other_levels[] = { 0, 1, 3, ARCHIVE_LEVEL_ULTRA };  // Store .. Ultra

    switch (archive_compression->sel)
    {
    case 0:
        return 6;
    case 1:
        return 9;
    default:
        return other_levels[compression_dropdown.sel];
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * The names to pack, relative to *@src_dir: the marked entries, or the current one, or on ".."
 * the panel directory itself (from its parent). *@src_dir is to be freed.
 */

static GPtrArray *
archive_sources (const WPanel *panel, vfs_path_t **src_dir)
{
    GPtrArray *names;

    names = g_ptr_array_new_with_free_func (g_free);
    *src_dir = vfs_path_clone (panel->cwd_vpath);

    if (panel->marked > 0)
    {
        int i;

        for (i = 0; i < panel->dir.len; i++)
            if (panel->dir.list[i].f.marked != 0 && !DIR_IS_DOTDOT (panel->dir.list[i].fname->str))
                g_ptr_array_add (names, g_strdup (panel->dir.list[i].fname->str));
    }
    else
    {
        const file_entry_t *fe;

        fe = panel_current_entry (panel);
        if (fe != NULL && !DIR_IS_DOTDOT (fe->fname->str))
            g_ptr_array_add (names, g_strdup (fe->fname->str));
        else if (fe != NULL)
        {
            const char *cwd = vfs_path_as_str (panel->cwd_vpath);
            char *parent, *base;

            parent = g_path_get_dirname (cwd);
            base = g_path_get_basename (cwd);
            if (strcmp (base, PATH_SEP_STR) != 0 && strcmp (base, ".") != 0)
            {
                vfs_path_free (*src_dir, TRUE);
                *src_dir = vfs_path_from_str (parent);
                g_ptr_array_add (names, base);
                base = NULL;
            }
            g_free (base);
            g_free (parent);
        }
    }

    return names;
}

/* --------------------------------------------------------------------------------------------- */
/** FALSE if the archive exists and the user does not want to overwrite it */

static gboolean
archive_confirm_target (const archive_pack_opts_t *opts)
{
    vfs_path_t *vpath;
    struct stat st;
    gboolean ok = TRUE;

    if (opts->archive_path == NULL || *opts->archive_path == '\0')
        return FALSE;

    vpath = vfs_path_from_str (opts->archive_path);
    if (mc_stat (vpath, &st) == 0)
        ok = query_dialog (_ ("Create archive"), _ ("The archive already exists. Overwrite it?"),
                           D_ERROR, 2, _ ("&Overwrite"), _ ("&Cancel"))
            == 0;
    vfs_path_free (vpath, TRUE);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/**
 * Show the Create archive dialog for the current panel, @cols wide.
 * @return TRUE if the user asked to go back to the Copy dialog
 */

gboolean
archive_dialog (int cols)
{
    const int lines = 18;
    int half;
    WPanel *panel = current_panel;
    const char *path_label = _ ("Archive path:");
    char *base, *name;
    WDialog *dlg;
    WGroup *g;
    WCheck *move;
    archive_pack_opts_t opts;
    WButton *buttons[4];
    int n = 0;
    int i, x, ret;

    // nothing to pack, as on ".." of the root directory
    {
        GPtrArray *names;
        gboolean empty;

        vfs_path_t *src_dir;

        names = archive_sources (panel, &src_dir);
        empty = names->len == 0;
        g_ptr_array_free (names, TRUE);
        vfs_path_free (src_dir, TRUE);
        if (empty)
        {
            message (D_ERROR, _ ("Create archive"), _ ("Select the files to pack."));
            return FALSE;
        }
    }

    memset (&opts, 0, sizeof (opts));
    cols = MIN (MAX (cols, 64), COLS - 4);
    half = (cols - 4) / 2;

    base = archive_base_name (panel);
    name = g_strdup_printf ("%s%s%s.%s", vfs_path_as_str (get_other_panel ()->cwd_vpath),
                            PATH_SEP_STR, base, format_items[0]);

    dlg = dlg_create (TRUE, 0, 0, lines, cols, WPOS_CENTER, FALSE, dialog_colors,
                      archive_dialog_callback, NULL, "[Pack files]", _ ("Create archive"));
    g = GROUP (dlg);

    // y 2, 3: the archive path
    group_add_widget (g, label_new (2, 3, path_label));
    archive_name =
        input_new (3, 3, input_colors, cols - 6, name, "mc.archive.name", INPUT_COMPLETE_FILENAMES);
    group_add_widget (g, archive_name);

    // y 4..9: format and compression, each a box of a radio and a dropdown
    archive_format = archive_add_choice (g, 4, 2, half, _ ("Format"), format_items,
                                         G_N_ELEMENTS (format_items), 0, &format_dropdown);
    archive_compression =
        archive_add_choice (g, 4, 2 + half, cols - 4 - half, _ ("Compression"), compression_items,
                            G_N_ELEMENTS (compression_items), 0, &compression_dropdown);

    // y 10..13: options
    group_add_widget (g, groupbox_new (10, 2, 4, cols - 4, _ ("Options")));
    move = check_new (11, 4, FALSE, _ ("&Move to archive"));
    group_add_widget (g, move);
    archive_encrypt = check_new (12, 4, FALSE, _ ("&Encrypt files"));
    group_add_widget (g, archive_encrypt);
    archive_encryption = button_new (12, 4 + widget_get_columns (WIDGET (archive_encrypt)) + 2, 0,
                                     NORMAL_BUTTON, _ ("Encrypt op&tions..."), archive_advanced_cb);
    group_add_widget (g, archive_encryption);

    // y 14: line, y 15: buttons, y 16: bottom frame
    group_add_widget (g, hline_new (lines - 4, -1, -1));
    buttons[n++] = button_new (lines - 3, 0, B_ENTER, DEFPUSH_BUTTON, _ ("&OK"), NULL);
#ifdef ENABLE_BACKGROUND
    buttons[n++] =
        button_new (lines - 3, 0, B_ARCHIVE_BG, NORMAL_BUTTON, _ ("&Background (F2)"), NULL);
#endif
    buttons[n++] = button_new (lines - 3, 0, B_USER, NORMAL_BUTTON, _ ("Co&py (F5)"), NULL);
    buttons[n++] = button_new (lines - 3, 0, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL);
    x = 0;
    for (i = 0; i < n; i++)
        x += widget_get_columns (WIDGET (buttons[i])) + 2;
    x = (cols - x + 2) / 2;
    for (i = 0; i < n; i++)
    {
        WIDGET (buttons[i])->rect.x = x;
        group_add_widget (g, buttons[i]);
        x += widget_get_columns (WIDGET (buttons[i])) + 2;
    }

    widget_select (WIDGET (archive_name));

    ret = dlg_run (dlg);

    if (ret == B_ENTER || ret == B_ARCHIVE_BG)
    {
        // an encrypted archive needs a password
        if (archive_can_encrypt () && archive_encrypt->state
            && (enc_password == NULL || *enc_password == '\0'))
            archive_advanced_cb (NULL, 0);

        if (archive_can_encrypt () && archive_encrypt->state
            && (enc_password == NULL || *enc_password == '\0'))
            ret = B_CANCEL;
        else
        {
            opts.archive_path = tilde_expand (input_get_ctext (archive_name));
            opts.format = archive_suffix ();
            opts.level = archive_level ();
            opts.move = move->state;
            if (archive_can_encrypt () && archive_encrypt->state)
            {
                opts.password = enc_password;
                enc_password = NULL;
                opts.aes = enc_method == 1;
            }
        }
    }

    widget_destroy (WIDGET (dlg));
    archive_name = NULL;
    archive_format = NULL;
    archive_encrypt = NULL;
    archive_encryption = NULL;
    format_dropdown.button = compression_dropdown.button = NULL;
    format_dropdown.radio = compression_dropdown.radio = NULL;
    MC_PTR_FREE (enc_password);

    if ((ret == B_ENTER || ret == B_ARCHIVE_BG) && archive_confirm_target (&opts))
    {
        GPtrArray *names;

        vfs_path_t *src_dir;

        names = archive_sources (panel, &src_dir);
        if (names->len != 0)
        {
            archive_pack_run (&opts, src_dir, names, ret == B_ARCHIVE_BG);
            update_panels (UP_RELOAD, UP_KEEPSEL);
            repaint_screen ();
        }
        g_ptr_array_free (names, TRUE);
        vfs_path_free (src_dir, TRUE);
    }
    archive_pack_opts_free (&opts);

    g_free (name);
    g_free (base);

    return ret == B_USER;
}

/* --------------------------------------------------------------------------------------------- */
