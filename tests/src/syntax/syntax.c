/*
   src/syntax - tests for looking up the syntax colors of text that does not change

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

#define TEST_SUITE_NAME "/src/syntax"

#include "tests/mctest.h"

#include <glib/gstdio.h>

#include "src/syntax/syntax.h"

/* explicit colors everywhere, so that no skin is needed */
static const char test_syntax[] = "file .\\* Test\n"
                                  "context default lightgray black\n"
                                  "    keyword whole int yellow black\n"
                                  "    keyword whole return yellow black\n"
                                  "context /\\* \\*/ brown black\n"
                                  "context \" \" green black\n"
                                  "    keyword \\\\\" brightgreen black\n";

extern GHashTable *mc_tty_color__hashtable;

static char *syntax_dir;
static char *syntax_file;
static GString *text;

/* --------------------------------------------------------------------------------------------- */

static int
get_text_byte (const void *data, off_t byte_index)
{
    const GString *s = (const GString *) data;

    return byte_index < 0 || byte_index >= (off_t) s->len ? '\n'
                                                          : (unsigned char) s->str[byte_index];
}

/* --------------------------------------------------------------------------------------------- */

static void
load (syntax_t *syn, gboolean fixed, off_t density, off_t sync_window)
{
    char *type_found, *error_file;
    int r;

    memset (syn, 0, sizeof (*syn));
    syn->get_byte = get_text_byte;
    syn->data = text;
    syn->normal_color = 1;
    syn->skin_group = "editor";
    syn->fixed = fixed;
    syn->checkpoint_density = density;
    syn->sync_window = sync_window;

    r = syntax_load (syn, NULL, syntax_file, "test", "", "Test", &type_found, &error_file);
    ck_assert_int_eq (r, 0);
    mctest_assert_str_eq (type_found, "Test");
    mctest_assert_not_null (syn->rules);
    g_free (type_found);
    g_free (error_file);

    // the state before the text
    (void) syntax_get_color (syn, -1);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Code with comments and strings that span lines, all shorter than the sync window used
 * below.  Unless @multiline_strings, no line starts inside a string: there, a fresh start
 * would take the closing quote for an opening one.
 */

static void
fill_text (gboolean multiline_strings)
{
    int i;

    g_string_truncate (text, 0);
    for (i = 0; i < 2000; i++)
        switch (i % 5)
        {
        case 0:
            g_string_append_printf (text, "int x%d = %d; /* a comment\n   on %d lines */\n", i, i,
                                    i % 3 + 2);
            break;
        case 1:
            g_string_append (text, "return \"a \\\" string\" + 1;\n");
            break;
        case 2:
            g_string_append (text,
                             multiline_strings ? "\"a string\nover two lines\" int\n"
                                               : "\"a string on one line\" int\n");
            break;
        case 3:
            g_string_append (text, "/**/int/* */ returning interest\n");
            break;
        default:
            g_string_append (text, "\n");
            break;
        }
}

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void
setup (void)
{
    mc_tty_color__hashtable = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    syntax_dir = g_dir_make_tmp ("mc-syntax-XXXXXX", NULL);
    syntax_file = g_build_filename (syntax_dir, "Syntax", (char *) NULL);
    g_file_set_contents (syntax_file, test_syntax, -1, NULL);

    text = g_string_new ("");
    fill_text (TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* @After */
static void
teardown (void)
{
    g_string_free (text, TRUE);
    g_unlink (syntax_file);
    g_rmdir (syntax_dir);
    g_free (syntax_file);
    g_free (syntax_dir);
    g_hash_table_destroy (mc_tty_color__hashtable);
    mc_tty_color__hashtable = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* colors met walking the text from the start, as the editor does */
static int *
reference_colors (void)
{
    syntax_t syn;
    int *colors;
    off_t i;

    load (&syn, FALSE, 0, 0);
    colors = g_new (int, text->len);
    for (i = 0; i < (off_t) text->len; i++)
        colors[i] = syntax_get_color (&syn, i);
    syntax_free (&syn);

    return colors;
}

/* --------------------------------------------------------------------------------------------- */

/* jumping around fixed text gives the colors of a walk from the start */
START_TEST (test_fixed_random_access)
{
    int *colors;
    syntax_t syn;
    guint32 seed = 12345;
    off_t i;
    int n;

    // given
    colors = reference_colors ();
    load (&syn, TRUE, 64, 0);

    // when/then: forward over the first half, then backward, then anywhere
    for (i = 0; i < (off_t) text->len / 2; i++)
        ck_assert_int_eq (syntax_get_color (&syn, i), colors[i]);
    for (i = (off_t) text->len - 1; i >= 0; i--)
        ck_assert_int_eq (syntax_get_color (&syn, i), colors[i]);
    for (n = 0; n < 20000; n++)
    {
        seed = seed * 1103515245 + 12345;
        i = (off_t) (seed >> 8) % (off_t) text->len;
        ck_assert_int_eq (syntax_get_color (&syn, i), colors[i]);
    }

    syntax_free (&syn);
    mctest_assert_null (syn.checkpoints);
    g_free (colors);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* a sync window longer than any comment or string, none of them cut by a line start, changes
   no color */
START_TEST (test_fixed_sync_window)
{
    int *colors;
    syntax_t syn;
    off_t i;

    // given
    fill_text (FALSE);
    colors = reference_colors ();
    load (&syn, TRUE, 64, 1024);

    // when/then: going back from the end, never from the start
    for (i = (off_t) text->len - 1; i >= 0; i -= 997)
        ck_assert_int_eq (syntax_get_color (&syn, i), colors[i]);
    syntax_free (&syn);

    load (&syn, TRUE, 64, 1024);
    for (i = (off_t) text->len - 1; i >= 0; i--)
        ck_assert_int_eq (syntax_get_color (&syn, i), colors[i]);

    syntax_free (&syn);
    g_free (colors);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_checked_fixture (tc_core, setup, teardown);

    // Add new tests here: ***************
    tcase_add_test (tc_core, test_fixed_random_access);
    tcase_add_test (tc_core, test_fixed_sync_window);
    // ***********************************

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
