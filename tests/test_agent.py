#!/usr/bin/env python3
"""Drive the headless UDP keyboard agent and check Font + reveal codes."""
import os
import socket
import subprocess
import sys
import time


def send(sock, addr, msg, timeout=2.0):
    sock.settimeout(timeout)
    sock.sendto(msg.encode("utf-8"), addr)
    try:
        data, _ = sock.recvfrom(8192)
    except socket.timeout:
        return ""
    return data.decode("utf-8", "replace")


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(here)
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()

    env = os.environ.copy()
    proc = subprocess.Popen(
        [os.path.join(here, "wp"), "--agent", str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=here,
    )
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    addr = ("127.0.0.1", port)
    try:
        pong = ""
        for _ in range(40):
            pong = send(sock, addr, "ping", timeout=0.15)
            if "pong" in pong:
                break
            if proc.poll() is not None:
                err = proc.stderr.read().decode("utf-8", "replace")
                print("FAIL: agent exited early\n", err, file=sys.stderr)
                return 1
        if "pong" not in pong:
            print("FAIL: no pong from agent", file=sys.stderr)
            return 1

        send(sock, addr, "type Hello")
        send(sock, addr, "font")
        send(sock, addr, "4")
        send(sock, addr, "3")
        send(sock, addr, "type  World")
        st = send(sock, addr, "status")
        if "Times" not in st:
            print("FAIL: status missing Times\n", st, file=sys.stderr)
            return 1
        if "Hello" not in st or "World" not in st:
            print("FAIL: status missing text\n", st, file=sys.stderr)
            return 1
        rev = send(sock, addr, "reveal")
        if "[Font:Times 12pt]" not in rev:
            print("FAIL: reveal missing Font:Times\n", rev, file=sys.stderr)
            return 1
        dump = send(sock, addr, "dump")
        if "Hello" not in dump or "World" not in dump:
            print("FAIL: dump missing text\n", dump, file=sys.stderr)
            return 1
        send(sock, addr, "f7")
        send(sock, addr, "no")
        for _ in range(40):
            if proc.poll() is not None:
                break
            time.sleep(0.05)
        if proc.poll() is None:
            proc.kill()
            proc.wait()
            print("FAIL: agent did not exit on F7", file=sys.stderr)
            return 1
        print("test_agent: ok")
        return 0
    finally:
        sock.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait()


if __name__ == "__main__":
    sys.exit(main())
