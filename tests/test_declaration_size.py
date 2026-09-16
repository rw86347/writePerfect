#!/usr/bin/env python3
"""Drive size hotkeys on declaration.md and compare WPD + MD to what was typed."""
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MARK_SZ = "<<SZ>>"
MARK_NM = "<<NM>>"


def send(sock, addr, msg, timeout=2.0):
    sock.settimeout(timeout)
    sock.sendto(msg.encode("utf-8"), addr)
    try:
        data, _ = sock.recvfrom(8192)
    except socket.timeout:
        return ""
    return data.decode("utf-8", "replace")


def wait_pong(proc, sock, addr):
    for _ in range(40):
        pong = send(sock, addr, "ping", timeout=0.15)
        if "pong" in pong:
            return True
        if proc.poll() is not None:
            err = proc.stderr.read().decode("utf-8", "replace")
            print("FAIL: agent exited early\n", err, file=sys.stderr)
            return False
    print("FAIL: no pong from agent", file=sys.stderr)
    return False


def start_agent(path, port):
    return subprocess.Popen(
        [os.path.join(HERE, "wp"), "--agent", str(port), path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=HERE,
    )


def quit_agent(sock, addr, proc):
    send(sock, addr, "f7")
    send(sock, addr, "no")
    for _ in range(40):
        if proc.poll() is not None:
            return
        time.sleep(0.05)
    if proc.poll() is None:
        proc.kill()
        proc.wait()


def must(cond, msg, extra=""):
    if not cond:
        print("FAIL:", msg, extra, file=sys.stderr)
        sys.exit(1)


def save_via_prompt(sock, addr, path):
    send(sock, addr, "f10")
    send(sock, addr, "ctrl-u")
    send(sock, addr, "input " + path)


def main():
    os.chdir(HERE)
    src = os.path.join(HERE, "declaration.md")
    must(os.path.isfile(src), "declaration.md missing")
    out_dir = os.path.join(HERE, "tests", "out")
    os.makedirs(out_dir, exist_ok=True)
    wpd = os.path.join(HERE, "Declaration.wpd")
    md_out = os.path.join(out_dir, "declaration-size.md")
    wpd_out = os.path.join(out_dir, "Declaration.wpd")
    reload_md = os.path.join(out_dir, "declaration-size-reload.md")

    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()

    proc = start_agent(src, port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    addr = ("127.0.0.1", port)
    try:
        must(wait_pong(proc, sock, addr), "agent start")

        send(sock, addr, "home")
        send(sock, addr, "f2")
        send(sock, addr, "input We hold")
        st = send(sock, addr, "status")
        must("We hold" in st, "did not land on We hold", st)

        send(sock, addr, "fine")
        send(sock, addr, "large")
        send(sock, addr, "type " + MARK_SZ)
        send(sock, addr, "normsize")
        send(sock, addr, "type " + MARK_NM)

        rev = send(sock, addr, "reveal")
        flat = rev.replace("\n", "")
        must("[LARGE]" + MARK_SZ + "[large]" + MARK_NM in flat,
             "reveal should replace Fine with Large around marker", rev)
        must("[FINE]" + MARK_SZ not in rev, "Fine must not stack before Large", rev)
        must("[FINE][LARGE]" not in rev and "[LARGE][FINE]" not in rev,
             "size codes must not stack unused", rev)

        save_via_prompt(sock, addr, wpd)
        save_via_prompt(sock, addr, md_out)
        st = send(sock, addr, "status")
        must(MARK_SZ in st and MARK_NM in st, "typed markers missing from status", st)
        quit_agent(sock, addr, proc)
        if proc.poll() is None:
            print("FAIL: agent did not exit on F7", file=sys.stderr)
            return 1
    finally:
        sock.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait()

    must(os.path.isfile(wpd), "Declaration.wpd not written")
    with open(wpd, "rb") as f:
        raw = f.read()
    must(raw[:4] == b"\xffWPC", "Declaration.wpd not WPD")
    doc = raw[16:]
    i = doc.find(b"<<SZ>>")
    must(i >= 2, "SZ marker missing in wpd")
    must(doc[i - 2 : i] == bytes([0xC3, 2]), "SZ should be preceded by [LARGE] only")
    must(doc[i - 4 : i] != bytes([0xC3, 4, 0xC3, 2]), "Fine+Large stacked before SZ")
    j = doc.find(b"<<NM>>")
    must(j > i, "NM marker missing in wpd")
    must(doc[j - 2 : j] == bytes([0xC4, 2]), "NM should follow [large] off")

    with open(md_out, "r", encoding="utf-8") as f:
        md = f.read()
    must(f'<span data-wp-size="LARGE">{MARK_SZ}</span>{MARK_NM}' in md,
         "md should show Large only on SZ, then Normal NM", md)
    must(f'<span data-wp-size="FINE">{MARK_SZ}' not in md, "md must not keep Fine on SZ", md)

    with open(src, "r", encoding="utf-8") as f:
        orig = f.read()
    must("We hold these truths" in orig, "source declaration.md changed unexpectedly")
    must(MARK_SZ not in orig, "must not rewrite declaration.md")

    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    proc = start_agent(wpd, port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    addr = ("127.0.0.1", port)
    try:
        must(wait_pong(proc, sock, addr), "reload agent start")
        dump = send(sock, addr, "dump")
        must(MARK_SZ in dump and MARK_NM in dump, "reload wpd lost typed markers", dump)
        rev = send(sock, addr, "reveal")
        must("[LARGE]" + MARK_SZ + "[large]" + MARK_NM in rev.replace("\n", ""),
             "reload wpd lost size codes", rev)
        save_via_prompt(sock, addr, reload_md)
        quit_agent(sock, addr, proc)
    finally:
        sock.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait()

    with open(reload_md, "r", encoding="utf-8") as f:
        md2 = f.read()
    must(f'<span data-wp-size="LARGE">{MARK_SZ}</span>{MARK_NM}' in md2,
         "wpd→md reload lost size span", md2)

    with open(wpd, "rb") as f:
        blob = f.read()
    with open(wpd_out, "wb") as f:
        f.write(blob)

    print("test_declaration_size: ok")
    print("  typed %s as Large, %s as Normal" % (MARK_SZ, MARK_NM))
    print("  wpd", wpd)
    print("  md ", md_out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
