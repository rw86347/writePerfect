#!/usr/bin/env python3
"""Check tests/corpus files are WordPerfect 5.1 documents (ÿWPC, product 1, type 0x0A)."""

from __future__ import annotations

import sys
from pathlib import Path

CORPUS = Path(__file__).resolve().parent / "corpus"


def classify(data: bytes) -> str:
    if len(data) < 16 or data[:4] != b"\xffWPC":
        return "not-wpc"
    prod, typ = data[8], data[9]
    hdr = int.from_bytes(data[4:8], "little")
    if prod == 1 and typ == 0x0A:
        if 16 <= hdr <= len(data):
            return "wp51-doc"
        return "wp51-doc-bad-hdr"
    return f"wpc-prod{prod}-type{typ:#x}"


def main() -> int:
    files = sorted(p for p in CORPUS.iterdir() if p.is_file()) if CORPUS.is_dir() else []
    if not files:
        print("FAIL: tests/corpus is empty")
        return 1
    bad = 0
    docs = 0
    other = 0
    for p in files:
        if p.suffix.lower() != ".wpd":
            print(f"FAIL {p.name:20} {p.stat().st_size:6} not-wpd")
            bad += 1
            continue
        kind = classify(p.read_bytes())
        mark = "ok" if kind.startswith("wp51-doc") or kind.startswith("wpc-") else "FAIL"
        if kind == "wp51-doc":
            docs += 1
        elif kind.startswith("wpc-"):
            other += 1
        else:
            bad += 1
            mark = "FAIL"
        print(f"{mark:4} {p.name:28} {p.stat().st_size:6} {kind}")
    print(f"{docs} wp51 .wpd, {other} other .wpd, {bad} bad")
    return 1 if bad or (docs + other) == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
