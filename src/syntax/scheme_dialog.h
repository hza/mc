/** \file
 *  \brief Header: the color scheme chooser of the editor and the viewer
 */

#ifndef MC__SYNTAX_SCHEME_DIALOG_H
#define MC__SYNTAX_SCHEME_DIALOG_H

/*** typedefs(not structures) and defined constants **********************************************/

/* repaint what is colored with the scheme, which has just changed */
typedef void (*syntax_scheme_apply_fn) (void *data);

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

void syntax_scheme_dialog (syntax_scheme_apply_fn apply, void *data);

/*** inline functions ****************************************************************************/

#endif
