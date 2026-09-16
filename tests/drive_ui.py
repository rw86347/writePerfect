#!/usr/bin/env python3
"""Drive the wp TUI through a pty and check the main key flows."""
import errno
import fcntl
import os
import pty
import select
import struct
import sys
import termios
import time


def read_avail(fd, timeout=0.4):
    out = bytearray()
    deadline = time.time() + timeout
    while time.time() < deadline:
        remain = deadline - time.time()
        r, _, _ = select.select([fd], [], [], max(0.0, remain))
        if not r:
            if out:
                deadline = time.time() + 0.08
                continue
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
        deadline = time.time() + 0.12
    return bytes(out)


def strip_ansi(data):
    text = data.decode("latin1", "replace")
    out = []
    i = 0
    while i < len(text):
        if text[i] == "\x1b":
            i += 1
            if i < len(text) and text[i] == "[":
                i += 1
                while i < len(text) and text[i] not in "ABCDEFGHJKLMPSTXfhlmnsu":
                    i += 1
                i += 1
            elif i < len(text) and text[i] == "]":
                while i < len(text) and text[i] != "\x07":
                    i += 1
                i += 1
            else:
                i += 1
            continue
        if text[i] not in "\x00\x0e\x0f":
            out.append(text[i])
        i += 1
    return "".join(out)


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(here)
    os.makedirs("tests/out", exist_ok=True)
    save_path = "tests/out/ui.wpd"
    if os.path.exists(save_path):
        os.remove(save_path)

    exe = os.path.join(here, "wp")
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"
        os.environ["LINES"] = "25"
        os.environ["COLUMNS"] = "80"
        os.execv(exe, [exe])

    winsz = struct.pack("HHHH", 25, 80, 0, 0)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, winsz)

    time.sleep(0.2)
    screen = strip_ansi(read_avail(fd, 0.5))
    if "Doc 1" not in screen:
        print("FAIL: status line missing after start", file=sys.stderr)
        print(repr(screen[-400:]), file=sys.stderr)
        os.close(fd)
        return 1

    # Type, bold, underline
    os.write(fd, b"Hello ")
    os.write(fd, b"\x1b[17~")  # F6 bold
    os.write(fd, b"Bold")
    os.write(fd, b"\x1b[17~")  # F6 off
    os.write(fd, b" ")
    os.write(fd, b"\x1b[19~")  # F8 underline
    os.write(fd, b"Und")
    os.write(fd, b"\x1b[19~")
    os.write(fd, b"\r")
    time.sleep(0.15)
    screen = strip_ansi(read_avail(fd, 0.4))
    if "Hello" not in screen:
        print("FAIL: typed text not visible", file=sys.stderr)
        print(repr(screen[-400:]), file=sys.stderr)
        os.close(fd)
        return 1

    # Reveal Codes
    os.write(fd, b"\x1bOR")  # F3 (xterm)
    time.sleep(0.1)
    screen = strip_ansi(read_avail(fd, 0.4))
    if "Reveal Codes" not in screen and "[BOLD]" not in screen:
        # try alternate F3
        os.write(fd, b"\x1b[13~")
        time.sleep(0.1)
        screen += strip_ansi(read_avail(fd, 0.4))
    if "Reveal Codes" not in screen and "[BOLD]" not in screen:
        print("FAIL: Reveal Codes not shown", file=sys.stderr)
        print(repr(screen[-500:]), file=sys.stderr)
        os.close(fd)
        return 1

    # List Files
    os.write(fd, b"\x1b[15~")  # F5
    time.sleep(0.15)
    screen = strip_ansi(read_avail(fd, 0.5))
    if "List Files" not in screen:
        print("FAIL: List Files not shown", file=sys.stderr)
        print(repr(screen[-500:]), file=sys.stderr)
        os.close(fd)
        return 1
    os.write(fd, b"\x1bOP")  # F1 cancel
    time.sleep(0.1)
    read_avail(fd, 0.3)

    # Save
    os.write(fd, b"\x1b[21~")  # F10
    time.sleep(0.1)
    read_avail(fd, 0.3)
    os.write(fd, save_path.encode() + b"\r")
    time.sleep(0.2)
    screen = strip_ansi(read_avail(fd, 0.5))
    if "saved" not in screen.lower() and not os.path.exists(save_path):
        print("FAIL: save did not complete", file=sys.stderr)
        print(repr(screen[-500:]), file=sys.stderr)
        os.close(fd)
        return 1

    # Exit (document is clean after save)
    os.write(fd, b"\x1b[18~")  # F7
    time.sleep(0.3)

    status = 0
    for _ in range(20):
        pid_done, status = os.waitpid(pid, os.WNOHANG)
        if pid_done:
            break
        time.sleep(0.05)
    else:
        os.kill(pid, 9)
        os.waitpid(pid, 0)
        print("FAIL: F7 did not exit", file=sys.stderr)
        return 1

    os.close(fd)

    if not os.path.exists(save_path):
        print("FAIL: %s missing" % save_path, file=sys.stderr)
        return 1
    data = open(save_path, "rb").read()
    if data[:4] != b"\xffWPC":
        print("FAIL: saved file is not WP 5.1", file=sys.stderr)
        return 1
    if b"Hello" not in data or data[8] != 1 or data[9] != 0x0A:
        print("FAIL: saved payload unexpected", file=sys.stderr)
        print(data, file=sys.stderr)
        return 1
    if b"\xc3\x0cBold\xc4\x0c" not in data:
        print("FAIL: bold codes missing in save", file=sys.stderr)
        print(list(data), file=sys.stderr)
        return 1
    if b"\xc3\x0eUnd\xc4\x0e" not in data:
        print("FAIL: underline codes missing in save", file=sys.stderr)
        print(list(data), file=sys.stderr)
        return 1

    print("drive_ui: all passed (%d bytes, %s)" % (len(data), save_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
