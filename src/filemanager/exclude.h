/** \file exclude.h
 *  \brief Header: hide panel entries matching patterns from the exclude file
 */

#ifndef MC__EXCLUDE_H
#define MC__EXCLUDE_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

void exclude_load (void);
void exclude_free (void);
gboolean exclude_match (const char *dir, const char *name, size_t len);

/*** inline functions ****************************************************************************/

#endif
