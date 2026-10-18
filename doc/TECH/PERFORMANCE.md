# Performance and size: fork vs upstream

The rule for this fork: **it must not be slower than upstream mc.** This file records the
measurements behind that claim, how they were taken, and how to repeat them.

Measured on 2026-10-18.

## Setup

| | |
|---|---|
| Machine | Intel Core i7-9750H @ 2.60 GHz, macOS 15.6.1 |
| Compiler | Apple clang 17.0.0, default `./configure` options (`-O2`) |
| Upstream | `MidnightCommander/mc` `master`: `b2706063b` for the size comparison (the commit the fork is based on), `ae42724d4` (`4.8.33-797`) for the timings, installed as `mcu` |
| Fork | `next_features`: `97e6d74f7` for the size comparison and the keyword index tables, `609c08f28` for the long-line typing test |

Test files:

- **Homebrew API cache** (`~/.cache/taproom`): `formula.jws.json` (33 MB),
  `cask.jws.json` (20 MB), `formula-analytics-90d.json` (2.3 MB),
  `cask-analytics-90d.json` (1.3 MB). These are minified: each is one line. For the
  highlighting benchmarks, pretty-printed copies were used (`json.dump(..., indent=2)`; for
  the `.jws.json` files, the JSON inside the signed `payload` field), which gives
  2.0 MB / 107,144 lines, 3.5 MB / 186,122 lines and 26 MB / 904,715 lines.
- **C**: `src/editor/syntax.c` repeated to 1.9 MB and 7.6 MB.
- **Shell**: `autogen.sh` plus a loop of `grep`/`sed`/`echo` lines, repeated to 1.9 MB and
  7.6 MB. Shell has the largest keyword list (about 1,100 command names).

## Binary size

Stripped `mc`, same compiler and `configure` options.

| | upstream | fork | difference |
|---|---:|---:|---:|
| `mc`, stripped | 944,104 B | 969,760 B | **+25,656 B (+2.7%)** |
| code (`__TEXT`) | 831,488 B | 856,064 B | +24,576 B (4 KB pages) |
| data (`__DATA`) | 135,168 B | 135,168 B | 0 |

Unstripped sizes are not comparable: symbol tables made the upstream binary look larger.

Code size per object file (`size`, text):

| Object | upstream | fork | difference | What |
|---|---:|---:|---:|---|
| `syntax_scheme.o` | — | 21,200 B | +21,200 B | color schemes: Sublime and VS Code loaders, selector matching, the built-in classic scheme, a small JSON parser |
| `syntax.o` | 13,117 B | 17,832 B | +4,715 B | scope tokens, `nested` regions, include stack, keyword index, live chooser dialogs |
| `edit.o` | 35,996 B | 36,820 B | +824 B | CRLF editing rules |
| `editdraw.o` | 7,271 B | 7,383 B | +112 B | blank CR of CRLF (before the long-line redraw fix) |

Theme support costs about 21 KB of code; the CRLF handling under 1 KB.

Data files (not in the binary): the installed `share/mc/syntax` grows from 1.25 MB to
2.17 MB, of which about 670 KB are the 21 bundled color schemes (VS Code themes are
verbose JSON). Packagers can ship fewer schemes without touching the binary.

## Opening a file

`open_time.py`: start `mc -e FILE` until the first screen is drawn, three runs.

| `formula.jws.json`, 33 MB, one line | run 1 | run 2 | run 3 |
|---|---:|---:|---:|
| upstream | 0.05 s | 0.04 s | 0.04 s |
| fork | 0.09 s | 0.04 s | 0.04 s |

No difference: opening highlights only the first screen. The slower first fork run is the
disk cache warming up. (With an empty home directory, as `open_time.py` uses when none is
given, mc adds its first-start setup, about 0.2 s.)

## Highlighting a whole file

`jump_to_end.py`: Ctrl-End in mcedit, which makes the highlighter run through the whole
file. Normal editing only highlights the screen plus at most 512 bytes (the state is
checkpointed every 512 bytes), so this is a stress test.

### Keyword index (`97e6d74f7`)

Before, the highlighter scanned the context's whole string of keyword first characters for
every byte of text (about 1,100 characters for shell, 3,000 for PHP), and keywords starting
with a set, such as numbers in JSON and C, were tried at every byte. Now each context has a
256-entry table of the keywords that can start with each byte.

| File | before | after | speedup |
|---|---:|---:|---:|
| JSON 2.0 MB (cask analytics) | 0.33 s | 0.17 s | 1.9× |
| JSON 3.5 MB (formula analytics) | 0.56 s | 0.32 s | 1.8× |
| JSON 26 MB (cask index) | 3.72 s | 1.74 s | 2.1× |
| C 1.9 MB | 0.63 s | 0.18 s | 3.5× |
| C 7.6 MB | 2.47 s | 0.65 s | 3.8× |
| shell 1.9 MB | 0.91 s | 0.29 s | 3.1× |
| shell 7.6 MB | 3.35 s | 1.13 s | 3.0× |

A first version indexed only literal first characters. It gave the same 3× for shell but
nothing for JSON and C (0.62 → 0.63 s), because their number keywords start with a set;
computing the possible first bytes of sets and optional sets fixed that.

Profile before the change (macOS `sample`, shell 7.6 MB): 75% of samples in
`apply_rules_going_right` (the first-character scan), 9% in `compare_word_to_right`.

### Against upstream

| JSON 26 MB (cask index, pretty-printed) | run 1 | run 2 | run 3 |
|---|---:|---:|---:|
| upstream | 2.32 s | 2.33 s | 2.30 s |
| fork | 1.79 s | 1.86 s | 1.78 s |

The fork is about 23% faster. Before the keyword index it was about 60% slower (3.72 s):
its JSON syntax has more rules than upstream's, such as the number patterns.

### Rejected: inlined byte access and set lookup tables

After the index, the time is in the pattern matcher: `compare_word_to_right`, the
`edit_buffer_get_byte()` call per byte, and `strchr()` on word-character sets (about 10% of
samples).

| Experiment | JSON 26 MB | C 7.6 MB | shell 7.6 MB |
|---|---:|---:|---:|
| keyword index | 1.74 s | 0.65 s | 1.13 s |
| + inlined `edit_buffer_get_byte()` | 1.70 s (−2%) | 0.57 s (−12%) | 0.99 s (−12%) |

With lookup tables for the sets the best case would be about 20% in total, only visible
when highlighting multi-megabyte files at once, and the inlined access needs a copy of
`editbuffer.c`'s private buffer layout. Not kept.

## Typing in a file with a very long line

`key_latency.py`: type one character, wait until the screen is updated, ten times. (Typing
in a burst hides the cost: mcedit skips redrawing while keys are queued.)

| `formula.jws.json`, 33 MB, one line | median | worst |
|---|---:|---:|
| upstream | 43 ms | 2,046 ms |
| fork (`609c08f28`) | 1 ms | 1 ms |

Profile in upstream code (`sample` while typing): 67% in `edit_render_keypress` →
`edit_buffer_get_forward_offset`, 33% in `edit_draw_this_line` → `edit_buffer_get_eol`.
Both scanned the 33 MB line to its end on every redraw:

- after drawing a row, the start of the next row was searched for also below the last line
  of the file, where the search only returns the same offset;
- visible trailing whitespace needed the end of each drawn line; it is now looked for only
  as far as the screen can show.

Still slow on such a line, in both: End, Ctrl-End and scrolling far right, which have to
walk the line to compute columns.

## Correctness of the optimizations

Each change was checked to leave the screen unchanged: `screen_hash.py` with the build
before and after, on 78 to 79 files of different syntaxes (C, headers, shell, Perl, Python,
make, m4, man, YAML, JSON, `.po`, TS/TSX, Dockerfile, batch, TSV, PKGBUILD, CRLF files),
with a color scheme (Dracula) and with the default settings (visible tabs and trailing
whitespace). All hashes were equal. The editor unit tests (`make -C tests/src/editor check`)
pass.

## Repeating the measurements

The scripts are in [`maint/bench/`](../../maint/bench/). Each takes the `mc` binary, a file
and optionally a home directory (for settings, e.g. a chosen color scheme):

```sh
cd maint/bench
./open_time.py   /usr/local/bin/mc  FILE [HOME]   # start until the first screen
./jump_to_end.py /usr/local/bin/mc  FILE [HOME]   # Ctrl-End: highlight the whole file
./key_latency.py /usr/local/bin/mc  FILE [HOME]   # per-key latency; modifies FILE
./screen_hash.py /usr/local/bin/mc  FILE [HOME]   # hash of the first screen (needs pyte)
```

Run the same command with the upstream binary to compare. For timings, run each command
a few times and ignore the first run (disk cache).
