#!/usr/bin/env python3
"""Time from starting mcedit on FILE until its first screen is drawn."""

import time

import _pty

binary, path, home = _pty.args_or_usage("open_time.py MC_BINARY FILE [HOME]")
t0 = time.time()
pid, fd = _pty.start(binary, ["-e", path], home)
end = _pty.drain_until_quiet(fd, 0.5)
print(f"{end - t0:.2f} s")
_pty.stop(pid)
