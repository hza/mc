/*
   Syntax highlighting rules shared by the editor and the viewer.

   Copyright (C) 1996-2026
   Free Software Foundation, Inc.

   Written by:
   Paul Sheer, 1998
   Leonard den Ottolander <leonard den ottolander nl>, 2005, 2006
   Egmont Koblinger <egmont@gmail.com>, 2010
   Slava Zanko <slavazanko@gmail.com>, 2013
   Andrew Borodin <aborodin@vmail.ru>, 2013, 2014, 2021

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

/** \file
 *  \brief Source: syntax highlighting rules
 *  \author Paul Sheer
 *  \date 1996, 1997
 *  \author Mikhail Pobolovets
 *  \date 2010
 *
 *  Reads a rule set from the syntax file and tells the color of any byte of a text it gets
 *  from a callback, so the editor and the viewer color the same way.
 */

#include <config.h>

#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "lib/global.h"
#include "lib/search.h"  // search engine
#include "lib/skin.h"
#include "lib/fileloc.h"  // EDIT_SYNTAX_DIR, EDIT_SYNTAX_FILE
#include "lib/util.h"
#include "lib/mcconfig.h"
#include "lib/tty/color.h"

#include "syntax.h"
#include "syntax_scheme.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define SYNTAX_MARKER_DENSITY 512
// include files may include other files up to this depth
#define SYNTAX_INCLUDE_MAX   4

#define RULE_ON_LEFT_BORDER  1
#define RULE_ON_RIGHT_BORDER 2

#define SYNTAX_TOKEN_STAR    '\001'
#define SYNTAX_TOKEN_PLUS    '\002'
#define SYNTAX_TOKEN_BRACKET '\003'
#define SYNTAX_TOKEN_BRACE   '\004'

#define break_a                                                                                    \
    {                                                                                              \
        result = line;                                                                             \
        break;                                                                                     \
    }
#define check_a                                                                                    \
    {                                                                                              \
        if (*a == NULL)                                                                            \
        {                                                                                          \
            result = line;                                                                         \
            break;                                                                                 \
        }                                                                                          \
    }
#define check_not_a                                                                                \
    {                                                                                              \
        if (*a != NULL)                                                                            \
        {                                                                                          \
            result = line;                                                                         \
            break;                                                                                 \
        }                                                                                          \
    }

#define SYNTAX_KEYWORD(x) ((syntax_keyword_t *) (x))
#define CONTEXT_RULE(x)   ((context_rule_t *) (x))

#define ARGS_LEN          1024

/*** file scope type declarations ****************************************************************/

typedef struct
{
    GString *keyword;
    char *whole_word_chars_left;
    char *whole_word_chars_right;
    gboolean line_start;
    int color;
    // "nested": the keyword opens a region highlighted with the default context until
    // nest_close; nest_open (its last character) nests inside that region
    unsigned char nest_open;
    unsigned char nest_close;
} syntax_keyword_t;

typedef struct
{
    GString *left;
    unsigned char first_left;
    GString *right;
    unsigned char first_right;
    gboolean line_start_left;
    gboolean line_start_right;
    gboolean between_delimiters;
    char *whole_word_chars_left;
    char *whole_word_chars_right;
    char *keyword_first_chars;
    /* keywords that can start with a given byte, in file order: the list for byte c begins
       at keyword_candidates[keyword_candidates_at[c]] and ends with 0 */
    unsigned short *keyword_candidates;
    unsigned int keyword_candidates_at[256];
    gboolean spelling;
    // first word is word[1]
    GPtrArray *keyword;
} context_rule_t;

typedef struct
{
    off_t offset;
    syntax_rule_t rule;
    gboolean guessed;  // walked from a guess, see get_rule_fixed()
} syntax_marker_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static char *error_file_name = NULL;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
syntax_keyword_free (gpointer keyword)
{
    syntax_keyword_t *k = SYNTAX_KEYWORD (keyword);

    g_string_free (k->keyword, TRUE);
    g_free (k->whole_word_chars_left);
    g_free (k->whole_word_chars_right);
    g_free (k);
}

/* --------------------------------------------------------------------------------------------- */

static void
context_rule_free (gpointer rule)
{
    context_rule_t *r = CONTEXT_RULE (rule);

    g_string_free (r->left, TRUE);
    g_string_free (r->right, TRUE);
    g_free (r->whole_word_chars_left);
    g_free (r->whole_word_chars_right);
    g_free (r->keyword_first_chars);
    g_free (r->keyword_candidates);

    if (r->keyword != NULL)
        g_ptr_array_free (r->keyword, TRUE);

    g_free (r);
}

/* --------------------------------------------------------------------------------------------- */

static gint
mc_defines_destroy (gpointer key, gpointer value, gpointer data)
{
    (void) data;

    g_free (key);
    g_strfreev ((char **) value);

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
/** Completely destroys the defines tree */

static void
destroy_defines (GTree **defines)
{
    g_tree_foreach (*defines, mc_defines_destroy, NULL);
    g_tree_destroy (*defines);
    *defines = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/** Wrapper for case insensitive mode */
inline static int
xx_tolower (const syntax_t *syn, int c)
{
    return syn->is_case_insensitive ? tolower (c) : c;
}

/* --------------------------------------------------------------------------------------------- */

static void
subst_defines (GTree *defines, char **argv, char **argv_end)
{
    for (; *argv != NULL && argv < argv_end; argv++)
    {
        char **t;

        t = g_tree_lookup (defines, *argv);
        if (t != NULL)
        {
            int argc, count;
            char **p;

            // Count argv array members
            argc = g_strv_length (argv + 1);

            // Count members of definition array
            count = g_strv_length (t);

            p = argv + count + argc;
            // Buffer overflow or infinitive loop in define
            if (p >= argv_end)
                break;

            // Move rest of argv after definition members
            while (argc >= 0)
                *p-- = argv[argc-- + 1];

            // Copy definition members to argv
            for (p = argv; *t != NULL; *p++ = *t++)
                ;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

static off_t
compare_word_to_right (const syntax_t *syn, off_t i, const GString *text, const char *whole_left,
                       const char *whole_right, gboolean line_start)
{
    const unsigned char *p, *q;
    int c, d, j;

    c = syn->get_byte (syn->data, i - 1);
    c = xx_tolower (syn, c);
    if ((line_start && c != '\n') || (whole_left != NULL && strchr (whole_left, c) != NULL))
        return -1;

    for (p = (const unsigned char *) text->str, q = p + text->len; p < q; p++, i++)
    {
        switch (*p)
        {
        case SYNTAX_TOKEN_STAR:
            if (++p > q)
                return -1;
            while (TRUE)
            {
                c = syn->get_byte (syn->data, i);
                c = xx_tolower (syn, c);
                if (*p == '\0' && whole_right != NULL && strchr (whole_right, c) == NULL)
                    break;
                if (c == *p)
                    break;
                if (c == '\n')
                    return -1;
                i++;
            }
            break;
        case SYNTAX_TOKEN_PLUS:
            if (++p > q)
                return -1;
            j = 0;
            while (TRUE)
            {
                c = syn->get_byte (syn->data, i);
                c = xx_tolower (syn, c);
                if (c == *p)
                {
                    j = i;
                    if (p[0] == text->str[0]
                        && p[1] == '\0')  // handle eg '+' and @+@ keywords properly
                        break;
                }
                if (j != 0
                    && strchr ((const char *) p + 1, c)
                        != NULL)  // c exists further down, so it will get matched later
                    break;
                if (whiteness (c) || (whole_right != NULL && strchr (whole_right, c) == NULL))
                {
                    if (*p == '\0')
                    {
                        i--;
                        break;
                    }
                    if (j == 0)
                        return -1;
                    i = j;
                    break;
                }
                i++;
            }
            break;
        case SYNTAX_TOKEN_BRACKET:
            if (++p > q)
                return -1;
            c = -1;
            while (TRUE)
            {
                d = c;
                c = syn->get_byte (syn->data, i);
                c = xx_tolower (syn, c);
                for (j = 0; p[j] != SYNTAX_TOKEN_BRACKET && p[j] != '\0'; j++)
                    if (c == p[j])
                        goto found_char2;
                break;
            found_char2:
                i++;
            }
            i--;
            while (*p != SYNTAX_TOKEN_BRACKET && p <= q)
                p++;
            if (p > q)
                return -1;
            if (p[1] == d)
                i--;
            break;
        case SYNTAX_TOKEN_BRACE:
            if (++p > q)
                return -1;
            c = syn->get_byte (syn->data, i);
            c = xx_tolower (syn, c);
            for (; *p != SYNTAX_TOKEN_BRACE && *p != '\0'; p++)
                if (c == *p)
                    goto found_char3;
            return -1;
        found_char3:
            while (*p != SYNTAX_TOKEN_BRACE && p < q)
                p++;
            break;
        default:
            c = syn->get_byte (syn->data, i);
            if (*p != xx_tolower (syn, c))
                return -1;
        }
    }

    if (whole_right == NULL)
        return i;

    c = syn->get_byte (syn->data, i);
    c = xx_tolower (syn, c);
    return strchr (whole_right, c) != NULL ? -1 : i;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Mark in @first the bytes a match of pattern @p can start with: a literal character, the
 * characters of a set \{...\}, or those of an optional set \[...\] together with whatever
 * can follow it.  A wildcard, or a pattern that may match nothing, can start anywhere.
 */

static void
mark_first_bytes (const syntax_t *syn, const unsigned char *p, gboolean *first)
{
    int b;

    while (TRUE)
        switch (*p)
        {
        case SYNTAX_TOKEN_BRACE:
            for (p++; *p != SYNTAX_TOKEN_BRACE && *p != '\0'; p++)
                first[xx_tolower (syn, *p)] = TRUE;
            return;
        case SYNTAX_TOKEN_BRACKET:
            for (p++; *p != SYNTAX_TOKEN_BRACKET && *p != '\0'; p++)
                first[xx_tolower (syn, *p)] = TRUE;
            if (*p == '\0')
                return;
            p++;
            break;
        case '\0':
        case SYNTAX_TOKEN_STAR:
        case SYNTAX_TOKEN_PLUS:
            for (b = 0; b < 256; b++)
                first[b] = TRUE;
            return;
        default:
            first[xx_tolower (syn, *p)] = TRUE;
            return;
        }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Index the keywords of @c by the bytes they can start with, so that only those are tried at
 * each position.  The order of the keywords is kept, since the first match wins.  Like the
 * scan of keyword_first_chars it replaces, the index ends at the first empty keyword.
 */

static void
index_keyword_candidates (const syntax_t *syn, context_rule_t *c)
{
    const unsigned char *first_chars = (const unsigned char *) c->keyword_first_chars;
    gboolean *first;
    GArray *candidates;
    unsigned short end = 0;
    unsigned short n, k;
    int b;

    // first_chars[0] stands for the context itself, not a keyword
    for (n = 1; first_chars[n] != '\0'; n++)
        ;

    // first[k * 256 + b]: keyword k can start with byte b
    first = g_new0 (gboolean, (gsize) n * 256);
    for (k = 1; k < n; k++)
        mark_first_bytes (syn,
                          (const unsigned char *) SYNTAX_KEYWORD (g_ptr_array_index (c->keyword, k))
                              ->keyword->str,
                          first + k * 256);

    candidates = g_array_new (FALSE, FALSE, sizeof (unsigned short));
    for (b = 0; b < 256; b++)
    {
        c->keyword_candidates_at[b] = candidates->len;
        for (k = 1; k < n; k++)
            if (first[k * 256 + b])
                g_array_append_val (candidates, k);
        g_array_append_val (candidates, end);
    }

    g_free (first);
    c->keyword_candidates = (unsigned short *) g_array_free (candidates, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static void
apply_rules_going_right (syntax_t *syn, off_t i)
{
    context_rule_t *r;
    int c;
    gboolean contextchanged = FALSE;
    gboolean found_left = FALSE, found_right = FALSE;
    gboolean keyword_foundleft = FALSE, keyword_foundright = FALSE;
    gboolean is_end;
    off_t end = 0;
    syntax_rule_t _rule = syn->rule;

    c = syn->get_byte (syn->data, i);
    c = xx_tolower (syn, c);
    if (c == 0)
        return;

    is_end = (syn->rule.end == i);

    // the opening delimiter of a nested region ended: continue with the default context
    // (in the default context itself this only tracks the region's end)
    if (_rule.keyword != 0 && is_end && _rule.nest_closing == 0 && _rule.nest_len < SYNTAX_NEST_MAX)
    {
        r = CONTEXT_RULE (g_ptr_array_index (syn->rules, _rule.context));
        if (SYNTAX_KEYWORD (g_ptr_array_index (r->keyword, _rule.keyword))->nest_close != '\0')
        {
            _rule.nest_context[_rule.nest_len] = _rule.context;
            _rule.nest_keyword[_rule.nest_len] = _rule.keyword;
            _rule.nest_depth[_rule.nest_len] = 0;
            _rule.nest_len++;
            _rule.context = 0;
            _rule._context = 0;
            _rule.border = 0;
            _rule.keyword = 0;
        }
    }

    if (is_end)
        _rule.nest_closing = 0;

    // check to turn off a keyword
    if (_rule.keyword != 0)
    {
        if (syn->get_byte (syn->data, i - 1) == '\n')
            _rule.keyword = 0;
        if (is_end)
        {
            _rule.keyword = 0;
            keyword_foundleft = TRUE;
        }
    }

    // check to turn off a context
    if (_rule.context != 0 && _rule.keyword == 0)
    {
        off_t e;

        r = CONTEXT_RULE (g_ptr_array_index (syn->rules, _rule.context));
        if (r->first_right == c && (syn->rule.border & RULE_ON_RIGHT_BORDER) == 0
            && r->right->len != 0
            && (e = compare_word_to_right (syn, i, r->right, r->whole_word_chars_left,
                                           r->whole_word_chars_right, r->line_start_right))
                > 0)
        {
            _rule.end = e;
            found_right = TRUE;
            _rule.border = RULE_ON_RIGHT_BORDER;
            if (r->between_delimiters)
                _rule.context = 0;
        }
        else if (is_end && (syn->rule.border & RULE_ON_RIGHT_BORDER) != 0)
        {
            // always turn off a context at 4
            found_left = TRUE;
            _rule.border = 0;
            if (!keyword_foundleft)
                _rule.context = 0;
        }
        else if (is_end && (syn->rule.border & RULE_ON_LEFT_BORDER) != 0)
        {
            // never turn off a context at 2
            found_left = TRUE;
            _rule.border = 0;
        }
    }

    // inside a nested region: balance its brackets, return to the outer context at its end
    if (_rule.nest_len != 0 && _rule.context == 0 && _rule.keyword == 0 && _rule.border == 0
        && !found_right)
    {
        const int top = _rule.nest_len - 1;
        const syntax_keyword_t *k;

        r = CONTEXT_RULE (g_ptr_array_index (syn->rules, _rule.nest_context[top]));
        k = SYNTAX_KEYWORD (g_ptr_array_index (r->keyword, _rule.nest_keyword[top]));

        if (c == k->nest_close && _rule.nest_depth[top] == 0)
        {
            // the closing delimiter takes the color of the opening one
            _rule.nest_len--;
            _rule.context = _rule.nest_context[top];
            _rule.keyword = _rule.nest_keyword[top];
            _rule.nest_closing = 1;
            _rule.end = i + 1;
            syn->rule = _rule;
            return;
        }
        if (c == k->nest_close)
            _rule.nest_depth[top]--;
        else if (c == k->nest_open && _rule.nest_depth[top] < 255)
            _rule.nest_depth[top]++;
    }

    // check to turn on a keyword
    if (_rule.keyword == 0)
    {
        const unsigned short *kc;

        r = CONTEXT_RULE (g_ptr_array_index (syn->rules, _rule.context));

        if (r->keyword_candidates != NULL)
            for (kc = r->keyword_candidates + r->keyword_candidates_at[c]; *kc != 0; kc++)
            {
                syntax_keyword_t *k;
                int count;
                off_t e = -1;

                count = *kc;
                k = SYNTAX_KEYWORD (g_ptr_array_index (r->keyword, count));
                if (k->keyword != 0)
                    e = compare_word_to_right (syn, i, k->keyword, k->whole_word_chars_left,
                                               k->whole_word_chars_right, k->line_start);
                if (e > 0)
                {
                    /* when both context and keyword terminate with a newline,
                       the context overflows to the next line and colorizes it incorrectly;
                       a line continuation (backslash, newline) must keep the newline */
                    if (e > i + 1 && _rule._context != 0
                        && k->keyword->str[k->keyword->len - 1] == '\n'
                        && strcmp (k->keyword->str, "\\\n") != 0)
                    {
                        r = CONTEXT_RULE (g_ptr_array_index (syn->rules, _rule._context));
                        if (r->right != NULL && r->right->len != 0
                            && r->right->str[r->right->len - 1] == '\n')
                            e--;
                    }

                    end = e;
                    _rule.end = e;
                    _rule.keyword = count;
                    keyword_foundright = TRUE;
                    break;
                }
            }
    }

    // check to turn on a context
    if (_rule.context == 0)
    {
        if (!found_left && is_end)
        {
            if ((syn->rule.border & RULE_ON_RIGHT_BORDER) != 0)
            {
                _rule.border = 0;
                _rule.context = 0;
                contextchanged = TRUE;
                _rule.keyword = 0;
            }
            else if ((syn->rule.border & RULE_ON_LEFT_BORDER) != 0)
            {
                r = CONTEXT_RULE (g_ptr_array_index (syn->rules, _rule._context));
                _rule.border = 0;
                if (r->between_delimiters)
                {
                    _rule.context = _rule._context;
                    contextchanged = TRUE;
                    _rule.keyword = 0;

                    if (r->first_right == c)
                    {
                        off_t e = -1;

                        if (r->right->len != 0)
                            e = compare_word_to_right (syn, i, r->right, r->whole_word_chars_left,
                                                       r->whole_word_chars_right,
                                                       r->line_start_right);
                        if (e >= end)
                        {
                            _rule.end = e;
                            found_right = TRUE;
                            _rule.border = RULE_ON_RIGHT_BORDER;
                            _rule.context = 0;
                        }
                    }
                }
            }
        }

        if (!found_right)
        {
            size_t count;

            for (count = 1; count < syn->rules->len; count++)
            {
                r = CONTEXT_RULE (g_ptr_array_index (syn->rules, count));
                if (r->first_left == c)
                {
                    off_t e = -1;

                    if (r->left->len != 0)
                        e = compare_word_to_right (syn, i, r->left, r->whole_word_chars_left,
                                                   r->whole_word_chars_right, r->line_start_left);
                    if (e >= end && (_rule.keyword == 0 || keyword_foundright))
                    {
                        _rule.end = e;
                        _rule.border = RULE_ON_LEFT_BORDER;
                        _rule._context = count;
                        if (!r->between_delimiters && _rule.keyword == 0)
                        {
                            _rule.context = count;
                            contextchanged = TRUE;
                        }
                        break;
                    }
                }
            }
        }
    }

    // check again to turn on a keyword if the context switched
    if (contextchanged && _rule.keyword == 0)
    {
        const unsigned short *kc;

        r = CONTEXT_RULE (g_ptr_array_index (syn->rules, _rule.context));

        for (kc = r->keyword_candidates + r->keyword_candidates_at[c]; *kc != 0; kc++)
        {
            syntax_keyword_t *k;
            int count;
            off_t e = -1;

            count = *kc;
            k = SYNTAX_KEYWORD (g_ptr_array_index (r->keyword, count));

            if (k->keyword->len != 0)
                e = compare_word_to_right (syn, i, k->keyword, k->whole_word_chars_left,
                                           k->whole_word_chars_right, k->line_start);
            if (e > 0)
            {
                _rule.end = e;
                _rule.keyword = count;
                break;
            }
        }
    }

    syn->rule = _rule;
}

/* --------------------------------------------------------------------------------------------- */

static void
get_rule (syntax_t *syn, off_t byte_index)
{
    off_t i;

    if (byte_index > syn->last_get_rule)
    {
        for (i = syn->last_get_rule + 1; i <= byte_index; i++)
        {
            off_t d = SYNTAX_MARKER_DENSITY;

            apply_rules_going_right (syn, i);

            if (syn->marker != NULL)
                d += ((syntax_marker_t *) syn->marker->data)->offset;

            if (i > d)
            {
                syntax_marker_t *s;

                s = g_new (syntax_marker_t, 1);
                s->offset = i;
                s->rule = syn->rule;
                syn->marker = g_slist_prepend (syn->marker, s);
            }
        }
    }
    else if (byte_index < syn->last_get_rule)
    {
        while (TRUE)
        {
            syntax_marker_t *s;

            if (syn->marker == NULL)
            {
                memset (&syn->rule, 0, sizeof (syn->rule));
                for (i = -1; i <= byte_index; i++)
                    apply_rules_going_right (syn, i);
                break;
            }

            s = (syntax_marker_t *) syn->marker->data;

            if (byte_index >= s->offset)
            {
                syn->rule = s->rule;
                for (i = s->offset + 1; i <= byte_index; i++)
                    apply_rules_going_right (syn, i);
                break;
            }

            g_free (s);
            syn->marker = g_slist_delete_link (syn->marker, syn->marker);
        }
    }
    syn->last_get_rule = byte_index;
}

/* --------------------------------------------------------------------------------------------- */

/** Index of the last checkpoint at or before @byte_index, or -1. */

static gssize
find_checkpoint (const GArray *checkpoints, off_t byte_index)
{
    gssize lo = 0, hi = (gssize) checkpoints->len;

    while (lo < hi)
    {
        const gssize mid = lo + (hi - lo) / 2;

        if (g_array_index (checkpoints, syntax_marker_t, mid).offset <= byte_index)
            lo = mid + 1;
        else
            hi = mid;
    }

    return lo - 1;
}

/* --------------------------------------------------------------------------------------------- */

/** Start of the line around @byte_index, looking back at most @limit bytes. */

static off_t
sync_point (const syntax_t *syn, off_t byte_index, off_t limit)
{
    off_t i;

    for (i = byte_index; i > 0 && byte_index - i < limit; i--)
        if (syn->get_byte (syn->data, i - 1) == '\n')
            return i;

    return i;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * get_rule() for text that does not change: saved states are kept and jumped to.
 *
 * Past sync_window bytes from a saved state, the walk starts afresh at a line start: a guess,
 * wrong inside a comment or string cut by that line start, and in the rest of it.  Most of
 * these end within a few lines, so states a little past the guess are kept, as guesses.
 */

static void
get_rule_fixed (syntax_t *syn, off_t byte_index)
{
    const off_t density =
        syn->checkpoint_density > 0 ? syn->checkpoint_density : SYNTAX_MARKER_DENSITY;
    // how far past a fresh start a state is good enough to keep
    const off_t warmup = syn->sync_window / 16;
    gssize c;
    off_t i, from;

    if (syn->checkpoints == NULL)
        syn->checkpoints = g_array_new (FALSE, FALSE, sizeof (syntax_marker_t));

    c = find_checkpoint (syn->checkpoints, byte_index);

    if (byte_index > syn->last_get_rule
        && (c < 0
            || g_array_index (syn->checkpoints, syntax_marker_t, c).offset <= syn->last_get_rule))
        from = syn->last_get_rule;  // no saved state on the way: walk on from where we are
    else if (c >= 0)
    {
        const syntax_marker_t *s = &g_array_index (syn->checkpoints, syntax_marker_t, c);

        from = s->offset;
        syn->rule = s->rule;
        syn->guessed = s->guessed;
        syn->guess_start = s->offset - warmup;
    }
    else
    {
        from = -2;
        memset (&syn->rule, 0, sizeof (syn->rule));
        syn->guessed = FALSE;
    }

    if (syn->sync_window > 0 && byte_index - from > syn->sync_window)
    {
        // too far to walk: guess the state at the line start before the window
        from = sync_point (syn, byte_index - syn->sync_window, syn->sync_window) - 1;
        memset (&syn->rule, 0, sizeof (syn->rule));
        syn->guessed = TRUE;
        syn->guess_start = from;
    }

    for (i = from + 1; i <= byte_index; i++)
    {
        apply_rules_going_right (syn, i);

        // keep the states sorted and one per stretch of density bytes
        if (i % density == 0 && (!syn->guessed || i - syn->guess_start >= warmup))
        {
            c = find_checkpoint (syn->checkpoints, i);
            if (c < 0 || g_array_index (syn->checkpoints, syntax_marker_t, c).offset != i)
            {
                syntax_marker_t s = { .offset = i, .rule = syn->rule, .guessed = syn->guessed };

                g_array_insert_val (syn->checkpoints, (guint) (c + 1), s);
            }
        }
    }

    syn->last_get_rule = byte_index;
}

/* --------------------------------------------------------------------------------------------- */

static int
translate_rule_to_color (const syntax_t *syn, const syntax_rule_t *rule)
{
    syntax_keyword_t *k;
    context_rule_t *r;

    r = CONTEXT_RULE (g_ptr_array_index (syn->rules, rule->context));
    k = SYNTAX_KEYWORD (g_ptr_array_index (r->keyword, rule->keyword));

    return k->color;
}

/* --------------------------------------------------------------------------------------------- */
/**
   Returns 0 on error/eof or a count of the number of bytes read
   including the newline. Result must be free'd.
   In case of an error, *line will not be modified.
 */

static size_t
read_one_line (char **line, FILE *f)
{
    GString *p;
    size_t r = 0;

    // not reallocate string too often
    p = g_string_sized_new (64);

    while (TRUE)
    {
        int c;

        c = fgetc (f);
        if (c == EOF)
        {
            if (ferror (f))
            {
                if (errno == EINTR)
                    continue;
                r = 0;
            }
            break;
        }
        r++;

        // handle all of \r\n, \r, \n correctly.
        if (c == '\n')
            break;
        if (c == '\r')
        {
            c = fgetc (f);
            if (c == '\n')
                r++;
            else
                ungetc (c, f);
            break;
        }

        g_string_append_c (p, c);
    }
    if (r != 0)
        *line = g_string_free (p, FALSE);
    else
        g_string_free (p, TRUE);

    return r;
}

/* --------------------------------------------------------------------------------------------- */

static char *
convert (char *s)
{
    char *r, *p;

    p = r = s;
    while (*s)
    {
        switch (*s)
        {
        case '\\':
            s++;
            switch (*s)
            {
            case ' ':
                *p = ' ';
                s--;
                break;
            case 'n':
                *p = '\n';
                break;
            case 'r':
                *p = '\r';
                break;
            case 't':
                *p = '\t';
                break;
            case 's':
                *p = ' ';
                break;
            case '*':
                *p = '*';
                break;
            case '\\':
                *p = '\\';
                break;
            case '[':
            case ']':
                *p = SYNTAX_TOKEN_BRACKET;
                break;
            case '{':
            case '}':
                *p = SYNTAX_TOKEN_BRACE;
                break;
            case 0:
                *p = *s;
                return r;
            default:
                *p = *s;
                break;
            }
            break;
        case '*':
            *p = SYNTAX_TOKEN_STAR;
            break;
        case '+':
            *p = SYNTAX_TOKEN_PLUS;
            break;
        default:
            *p = *s;
            break;
        }
        s++;
        p++;
    }
    *p = '\0';
    return r;
}

/* --------------------------------------------------------------------------------------------- */

static int
get_args (char *l, char **args, int args_size)
{
    int argc = 0;

    while (argc < args_size)
    {
        char *p = l;

        while (*p != '\0' && whiteness (*p))
            p++;
        if (*p == '\0')
            break;
        for (l = p + 1; *l != '\0' && !whiteness (*l); l++)
            ;
        if (*l != '\0')
            *l++ = '\0';
        args[argc++] = convert (p);
    }
    args[argc] = (char *) NULL;
    return argc;
}

/* --------------------------------------------------------------------------------------------- */

static int
this_try_alloc_color_pair (const syntax_t *syn, tty_color_pair_t *color)
{
    char f[80], b[80], a[80], *p;

    if (color->bg != NULL && *color->bg == '\0')
        color->bg = NULL;
    if (color->fg != NULL && *color->fg == '\0')
        color->fg = NULL;
    if (color->attrs != NULL && *color->attrs == '\0')
        color->attrs = NULL;

    if (color->fg == NULL && color->bg == NULL && color->attrs == NULL)
        return syn->normal_color;

    if (color->fg != NULL)
    {
        g_strlcpy (f, color->fg, sizeof (f));
        p = strchr (f, '/');
        if (p != NULL)
            *p = '\0';
        color->fg = f;
    }
    if (color->bg != NULL)
    {
        g_strlcpy (b, color->bg, sizeof (b));
        p = strchr (b, '/');
        if (p != NULL)
            *p = '\0';
        color->bg = b;
    }
    if (color->fg == NULL || color->bg == NULL)
    {
        // get colors from skin
        char *editnormal;

        editnormal = mc_skin_get (syn->skin_group, "_default_", "default;default");

        if (color->fg == NULL)
        {
            g_strlcpy (f, editnormal, sizeof (f));
            p = strchr (f, ';');
            if (p != NULL)
                *p = '\0';
            if (f[0] == '\0')
                g_strlcpy (f, "default", sizeof (f));
            color->fg = f;
        }
        if (color->bg == NULL)
        {
            p = strchr (editnormal, ';');
            if ((p != NULL) && (*(++p) != '\0'))
                g_strlcpy (b, p, sizeof (b));
            else
                g_strlcpy (b, "default", sizeof (b));
            color->bg = b;
        }

        g_free (editnormal);
    }

    if (color->attrs != NULL)
    {
        g_strlcpy (a, color->attrs, sizeof (a));
        p = strchr (a, '/');
        if (p != NULL)
            *p = '\0';
        // get_args() mangles the + signs, unmangle 'em
        p = a;
        while ((p = strchr (p, SYNTAX_TOKEN_PLUS)) != NULL)
            *p++ = '+';
        color->attrs = a;
    }

    return tty_try_alloc_color_pair (color, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Allocate color of context or keyword.  If the foreground is a scope token
 * (e.g. KEYWORD_CONTROL), colors are taken from the color scheme;
 * explicitly given background and attributes still take precedence.
 * ctx_token is the scope token of the enclosing context or NULL.
 */

static int
syntax_alloc_color (const syntax_t *syn, tty_color_pair_t *color, const char *ctx_token)
{
    syntax_scheme_style_t style;

    if (syntax_scheme_is_scope_token (color->fg))
    {
        syntax_scheme_lookup (ctx_token, color->fg, &style);
        color->fg = style.fg;
        if (color->bg == NULL || *color->bg == '\0')
            color->bg = style.bg;
        if (color->attrs == NULL || *color->attrs == '\0')
            color->attrs = style.attrs;
    }

    return this_try_alloc_color_pair (syn, color);
}

/* --------------------------------------------------------------------------------------------- */

static FILE *
open_include_file (const char *filename)
{
    FILE *f;

    g_free (error_file_name);
    error_file_name = g_strdup (filename);
    if (g_path_is_absolute (filename))
        return fopen (filename, "r");

    g_free (error_file_name);
    error_file_name =
        g_build_filename (mc_config_get_data_path (), EDIT_SYNTAX_DIR, filename, (char *) NULL);
    f = fopen (error_file_name, "r");
    if (f != NULL)
        return f;

    g_free (error_file_name);
    error_file_name =
        g_build_filename (mc_global.share_data_dir, EDIT_SYNTAX_DIR, filename, (char *) NULL);

    return fopen (error_file_name, "r");
}

/* --------------------------------------------------------------------------------------------- */

/* lower-case a match pattern; color fields are left alone so scope tokens keep their case */
inline static void
xx_lowerize_pattern (const syntax_t *syn, char *pattern)
{
    if (syn->is_case_insensitive)
        for (; *pattern != '\0'; pattern++)
            *pattern = tolower ((unsigned char) *pattern);
}

/* --------------------------------------------------------------------------------------------- */
/** returns line number on error */

static int
read_syntax_rules (syntax_t *syn, FILE *f, char **args, int args_size)
{
    // files that include the one being read, innermost last
    FILE *includers[SYNTAX_INCLUDE_MAX];
    char *includer_names[SYNTAX_INCLUDE_MAX];
    int includer_lines[SYNTAX_INCLUDE_MAX];
    int depth = 0;
    tty_color_pair_t color;
    char last_fg[BUF_TINY / 2] = "";
    char last_bg[BUF_TINY / 2] = "";
    char last_attrs[BUF_TINY] = "";
    char last_scope[BUF_TINY] = "";
    char whole_right[BUF_MEDIUM];
    char whole_left[BUF_MEDIUM];
    char *l = NULL;
    int line = 0;
    context_rule_t *c = NULL;
    gboolean no_words = TRUE;
    int result = 0;

    args[0] = NULL;
    syn->is_case_insensitive = FALSE;

    strcpy (whole_left, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_01234567890");
    strcpy (whole_right, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_01234567890");

    syn->rules = g_ptr_array_new_with_free_func (context_rule_free);

    if (syn->defines == NULL)
        syn->defines = g_tree_new ((GCompareFunc) strcmp);

    while (TRUE)
    {
        char **a;
        size_t len;
        int argc;

        line++;
        l = NULL;

        len = read_one_line (&l, f);
        // end of an included file: continue after the include line
        while (len == 0 && depth > 0)
        {
            fclose (f);
            depth--;
            f = includers[depth];
            line = includer_lines[depth] + 1;
            g_free (error_file_name);
            error_file_name = includer_names[depth];
            MC_PTR_FREE (l);
            len = read_one_line (&l, f);
        }
        if (len == 0)
            break;

        argc = get_args (l, args, args_size);
        a = args + 1;
        if (args[0] == NULL)
        {
            // do nothing
        }
        else if (strcmp (args[0], "include") == 0)
        {
            FILE *inc;
            char *name;

            if (depth >= SYNTAX_INCLUDE_MAX || argc != 2)
            {
                result = line;
                break;
            }
            name = error_file_name;
            error_file_name = NULL;
            inc = open_include_file (args[1]);
            if (inc == NULL)
            {
                g_free (error_file_name);
                error_file_name = name;
                result = line;
                break;
            }
            includers[depth] = f;
            includer_names[depth] = name;
            includer_lines[depth] = line;
            depth++;
            f = inc;
            line = 0;
        }
        else if (strcmp (args[0], "caseinsensitive") == 0)
        {
            syn->is_case_insensitive = TRUE;
        }
        else if (strcmp (args[0], "wholechars") == 0)
        {
            check_a;
            if (strcmp (*a, "left") == 0)
            {
                a++;
                g_strlcpy (whole_left, *a, sizeof (whole_left));
                xx_lowerize_pattern (syn, whole_left);
            }
            else if (strcmp (*a, "right") == 0)
            {
                a++;
                g_strlcpy (whole_right, *a, sizeof (whole_right));
                xx_lowerize_pattern (syn, whole_right);
            }
            else
            {
                g_strlcpy (whole_left, *a, sizeof (whole_left));
                g_strlcpy (whole_right, *a, sizeof (whole_right));
                xx_lowerize_pattern (syn, whole_left);
                xx_lowerize_pattern (syn, whole_right);
            }
            a++;
            check_not_a;
        }
        else if (strcmp (args[0], "context") == 0)
        {
            syntax_keyword_t *k;

            check_a;
            if (syn->rules->len == 0)
            {
                // first context is the default
                if (strcmp (*a, "default") != 0)
                    break_a;

                a++;
                c = g_new0 (context_rule_t, 1);
                g_ptr_array_add (syn->rules, c);
                c->left = g_string_new (" ");
                c->right = g_string_new (" ");
            }
            else
            {
                // Start new context.
                c = g_new0 (context_rule_t, 1);
                g_ptr_array_add (syn->rules, c);
                if (strcmp (*a, "exclusive") == 0)
                {
                    a++;
                    c->between_delimiters = TRUE;
                }
                check_a;
                if (strcmp (*a, "whole") == 0)
                {
                    a++;
                    c->whole_word_chars_left = g_strdup (whole_left);
                    c->whole_word_chars_right = g_strdup (whole_right);
                }
                else if (strcmp (*a, "wholeleft") == 0)
                {
                    a++;
                    c->whole_word_chars_left = g_strdup (whole_left);
                }
                else if (strcmp (*a, "wholeright") == 0)
                {
                    a++;
                    c->whole_word_chars_right = g_strdup (whole_right);
                }
                check_a;
                if (strcmp (*a, "linestart") == 0)
                {
                    a++;
                    c->line_start_left = TRUE;
                }
                check_a;
                c->left = g_string_new (*a++);
                check_a;
                if (strcmp (*a, "linestart") == 0)
                {
                    a++;
                    c->line_start_right = TRUE;
                }
                check_a;
                c->right = g_string_new (*a++);
                xx_lowerize_pattern (syn, c->left->str);
                xx_lowerize_pattern (syn, c->right->str);
                c->first_left = c->left->str[0];
                c->first_right = c->right->str[0];
            }
            c->keyword = g_ptr_array_new_with_free_func (syntax_keyword_free);
            k = g_new0 (syntax_keyword_t, 1);
            g_ptr_array_add (c->keyword, k);
            no_words = FALSE;
            subst_defines (syn->defines, a, &args[ARGS_LEN]);
            color.fg = *a;
            if (*a != NULL)
                a++;
            color.bg = *a;
            if (*a != NULL)
                a++;
            color.attrs = *a;
            if (*a != NULL)
                a++;
            g_strlcpy (last_fg, color.fg != NULL ? color.fg : "", sizeof (last_fg));
            g_strlcpy (last_bg, color.bg != NULL ? color.bg : "", sizeof (last_bg));
            g_strlcpy (last_attrs, color.attrs != NULL ? color.attrs : "", sizeof (last_attrs));
            g_strlcpy (last_scope, syntax_scheme_is_scope_token (color.fg) ? color.fg : "",
                       sizeof (last_scope));
            k->color = syntax_alloc_color (syn, &color, NULL);
            k->keyword = g_string_new (" ");
            check_not_a;
        }
        else if (strcmp (args[0], "spellcheck") == 0)
        {
            if (c == NULL)
            {
                result = line;
                break;
            }
            c->spelling = TRUE;
        }
        else if (strcmp (args[0], "keyword") == 0 || strcmp (args[0], "nested") == 0)
        {
            context_rule_t *last_rule;
            syntax_keyword_t *k;
            const gboolean nested = (args[0][0] == 'n');

            if (no_words)
                break_a;
            check_a;
            last_rule = CONTEXT_RULE (g_ptr_array_index (syn->rules, syn->rules->len - 1));
            k = g_new0 (syntax_keyword_t, 1);
            g_ptr_array_add (last_rule->keyword, k);
            if (!nested)
            {
                if (strcmp (*a, "whole") == 0)
                {
                    a++;
                    k->whole_word_chars_left = g_strdup (whole_left);
                    k->whole_word_chars_right = g_strdup (whole_right);
                }
                else if (strcmp (*a, "wholeleft") == 0)
                {
                    a++;
                    k->whole_word_chars_left = g_strdup (whole_left);
                }
                else if (strcmp (*a, "wholeright") == 0)
                {
                    a++;
                    k->whole_word_chars_right = g_strdup (whole_right);
                }
                check_a;
                if (strcmp (*a, "linestart") == 0)
                {
                    a++;
                    k->line_start = TRUE;
                }
                check_a;
                if (strcmp (*a, "whole") == 0)
                    break_a;
            }

            k->keyword = g_string_new (*a++);
            xx_lowerize_pattern (syn, k->keyword->str);
            if (nested)
            {
                // a one-character closing delimiter
                check_a;
                if (k->keyword->len == 0 || strchr (k->keyword->str, '*') != NULL
                    || strlen (*a) != 1)
                    break_a;
                k->nest_open = (unsigned char) k->keyword->str[k->keyword->len - 1];
                k->nest_close = (unsigned char) **a;
                a++;
            }
            subst_defines (syn->defines, a, &args[ARGS_LEN]);
            color.fg = *a;
            if (*a != NULL)
                a++;
            color.bg = *a;
            if (*a != NULL)
                a++;
            color.attrs = *a;
            if (*a != NULL)
                a++;
            if (color.fg == NULL)
                color.fg = last_fg;
            if (color.bg == NULL)
                color.bg = last_bg;
            if (color.attrs == NULL)
                color.attrs = last_attrs;
            k->color = syntax_alloc_color (syn, &color, last_scope);
            check_not_a;
        }
        else if (*(args[0]) == '#')
        {
            // do nothing for comment
        }
        else if (strcmp (args[0], "file") == 0)
        {
            break;
        }
        else if (strcmp (args[0], "define") == 0)
        {
            char *key = *a++;
            char **argv;

            if (argc < 3)
                break_a;
            argv = g_tree_lookup (syn->defines, key);
            if (argv != NULL)
                mc_defines_destroy (NULL, argv, NULL);
            else
                key = g_strdup (key);

            argv = g_new (char *, argc - 1);
            g_tree_insert (syn->defines, key, argv);
            while (*a != NULL)
                *argv++ = g_strdup (*a++);
            *argv = NULL;
        }
        else
        {
            // anything else is an error
            break_a;
        }
        MC_PTR_FREE (l);
    }
    MC_PTR_FREE (l);

    // an error inside an included file: report it there, close the files it left open
    while (depth > 0)
    {
        fclose (f);
        depth--;
        f = includers[depth];
        g_free (includer_names[depth]);
    }

    if (syn->rules->len == 0)
    {
        g_ptr_array_free (syn->rules, TRUE);
        syn->rules = NULL;
    }

    if (result == 0)
    {
        size_t i;
        GString *first_chars;

        if (syn->rules == NULL)
            return line;

        first_chars = g_string_sized_new (32);

        // collect first character of keywords
        for (i = 0; i < syn->rules->len; i++)
        {
            size_t j;

            g_string_set_size (first_chars, 0);
            c = CONTEXT_RULE (g_ptr_array_index (syn->rules, i));

            g_string_append_c (first_chars, (char) 1);
            for (j = 1; j < c->keyword->len; j++)
            {
                syntax_keyword_t *k;

                k = SYNTAX_KEYWORD (g_ptr_array_index (c->keyword, j));
                g_string_append_c (first_chars, k->keyword->str[0]);
            }

            c->keyword_first_chars = g_strndup (first_chars->str, first_chars->len);
            index_keyword_candidates (syn, c);
        }

        g_string_free (first_chars, TRUE);
    }

    return result;
}

/* --------------------------------------------------------------------------------------------- */

/* returns -1 on file error, line number on error in file syntax */
static int
read_syntax_file (syntax_t *syn, GPtrArray *pnames, const char *syntax_file, const char *file_name,
                  const char *first_line, const char *type, char **type_found)
{
    FILE *f, *g = NULL;
    char *args[ARGS_LEN], *l = NULL;
    long line = 0;
    int result = 0;
    gboolean found = FALSE;

    f = fopen (syntax_file, "r");
    if (f == NULL)
    {
        char *global_syntax_file;

        global_syntax_file =
            g_build_filename (mc_global.share_data_dir, EDIT_SYNTAX_FILE, (char *) NULL);
        f = fopen (global_syntax_file, "r");
        g_free (global_syntax_file);
        if (f == NULL)
            return -1;
    }

    args[0] = NULL;
    while (TRUE)
    {
        line++;
        MC_PTR_FREE (l);
        if (read_one_line (&l, f) == 0)
            break;
        (void) get_args (l, args, ARGS_LEN - 1);  // Final NULL
        if (args[0] == NULL)
            continue;

        // Looking for 'include ...' lines before first 'file ...' ones
        if (!found && strcmp (args[0], "include") == 0)
        {
            if (args[1] == NULL || (g = open_include_file (args[1])) == NULL)
            {
                result = line;
                break;
            }
            goto found_type;
        }

        // looking for 'file ...' lines only
        if (strcmp (args[0], "file") != 0)
            continue;

        found = TRUE;

        // must have two args or report error
        if (args[1] == NULL || args[2] == NULL)
        {
            result = line;
            break;
        }

        if (pnames != NULL)
        {
            // 1: just collecting a list of names of rule sets
            g_ptr_array_add (pnames, g_strdup (args[2]));
        }
        else if (type != NULL)
        {
            // 2: rule set was explicitly specified by the caller
            if (strcmp (type, args[2]) == 0)
                goto found_type;
        }
        else if (file_name != NULL)
        {
            // 3: auto-detect rule set from regular expressions
            gboolean q;

            q = mc_search (args[1], NULL, file_name, MC_SEARCH_T_REGEX);
            // does filename match arg 1 ?
            if (!q && args[3] != NULL)
            {
                // does first line match arg 3 ?
                q = mc_search (args[3], NULL, first_line, MC_SEARCH_T_REGEX);
            }
            if (q)
            {
                int line_error;
                char *syntax_type;

            found_type:
                syntax_type = args[2];
                line_error = read_syntax_rules (syn, g ? g : f, args, ARGS_LEN - 1);
                if (line_error != 0)
                {
                    if (error_file_name == NULL)  // an included file
                        result = line + line_error;
                    else
                        result = line_error;
                }
                else
                {
                    g_free (*type_found);
                    *type_found = g_strdup (syntax_type);
                    // if there are no rules then turn off syntax highlighting for speed
                    if (g == NULL && syn->rules->len == 1)
                    {
                        context_rule_t *r0;

                        r0 = CONTEXT_RULE (g_ptr_array_index (syn->rules, 0));
                        if (r0->keyword->len == 1 && !r0->spelling)
                        {
                            syntax_free (syn);
                            break;
                        }
                    }
                }

                if (g == NULL)
                    break;

                fclose (g);
                g = NULL;
            }
        }
    }
    g_free (l);
    fclose (f);
    return result;
}
/* --------------------------------------------------------------------------------------------- */
/**
 * Read the Syntax file in use: the user's one, or the system one when the user has none.
 * @return its lines, or NULL when neither can be read
 */

static char **
syntax_file_read_lines (void)
{
    char *path, *contents = NULL;
    char **lines;

    path = mc_config_get_full_path (EDIT_SYNTAX_FILE);
    if (path == NULL || !g_file_get_contents (path, &contents, NULL, NULL))
    {
        g_free (path);
        path = g_build_filename (mc_global.share_data_dir, EDIT_SYNTAX_FILE, (char *) NULL);
        if (!g_file_get_contents (path, &contents, NULL, NULL))
            contents = NULL;
    }
    g_free (path);

    if (contents == NULL)
        return NULL;

    lines = g_strsplit (contents, "\n", -1);
    g_free (contents);
    return lines;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Field @n of the line @l as it is written in the file.
 * @return its start, with its length in @len, or NULL when the line is shorter
 */

static const char *
syntax_line_field (const char *l, int n, size_t *len)
{
    while (TRUE)
    {
        const char *e;

        while (*l != '\0' && whiteness (*l))
            l++;
        if (*l == '\0')
            return NULL;
        for (e = l; *e != '\0' && !whiteness (*e); e++)
            ;
        if (n-- == 0)
        {
            *len = (size_t) (e - l);
            return l;
        }
        l = e;
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
syntax_line_is_word (const char *l, const char *word)
{
    const char *p;
    size_t len;

    p = syntax_line_field (l, 0, &len);
    return p != NULL && len == strlen (word) && strncmp (p, word, len) == 0;
}

/* --------------------------------------------------------------------------------------------- */

/* is @l the 'file' line of the syntax @type, or of any syntax when @type is NULL? */
static gboolean
syntax_line_is_entry (const char *l, const char *type)
{
    const char *p;
    size_t len;
    char *name;
    gboolean ret;

    if (!syntax_line_is_word (l, "file"))
        return FALSE;
    if (type == NULL)
        return TRUE;

    p = syntax_line_field (l, 2, &len);
    if (p == NULL)
        return FALSE;
    name = g_strndup (p, len);
    ret = strcmp (convert (name), type) == 0;
    g_free (name);
    return ret;
}

/* --------------------------------------------------------------------------------------------- */

/* write @c the way convert() reads it back */
static void
syntax_escape_char (GString *s, char c)
{
    switch (c)
    {
    case ' ':
        g_string_append (s, "\\s");
        break;
    case '\t':
        g_string_append (s, "\\t");
        break;
    case '\\':
    case '*':
    case '+':
        g_string_append_c (s, '\\');
        g_string_append_c (s, c);
        break;
    default:
        g_string_append_c (s, c);
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* NULL when @pattern and @name can make an entry, or the error message */
static char *
syntax_check_entry (const char *pattern, const char *name)
{
    char *test;
    size_t len;
    GRegex *re;
    GError *error = NULL;

    if (name[0] == '\0')
        return g_strdup (_ ("Give the entry a name"));
    if (pattern[0] == '\0' || syntax_line_field (pattern, 1, &len) != NULL)
        return g_strdup (_ ("The pattern must be one word: write a space as \\s"));

    test = g_strdup (pattern);
    re = g_regex_new (convert (test), 0, 0, &error);
    g_free (test);
    if (re == NULL)
    {
        char *msg;

        msg = g_strdup_printf (_ ("Bad pattern:\n%s"), error->message);
        g_error_free (error);
        return msg;
    }
    g_regex_unref (re);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* the 'file' line of an entry: @pattern and @name as written in the file, then @rest */
static char *
syntax_entry_line (const char *pattern, const char *name, const char *rest)
{
    GString *s;

    s = g_string_new ("file ");
    g_string_append (s, pattern);
    g_string_append_c (s, ' ');
    for (; *name != '\0'; name++)
        syntax_escape_char (s, *name);
    g_string_append (s, rest);
    return g_string_free (s, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Write @lines, free'd here, to the user's Syntax file.
 * @return NULL, or the error message
 */

static char *
syntax_file_write_lines (char **lines)
{
    char *path, *dir, *tmp_path, *data, *msg = NULL;
    FILE *f;
    gboolean ok;

    data = g_strjoinv ("\n", lines);
    g_strfreev (lines);

    path = mc_config_get_full_path (EDIT_SYNTAX_FILE);
    dir = g_path_get_dirname (path);
    (void) g_mkdir_with_parents (dir, 0700);
    g_free (dir);

    f = mc_util_replace_begin (path, &tmp_path);
    ok = f != NULL;
    if (ok)
        ok = mc_util_replace_end (f, tmp_path, path, fputs (data, f) >= 0);
    g_free (data);

    if (!ok)
        msg = g_strdup_printf (_ ("Cannot write %s:\n%s"), path, unix_error_string (errno));
    g_free (path);
    return msg;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/**
 * Load the rules of the syntax @type, or of the one matching @file_name or @first_line when
 * @type is NULL, into @syn.  With @pnames non-NULL, only collect the names of all syntaxes.
 *
 * @param type_found the name of the syntax found, or NULL when none was; free'd by the caller.
 *                   A syntax without rules is found, but leaves @syn without rules too.
 * @param error_file where the error is, on a syntax error; free'd by the caller.
 *                   On any error, what was read is left for syntax_free().
 * @return -1 when the syntax file cannot be read, the line of the error, or 0
 */

int
syntax_load (syntax_t *syn, GPtrArray *pnames, const char *syntax_file, const char *file_name,
             const char *first_line, const char *type, char **type_found, char **error_file)
{
    int r;

    *type_found = NULL;
    *error_file = NULL;

    r = read_syntax_file (syn, pnames, syntax_file, file_name, first_line, type, type_found);
    if (r != 0)
        MC_PTR_FREE (*type_found);
    if (r > 0)
    {
        *error_file = error_file_name;
        error_file_name = NULL;
    }

    return r;
}

/* --------------------------------------------------------------------------------------------- */

void
syntax_free (syntax_t *syn)
{
    if (syn->defines != NULL)
        destroy_defines (&syn->defines);

    if (syn->rules == NULL)
        return;

    get_rule (syn, -1);

    g_ptr_array_free (syn->rules, TRUE);
    syn->rules = NULL;
    g_clear_slist (&syn->marker, g_free);
    if (syn->checkpoints != NULL)
    {
        g_array_free (syn->checkpoints, TRUE);
        syn->checkpoints = NULL;
    }
    syn->guessed = FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/** Color of the byte at @byte_index; the rules must be loaded. */

int
syntax_get_color (syntax_t *syn, off_t byte_index)
{
    if (syn->fixed)
        get_rule_fixed (syn, byte_index);
    else
        get_rule (syn, byte_index);
    return translate_rule_to_color (syn, &syn->rule);
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/**
 * A file name pattern for the Syntax file that matches files like @file_name: its extension,
 * or its whole name when it has none, as written in the file.
 */

char *
syntax_name_pattern (const char *file_name)
{
    const char *base, *dot, *p;
    GString *s;

    base = strrchr (file_name, PATH_SEP);
    base = base == NULL ? file_name : base + 1;
    dot = strrchr (base, '.');

    s = g_string_new (NULL);
    if (dot != NULL && dot != base)
        p = dot;
    else
    {
        g_string_append (s, "/");
        p = base;
    }

    for (; *p != '\0'; p++)
    {
        // a regex escape, then the file's own escape of the character
        if (strchr (".^$|()[]{}?+*\\", *p) != NULL)
            g_string_append (s, "\\\\");
        syntax_escape_char (s, *p);
    }
    g_string_append_c (s, '$');

    return g_string_free (s, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * The entry of the syntax @type: its file name pattern as written in the Syntax file, and the
 * file its rules are included from, or NULL when they are written inline.
 *
 * @return FALSE when there is no such entry; the results are free'd by the caller
 */

gboolean
syntax_entry_get (const char *type, char **pattern, char **include)
{
    char **lines;
    const char *p;
    size_t len;
    int i;

    *pattern = NULL;
    *include = NULL;

    lines = syntax_file_read_lines ();
    if (lines == NULL)
        return FALSE;

    for (i = 0; lines[i] != NULL; i++)
        if (syntax_line_is_entry (lines[i], type))
        {
            p = syntax_line_field (lines[i], 1, &len);
            *pattern = g_strndup (p, len);
            if (lines[i + 1] != NULL && syntax_line_is_word (lines[i + 1], "include")
                && (p = syntax_line_field (lines[i + 1], 1, &len)) != NULL)
                *include = g_strndup (p, len);
            break;
        }

    g_strfreev (lines);
    return *pattern != NULL;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Add the entry @name for files matching @pattern, with the rules of the syntax @type, after the
 * comments at the top of the user's Syntax file, which is made from the system one when the
 * user has none.
 *
 * @param pattern a file name pattern as written in the Syntax file
 * @return NULL, or the error message; free'd by the caller
 */

char *
syntax_associate (const char *type, const char *pattern, const char *name)
{
    char **lines;
    char *msg, *rest;
    const char *p, *inc = NULL;
    size_t len, inc_len = 0;
    int i, top = -1;

    msg = syntax_check_entry (pattern, name);
    if (msg != NULL)
        return msg;

    lines = syntax_file_read_lines ();
    if (lines == NULL)
        return g_strdup (_ ("Cannot read the Syntax file"));

    for (i = 0; lines[i] != NULL; i++)
    {
        p = syntax_line_field (lines[i], 0, &len);
        if (top < 0 && p != NULL && *p != '#')
            top = i;
        if (syntax_line_is_entry (lines[i], name))
        {
            g_strfreev (lines);
            return g_strdup_printf (_ ("There is a syntax \"%s\" already"), name);
        }
        // the rules of @type: the include right after its entry
        if (inc == NULL && syntax_line_is_entry (lines[i], type) && lines[i + 1] != NULL
            && syntax_line_is_word (lines[i + 1], "include"))
            inc = syntax_line_field (lines[i + 1], 1, &inc_len);
    }
    if (inc == NULL || top < 0)
    {
        g_strfreev (lines);
        return g_strdup_printf (_ ("No include file for \"%s\" in the Syntax file"), type);
    }

    rest = g_strdup_printf ("\ninclude %.*s\n\n%s", (int) inc_len, inc, lines[top]);
    g_free (lines[top]);
    lines[top] = syntax_entry_line (pattern, name, rest);
    g_free (rest);

    return syntax_file_write_lines (lines);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Give the entry of the syntax @type the file name @pattern and the @name, in the user's Syntax
 * file, which is made from the system one when the user has none.  Its first line pattern and
 * its rules stay.
 *
 * @param pattern a file name pattern as written in the Syntax file
 * @return NULL, or the error message; free'd by the caller
 */

char *
syntax_entry_edit (const char *type, const char *pattern, const char *name)
{
    char **lines;
    char *msg, *l;
    const char *p;
    size_t len;
    int i, entry = -1;

    msg = syntax_check_entry (pattern, name);
    if (msg != NULL)
        return msg;

    lines = syntax_file_read_lines ();
    if (lines == NULL)
        return g_strdup (_ ("Cannot read the Syntax file"));

    for (i = 0; lines[i] != NULL; i++)
        if (entry < 0 && syntax_line_is_entry (lines[i], type))
            entry = i;
        else if (strcmp (name, type) != 0 && syntax_line_is_entry (lines[i], name))
        {
            g_strfreev (lines);
            return g_strdup_printf (_ ("There is a syntax \"%s\" already"), name);
        }
    if (entry < 0)
    {
        g_strfreev (lines);
        return g_strdup_printf (_ ("No syntax \"%s\" in the Syntax file"), type);
    }

    // keep what follows the name: the first line pattern
    p = syntax_line_field (lines[entry], 2, &len);
    l = syntax_entry_line (pattern, name, p + len);
    g_free (lines[entry]);
    lines[entry] = l;

    return syntax_file_write_lines (lines);
}

/* --------------------------------------------------------------------------------------------- */
