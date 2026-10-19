#ifndef MC__FILEHIGHLIGHT_INTERNAL_H
#define MC__FILEHIGHLIGHT_INTERNAL_H

#include "lib/search.h"  // mc_search_t

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

typedef enum
{
    MC_FLHGH_T_FTYPE,
    MC_FLHGH_T_EXT,
    MC_FLHGH_T_FREGEXP,
    MC_FLHGH_T_SUFFIX  // literal "*.ext" style entries of a vivid database, longest suffix wins
} mc_flhgh_filter_type;

typedef enum
{
    MC_FLHGH_FTYPE_T_FILE,
    MC_FLHGH_FTYPE_T_FILE_EXE,
    MC_FLHGH_FTYPE_T_DIR,
    MC_FLHGH_FTYPE_T_LINK_DIR,
    MC_FLHGH_FTYPE_T_LINK,
    MC_FLHGH_FTYPE_T_HARDLINK,
    MC_FLHGH_FTYPE_T_SYMLINK,
    MC_FLHGH_FTYPE_T_STALE_LINK,
    MC_FLHGH_FTYPE_T_DEVICE,
    MC_FLHGH_FTYPE_T_DEVICE_BLOCK,
    MC_FLHGH_FTYPE_T_DEVICE_CHAR,
    MC_FLHGH_FTYPE_T_SPECIAL,
    MC_FLHGH_FTYPE_T_SPECIAL_SOCKET,
    MC_FLHGH_FTYPE_T_SPECIAL_FIFO,
    MC_FLHGH_FTYPE_T_SPECIAL_DOOR,
} mc_flhgh_ftype_type;

// Longest vivid suffix that is matched by the hash table; longer ones are dropped
#define VIVID_SUFFIX_KEY_MAX 200

typedef enum
{
    MC_FHL_YAML_SCALAR,
    MC_FHL_YAML_MAP,
    MC_FHL_YAML_SEQ
} mc_fhl_yaml_kind_t;

/*** structures declarations (and typedefs of structures)*****************************************/

// Tree of the YAML subset used by vivid. Scalars keep their text, maps keep parallel key/item
// arrays, sequences keep only items.
typedef struct mc_fhl_yaml_node_struct
{
    mc_fhl_yaml_kind_t kind;
    gchar *scalar;
    GPtrArray *keys;   // gchar *, maps only
    GPtrArray *items;  // mc_fhl_yaml_node_t *
} mc_fhl_yaml_node_t;

typedef struct mc_fhl_filter_struct
{

    int color_pair_index;
    gchar *fgcolor;
    gchar *bgcolor;
    mc_flhgh_filter_type type;
    mc_search_t *search_condition;
    mc_flhgh_ftype_type file_type;
    GHashTable *suffixes;  // MC_FLHGH_T_SUFFIX only: ".ext" -> gint color pair index
    gsize suffix_max_len;

} mc_fhl_filter_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

void mc_fhl_filter_free (gpointer data);
void mc_fhl_array_free (mc_fhl_t *fhl);

gboolean mc_fhl_init_from_standard_files (mc_fhl_t *fhl);

mc_fhl_yaml_node_t *mc_fhl_yaml_parse (const gchar *text, gchar **error);
void mc_fhl_yaml_free (mc_fhl_yaml_node_t *node);
const mc_fhl_yaml_node_t *mc_fhl_yaml_get (const mc_fhl_yaml_node_t *map, const gchar *key);

gboolean mc_fhl_vivid_load (mc_fhl_t *fhl, const gchar *theme_name);

/*** inline functions ****************************************************************************/

#endif
