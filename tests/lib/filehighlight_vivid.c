/*
   lib/filehighlight - tests for the YAML subset read from vivid files.

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

#define TEST_SUITE_NAME "/lib/filehighlight"

#include "tests/mctest.h"

#include "lib/global.h"  // include <glib.h>
#include "lib/filehighlight.h"
#include "lib/filehighlight/internal.h"

/* --------------------------------------------------------------------------------------------- */

static void
setup (void)
{
}

static void
teardown (void)
{
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_nested_maps_and_lists)
{
    const char *text = "core:\n"
                       "  directory: [$di]\n"
                       "  symlink:\n"
                       "    - $ln\n"
                       "text:\n"
                       "  special:\n"
                       "    - AUTHORS\n"
                       "    - \".1\"\n";
    gchar *error = NULL;
    mc_fhl_yaml_node_t *root = mc_fhl_yaml_parse (text, &error);
    const mc_fhl_yaml_node_t *dir, *link, *special;

    ck_assert_msg (root != NULL, "parse failed: %s", error);
    ck_assert_msg (error == NULL, "unexpected error: %s", error);

    dir = mc_fhl_yaml_get (mc_fhl_yaml_get (root, "core"), "directory");
    ck_assert_int_eq (dir->kind, MC_FHL_YAML_SEQ);
    ck_assert_str_eq (((mc_fhl_yaml_node_t *) g_ptr_array_index (dir->items, 0))->scalar, "$di");

    link = mc_fhl_yaml_get (mc_fhl_yaml_get (root, "core"), "symlink");
    ck_assert_int_eq (link->items->len, 1);
    ck_assert_str_eq (((mc_fhl_yaml_node_t *) g_ptr_array_index (link->items, 0))->scalar, "$ln");

    special = mc_fhl_yaml_get (mc_fhl_yaml_get (root, "text"), "special");
    ck_assert_int_eq (special->items->len, 2);
    ck_assert_str_eq (((mc_fhl_yaml_node_t *) g_ptr_array_index (special->items, 1))->scalar, ".1");

    mc_fhl_yaml_free (root);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_theme_style)
{
    const char *text = "colors:\n"
                       "  black        :  '282a36'\n"
                       "  green        :  \"50fa7b\"\n"
                       "core:\n"
                       "  socket:\n"
                       "    foreground: green\n"
                       "    background: black\n"
                       "    font-style: bold\n"
                       "  multi_hard_link: {}\n"
                       "  setuid:\n"
                       "    font-style: [bold, underline]\n";
    gchar *error = NULL;
    mc_fhl_yaml_node_t *root = mc_fhl_yaml_parse (text, &error);
    const mc_fhl_yaml_node_t *socket, *colors, *style;

    ck_assert_msg (root != NULL, "parse failed: %s", error);

    colors = mc_fhl_yaml_get (root, "colors");
    ck_assert_str_eq (((mc_fhl_yaml_node_t *) g_ptr_array_index (colors->items, 0))->scalar,
                      "282a36");

    socket = mc_fhl_yaml_get (mc_fhl_yaml_get (root, "core"), "socket");
    ck_assert_str_eq (mc_fhl_yaml_get (socket, "foreground")->scalar, "green");
    ck_assert_str_eq (mc_fhl_yaml_get (socket, "background")->scalar, "black");
    ck_assert_str_eq (mc_fhl_yaml_get (socket, "font-style")->scalar, "bold");

    style = mc_fhl_yaml_get (mc_fhl_yaml_get (root, "core"), "multi_hard_link");
    ck_assert_int_eq (style->kind, MC_FHL_YAML_MAP);
    ck_assert_int_eq (style->keys->len, 0);

    style = mc_fhl_yaml_get (mc_fhl_yaml_get (root, "core"), "setuid");
    ck_assert_int_eq (mc_fhl_yaml_get (style, "font-style")->items->len, 2);

    mc_fhl_yaml_free (root);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_sequence_of_maps)
{
    const char *text = "items:\n"
                       "  - name: first\n"
                       "    value: 1\n"
                       "  - name: second\n"
                       "    value: 2\n";
    gchar *error = NULL;
    mc_fhl_yaml_node_t *root = mc_fhl_yaml_parse (text, &error);
    const mc_fhl_yaml_node_t *items = mc_fhl_yaml_get (root, "items");
    const mc_fhl_yaml_node_t *second;

    ck_assert_msg (root != NULL, "parse failed: %s", error);
    ck_assert_int_eq (items->kind, MC_FHL_YAML_SEQ);
    ck_assert_int_eq (items->items->len, 2);

    second = g_ptr_array_index (items->items, 1);
    ck_assert_str_eq (mc_fhl_yaml_get (second, "name")->scalar, "second");
    ck_assert_str_eq (mc_fhl_yaml_get (second, "value")->scalar, "2");

    mc_fhl_yaml_free (root);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_flow_lists_and_comments)
{
    const char *text = "# whole line comment\n"
                       "a: [.bat, .com, .exe]   # trailing comment\n"
                       "b: []\n"
                       "c: 'x # not a comment'\n"
                       "d:   spaced value   \n"
                       "black  :  \"ansi:black\"\n";
    gchar *error = NULL;
    mc_fhl_yaml_node_t *root = mc_fhl_yaml_parse (text, &error);

    ck_assert_msg (root != NULL, "parse failed: %s", error);
    ck_assert_int_eq (mc_fhl_yaml_get (root, "a")->items->len, 3);
    ck_assert_int_eq (mc_fhl_yaml_get (root, "b")->items->len, 0);
    ck_assert_str_eq (mc_fhl_yaml_get (root, "c")->scalar, "x # not a comment");
    ck_assert_str_eq (mc_fhl_yaml_get (root, "d")->scalar, "spaced value");
    ck_assert_str_eq (mc_fhl_yaml_get (root, "black")->scalar, "ansi:black");

    mc_fhl_yaml_free (root);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_rejects_unsupported_syntax)
{
    const char *bad[] = {
        "a:\n\tb: 1\n",      // tab indentation
        "a: &anchor 1\n",    // anchors
        "a: *alias\n",       // aliases
        "a: |\n  text\n",    // block scalars
        "a: [x, y\n",        // unterminated flow list
        "a: 1\na: 2\n",      // duplicate key
        "a: 1\n    b: 2\n",  // unexpected indentation
        "just text\n",       // no key
    };
    gsize i;

    for (i = 0; i < G_N_ELEMENTS (bad); i++)
    {
        gchar *error = NULL;
        mc_fhl_yaml_node_t *root = mc_fhl_yaml_parse (bad[i], &error);

        ck_assert_msg (root == NULL, "accepted: %s", bad[i]);
        ck_assert_msg (error != NULL, "no error message for: %s", bad[i]);
        g_free (error);
    }
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
    tcase_add_test (tc_core, test_yaml_nested_maps_and_lists);
    tcase_add_test (tc_core, test_yaml_theme_style);
    tcase_add_test (tc_core, test_yaml_sequence_of_maps);
    tcase_add_test (tc_core, test_yaml_flow_lists_and_comments);
    tcase_add_test (tc_core, test_yaml_rejects_unsupported_syntax);
    // ***********************************

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
