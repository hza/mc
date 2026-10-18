#!/usr/bin/env python3
"""Print a hash of mcedit's first screen of FILE, including colors.

Run it with two builds: equal hashes mean an identical screen.  Needs pyte.
"""

import hashlib
import os
import select
import time

import pyte

import _pty

binary, path, home = _pty.args_or_usage("screen_hash.py MC_BINARY FILE [HOME]")
cols, rows = 160, 60
pid, fd = _pty.start(binary, ["-e", path], home, rows, cols)
screen = pyte.Screen(cols, rows)
stream = pyte.ByteStream(screen)
end = time.time() + 1.5
while time.time() < end:
    ready, _, _ = select.select([fd], [], [], 0.05)
    if ready:
        try:
            stream.feed(os.read(fd, 65536))
        except OSError:
            break
_pty.stop(pid)
cells = []
for y in range(rows):
    line = screen.buffer[y]
    cells.append("".join(line[x].data + line[x].fg + line[x].bg + str(line[x].bold) for x in range(cols)))
print(hashlib.md5("\n".join(cells).encode()).hexdigest())
