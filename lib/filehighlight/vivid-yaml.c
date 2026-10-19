/*
   File highlight plugin.
   Parser for the YAML subset used by vivid's filetypes.yml and theme files.

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

#include <config.h>
#include <string.h>

#include "lib/global.h"

#include "lib/filehighlight.h"

#include "internal.h"

/*** file scope macro definitions ****************************************************************/

// The only flow mapping that vivid uses
#define MC_FHL_YAML_EMPTY_MAP "{}"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    gsize indent;
    gchar *text;
    gsize lineno;
} mc_fhl_yaml_line_t;

typedef struct
{
    GPtrArray *lines;
    gsize pos;
    gchar *error;
} mc_fhl_yaml_ctx_t;

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
mc_fhl_yaml_line_free (gpointer data)
{
    mc_fhl_yaml_line_t *line = (mc_fhl_yaml_line_t *) data;

    g_free (line->text);
    g_free (line);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_fhl_yaml_fail (mc_fhl_yaml_ctx_t *ctx, gsize lineno, const gchar *what)
{
    if (ctx->error == NULL)
        ctx->error = g_strdup_printf ("line %zu: %s", lineno, what);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
/** Remove a comment: a '#' at the start or after a blank, outside quotes. */

static void
mc_fhl_yaml_strip_comment (gchar *text)
{
    gboolean in_single = FALSE, in_double = FALSE;
    gchar *p;

    for (p = text; *p != '\0'; p++)
    {
        if (*p == '\'' && !in_double)
            in_single = !in_single;
        else if (*p == '"' && !in_single)
            in_double = !in_double;
        else if (*p == '#' && !in_single && !in_double && (p == text || g_ascii_isspace (p[-1])))
        {
            *p = '\0';
            break;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_fhl_yaml_preprocess (const gchar *text, GPtrArray *lines, gchar **error)
{
    gchar **raw;
    gsize i;

    raw = g_strsplit (text, "\n", -1);
    for (i = 0; raw[i] != NULL; i++)
    {
        gchar *line = raw[i];
        gsize indent = 0, lineno = i + 1;
        mc_fhl_yaml_line_t *item;

        if (strchr (line, '\r') != NULL)
            *strchr (line, '\r') = '\0';

        while (line[indent] == ' ')
            indent++;

        if (line[indent] == '\t')
        {
            *error = g_strdup_printf ("line %zu: tabs are not supported", lineno);
            g_strfreev (raw);
            return FALSE;
        }

        item = g_new0 (mc_fhl_yaml_line_t, 1);
        item->indent = indent;
        item->lineno = lineno;
        item->text = g_strdup (line + indent);
        mc_fhl_yaml_strip_comment (item->text);
        g_strchomp (item->text);

        if (item->text[0] == '\0' || strcmp (item->text, "---") == 0)
            mc_fhl_yaml_line_free (item);
        else
            g_ptr_array_add (lines, item);
    }
    g_strfreev (raw);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_fhl_yaml_is_seq_item (const gchar *text)
{
    return text[0] == '-' && (text[1] == '\0' || text[1] == ' ');
}

/* --------------------------------------------------------------------------------------------- */
/** Strip matching quotes around a scalar. Returns NULL for an unterminated quote. */

static gchar *
mc_fhl_yaml_unquote (const gchar *s)
{
    gsize len = strlen (s);

    if (len >= 2 && (s[0] == '"' || s[0] == '\'') && s[len - 1] == s[0])
        return g_strndup (s + 1, len - 2);

    if (s[0] == '"' || s[0] == '\'')
        return NULL;

    return g_strdup (s);
}

/* --------------------------------------------------------------------------------------------- */
/** Split "key: rest" at the first colon that is followed by a blank or the end of the line. */

static gboolean
mc_fhl_yaml_split_key (const gchar *text, gchar **key, const gchar **rest)
{
    const gchar *p = text, *colon = NULL;
    gchar *raw_key;

    if (text[0] == '"' || text[0] == '\'')
    {
        const gchar *close = strchr (text + 1, text[0]);

        if (close == NULL)
            return FALSE;
        colon = close + 1;
        while (*colon == ' ')
            colon++;
        if (*colon != ':')
            return FALSE;
        raw_key = g_strndup (text, colon - text);
    }
    else
    {
        for (p = text; *p != '\0'; p++)
            if (*p == ':' && (p[1] == ' ' || p[1] == '\0'))
            {
                colon = p;
                break;
            }
        if (colon == NULL)
            return FALSE;
        raw_key = g_strndup (text, colon - text);
    }

    *key = mc_fhl_yaml_unquote (g_strstrip (raw_key));
    g_free (raw_key);
    if (*key == NULL || (*key)[0] == '\0')
    {
        g_free (*key);
        *key = NULL;
        return FALSE;
    }

    *rest = colon + 1;
    while (**rest == ' ')
        (*rest)++;

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static mc_fhl_yaml_node_t *
mc_fhl_yaml_node_new (mc_fhl_yaml_kind_t kind)
{
    mc_fhl_yaml_node_t *node = g_new0 (mc_fhl_yaml_node_t, 1);

    node->kind = kind;
    node->keys = g_ptr_array_new_with_free_func (g_free);
    node->items = g_ptr_array_new_with_free_func ((GDestroyNotify) mc_fhl_yaml_free);

    return node;
}

/* --------------------------------------------------------------------------------------------- */
/** A value that is not a block: a scalar, "[a, b]", "[]" or "{}". */

static mc_fhl_yaml_node_t *
mc_fhl_yaml_parse_inline (mc_fhl_yaml_ctx_t *ctx, const gchar *s, gsize lineno)
{
    mc_fhl_yaml_node_t *node;

    if (strcmp (s, MC_FHL_YAML_EMPTY_MAP) == 0)
        return mc_fhl_yaml_node_new (MC_FHL_YAML_MAP);

    if (s[0] == '[')
    {
        gsize len = strlen (s);
        gchar *inner, **parts;
        gsize i;

        if (s[len - 1] != ']')
        {
            mc_fhl_yaml_fail (ctx, lineno, "unterminated flow list");
            return NULL;
        }

        node = mc_fhl_yaml_node_new (MC_FHL_YAML_SEQ);
        if (len == 2)
            return node;

        inner = g_strndup (s + 1, len - 2);
        parts = g_strsplit (inner, ",", -1);
        g_free (inner);
        for (i = 0; parts[i] != NULL; i++)
        {
            gchar *item = mc_fhl_yaml_unquote (g_strstrip (parts[i]));

            if (item == NULL || item[0] == '\0')
            {
                g_free (item);
                g_strfreev (parts);
                mc_fhl_yaml_free (node);
                mc_fhl_yaml_fail (ctx, lineno, "bad item in flow list");
                return NULL;
            }

            {
                mc_fhl_yaml_node_t *scalar = mc_fhl_yaml_node_new (MC_FHL_YAML_SCALAR);

                scalar->scalar = item;
                g_ptr_array_add (node->items, scalar);
            }
        }
        g_strfreev (parts);
        return node;
    }

    if (s[0] == '{' || s[0] == '&' || s[0] == '*' || s[0] == '!' || s[0] == '|' || s[0] == '>')
    {
        mc_fhl_yaml_fail (ctx, lineno, "this YAML syntax is not supported");
        return NULL;
    }

    {
        gchar *scalar = mc_fhl_yaml_unquote (s);

        if (scalar == NULL)
        {
            mc_fhl_yaml_fail (ctx, lineno, "unterminated quoted string");
            return NULL;
        }

        node = mc_fhl_yaml_node_new (MC_FHL_YAML_SCALAR);
        node->scalar = scalar;
        return node;
    }
}

/* --------------------------------------------------------------------------------------------- */

static mc_fhl_yaml_node_t *mc_fhl_yaml_parse_node (mc_fhl_yaml_ctx_t *ctx, gsize indent);

/* --------------------------------------------------------------------------------------------- */

static mc_fhl_yaml_node_t *
mc_fhl_yaml_parse_seq (mc_fhl_yaml_ctx_t *ctx, gsize indent)
{
    mc_fhl_yaml_node_t *node = mc_fhl_yaml_node_new (MC_FHL_YAML_SEQ);

    while (ctx->pos < ctx->lines->len)
    {
        mc_fhl_yaml_line_t *line = g_ptr_array_index (ctx->lines, ctx->pos);
        const gchar *item;
        gsize item_offset;

        if (line->indent != indent || !mc_fhl_yaml_is_seq_item (line->text))
            break;

        item = line->text + 1;
        item_offset = line->indent + 1;
        while (*item == ' ')
        {
            item++;
            item_offset++;
        }

        if (*item == '\0')
        {
            mc_fhl_yaml_line_t *next;

            ctx->pos++;
            if (ctx->pos >= ctx->lines->len)
            {
                mc_fhl_yaml_fail (ctx, line->lineno, "empty list item");
                goto fail;
            }
            next = g_ptr_array_index (ctx->lines, ctx->pos);
            if (next->indent <= indent)
            {
                mc_fhl_yaml_fail (ctx, line->lineno, "empty list item");
                goto fail;
            }
            g_ptr_array_add (node->items, mc_fhl_yaml_parse_node (ctx, next->indent));
            if (ctx->error != NULL)
                goto fail;
            continue;
        }

        {
            gchar *key;
            const gchar *rest;

            if (mc_fhl_yaml_split_key (item, &key, &rest))
            {
                // "- key: value" starts a mapping whose keys line up after the dash
                gchar *copy = g_strdup (item);

                // item points into line->text, so copy it before freeing the old text
                line->indent = item_offset;
                g_free (line->text);
                line->text = copy;
                g_free (key);

                g_ptr_array_add (node->items, mc_fhl_yaml_parse_node (ctx, item_offset));
                if (ctx->error != NULL)
                    goto fail;
                continue;
            }
        }

        {
            mc_fhl_yaml_node_t *value = mc_fhl_yaml_parse_inline (ctx, item, line->lineno);

            if (value == NULL)
                goto fail;
            g_ptr_array_add (node->items, value);
        }
        ctx->pos++;
    }

    return node;

fail:
    mc_fhl_yaml_free (node);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static mc_fhl_yaml_node_t *
mc_fhl_yaml_parse_map (mc_fhl_yaml_ctx_t *ctx, gsize indent)
{
    mc_fhl_yaml_node_t *node = mc_fhl_yaml_node_new (MC_FHL_YAML_MAP);

    while (ctx->pos < ctx->lines->len)
    {
        mc_fhl_yaml_line_t *line = g_ptr_array_index (ctx->lines, ctx->pos);
        gchar *key;
        const gchar *rest;
        mc_fhl_yaml_node_t *value;

        if (line->indent < indent)
            break;
        if (line->indent > indent)
        {
            mc_fhl_yaml_fail (ctx, line->lineno, "unexpected indentation");
            goto fail;
        }
        if (mc_fhl_yaml_is_seq_item (line->text))
        {
            mc_fhl_yaml_fail (ctx, line->lineno, "list item in a mapping");
            goto fail;
        }
        if (!mc_fhl_yaml_split_key (line->text, &key, &rest))
        {
            mc_fhl_yaml_fail (ctx, line->lineno, "expected 'key: value'");
            goto fail;
        }

        for (gsize i = 0; i < node->keys->len; i++)
            if (strcmp (g_ptr_array_index (node->keys, i), key) == 0)
            {
                g_free (key);
                mc_fhl_yaml_fail (ctx, line->lineno, "duplicate key");
                goto fail;
            }

        if (*rest != '\0')
        {
            value = mc_fhl_yaml_parse_inline (ctx, rest, line->lineno);
            ctx->pos++;
        }
        else
        {
            ctx->pos++;
            if (ctx->pos < ctx->lines->len)
            {
                mc_fhl_yaml_line_t *next = g_ptr_array_index (ctx->lines, ctx->pos);

                if (next->indent > indent
                    && (strcmp (next->text, MC_FHL_YAML_EMPTY_MAP) == 0 || next->text[0] == '['))
                {
                    // a flow value written on the line below its key
                    value = mc_fhl_yaml_parse_inline (ctx, next->text, next->lineno);
                    ctx->pos++;
                }
                else if (next->indent > indent)
                    value = mc_fhl_yaml_parse_node (ctx, next->indent);
                else if (next->indent == indent && mc_fhl_yaml_is_seq_item (next->text))
                    value = mc_fhl_yaml_parse_seq (ctx, indent);
                else
                    value = mc_fhl_yaml_node_new (MC_FHL_YAML_MAP);
            }
            else
                value = mc_fhl_yaml_node_new (MC_FHL_YAML_MAP);
        }

        if (value == NULL || ctx->error != NULL)
        {
            g_free (key);
            mc_fhl_yaml_free (value);
            goto fail;
        }

        g_ptr_array_add (node->keys, key);
        g_ptr_array_add (node->items, value);
    }

    return node;

fail:
    mc_fhl_yaml_free (node);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static mc_fhl_yaml_node_t *
mc_fhl_yaml_parse_node (mc_fhl_yaml_ctx_t *ctx, gsize indent)
{
    mc_fhl_yaml_line_t *line = g_ptr_array_index (ctx->lines, ctx->pos);

    if (mc_fhl_yaml_is_seq_item (line->text))
        return mc_fhl_yaml_parse_seq (ctx, indent);

    return mc_fhl_yaml_parse_map (ctx, indent);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
mc_fhl_yaml_free (mc_fhl_yaml_node_t *node)
{
    if (node == NULL)
        return;

    g_free (node->scalar);
    g_ptr_array_free (node->keys, TRUE);
    g_ptr_array_free (node->items, TRUE);
    g_free (node);
}

/* --------------------------------------------------------------------------------------------- */

const mc_fhl_yaml_node_t *
mc_fhl_yaml_get (const mc_fhl_yaml_node_t *map, const gchar *key)
{
    gsize i;

    if (map == NULL || map->kind != MC_FHL_YAML_MAP)
        return NULL;

    for (i = 0; i < map->keys->len; i++)
        if (strcmp (g_ptr_array_index (map->keys, i), key) == 0)
            return g_ptr_array_index (map->items, i);

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

mc_fhl_yaml_node_t *
mc_fhl_yaml_parse (const gchar *text, gchar **error)
{
    mc_fhl_yaml_ctx_t ctx;
    mc_fhl_yaml_node_t *root = NULL;

    ctx.lines = g_ptr_array_new_with_free_func (mc_fhl_yaml_line_free);
    ctx.pos = 0;
    ctx.error = NULL;

    if (mc_fhl_yaml_preprocess (text, ctx.lines, &ctx.error) && ctx.lines->len > 0)
    {
        root = mc_fhl_yaml_parse_node (
            &ctx, ((mc_fhl_yaml_line_t *) g_ptr_array_index (ctx.lines, 0))->indent);

        if (root != NULL && ctx.pos < ctx.lines->len && ctx.error == NULL)
            mc_fhl_yaml_fail (
                &ctx, ((mc_fhl_yaml_line_t *) g_ptr_array_index (ctx.lines, ctx.pos))->lineno,
                "unexpected indentation");
        if (ctx.error != NULL)
        {
            mc_fhl_yaml_free (root);
            root = NULL;
        }
    }
    else if (ctx.error == NULL)
        ctx.error = g_strdup ("empty document");

    if (error != NULL)
        *error = ctx.error;
    else
        g_free (ctx.error);

    g_ptr_array_free (ctx.lines, TRUE);

    return root;
}

/* --------------------------------------------------------------------------------------------- */
