#!/usr/bin/env python3
"""Unpack WP 5.1 LEARN.S01 / LEARN.SPN into tests/corpus (documents only)."""

from __future__ import annotations

import re
import sys
from pathlib import Path

KEEP = (".WKB", ".TUT", ".DOC", ".TST")


def parse_spn(spn: bytes) -> list[tuple[str, int, int]]:
    pat = re.compile(rb"[A-Z0-9{}_-]{1,8}\.[A-Z0-9]{3}")
    names = [(m.start(), m.group().decode()) for m in pat.finditer(spn)]
    recs = []
    for off, name in names:
        meta = spn[off : off + 33][-20:]
        unpacked = int.from_bytes(meta[0:4], "little")
        packed = int.from_bytes(meta[8:12], "little")
        recs.append((name, unpacked, packed))
    return recs


def lzss(src: bytes, destlen: int) -> bytes:
    """WordPerfect 5.1 install LZSS (4K window, flag LSB, 1=literal, len+3)."""
    n = 4096
    buf = bytearray(n)
    r = n - 16
    out = bytearray()
    i = 0

    def get() -> int | None:
        nonlocal i
        if i >= len(src):
            return None
        b = src[i]
        i += 1
        return b

    while len(out) < destlen and i < len(src):
        flags = get()
        if flags is None:
            break
        for bitn in range(8):
            if len(out) >= destlen:
                break
            if (flags >> bitn) & 1:
                c = get()
                if c is None:
                    return bytes(out)
                out.append(c)
                buf[r] = c
                r = (r + 1) % n
            else:
                a = get()
                b = get()
                if a is None or b is None:
                    return bytes(out)
                pos = a | ((b & 0xF0) << 4)
                ln = (b & 0x0F) + 3
                for k in range(ln):
                    c = buf[(pos + k) % n]
                    out.append(c)
                    buf[r] = c
                    r = (r + 1) % n
                    if len(out) >= destlen:
                        break
    return bytes(out)


def extract(s01: Path, spn: Path, dest: Path) -> int:
    recs = parse_spn(spn.read_bytes())
    data = s01.read_bytes()
    off = 16
    n = 0
    dest.mkdir(parents=True, exist_ok=True)
    for name, unpacked, packed in recs:
        blob = data[off : off + packed] if off + packed <= len(data) else b""
        off += packed
        if not name.endswith(KEEP) or len(blob) != packed:
            continue
        out = blob if unpacked == packed else lzss(blob, unpacked)
        if len(out) != unpacked or out[:4] != b"\xffWPC":
            continue
        if len(out) < 16 or out[8] != 1 or out[9] != 0x0A:
            continue
        stem = Path(name.lower()).stem
        path = dest / f"{stem}.wpd"
        i = 2
        while path.exists() and path.read_bytes() != out:
            path = dest / f"{stem}-{i}.wpd"
            i += 1
        path.write_bytes(out)
        print(path.name, unpacked)
        n += 1
    return n


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    s01 = root / "Install+Learn" / "LEARN.S01"
    if not s01.is_file():
        s01 = root.parent / "Install+Learn" / "LEARN.S01"
    spn = s01.with_name("LEARN.SPN")
    dest = Path(__file__).resolve().parent / "corpus"
    if len(sys.argv) >= 2:
        s01 = Path(sys.argv[1])
        spn = s01.with_name("LEARN.SPN")
    if len(sys.argv) >= 3:
        dest = Path(sys.argv[2])
    if not s01.is_file() or not spn.is_file():
        print("need LEARN.S01 and LEARN.SPN", file=sys.stderr)
        return 1
    print("extracted", extract(s01, spn, dest), "files to", dest)
    decl = dest.parent.parent / "Declaration.wpd"
    if decl.is_file():
        (dest / "declaration.wpd").write_bytes(decl.read_bytes())
        print("declaration.wpd")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
