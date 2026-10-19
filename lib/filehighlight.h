#ifndef MC__FILEHIGHLIGHT_H
#define MC__FILEHIGHLIGHT_H

#include "lib/mcconfig.h"  // mc_config_t
#include "lib/file-entry.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct mc_fhl_struct
{
    mc_config_t *config;
    GPtrArray *filters;
    gchar *vivid_theme_path;  // theme file the colors come from, NULL when using the ini file
} mc_fhl_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

mc_fhl_t *mc_fhl_new (gboolean need_auto_fill);
void mc_fhl_free (mc_fhl_t **fhl);

// vivid themes: names of the themes found, and the file of one theme (NULL when not found)
GPtrArray *mc_fhl_vivid_list_themes (void);
gchar *mc_fhl_vivid_theme_path (const gchar *theme_name);

int mc_fhl_get_color (const mc_fhl_t *fhl, const file_entry_t *fe);

gboolean mc_fhl_read_ini_file (mc_fhl_t *fhl, const gchar *filename);
gboolean mc_fhl_parse_ini_file (mc_fhl_t *fhl);
void mc_fhl_clear (mc_fhl_t *fhl);

/*** inline functions ****************************************************************************/

#endif
