# mc slopfork

A fork of [GNU Midnight Commander](https://github.com/MidnightCommander/mc) with
improvements on top of upstream. So far they are in the editor: modern color schemes for
syntax highlighting, editing files with Windows (CRLF) line ends without damaging them,
and a smarter syntax engine.

It follows upstream `master` and adds its own commits on top. See the upstream
[README](../README) for mc itself.

It is developed with [Claude Code](https://claude.com/claude-code) — see the
[FAQ](#faq) for why it is called a slopfork and why it is a fork at all.

## Color schemes for syntax highlighting

Syntax files give each keyword and context a **scope token** that says what the text *is*
(`KEYWORD_CONTROL`, `STRING_QUOTED_DOUBLE`, `COMMENT_LINE`, …), and the colors come from a
**color scheme**. The tokens are Sublime Text's standard
[scope names](https://www.sublimetext.com/docs/scope_naming.html) (`keyword.control`,
`string.quoted.double`) in upper case, so this is not a new vocabulary.

```
keyword  whole  if   KEYWORD_CONTROL
context  " " STRING_QUOTED_DOUBLE
    keyword  \\"   CONSTANT_CHARACTER_ESCAPE
```

- **Sublime Text `.sublime-color-scheme`** files work as they are, with Sublime's selector
  rules: prefix matching, parent scopes (`string constant.character.escape`) and
  comma-separated alternatives.
- **VS Code color themes** (`.json` with `tokenColors`) work too, including `include`, so
  themes such as Dark+ and Light+ load directly.
- **21 schemes ship with the fork**, all MIT-licensed: Ayu (Dark, Light, Mirage),
  Catppuccin (Frappé, Macchiato, Mocha), Dracula and Dracula Soft, Gruvbox, Night Owl and
  Night Owl Light (each also without italics), Nord, Rosé Pine, Solarized (Dark, Light) and
  Tokyo Night (Night, Storm, Light). Licenses are in
  [`misc/syntax/schemes/LICENSE`](../misc/syntax/schemes/LICENSE).
- With no scheme selected, a built-in scheme reproduces the **classic mcedit colors**.

### Choosing a scheme

**Options → Color scheme…** (<kbd>Alt</kbd>-<kbd>Shift</kbd>-<kbd>S</kbd>) lists every
installed scheme by its display name, with its format (Sublime or VS Code) on the right.
**Moving the cursor repaints all open editor windows** with the highlighted scheme, so you
can compare themes live; <kbd>Enter</kbd> keeps the choice, <kbd>Esc</kbd> goes back. If a
scheme cannot be loaded, the classic colors stay and the reason is shown under the list.

<img width="1056" alt="Color scheme chooser" src="https://github.com/user-attachments/assets/76f02fea-8827-4120-93ed-6cc7945c13a3" />
<img width="1056" alt="Color scheme chooser with a VS Code theme" src="https://github.com/user-attachments/assets/e2c5d0e1-931c-4a7a-af90-4ec420de5ac9" />

<img width="1057" alt="mcedit with a color scheme" src="https://github.com/user-attachments/assets/725f4366-7a91-4155-b22a-1ba45f8b1341" />
<img width="1057" alt="mcedit with another color scheme" src="https://github.com/user-attachments/assets/ea5376d5-c16a-4870-a28f-0f299ce9af6a" />
<img width="1057" alt="mcedit with a third color scheme" src="https://github.com/user-attachments/assets/e621958f-e28f-4b7e-a976-4b88d5a80a68" />

- Your own schemes go to `~/.local/share/mc/syntax/schemes/`; they are found before the
  shipped ones. Display names come from `schemes.ini`
  (`[Schemes]` `night-owl=Night Owl`); a `schemes.ini` next to your schemes overrides them.
- A skin can suggest a scheme with `syntax_scheme=` in its `[editor]` section; your own
  choice wins.
- **Old syntax files with literal colors keep working unchanged**, so your own `.syntax`
  files need no changes.

### Choosing a syntax

**Options → Syntax highlighting…** works the same way: moving through the list
re-highlights the file with that syntax right away, which helps when a file was detected
as the wrong language. <kbd>Enter</kbd> keeps it, <kbd>Esc</kbd> restores the original.

## Windows (CRLF) line ends

Upstream mcedit draws the CR of every Windows line end as `^M` and treats it as an
ordinary character, so editing a CRLF file is noisy and easy to get wrong: typing at the
end of a line puts text after the CR, Enter inserts a bare LF, Backspace removes only half
of a line end. The file ends up with mixed line ends.
[Upstream ticket #1652](https://github.com/MidnightCommander/mc/issues/1652) asks for this
since 2009; earlier patches stalled because they converted line ends on load or save.

This fork **converts nothing**. Loading and saving keep every byte, and files with mixed
line ends stay mixed. Only display and editing change:

- the CR of a CRLF line end is drawn blank instead of `^M`; a lone CR still shows `^M`;
- the cursor never stops between the CR and the LF, and End stops before the CR;
- Enter copies the line end of the current line (or, at the end of the file, of the
  previous one), so new lines in a CRLF file get CRLF;
- Backspace at the start of a line and Delete at its end remove CR and LF together.

The behavior is covered by unit tests (`tests/src/editor/edit_crlf.c`).

## A smarter syntax engine

- **Nested regions**: a `nested <open> <close>` rule highlights embedded code with the
  file's own rules, at any depth. Template strings (`` `a${b ? `c${d}` : ''}` ``), JSX
  `{expressions}`, shell `$(…)` inside strings and make functions such as
  `$(patsubst …,$(filter …))` are highlighted correctly.
- **Nested includes**: syntax files can share keyword lists. The thousand shell command
  keywords live in one file, `sh-words.syntax`, used by sh, PKGBUILD, ebuild, procmail,
  Dockerfiles and makefiles.
- **Line continuations** (`\` at the end of a line) work again inside contexts, for example
  in multi-line C `#define`s.

## New and improved syntax files

- **TSX / JSX**, based on the TypeScript and HTML rules.
- **Dockerfile**: `RUN`, `CMD` and `ENTRYPOINT` are highlighted as shell commands,
  including continued lines.
- **Makefiles**: shell commands in recipes and in `$(shell …)`.
- **Batch files** (`.bat`, `.cmd`): an apostrophe no longer starts a string that swallows
  the rest of the file; `^` continuations, `!delayed!` and `%%i` variables, labels and many
  more commands.
- **TSV** files, with alternating column colors.
- Annotations and decorators in Java, JavaScript and Python; better JavaScript, shell, YAML
  and makefile rules; `.sublime-color-scheme` files are highlighted as JSON.

The scope token reference and advice for syntax authors are in
[`misc/syntax/README.txt`](../misc/syntax/README.txt) and `man mcedit`.

## Building

Build it like upstream mc:

```sh
./autogen.sh
./configure --with-screen=slang --enable-vfs-sftp
make
make install
```

On macOS with Homebrew, the dependencies are
`brew install autoconf automake libtool pkgconf gettext glib libssh2 openssl s-lang`.

## FAQ

### Why "slopfork"?

The name comes from the upstream maintainer. When the color scheme work was proposed
upstream in [PR #5189](https://github.com/MidnightCommander/mc/pull/5189), it was declined
with:

> I'm not taking any human or LLM-generated walls of code without a solid plan and
> maintenance commitment behind them. […]
>
> If you need your stuff now, then there is a new slopfork of mc; maybe you can get your
> problems solved there, or else you can keep Claude rebasing a pick of stuff you need on
> top of our tree for the time being.
>
> — [zyv, PR #5189](https://github.com/MidnightCommander/mc/pull/5189)

He probably meant someone else's fork. But turning up there with a pile of unrequested
changes would be imposing on its owner, so this slopfork was started instead, and the name
is worn with a smile. The code is written with Claude Code; every change is tested by
rendering real files in mcedit, and the editor changes have unit tests.

### Why a fork, and not pull requests upstream?

Because upstream said no, and for reasons that are fair from their side:

- **AI-assisted code is not accepted.** The maintainers do not want code they did not write
  and cannot vouch for, and they do not want to own its maintenance afterwards.
- **There is little review capacity.** mc is maintained a few hours a week without funding.
  In the same thread the maintainer explained that editor, viewer and syntax highlighting
  changes are low priority next to bug fixes, and that highlighting should only change
  after research and an overall plan.
- **Upstream wants a different path.** The stated direction is to modernize the core first
  and to rewrite the highlighter with tests before extending it; this fork improves what is
  there today.

The fork is the "keep rebasing a pick of stuff on top of our tree" option from the quote:
it stays close to upstream `master`, carries its changes as separate commits, and leaves
upstream files alone wherever it can (this README lives in `.github/` for that reason).
If upstream ever wants any of it, the commits are there.

## License

GNU General Public License v3 or later, like Midnight Commander. The bundled color schemes
are MIT-licensed by their authors; see
[`misc/syntax/schemes/LICENSE`](../misc/syntax/schemes/LICENSE).
