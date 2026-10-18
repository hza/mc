# Benchmarks for mcedit

Small scripts that run `mc -e` in a pseudo-terminal and measure what a user feels:
how long a file takes to open, to highlight, and to react to a key. They also compare
screens between two builds, to check that an optimization changes nothing visible.

Results go to dated reports in [`doc/perf/`](../../doc/perf/), e.g. `perf-2026-10-18.md`.

## Requirements

- Python 3 and a Unix pseudo-terminal (macOS or Linux).
- [`pyte`](https://pypi.org/project/pyte/) for `screen_hash.py` only (`pip install pyte`).
- Two `mc` builds to compare, e.g. this fork as `mc` and upstream as `mcu`
  (built with `./configure --prefix=$HOME/.local/opt/mc-upstream` and linked).

## Scripts

Every script takes the `mc` binary, a file, and optionally a home directory:

```sh
./open_time.py   MC_BINARY FILE [HOME]
```

Without `HOME`, mc runs with a fresh, empty home directory, so your settings do not
affect the result, but mc does its first-start setup (about 0.2 s). Pass a home directory
to use settings, for example a chosen color scheme in `HOME/.config/mc/ini`:

```ini
[Midnight-Commander]
editor_syntax_color_scheme=dracula
```

| Script | Measures | Notes |
|---|---|---|
| `open_time.py` | start until the first screen is drawn | only the first screen is highlighted |
| `jump_to_end.py` | Ctrl-End: highlighting runs through the whole file | a stress test; normal editing highlights only the screen plus up to 512 bytes |
| `key_latency.py` | per-key latency: one `x` at a time, waiting for the screen | prints median and worst of 10 keys; **modifies FILE**, use a copy |
| `screen_hash.py` | hash of the first screen, with colors | equal hashes mean an identical screen |

`key_latency.py` sends keys one at a time on purpose: mcedit skips redrawing while keys
are queued, so typing in a burst hides the cost of a redraw.

`_pty.py` is the shared helper that starts mc on a 160×50 terminal (`TERM=xterm`) and
reads its output until it goes quiet.

## Comparing two builds

Timings: run each command a few times with both binaries and ignore the first run, which
fills the disk cache.

```sh
for b in mc mcu; do
    for i in 1 2 3; do ./jump_to_end.py /usr/local/bin/$b big.json; done
done
```

Screens: after changing the highlighter or the drawing code, hash many files with the old
and the new build. Any difference is a visible change.

```sh
for f in $(git ls-files | grep -E '\.(c|h|sh|py|pl|json|yml|am|1\.in)$' | head -100); do
    [ "$(./screen_hash.py /path/to/old/mc "$f")" = "$(./screen_hash.py /usr/local/bin/mc "$f")" ] \
        || echo "DIFF $f"
done
```

Run it twice: with an empty home and with one that selects a color scheme, and include
files with tabs and trailing spaces (they are drawn when visible whitespace is on).

## Test files

Large files make differences visible. Minified files are a single line, which never makes
the highlighter work through the text, so pretty-print them for highlighting benchmarks:

```sh
python3 -c 'import json,sys; json.dump(json.load(open(sys.argv[1])), open(sys.argv[2], "w"), indent=2)' \
    in.json big.json
```

Keep the minified original for `key_latency.py`: a very long line is the hard case for
redrawing. Repeat a source file to get a large file of that syntax:

```sh
python3 -c 'import sys; s=open(sys.argv[1]).read(); open(sys.argv[2],"w").write(s*(8_000_000//len(s)+1))' \
    src/editor/syntax.c big.c
```

## Finding the hot spot

On macOS, sample mc while a script runs (the pty must keep being read, or mc only waits
on `write`):

```sh
sample $(pgrep -n mc) 3
```

Look at "Sort by top of stack" for the functions that use the time. On Linux, use
`perf record -p PID` instead.
