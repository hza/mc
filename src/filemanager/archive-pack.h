/** \file archive-pack.h
 *  \brief Header: pack files into an archive with libarchive
 */

#ifndef MC__ARCHIVE_PACK_H
#define MC__ARCHIVE_PACK_H

#include "lib/global.h"
#include "lib/vfs/vfs.h"

/*** typedefs(not structures) and defined constants **********************************************/

#define ARCHIVE_LEVEL_ULTRA 10  // the strongest level each format has

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct
{
    char *archive_path;  // destination, may be on any VFS
    const char *format;  // "zip", "7z", "tar", "tar.gz", "tar.bz2", "tar.xz", "tar.zst"
    int level;           // 0 store, 1 fastest ... 9 maximum, ARCHIVE_LEVEL_ULTRA
    gboolean move;       // delete the sources after the archive is written
    char *password;      // NULL: no encryption (zip only)
    gboolean aes;        // AES-256, else ZipCrypto
} archive_pack_opts_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

void archive_pack_opts_free (archive_pack_opts_t *opts);
gboolean archive_pack_run (archive_pack_opts_t *opts, const vfs_path_t *src_dir, GPtrArray *names,
                           gboolean background);

/*** inline functions ****************************************************************************/

#endif
