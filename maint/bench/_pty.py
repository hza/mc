"""Run mc in a pseudo-terminal for the benchmarks in this directory."""

import fcntl
import os
import pty
import select
import struct
import sys
import tempfile
import termios
import time


def start(binary, args, home=None, rows=50, cols=160):
    """Start BINARY with ARGS on a ROWSxCOLS pty; return (pid, fd).

    HOME defaults to an empty temporary directory, so no user settings apply.
    """
    if home is None:
        home = tempfile.mkdtemp(prefix="mc-bench-")
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.update(TERM="xterm", HOME=home)
        os.execv(binary, [binary] + args)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    return pid, fd


def drain_until_quiet(fd, quiet, first_wait=30.0):
    """Read output until none arrives for QUIET seconds; return the time of the last output."""
    last = None
    while True:
        ready, _, _ = select.select([fd], [], [], quiet if last else first_wait)
        if not ready:
            return last if last else time.time()
        try:
            os.read(fd, 65536)
        except OSError:
            return last if last else time.time()
        last = time.time()


def stop(pid):
    os.kill(pid, 9)
    os.waitpid(pid, 0)


def args_or_usage(usage):
    if len(sys.argv) < 3:
        sys.exit("usage: " + usage)
    return sys.argv[1], sys.argv[2], (sys.argv[3] if len(sys.argv) > 3 else None)
