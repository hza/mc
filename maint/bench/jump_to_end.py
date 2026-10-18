#!/usr/bin/env python3
"""Time Ctrl-End in mcedit: highlighting has to run through the whole FILE."""

import os
import select
import time

import _pty

binary, path, home = _pty.args_or_usage("jump_to_end.py MC_BINARY FILE [HOME]")
pid, fd = _pty.start(binary, ["-e", path], home)
_pty.drain_until_quiet(fd, 1.0)
t0 = time.time()
os.write(fd, b"\x1b[1;5F")  # Ctrl-End
select.select([fd], [], [], 30)
end = _pty.drain_until_quiet(fd, 0.3)
print(f"{end - t0:.2f} s")
_pty.stop(pid)
