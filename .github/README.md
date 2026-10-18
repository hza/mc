# mc slopfork

A fork of [GNU Midnight Commander](https://github.com/MidnightCommander/mc) with
improvements on top of upstream: in the file manager hiding files with exclude patterns
and creating zip, 7z and tar archives from the Copy dialog, and in the editor modern color
schemes for syntax highlighting, editing files with Windows (CRLF) line ends without
damaging them, and a smarter syntax engine.

It follows upstream `master` and adds its own commits on top. See the upstream
[README](../README) for mc itself.

It is developed with [Claude Code](https://claude.com/claude-code) — see the
[FAQ](#faq) for why it is called a slopfork and why it is a fork at all.

## Hiding files with exclude patterns

Every Mac folder you have touched holds a `.DS_Store`, and USB sticks from Windows bring
`Thumbs.db`. Other file managers have long been able to hide such files: Total Commander and
Double Commander have an *ignore list*, VS Code has `files.exclude`, and Finder hides
`.DS_Store` on its own. Upstream mc can only hide all dot files or none.

This fork adds an exclude list, modelled on VS Code's `files.exclude`. Files and directories
that match it are not shown in the panels.

- **Name patterns** without `/` hide a name in every directory: `.DS_Store`, `Thumbs.db`,
  `*.pyc`. Shell wildcards work.
- **Path patterns** with `/` hide one exact place, and `~` is your home directory:
  `~/.testcontainers.properties`, `~/Library`.
- **VS Code patterns paste as they are**: a leading `**/` is ignored.

The list is kept in `~/.config/mc/exclude`, one pattern per line, with `#` for comments. A
new installation hides `.DS_Store` and `Thumbs.db`.

### Managing the list

**Options → Exclude patterns…** opens the list. The box under it shows the file under the
cursor in the file panel, or the marked files:

- **Add current** adds them as full paths, with `~` for the home directory. It is the
  default button, and after it the focus moves to **OK**, so <kbd>Enter</kbd>
  <kbd>Enter</kbd> hides the current file and saves.
- **Remove current** takes them out of the list again.
- **Add**, **Edit** and **Delete** change patterns by hand. Nothing is saved until **OK**.

<kbd>Alt</kbd>-<kbd>/</kbd> shows or hides the excluded files, like
**Options → Panel options → Use exclude file**; <kbd>Alt</kbd>-<kbd>.</kbd> still toggles dot
files. **Command → Edit exclude file** opens the file itself in the editor. The panels reload
as soon as the list changes.

## Creating archives

Upstream mc can browse into archives, but making one means the shell or a user menu
entry that runs `tar`. Total Commander packs files with <kbd>Alt</kbd>-<kbd>F5</kbd>; this
fork does it from the Copy dialog, with [libarchive](https://libarchive.org) built in, so
no `zip` or `7z` command is needed.

Press <kbd>F5</kbd> on a file or directory, then <kbd>F5</kbd> again (or **Archive (F5)**).
The **Create archive** dialog opens:

- **Archive path**: the other panel's directory plus the name of the current file, or of
  the current directory when several files are marked, with the extension of the format.
- **Format**: zip, 7z, or *Other:* tar, tar.gz, tar.bz2, tar.xz and tar.zst. Changing the
  format changes the extension in the path.
- **Compression**: Normal, Maximum, or *Other:* Store, Fastest, Fast and Ultra.
- **Move to archive** deletes the files once the archive is complete.
- **Encrypt files** protects a zip with a password; **Encrypt options…** sets it and
  chooses AES-256 or ZipCrypto (for old unzip tools). libarchive cannot encrypt 7z and tar
  cannot be encrypted, so the options are greyed out for them.

<kbd>Enter</kbd> packs with the usual progress dialog: one bar for the current file and
one for the total, counted in the original sizes, for every format. **Background (F9)**
packs in a background job and leaves mc free; **Copy (F5)** goes back to the Copy
dialog.

- Files are read and the archive is written through mc's virtual file systems, so you can
  pack from or to an SFTP or FTP panel.
- Symlinks are stored as links, not as the files they point to.
- The archive is written as `name.part` and renamed when it is complete, so an aborted or
  failed run leaves nothing half-written behind. An existing archive is only replaced
  after you confirm.

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

Two buttons under the list save the choice for files like this one, for example `.gitconfig`
as *INI-based File*. Both show the file and the rules file of the syntax (`ini.syntax`), and
take a name and a file name pattern written the way the `Syntax` file writes patterns.

- **Add entry…** puts a new entry (*.gitconfig Files*) with the rules of the chosen syntax
  after the comments at the top of the file. The pattern is prefilled with the file's
  extension, or its name when it has none.
- **Edit entry…** changes the name and the pattern of the chosen syntax's own entry. The
  pattern is prefilled with the entry's one, plus this file's when it is not in it yet.

<kbd>F4</kbd>, in the list or in these dialogs, opens the `Syntax` file in the editor instead.
Changes go to `~/.local/share/mc/syntax/Syntax`, which is copied from the system one first
if you have none; the editor and the viewer both use it from then on. Note that once you
have your own copy, changes to the system `Syntax` file no longer reach you.

**Options → Syntax file** in the editor works the same way: without your own copy it opens
the system file under your own path, and the copy is made only when you save it.

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
- **Indentation-ended contexts**: `context indent <start>` runs until a line indented no
  deeper than its start line, without consuming that line. YAML block scalars (`key: |`,
  `- >-`) take one rule at any depth, and the next key keeps its colour.
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
`brew install autoconf automake libtool pkgconf gettext glib libarchive libssh2 openssl s-lang`.
Homebrew does not put libarchive on the `pkg-config` path, so point configure at it:

```sh
PKG_CONFIG_PATH="$(brew --prefix libarchive)/lib/pkgconfig" ./configure --with-screen=slang --enable-vfs-sftp
```

On Linux install `libarchive-dev` (Debian, Ubuntu), `libarchive-devel` (Fedora) or
`libarchive` (Arch). configure finds it on its own and prints `With libarchive: yes`;
without it mc still builds, only creating archives is unavailable. `--without-libarchive`
leaves it out on purpose.

## FAQ

### How do I hide `.DS_Store` (or `Thumbs.db`, or other files) in Midnight Commander?

In **mc slopfork**, add the name to the exclude list: **Options → Exclude patterns…**, or put
one pattern per line in `~/.config/mc/exclude`. `.DS_Store` and `Thumbs.db` are hidden out of
the box, and <kbd>Alt</kbd>-<kbd>/</kbd> shows them again. See
[Hiding files with exclude patterns](#hiding-files-with-exclude-patterns).

Upstream GNU Midnight Commander has no exclude list. What it offers:

- <kbd>Alt</kbd>-<kbd>.</kbd> (or **Panel options → Show hidden files**) hides *all* dot files,
  not only `.DS_Store`.
- **Left/Right → Filter…** with "Using shell patterns" switched off accepts a regular
  expression, so `^(?!\.DS_Store$)` hides `.DS_Store`. The filter applies to one panel and is
  not saved.
- There is no `MC_IGNORE` environment variable and no ignore setting in the `ini` file;
  advice that mentions them is wrong. **Listing format…** only chooses the columns.

### How do I create a zip or tar.gz archive in Midnight Commander?

In **mc slopfork**, press <kbd>F5</kbd> twice on the files: the Create archive dialog packs
them as zip, 7z, tar, tar.gz, tar.bz2, tar.xz or tar.zst, with a progress bar and an
optional password for zip. See [Creating archives](#creating-archives).

Upstream GNU Midnight Commander has no pack dialog. The user menu (<kbd>F2</kbd>) has
entries that compress the current subdirectory to tar.gz, tar.bz2 and tar.7z by running
`tar` in the shell, without a progress bar. For anything else run the tool yourself, for
example `zip -r name.zip dir` on the command line (<kbd>Ctrl</kbd>-<kbd>Enter</kbd> puts the
current file name there).

### How do I use color themes like Dracula, Night Owl or Ayu with Midnight Commander?

Upstream GNU Midnight Commander does not support these themes. It has only **skins**, which
color the panels, menus and dialogs (**Options → Appearance…** or `mc -S <skin>`). It ships
its own skins (`modarin256`, `xoria256`, `tokyo-night-16M`, …) and cannot read VS Code or
Sublime Text themes. The colors of code in the editor are fixed inside each `.syntax` file.

**mc slopfork** adds **color schemes** for the editor, and they are real theme files. Open
**Options → Color scheme…** (<kbd>Alt</kbd>-<kbd>Shift</kbd>-<kbd>S</kbd>) in the editor and
move through the list; the open files repaint as you go. Ayu, Catppuccin, Dracula, Gruvbox,
Night Owl, Nord, Rosé Pine, Solarized and Tokyo Night ship with the fork. Skins work as in
upstream and still color the rest of mc.

For any other theme, copy its file to `~/.local/share/mc/syntax/schemes/`:

- a VS Code theme: the `.json` file with `tokenColors` from the extension's `themes/` folder;
- a Sublime Text theme: the `.sublime-color-scheme` file.

It then appears in the same list. See
[Color schemes for syntax highlighting](#color-schemes-for-syntax-highlighting).

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

He probably meant someone else's fork. But turning up there with these changes would
have meant telling its author "I was sent here because yours is the slopfork", which is not
a nice way to introduce yourself. So this slopfork was started instead, and the name is
worn with a smile. The code is written with Claude Code; every change is tested by
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
