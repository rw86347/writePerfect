#!/usr/bin/env python3
"""Start the ncurses UI, check the status line, exit with F7."""
import errno
import fcntl
import os
import pty
import select
import struct
import sys
import termios
import time


def read_avail(fd, timeout):
    out = bytearray()
    deadline = time.time() + timeout
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], max(0.0, deadline - time.time()))
        if not r:
            break
        try:
            chunk = os.read(fd, 8192)
        except OSError as e:
            if e.errno == errno.EIO:
                break
            raise
        if not chunk:
            break
        out += chunk
    return bytes(out)


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(here)
    winsz = struct.pack("HHHH", 25, 80, 0, 0)
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"
        os.environ["LINES"] = "25"
        os.environ["COLUMNS"] = "80"
        try:
            fcntl.ioctl(0, termios.TIOCSWINSZ, winsz)
        except OSError:
            pass
        os.execv(os.path.join(here, "wp"), ["wp"])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, winsz)
    time.sleep(0.4)
    raw = read_avail(fd, 1.0)
    text = raw.decode("latin1", "replace")
    if "Doc 1" not in text or "Pos" not in text:
        print("FAIL: TUI status line not drawn", file=sys.stderr)
        print(repr(text[-400:]), file=sys.stderr)
        os.kill(pid, 9)
        os.waitpid(pid, 0)
        os.close(fd)
        return 1
    if "Bold" not in text or "Search" not in text or "Center" not in text:
        print("FAIL: F-key template not drawn", file=sys.stderr)
        print(repr(text[-500:]), file=sys.stderr)
        os.kill(pid, 9)
        os.waitpid(pid, 0)
        os.close(fd)
        return 1
    os.write(fd, b"\x1b[18~")  # F7
    ok = False
    for _ in range(50):
        read_avail(fd, 0.05)
        wpid, status = os.waitpid(pid, os.WNOHANG)
        if wpid:
            ok = True
            break
        time.sleep(0.05)
    if not ok:
        os.kill(pid, 9)
        os.waitpid(pid, 0)
        os.close(fd)
        print("FAIL: TUI did not exit on F7", file=sys.stderr)
        return 1
    os.close(fd)
    print("tui_smoke: status line and F-key template present, F7 exited")
    return 0


if __name__ == "__main__":
    sys.exit(main())
