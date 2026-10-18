# mc slopfork

A fork of GNU Midnight Commander that follows upstream `master` and carries its own commits on
top. What the fork adds is described in [.github/README.md](.github/README.md).

## Rules

- Leave upstream files alone where possible. Fork docs go in `.github/README.md`, not in
  `README` / `doc/README`.
- Commit subjects: `area: imperative verb ...` (for example `filemanager: hide files listed in
  an exclude file`, `viewer: edit the shown file with F6`). No `Co-Authored-By` or other AI
  attribution lines.
- Do not push, open or edit PRs, or comment on them without asking first.

## Build and test

```sh
make -j8            # build
make -j8 install    # installs over the Homebrew mc Cellar prefix on purpose
make -j8 check      # unit tests
```

- Run `make install` after changing `.syntax` files or anything the user will try in mc.
- Maintainer mode is off, so editing a `Makefile.am` does not regenerate anything. After
  adding a source or data file, run `automake <dir>/Makefile` and
  `./config.status <dir>/Makefile`.
- Format C with `clang-format -i` (the repo has a `.clang-format`), but only on files you
  edit, and check `git diff`: it reflows untouched code in `lib/keybind.c` and the
  `panels_layout` initializer in `layout.c`. Revert those hunks.

## Trying mc from a script

Use tmux (`/usr/local/bin/tmux`). Start mc like this:

```sh
D=<scratchpad>; mkdir -p $D/tmp $D/home
tmux new-session -d -s t -x 100 -y 14 "env TERM=xterm-256color TMPDIR=$D/tmp MC_TMPDIR=$D/tmp \
  XDG_CONFIG_HOME=$D/home/cfg XDG_DATA_HOME=$D/home/data XDG_CACHE_HOME=$D/home/cache \
  ./src/mc -u <left dir> <right dir>"
tmux send-keys -t t M-F1; tmux capture-pane -p -t t
```

- `TERM=xterm-256color`: the user's `xoria256` skin stops mc at an error dialog under plain
  `xterm`.
- `TMPDIR` and `MC_TMPDIR`: the sandbox blocks the system temp dir, and mc then waits at
  "Cannot create temporary directory ... Press any key", ignoring every key you send.
- The `XDG_*` dirs keep the test from overwriting the user's mc settings.
- Type plain text first and check that it shows on the command line.

## Code map

- File manager: `src/filemanager/` (`filemanager.c` holds the menus and the command dispatch
  in `midnight_execute_cmd`, `dir.c` reads directories, `boxes.c` has the option dialogs).
- Editor: `src/editor/`, viewer: `src/viewer/`, widgets: `lib/widget/`.
- Panel options saved in the ini file: `panels_options_t` in `src/setup.h` plus a row in
  `panels_ini_options[]` in `src/setup.c`.
- User config files must be listed in `mc_config_files_reference[]` in
  `lib/mcconfig/paths.c`, or `mc_config_get_full_path()` returns NULL.

## Adding a command with a key

1. `CK_Name` in `lib/keybind.h` and `ADD_KEYMAP_NAME (Name)` in `lib/keybind.c`.
2. A default key in `src/keymap.c`, and the same line in `misc/mc.default.keymap`,
   `misc/mc.emacs.keymap` and `misc/mc.vim.keymap` (`# Name =` when it has no key).
3. A `case CK_Name:` in the dispatch and, if needed, a `menu_entry_new` in `filemanager.c`.

Terminals do not pass Command or Control+punctuation to mc. Use `alt-…` keys
(`alt-slash`, `alt-dot`).

## Exclude patterns

`src/filemanager/exclude.c` reads `~/.config/mc/exclude` (default: `misc/exclude`), matches it in
`handle_dirent()` in `dir.c`, and has the Options → Exclude patterns dialog.
