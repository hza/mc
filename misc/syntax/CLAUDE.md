# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Syntax-highlighting definitions and color schemes for Midnight Commander's editor (mcedit). They are installed by the automake build into `$(pkgdatadir)/syntax`.

## Commands

**Always run `make install` after changing any `.syntax` file or `Syntax.in`.** It installs into the configured prefix.

- `make install`: install the syntax files and schemes
- New files must be added to `SYNTAXFILES` or `SCHEMEFILES` in `Makefile.am`; then run `automake misc/syntax/Makefile && ./config.status misc/syntax/Makefile` from the repo root
- `./check-tokens.py [--strict] [FILE ...]`: fail when a keyword or context foreground, or a define value, is a literal color instead of a scope token. Backgrounds may stay literal. Rules under a `# TODO scope` comment are exempt unless `--strict` is passed. A file containing the line `# check-tokens: literal colors by design` is skipped entirely (used by `syntax.syntax` and `filehighlight.syntax`, which show each color name in its own color).

To verify a change, open a sample file in `mcedit`. The `Syntax` file is rescanned whenever a new editor file is opened.

## Structure

- `Syntax`: the index. Each `file <regex> <description> [first-line regex]` entry is followed by `include <name>.syntax`. **The first match wins**, so order matters.
- `*.syntax`: per-language rules made of `context` blocks (`context default`, `context <start> <end> <color>`, `context linestart ...`, `context exclusive ...`) containing `keyword [whole|wholeleft|wholeright|linestart] <pattern> <color>`.

## mcedit syntax gotchas

See [MC_GOTCHAS.md](MC_GOTCHAS.md) for pattern syntax, the JSON key/value colouring trick, and context rules. Read it before editing `.syntax` files.

## Scope tokens (Sublime color schemes)

Instead of literal colors, a color can be an upper-case scope token:
`keyword whole if KEYWORD_CONTROL`. The token maps to the Sublime scope `keyword.control`
(lower case, `_` becomes `.`). A `.sublime-color-scheme` file then colors it.

**Choosing the scheme:**
- The user picks it in mcedit: Options → Color scheme (`alt-shift-s`). It is stored as
  `editor_syntax_color_scheme` in the `[Midnight-Commander]` ini section.
- If that option is empty, a skin's `[editor] syntax_scheme=` is used.
- If neither is set, the built-in scheme reproduces classic MC colors.
- Scheme files are looked up in `~/.local/share/mc/syntax/schemes/` first, then in the
  system `syntax/schemes/` directory.

**How tokens resolve:**
- A keyword is resolved with its context's scope as parent, so selectors such as
  `string constant.character.escape` work.
- A bg or attrs given after the token overrides the scheme.
- The scheme's global background is ignored; the editor background stays the skin's.
- Code: `src/editor/syntax_scheme.c`. Converted so far: all `*.syntax` files in this directory. Rules still using a literal color carry a `# TODO scope` comment above them.

## Converting a .syntax file to scope tokens (instructions for AI agents)

**Goal:** replace every literal color with the scope token that describes what the text
*is*. Do not choose a token for the color it produces. With no scheme configured, the file
must look exactly as before; any intended difference must be listed explicitly.

### Rules

1. **Change only color tokens.** Leave patterns, `whole`/`linestart` flags, context
   delimiters, `spellcheck`, `wholechars`, `define` and comments untouched. Process one
   file per change.
2. **Every `context` and every `keyword` that has a color gets a token.** A keyword with
   no color inherits its context's token; keep it that way.
3. **A literal background stays literal after the token.** `context " " green black`
   becomes `context " " STRING_QUOTED_DOUBLE black`. Note that this pins the background
   for every scheme. Drop the `/NN` suffixes of old colors.
4. **Use the most specific scope you are sure of.** Schemes match by prefix:
   `KEYWORD_CONTROL_IMPORT` falls back to `keyword.control`, then `keyword`. Use names
   from the vocabulary below, which follows Sublime's scope-naming guide
   (sublimetext.com/docs/scope_naming.html). Do not invent new roots. If a construct
   has no fitting name, use `…_OTHER` under the closest root.
5. **Keep tokens consistent across languages.** The same construct gets the same token
   in every file. Check `java.syntax` before inventing a variant.

### Vocabulary and built-in (classic) colors

The built-in scheme is `builtin_scheme[]` in `src/editor/syntax_scheme.c`.

| What it is | Token | Built-in color |
|---|---|---|
| Flow control: if, for, return, switch, try… | `KEYWORD_CONTROL` | yellow |
| import / include / use | `KEYWORD_CONTROL_IMPORT` | yellow |
| Other keywords (package, new, reserved words) | `KEYWORD_OTHER_*` | yellow |
| Markers in comments: BUG, FIXME, TODO, NOTE, `!!!`, `???` | `KEYWORD_OTHER_TODO` | brightred |
| Operators (`+ - = == <`) | `KEYWORD_OPERATOR` | yellow |
| Word operators (instanceof, and, not) | `KEYWORD_OPERATOR_WORD` | yellow |
| Types (int, void, char) | `STORAGE_TYPE` / `STORAGE_TYPE_PRIMITIVE` | yellow |
| class / struct / enum / interface | `STORAGE_TYPE_CLASS` | yellow |
| Modifiers (public, static, const, final) | `STORAGE_MODIFIER` | yellow |
| true / false / null / nil | `CONSTANT_LANGUAGE` | yellow |
| this / self / super | `VARIABLE_LANGUAGE` | yellow |
| Numbers | `CONSTANT_NUMERIC` | brightgreen |
| Character literals | `CONSTANT_CHARACTER` | brightgreen |
| Escapes inside strings (`\n`, `\\`) | `CONSTANT_CHARACTER_ESCAPE` | brightgreen |
| printf placeholders (`%d`, `%s`) | `CONSTANT_OTHER_PLACEHOLDER` | brightgreen |
| Other constants | `CONSTANT_OTHER` | brightgreen |
| Double-quoted string | `STRING_QUOTED_DOUBLE` | green |
| Single-quoted string | `STRING_QUOTED_SINGLE` | green |
| Other strings: heredoc, `<…>` include, backtick | `STRING_QUOTED_OTHER` / `STRING_UNQUOTED` | green |
| Regex literal | `STRING_REGEXP` | green |
| Line comment | `COMMENT_LINE` | brown |
| Block comment | `COMMENT_BLOCK` | brown |
| Doc comment | `COMMENT_BLOCK_DOCUMENTATION` | brown |
| Built-in or library functions | `SUPPORT_FUNCTION` | brightred |
| Built-in types or classes | `SUPPORT_TYPE` / `SUPPORT_CLASS` | brightred |
| Defined names: function, class, section, tag | `ENTITY_NAME_*` | brightcyan |
| Labels (`name:` jump targets, `label@`) | `ENTITY_NAME_LABEL` | brightcyan |
| Attribute names (HTML/XML) | `ENTITY_OTHER_ATTRIBUTE_NAME` | *no rule: skin default* |
| Brackets `{ } ( ) [ ]` | `PUNCTUATION_SECTION` | brightcyan |
| `,` `:` `?` | `PUNCTUATION_SEPARATOR` | brightcyan |
| `;` | `PUNCTUATION_TERMINATOR` | brightmagenta |
| Preprocessor line (context) | `META_PREPROCESSOR` | brightred |
| …strings inside it | `STRING_QUOTED_*` | red |
| …line continuation `\` inside it | `PUNCTUATION_SEPARATOR_CONTINUATION` | yellow |
| Errors, illegal constructs | `INVALID` / `INVALID_ILLEGAL` | white on red |
| Deprecated names (old commands, removed functions) | `INVALID_DEPRECATED` | white on red |
| Markup heading (man `.SH`, markdown `#`) | `MARKUP_HEADING` | brightcyan |
| Bold / italic text | `MARKUP_BOLD` / `MARKUP_ITALIC` | white / yellow |
| Links, URLs in prose | `MARKUP_UNDERLINE_LINK` | yellow |
| List bullets | `MARKUP_LIST` | yellow |
| Quoted text (`> ` in mail, markdown) | `MARKUP_QUOTE` | green |
| Verbatim / code blocks in prose | `MARKUP_RAW` | green |
| Diff: added / removed / changed lines | `MARKUP_INSERTED` / `MARKUP_DELETED` / `MARKUP_CHANGED` | brightgreen / brightred / yellow |
| Diff: hunk header `@@ … @@` / file header `---`, `+++` | `META_DIFF_RANGE` / `META_DIFF_HEADER` | brightcyan / brightmagenta |
| Mail header blocks | `META_HEADER` | *no rule: skin default* |
| Math (LaTeX `$…$`, equation environments) | `STRING_OTHER_MATH` | green |
| Background-only highlight (tab, indent markers) | `default <bg>` | the background only |
| Code embedded in another language (php `<? ?>`, inline asm) | `META_EMBEDDED` | *no rule: skin default* |
| Member access (`.length`, `.sizeof`) | `VARIABLE_OTHER_MEMBER` | *no rule: skin default* |
| Annotations, decorators (`@Override`, `@Component`) | `STORAGE_TYPE_ANNOTATION` | yellow |
| Variables (`$x`, `${x}`) | `VARIABLE_OTHER` | *no rule: skin default* |
| Parameters | `VARIABLE_PARAMETER` | *no rule: skin default* |

Inside a preprocessor context, `comment` and `punctuation` also have their own rules. In
general, a keyword's color can depend on the context it sits in.

### When the old color does not match the built-in color

The scope's meaning always wins. Never pick a wrong token to keep an old color. If
"no scheme" mode would then look different:

- **The old color was arbitrary** (e.g. a keyword colored magenta just to stand out): accept
  the new built-in color and list the change in the commit message.
- **The color matters for that language**, e.g. a markup heading: add a context-qualified
  rule to `builtin_scheme[]` (for example `"source.markdown entity.name.section"`). Do
  not change a root color, since that changes every converted language. Mention the new
  rule in the commit.
- **Unsure:** keep the literal color for that one rule and leave a `# TODO scope` comment
  next to it. Mixing literal colors and tokens in one file is supported.

### Procedure

1. Read the file and [MC_GOTCHAS.md](MC_GOTCHAS.md). List every distinct (context, color)
   pair and decide a token for each before editing.
2. Edit with a script or by hand, changing only the color column. `git diff --stat`
   should show the same number of inserted and deleted lines.
3. Check that no lower-case color is left: tokens are `[A-Z0-9_]+`, and colors are lower
   case or `#rrggbb`. Grep the context and keyword lines for the old names
   (`yellow`, `brightgreen` and so on).
4. Run `make install` (see Commands above).
5. **Verify there is no visual change** in classic mode:
   - Put the *old* file in a temporary HOME as
     `$HOME/.local/share/mc/syntax/<name>.syntax`. User copies override system ones.
   - Run `mcedit sample-file` in a pty (`TERM=xterm-256color`, a fixed window size)
     against both that HOME and an empty one, and capture the raw output.
   - The two captures must be byte-identical, apart from any differences you listed on
     purpose.
   - Note: the arrow keys in the pty are `ESC O A`, not `ESC [ A`.
6. Check against a real scheme (e.g. `Monokai`): put it in the temporary HOME's
   `syntax/schemes/` directory and set `editor_syntax_color_scheme=Monokai` in its
   `.config/mc/ini`. Confirm that keywords, strings and comments get distinct scheme
   colors.
7. Update the "Converted so far" list above.

### Don'ts

- Don't make up tokens for colors (`YELLOW`, `HIGHLIGHT`); they match no scheme rule.
- Don't write lower-case tokens: `keyword.control` is parsed as a color name and fails.
- Don't rely on the scheme background or globals: they are ignored.
- Don't add rules to `builtin_scheme[]` just to save yourself from choosing a token; it
  is shared by every language.
