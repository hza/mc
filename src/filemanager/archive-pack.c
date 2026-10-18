/*
   Pack files into an archive with libarchive

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

/** \file src/filemanager/archive-pack.c
 *  \brief Source: pack files into an archive with libarchive
 *
 *  Sources are read and the archive is written through the mc VFS, so both
 *  can be on any panel. Every byte of the sources goes through
 *  archive_write_data() here, which drives the usual file operation progress
 *  dialog: the current file and the total, counted in uncompressed bytes.
 *  The archive is written to "name.part" and renamed when it is complete.
 */

#include <config.h>

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <setjmp.h>

#ifdef HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#include "lib/global.h"
#include "lib/util.h"
#include "lib/vfs/vfs.h"
#include "lib/widget.h"

#include "src/background.h"  // do_background()

#include "file.h"     // file_error(), erase_dir()
#include "filegui.h"  // file_op_context_t, file_progress_*()

#include "archive-pack.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define PACK_BLOCK (64 * 1024)

/*** file scope type declarations ****************************************************************/

#ifdef HAVE_LIBARCHIVE
typedef struct
{
    file_op_context_t *ctx;
    struct archive *a;
    vfs_path_t *target_vpath;     // shown as the target in the progress dialog
    const vfs_path_t *out_vpath;  // the archive being written, never packed into itself
    char *buf;
    gint64 tv_last;
} pack_t;

/** Where libarchive's output goes */
typedef struct
{
    vfs_path_t *vpath;
    int fd;
} pack_out_t;
#endif

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

#ifdef HAVE_LIBARCHIVE

static int
pack_out_open (struct archive *a, void *data)
{
    pack_out_t *out = (pack_out_t *) data;

    out->fd = mc_open (out->vpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out->fd == -1)
    {
        archive_set_error (a, errno, "%s", unix_error_string (errno));
        return ARCHIVE_FATAL;
    }
    return ARCHIVE_OK;
}

/* --------------------------------------------------------------------------------------------- */

static la_ssize_t
pack_out_write (struct archive *a, void *data, const void *buf, size_t len)
{
    pack_out_t *out = (pack_out_t *) data;
    const char *p = (const char *) buf;
    size_t left = len;

    while (left > 0)
    {
        ssize_t n;

        n = mc_write (out->fd, p, left);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            archive_set_error (a, errno, "%s", unix_error_string (errno));
            return -1;
        }
        p += n;
        left -= (size_t) n;
    }

    return (la_ssize_t) len;
}

/* --------------------------------------------------------------------------------------------- */

static int
pack_out_close (struct archive *a, void *data)
{
    pack_out_t *out = (pack_out_t *) data;

    if (out->fd != -1 && mc_close (out->fd) != 0)
    {
        out->fd = -1;
        archive_set_error (a, errno, "%s", unix_error_string (errno));
        return ARCHIVE_FATAL;
    }
    out->fd = -1;
    return ARCHIVE_OK;
}

/* --------------------------------------------------------------------------------------------- */
/** The level a filter or format uses for our 0..9 / ultra scale */

static int
pack_level (const char *filter, int level)
{
    if (strcmp (filter, "zstd") == 0)
        return level >= ARCHIVE_LEVEL_ULTRA ? 22 : level == 9 ? 19 : MAX (level, 1);
    if (strcmp (filter, "bzip2") == 0)
        return CLAMP (level, 1, 9);
    return MIN (level, 9);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
pack_set_option (struct archive *a, const char *module, const char *option, const char *value)
{
    return archive_write_set_option (a, module, option, value) == ARCHIVE_OK;
}

/* --------------------------------------------------------------------------------------------- */
/** Set up format, filter, level and encryption. FALSE with the libarchive error on failure. */

static gboolean
pack_setup (struct archive *a, const archive_pack_opts_t *o)
{
    const char *filter = NULL;
    char level[8];
    int r = ARCHIVE_OK;

    if (strcmp (o->format, "zip") == 0)
    {
        r = archive_write_set_format_zip (a);
        if (r == ARCHIVE_OK)
        {
            if (o->level == 0)
                r = pack_set_option (a, "zip", "compression", "store") ? ARCHIVE_OK
                                                                       : ARCHIVE_FAILED;
            else
            {
                g_snprintf (level, sizeof (level), "%d", pack_level ("zip", o->level));
                r = pack_set_option (a, "zip", "compression-level", level) ? ARCHIVE_OK
                                                                           : ARCHIVE_FAILED;
            }
        }
        if (r == ARCHIVE_OK && o->password != NULL)
        {
            r = archive_write_set_passphrase (a, o->password);
            if (r == ARCHIVE_OK
                && !pack_set_option (a, "zip", "encryption", o->aes ? "aes256" : "zipcrypt"))
                r = ARCHIVE_FAILED;
        }
        return r == ARCHIVE_OK;
    }

    if (strcmp (o->format, "7z") == 0)
    {
        r = archive_write_set_format_7zip (a);
        if (r == ARCHIVE_OK)
        {
            if (o->level == 0)
                r = pack_set_option (a, "7zip", "compression", "store") ? ARCHIVE_OK
                                                                        : ARCHIVE_FAILED;
            else
            {
                g_snprintf (level, sizeof (level), "%d", pack_level ("7zip", o->level));
                r = pack_set_option (a, "7zip", "compression-level", level) ? ARCHIVE_OK
                                                                            : ARCHIVE_FAILED;
            }
        }
        return r == ARCHIVE_OK;
    }

    // tar, with or without a compression filter
    r = archive_write_set_format_pax_restricted (a);
    if (r != ARCHIVE_OK)
        return FALSE;

    if (strcmp (o->format, "tar.gz") == 0)
    {
        filter = "gzip";
        r = archive_write_add_filter_gzip (a);
    }
    else if (strcmp (o->format, "tar.bz2") == 0)
    {
        filter = "bzip2";
        r = archive_write_add_filter_bzip2 (a);
    }
    else if (strcmp (o->format, "tar.xz") == 0)
    {
        filter = "xz";
        r = archive_write_add_filter_xz (a);
    }
    else if (strcmp (o->format, "tar.zst") == 0)
    {
        filter = "zstd";
        r = archive_write_add_filter_zstd (a);
    }

    if (r != ARCHIVE_OK)
        return FALSE;

    if (filter != NULL)
    {
        g_snprintf (level, sizeof (level), "%d", pack_level (filter, o->level));
        if (!pack_set_option (a, filter, "compression-level", level))
            return FALSE;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/** Count the files and the bytes of regular files under @vpath */

static void
pack_count (const vfs_path_t *vpath, size_t *count, uintmax_t *bytes)
{
    struct stat st;

    if (mc_lstat (vpath, &st) != 0)
        return;

    (*count)++;

    if (S_ISREG (st.st_mode))
        *bytes += (uintmax_t) st.st_size;
    else if (S_ISDIR (st.st_mode))
    {
        DIR *dir;
        struct vfs_dirent *de;

        dir = mc_opendir (vpath);
        if (dir == NULL)
            return;

        while ((de = mc_readdir (dir)) != NULL)
        {
            vfs_path_t *sub;

            if (DIR_IS_DOT (de->d_name) || DIR_IS_DOTDOT (de->d_name))
                continue;
            sub = vfs_path_append_new (vpath, de->d_name, (char *) NULL);
            pack_count (sub, count, bytes);
            vfs_path_free (sub, TRUE);
        }
        mc_closedir (dir);
    }
}

/* --------------------------------------------------------------------------------------------- */
/** Show where we are: the current file at @done of @size, and the total */

static FileProgressStatus
pack_progress (pack_t *p, off_t done, off_t size, gboolean force)
{
    file_op_context_t *ctx = p->ctx;
    const gint64 now = g_get_monotonic_time ();

    if (!force && now - p->tv_last < 100 * 1000)
        return FILE_CONT;
    p->tv_last = now;

    {
        const double dt = (now - ctx->pauses - ctx->transfer_start) / (double) G_USEC_PER_SEC;
        const double tdt =
            (now - ctx->pauses - ctx->total_transfer_start) / (double) G_USEC_PER_SEC;
        const uintmax_t total_done = ctx->total_progress_bytes + (uintmax_t) done;

        ctx->bps = dt > 0 ? (long) (done / dt) : 0;
        ctx->eta_secs = done > 0 && dt > 0 ? ((double) size / done - 1) * dt : 0.0;
        ctx->total_bps = tdt > 0 ? (long) (total_done / tdt) : 0;
        ctx->total_eta_secs =
            total_done > 0 && tdt > 0 ? ((double) ctx->total_bytes / total_done - 1) * tdt : 0.0;

        file_progress_show (ctx, done, size, "", force);
        file_progress_show_count (ctx);
        file_progress_show_total (ctx, total_done, now, force);
    }

    return file_progress_check_buttons (ctx);
}

/* --------------------------------------------------------------------------------------------- */
/** Write the data of a regular file */

static FileProgressStatus
pack_data (pack_t *p, const vfs_path_t *vpath, off_t size)
{
    FileProgressStatus status = FILE_CONT;
    off_t done = 0;
    int fd;

    while ((fd = mc_open (vpath, O_RDONLY)) == -1)
    {
        status = file_error (p->ctx, TRUE, _ ("Cannot open source file \"%s\"\n%s"),
                             vfs_path_as_str (vpath));
        if (status != FILE_RETRY)
            return status;
    }

    while (TRUE)
    {
        ssize_t n;

        n = mc_read (fd, p->buf, PACK_BLOCK);
        if (n == 0)
            break;
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            status = file_error (p->ctx, FALSE, _ ("Cannot read source file \"%s\"\n%s"),
                                 vfs_path_as_str (vpath));
            break;
        }

        if (archive_write_data (p->a, p->buf, (size_t) n) < 0)
        {
            message (D_ERROR, MSG_ERROR, _ ("Cannot write archive:\n%s"),
                     archive_error_string (p->a));
            status = FILE_ABORT;
            break;
        }

        done += n;
        status = pack_progress (p, done, size, FALSE);
        if (status == FILE_ABORT)
            break;
        status = FILE_CONT;
    }

    mc_close (fd);
    if (status == FILE_CONT)
        pack_progress (p, done, size, TRUE);
    return status;
}

/* --------------------------------------------------------------------------------------------- */
/** Add @vpath to the archive as @name, recursing into directories */

static FileProgressStatus
pack_add (pack_t *p, const vfs_path_t *vpath, const char *name)
{
    file_op_context_t *ctx = p->ctx;
    FileProgressStatus status = FILE_CONT;
    struct archive_entry *entry;
    struct stat st;

    if (vfs_path_equal (vpath, p->out_vpath))
        return FILE_CONT;

    while (mc_lstat (vpath, &st) != 0)
    {
        status = file_error (ctx, TRUE, _ ("Cannot stat source file \"%s\"\n%s"),
                             vfs_path_as_str (vpath));
        if (status != FILE_RETRY)
            return status == FILE_SKIP ? FILE_CONT : status;
    }

    // devices, fifos and sockets are not packed
    if (!S_ISREG (st.st_mode) && !S_ISDIR (st.st_mode) && !S_ISLNK (st.st_mode))
    {
        ctx->total_progress_count++;
        return FILE_CONT;
    }

    file_progress_show_source (ctx, vpath);
    file_progress_show_target (ctx, p->target_vpath);
    ctx->transfer_start = g_get_monotonic_time ();

    entry = archive_entry_new ();
    archive_entry_copy_stat (entry, &st);
    archive_entry_set_pathname_utf8 (entry, name);

    if (S_ISLNK (st.st_mode))
    {
        char link[MC_MAXPATHLEN];
        int len;

        len = mc_readlink (vpath, link, sizeof (link) - 1);
        if (len < 0)
            len = 0;
        link[len] = '\0';
        archive_entry_set_symlink_utf8 (entry, link);
        archive_entry_set_size (entry, 0);
    }
    else if (!S_ISREG (st.st_mode))
        archive_entry_set_size (entry, 0);

    if (archive_write_header (p->a, entry) != ARCHIVE_OK)
    {
        const int err = archive_errno (p->a);

        archive_entry_free (entry);
        // a warning (for example a long name) still writes the entry
        if (err != 0 && archive_write_header (p->a, NULL) == ARCHIVE_FATAL)
        {
            message (D_ERROR, MSG_ERROR, _ ("Cannot write archive:\n%s"),
                     archive_error_string (p->a));
            return FILE_ABORT;
        }
        entry = NULL;
    }
    if (entry != NULL)
        archive_entry_free (entry);

    if (S_ISREG (st.st_mode))
    {
        status = pack_data (p, vpath, st.st_size);
        ctx->total_progress_bytes += (uintmax_t) st.st_size;
    }
    ctx->total_progress_count++;

    if (status == FILE_CONT && S_ISDIR (st.st_mode))
    {
        DIR *dir;
        struct vfs_dirent *de;

        dir = mc_opendir (vpath);
        if (dir == NULL)
            return file_error (ctx, FALSE, _ ("Cannot open directory \"%s\"\n%s"),
                               vfs_path_as_str (vpath))
                    == FILE_ABORT
                ? FILE_ABORT
                : FILE_CONT;

        while (status != FILE_ABORT && (de = mc_readdir (dir)) != NULL)
        {
            vfs_path_t *sub;
            char *sub_name;

            if (DIR_IS_DOT (de->d_name) || DIR_IS_DOTDOT (de->d_name))
                continue;

            sub = vfs_path_append_new (vpath, de->d_name, (char *) NULL);
            sub_name = g_strconcat (name, PATH_SEP_STR, de->d_name, (char *) NULL);
            status = pack_add (p, sub, sub_name);
            g_free (sub_name);
            vfs_path_free (sub, TRUE);
        }
        mc_closedir (dir);
    }

    return status == FILE_SKIP ? FILE_CONT : status;
}

/* --------------------------------------------------------------------------------------------- */
/** Delete the sources after they were packed */

static void
pack_erase_sources (file_op_context_t *ctx, const vfs_path_t *src_dir, GPtrArray *names)
{
    guint i;

    ctx->recursive_result = RECURSIVE_ALWAYS;

    for (i = 0; i < names->len; i++)
    {
        vfs_path_t *vpath;
        struct stat st;

        vpath = vfs_path_append_new (src_dir, g_ptr_array_index (names, i), (char *) NULL);
        if (mc_lstat (vpath, &st) == 0)
        {
            FileProgressStatus status = FILE_CONT;

            if (S_ISDIR (st.st_mode))
                status = erase_dir (ctx, vpath);
            else
                while (mc_unlink (vpath) != 0
                       && (status = file_error (ctx, TRUE, _ ("Cannot delete file \"%s\"\n%s"),
                                                vfs_path_as_str (vpath)))
                           == FILE_RETRY)
                    ;
            if (status == FILE_ABORT)
            {
                vfs_path_free (vpath, TRUE);
                break;
            }
        }
        vfs_path_free (vpath, TRUE);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
pack_do (file_op_context_t *ctx, archive_pack_opts_t *o, const vfs_path_t *src_dir,
         GPtrArray *names)
{
    pack_t p;
    pack_out_t out;
    vfs_path_t *final_vpath;
    char *part;
    FileProgressStatus status = FILE_CONT;
    gboolean ok;
    guint i;

    final_vpath = vfs_path_from_str (o->archive_path);
    part = g_strconcat (o->archive_path, ".part", (char *) NULL);
    out.vpath = vfs_path_from_str (part);
    out.fd = -1;

    memset (&p, 0, sizeof (p));
    p.ctx = ctx;
    p.target_vpath = final_vpath;
    p.out_vpath = out.vpath;
    p.buf = g_malloc (PACK_BLOCK);
    p.a = archive_write_new ();

    ok = pack_setup (p.a, o);
    if (!ok)
        message (D_ERROR, MSG_ERROR, _ ("Cannot create %s archive:\n%s"), o->format,
                 archive_error_string (p.a) != NULL ? archive_error_string (p.a) : "");
    else if (archive_write_open2 (p.a, &out, pack_out_open, pack_out_write, pack_out_close, NULL)
             != ARCHIVE_OK)
    {
        message (D_ERROR, MSG_ERROR, _ ("Cannot create archive \"%s\":\n%s"), part,
                 archive_error_string (p.a));
        ok = FALSE;
    }

    for (i = 0; ok && status != FILE_ABORT && i < names->len; i++)
    {
        const char *name = g_ptr_array_index (names, i);
        vfs_path_t *vpath;

        vpath = vfs_path_append_new (src_dir, name, (char *) NULL);
        status = pack_add (&p, vpath, name);
        vfs_path_free (vpath, TRUE);
    }

    if (ok && archive_write_close (p.a) != ARCHIVE_OK && status != FILE_ABORT)
    {
        message (D_ERROR, MSG_ERROR, _ ("Cannot write archive:\n%s"), archive_error_string (p.a));
        ok = FALSE;
    }
    archive_write_free (p.a);

    ok = ok && status != FILE_ABORT;

    if (ok && mc_rename (out.vpath, final_vpath) != 0)
    {
        message (D_ERROR, MSG_ERROR, _ ("Cannot rename \"%s\" to \"%s\":\n%s"), part,
                 o->archive_path, unix_error_string (errno));
        ok = FALSE;
    }
    if (!ok)
        mc_unlink (out.vpath);

    if (ok && o->move)
        pack_erase_sources (ctx, src_dir, names);

    g_free (p.buf);
    g_free (part);
    vfs_path_free (out.vpath, TRUE);
    vfs_path_free (final_vpath, TRUE);

    return ok;
}

#endif /* HAVE_LIBARCHIVE */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
archive_pack_opts_free (archive_pack_opts_t *opts)
{
    g_free (opts->archive_path);
    if (opts->password != NULL)
    {
        memset (opts->password, 0, strlen (opts->password));
        g_free (opts->password);
    }
    memset (opts, 0, sizeof (*opts));
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Pack @names (relative to @src_dir) as @opts says, with the progress dialog or in the background.
 * @return TRUE when the archive was written (always FALSE in the parent of a background job)
 */

gboolean
archive_pack_run (archive_pack_opts_t *opts, const vfs_path_t *src_dir, GPtrArray *names,
                  gboolean background)
{
#ifdef HAVE_LIBARCHIVE
    file_op_context_t *ctx;
    gboolean ok;
    guint i;
#ifdef ENABLE_BACKGROUND
    jmp_buf bg_jump;
#endif

    ctx = file_op_context_new (OP_COPY);
    ctx->total_transfer_start = g_get_monotonic_time ();

#ifdef ENABLE_BACKGROUND
    if (background)
    {
        int v;

        v = do_background (
            ctx, g_strconcat (_ ("Create archive"), ": ", opts->archive_path, (char *) NULL));
        if (v == -1)
            message (D_ERROR, MSG_ERROR, _ ("Sorry, I could not put the job in background"));
        if (v == 1)
            return FALSE;  // the parent; the child packs
    }
#else
    (void) background;
#endif

    for (i = 0; i < names->len; i++)
    {
        vfs_path_t *vpath;

        vpath = vfs_path_append_new (src_dir, g_ptr_array_index (names, i), (char *) NULL);
        pack_count (vpath, &ctx->total_count, &ctx->total_bytes);
        vfs_path_free (vpath, TRUE);
    }
    ctx->totals_computed = TRUE;
    // a file cannot be left out of the archive once its data has started
    ctx->no_skip = TRUE;

#ifdef ENABLE_BACKGROUND
    if (!mc_global.we_are_background)
    {
        if (setjmp (bg_jump) != 0)
        {
            // the Background button: the child packs on with our VFS connections
            vfs_path_t *vpath;

            mc_setctl (src_dir, VFS_SETCTL_FORGET, NULL);
            vpath = vfs_path_from_str (opts->archive_path);
            mc_setctl (vpath, VFS_SETCTL_FORGET, NULL);
            vfs_path_free (vpath, TRUE);
            return FALSE;  // the context stays with the job
        }
        ctx->bg_jump = &bg_jump;
    }
#endif

    if (!mc_global.we_are_background)
    {
        file_progress_ui_create (ctx, TRUE, FILEGUI_DIALOG_MULTI_ITEM);
        file_progress_ui_set_title (ctx, _ ("Pack"));
    }

    ok = pack_do (ctx, opts, src_dir, names);
#ifdef ENABLE_BACKGROUND
    ctx->bg_jump = NULL;
#endif

#ifdef ENABLE_BACKGROUND
    if (mc_global.we_are_background)
        file_op_background_done (ctx);
#endif

    file_op_context_destroy (ctx);
    return ok;
#else
    (void) opts;
    (void) src_dir;
    (void) names;
    (void) background;

    message (D_ERROR, MSG_ERROR,
             _ ("mc was built without libarchive, archives cannot be created."));
    return FALSE;
#endif
}

/* --------------------------------------------------------------------------------------------- */
