#!/usr/bin/env python3
import sys

JUST, FULL = 0xC8, 3
with open("tests/out/just.wpd", "rb") as f:
    data = f.read()
if data[:4] != b"\xffWPC":
    print("FAIL: not a WPD", file=sys.stderr)
    sys.exit(1)
doc = data[16:]
if doc[:2] != bytes([JUST, FULL]):
    print("FAIL: missing [Just:Full] at start", doc[:12], file=sys.stderr)
    sys.exit(1)
if b"When in the Course" not in doc:
    print("FAIL: missing text", file=sys.stderr)
    sys.exit(1)
with open("tests/out/just.md", "r", encoding="utf-8") as f:
    md = f.read()
if 'data-wp-just="FULL"' not in md or "text-align:justify" not in md:
    print("FAIL: md missing justify\n", md, file=sys.stderr)
    sys.exit(1)
print("check_just: ok")
