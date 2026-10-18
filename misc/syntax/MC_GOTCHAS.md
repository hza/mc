# Midnight Commander (mcedit) syntax highlighting gotchas

Practical notes for writing and customizing mcedit `.syntax` files for Midnight Commander (mc). The main example colours JSON keys and values differently.

## Patterns are not regular expressions

| Sequence | Meaning |
|---|---|
| `*` | any run of characters on one line; not allowed as the first or last character of a pattern |
| `\{set\}` | exactly one character from `set` |
| `\[set\]` | zero or more characters from `set` |
| `\s` / `\t` / `\n` | space / tab / newline |
| `\*` `\+` `\\` | literal `*`, `+`, `\` |

Whitespace separates fields, so a literal space must be written as `\s`.

## Colour JSON keys and values differently

mcedit has no lookahead, and contexts can't nest (only a `nested` rule can switch back to the default rules, e.g. `${...}` in a template string; see `man mcedit`), and a `context indent` can end by indentation instead of a delimiter (YAML block scalars). However, keywords in `context default` take priority over a context that would start at the same spot. A wildcard keyword can therefore claim the `"key":` text before the string context starts:

```
context default
    keyword : brightcyan
    # Property names: "key":
    keyword "*"\[\s\]: lightgray

# Every other string is a value
context " " yellow
```

Result: `"property": "value"` shows the property in light grey and the value in yellow.

Caveats:
- The colon after a key takes the key colour.
- A key that contains an escaped quote (`\"`) may not match.
- Strings in arrays count as values.

### What didn't work well

`context :\[\s\]" " yellow` starts the value context at the colon. That colours the colon, any spaces and the opening quote in the value colour. A `keyword :` inside that context doesn't fix it.

## Contexts

- `context exclusive A B color` colours only the text between the delimiters, not the delimiters themselves. Use it to give quotes a different colour from the string's contents.
- The first matching context in file order wins, so put specific contexts before generic ones.
- Wildcards work in context delimiters too, e.g. `<<\[\s\]\[-\]\[\s\]\[\\\]EOF EOF` in `sh.syntax`.

## Colours

black, blue, brightblue, brightcyan, brightgreen, brightmagenta, brightred, brown, cyan, gray, green, lightgray, magenta, red, white, yellow

## Where files live

- System-wide: `$(brew --prefix mc)/share/mc/syntax/` on macOS with Homebrew
- Per user: `~/.local/share/mc/syntax/`, which takes priority if present
- The `Syntax` index maps file name patterns to `.syntax` files. The first match wins, and the index is rescanned each time a file is opened.
- Reference: `man mcedit`, section "Syntax Highlighting".
