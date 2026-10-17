/*
   src/editor - tests for editing files with CR LF line ends

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Henady Zakalusky <zakalusky@scnsoft.com>, 2026

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

/*
 * The CR of a CR LF line end is drawn blank instead of ^M.  Editing must keep such line
 * ends whole: the cursor never stays between the CR and the LF, End stops before the CR,
 * Enter copies the line end of the current (or, at the end of the file, previous) line,
 * and Backspace and Delete remove the CR and the LF together.  Each case below replays
 * key commands on a buffer and compares the resulting bytes.
 */

#define TEST_SUITE_NAME "/src/editor"

#include "tests/mctest.h"

#include "lib/charsets.h"
#include "lib/keybind.h"
#include "src/selcodepage.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"

static WGroup owner;
static WEdit *test_edit;

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void
setup (void)
{
    WRect r;

    str_init_strings (NULL);

    mc_global.sysconfig_dir = (char *) TEST_SHARE_DIR;
    load_codepages_list ();

    edit_options.filesize_threshold = (char *) "64M";

    rect_init (&r, 0, 0, 24, 80);
    test_edit = edit_init (NULL, &r, NULL);
    memset (&owner, 0, sizeof (owner));
    group_add_widget (&owner, WIDGET (test_edit));

    mc_global.source_codepage = 0;
    mc_global.display_codepage = 0;
    cp_source = "ASCII";
    cp_display = "ASCII";

    do_set_codepage (0);
    edit_set_codeset (test_edit);
}

/* --------------------------------------------------------------------------------------------- */

/* @After */
static void
teardown (void)
{
    edit_clean (test_edit);
    group_remove_widget (test_edit);
    g_free (test_edit);

    free_codepages_list ();
    str_uninit_strings ();
}

/* --------------------------------------------------------------------------------------------- */

static void
test_load_text (const char *text)
{
    // edit_insert() keeps the line counters that Up and Down rely on
    for (; *text != '\0'; text++)
        edit_insert (test_edit, *text);
    // start at the beginning of the file, as after loading it
    edit_cursor_move (test_edit, -test_edit->buffer.curs1);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Replay @keys: upper case letters are commands (R right, L left, U up, D down, E end,
 * N Enter, B Backspace, X Delete), anything else is typed.
 */

static void
test_press_keys (const char *keys)
{
    for (; *keys != '\0'; keys++)
    {
        long command = CK_InsertChar;
        int c = -1;

        switch (*keys)
        {
        case 'R':
            command = CK_Right;
            break;
        case 'L':
            command = CK_Left;
            break;
        case 'U':
            command = CK_Up;
            break;
        case 'D':
            command = CK_Down;
            break;
        case 'E':
            command = CK_End;
            break;
        case 'N':
            command = CK_Enter;
            break;
        case 'B':
            command = CK_BackSpace;
            break;
        case 'X':
            command = CK_Delete;
            break;
        default:
            c = (unsigned char) *keys;
            break;
        }

        edit_execute_cmd (test_edit, command, c);
    }
}

/* --------------------------------------------------------------------------------------------- */

static char *
test_get_text (void)
{
    GString *text;

    text = g_string_sized_new (test_edit->buffer.size);
    for (off_t i = 0; i < test_edit->buffer.size; i++)
        g_string_append_c (text, (gchar) edit_buffer_get_byte (&test_edit->buffer, i));

    return g_string_free (text, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static const struct test_crlf_ds
{
    const char *input_text;
    const char *keys;
    const char *expected_text;
} test_crlf_ds[] = {
    {
        // 0. Lines added at the end of the file get CR LF
        "ab\r\ncd\r\n",
        "DDxNyN",
        "ab\r\ncd\r\nx\r\ny\r\n",
    },
    {
        // 1. Enter on a last line without a line end follows the previous line
        "ab\r\ncd",
        "DENz",
        "ab\r\ncd\r\nz",
    },
    {
        // 2. End stops before the CR; Enter keeps CR LF
        "ab\r\ncd\r\n",
        "ExNy",
        "abx\r\ny\r\ncd\r\n",
    },
    {
        // 3. Right past the end of the line goes on to the next line
        "ab\r\ncd\r\n",
        "RRRx",
        "ab\r\nxcd\r\n",
    },
    {
        // 4. Down from a longer line lands before the CR
        "abcdef\r\nab\r\n",
        "EDx",
        "abcdef\r\nabx\r\n",
    },
    {
        // 5. Up from a longer line lands before the CR
        "ab\r\nabcdef\r\n",
        "DEUx",
        "abx\r\nabcdef\r\n",
    },
    {
        // 6. Left from the start of a line lands before the CR
        "ab\r\ncd\r\n",
        "DLx",
        "abx\r\ncd\r\n",
    },
    {
        // 7. Backspace at the start of a line removes CR LF
        "ab\r\ncd\r\n",
        "DB",
        "abcd\r\n",
    },
    {
        // 8. Delete at the end of a line removes CR LF
        "ab\r\ncd\r\n",
        "EX",
        "abcd\r\n",
    },
    {
        // 9. Files with LF line ends are not affected
        "ab\ncd\n",
        "DDxN",
        "ab\ncd\nx\n",
    },
    {
        // 10. A lone CR at the end of the file is not a line end
        "ab\r",
        "Ex",
        "ab\rx",
    },
};

/* @Test(dataSource = "test_crlf_ds") */
START_PARAMETRIZED_TEST (test_crlf, test_crlf_ds)
{
    char *actual_text;

    // given
    test_load_text (data->input_text);

    // when
    test_press_keys (data->keys);

    // then
    actual_text = test_get_text ();
    mctest_assert_str_eq (actual_text, data->expected_text);
    g_free (actual_text);
}
END_PARAMETRIZED_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_checked_fixture (tc_core, setup, teardown);

    // Add new tests here: ***************
    mctest_add_parameterized_test (tc_core, test_crlf, test_crlf_ds);
    // ***********************************

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
