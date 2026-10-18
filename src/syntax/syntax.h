/** \file
 *  \brief Header: syntax highlighting rules shared by the editor and the viewer
 */

#ifndef MC__SYNTAX_H
#define MC__SYNTAX_H

/*** typedefs(not structures) and defined constants **********************************************/

#define SYNTAX_NEST_MAX 8

/**
 * Byte source of the highlighted text.  Must return '\n' for byte_index < 0 and for
 * byte_index past the end: the rules look one byte back and up to the end of the line forward.
 */
typedef int (*syntax_get_byte_fn) (const void *data, off_t byte_index);

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct syntax_rule_t syntax_rule_t;
struct syntax_rule_t
{
    unsigned short keyword;
    off_t end;
    unsigned short context;
    unsigned short _context;
    unsigned char border;
    // "nested" regions entered so far: the context and its "nested" keyword to return to
    unsigned char nest_len;
    // the current keyword is the closing delimiter of a nested region
    unsigned char nest_closing;
    unsigned short nest_context[SYNTAX_NEST_MAX];
    unsigned short nest_keyword[SYNTAX_NEST_MAX];
    // unbalanced opening characters seen inside each nested region
    unsigned char nest_depth[SYNTAX_NEST_MAX];
};

/* rules of one syntax applied to one byte source */
typedef struct
{
    // set by the owner before loading
    syntax_get_byte_fn get_byte;
    const void *data;
    int normal_color;        // color of text no rule sets a color for
    const char *skin_group;  // skin section whose _default_ fills colors a rule leaves unset
    /* The text never changes, so every saved state stays good: keep them all, one per
       checkpoint_density bytes, and jump to any offset from the nearest one. */
    gboolean fixed;
    off_t checkpoint_density;
    /* With fixed text, never walk more than this many bytes to an offset: start afresh at
       a line start instead, at the price of a wrong color where a long comment or string
       began before it.  0 means always walk from the start of the text. */
    off_t sync_window;

    GPtrArray *rules;
    syntax_rule_t rule;  // state at last_get_rule
    off_t last_get_rule;
    GSList *marker;                // saved states, last first
    GArray *checkpoints;           // saved states of fixed text, by offset
    gboolean guessed;              // the state at last_get_rule comes from a sync point
    GTree *defines;                // List of defines
    gboolean is_case_insensitive;  // selects language case sensitivity
} syntax_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

int syntax_load (syntax_t *syn, GPtrArray *pnames, const char *syntax_file, const char *file_name,
                 const char *first_line, const char *type, char **type_found, char **error_file);
void syntax_free (syntax_t *syn);
int syntax_get_color (syntax_t *syn, off_t byte_index);

/*** inline functions ****************************************************************************/

#endif
