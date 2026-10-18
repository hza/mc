/*
   lib - replacing a file in one step

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

#define TEST_SUITE_NAME "/lib/util"

#include "tests/mctest.h"

#include <sys/stat.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "lib/util.h"

static char *dir;

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void
setup (void)
{
    dir = g_dir_make_tmp ("mc-replace-XXXXXX", NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* @After */
static void
teardown (void)
{
    GDir *d;
    const char *name;

    d = g_dir_open (dir, 0, NULL);
    while ((name = g_dir_read_name (d)) != NULL)
    {
        char *path = g_build_filename (dir, name, (char *) NULL);

        g_unlink (path);
        g_free (path);
    }
    g_dir_close (d);
    g_rmdir (dir);
    g_free (dir);
}

/* --------------------------------------------------------------------------------------------- */

static char *
path_in_dir (const char *name)
{
    return g_build_filename (dir, name, (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

static char *
contents (const char *path)
{
    char *s = NULL;

    g_file_get_contents (path, &s, NULL, NULL);
    return s;
}

/* --------------------------------------------------------------------------------------------- */

static int
files_in_dir (void)
{
    GDir *d;
    int n = 0;

    d = g_dir_open (dir, 0, NULL);
    while (g_dir_read_name (d) != NULL)
        n++;
    g_dir_close (d);

    return n;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
replace (const char *path, const char *text, gboolean ok)
{
    char *tmp_path;
    FILE *f;

    f = mc_util_replace_begin (path, &tmp_path);
    mctest_assert_not_null (f);
    fputs (text, f);

    return mc_util_replace_end (f, tmp_path, path, ok);
}

/* --------------------------------------------------------------------------------------------- */

/* a file that does not exist yet is created, with the permissions the umask leaves */
START_TEST (test_new_file)
{
    char *path, *s;
    struct stat st;
    mode_t mask;

    // given
    path = path_in_dir ("new");

    // when
    mask = umask (022);
    mctest_assert_true (replace (path, "one\n", TRUE));
    umask (mask);

    // then
    s = contents (path);
    mctest_assert_str_eq (s, "one\n");
    stat (path, &st);
    ck_assert_int_eq (st.st_mode & 07777, 0644);
    ck_assert_int_eq (files_in_dir (), 1);

    g_free (s);
    g_free (path);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* the new contents keep the permissions of the old file */
START_TEST (test_permissions)
{
    char *path, *s;
    struct stat st;

    // given
    path = path_in_dir ("file");
    g_file_set_contents (path, "old\n", -1, NULL);
    chmod (path, 0640);

    // when
    mctest_assert_true (replace (path, "new\n", TRUE));

    // then
    s = contents (path);
    mctest_assert_str_eq (s, "new\n");
    stat (path, &st);
    ck_assert_int_eq (st.st_mode & 07777, 0640);
    ck_assert_int_eq (files_in_dir (), 1);

    g_free (s);
    g_free (path);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* a symbolic link stays, and the file it points to gets the new contents */
START_TEST (test_symlink)
{
    char *target, *link, *s;
    struct stat st;

    // given
    target = path_in_dir ("target");
    link = path_in_dir ("link");
    g_file_set_contents (target, "old\n", -1, NULL);
    mctest_assert_true (symlink (target, link) == 0);

    // when
    mctest_assert_true (replace (link, "new\n", TRUE));

    // then
    mctest_assert_true (lstat (link, &st) == 0 && S_ISLNK (st.st_mode));
    s = contents (target);
    mctest_assert_str_eq (s, "new\n");
    ck_assert_int_eq (files_in_dir (), 2);

    g_free (s);
    g_free (link);
    g_free (target);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* on failure the old file stays as it was, and nothing is left behind */
START_TEST (test_failure)
{
    char *path, *s;

    // given
    path = path_in_dir ("file");
    g_file_set_contents (path, "old\n", -1, NULL);

    // when
    mctest_assert_false (replace (path, "new\n", FALSE));

    // then
    s = contents (path);
    mctest_assert_str_eq (s, "old\n");
    ck_assert_int_eq (files_in_dir (), 1);

    g_free (s);
    g_free (path);
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
    tcase_add_test (tc_core, test_new_file);
    tcase_add_test (tc_core, test_permissions);
    tcase_add_test (tc_core, test_symlink);
    tcase_add_test (tc_core, test_failure);
    // ***********************************

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
