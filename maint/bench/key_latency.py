#!/usr/bin/env python3
"""Per-keystroke latency: type one character, wait for the screen update, repeat.

Keys are sent one at a time on purpose: mcedit skips redrawing while keys are
queued, so typing in a burst hides the cost of a redraw.  FILE is modified.
"""

import os
import select
import time

import _pty

binary, path, home = _pty.args_or_usage("key_latency.py MC_BINARY FILE [HOME]")
pid, fd = _pty.start(binary, ["-e", path], home)
_pty.drain_until_quiet(fd, 1.0)
latencies = []
for _ in range(10):
    t0 = time.time()
    os.write(fd, b"x")
    select.select([fd], [], [], 10)
    latencies.append(_pty.drain_until_quiet(fd, 0.15) - t0)
latencies.sort()
print(f"median {latencies[len(latencies) // 2] * 1000:.0f} ms, max {latencies[-1] * 1000:.0f} ms")
_pty.stop(pid)
