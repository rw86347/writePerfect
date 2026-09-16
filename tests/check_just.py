#!/usr/bin/env python3
import sys

with open("tests/out/just.wpd", "rb") as f:
    data = f.read()
if data[:4] != b"\xffWPC":
    print("FAIL: not a WPD", file=sys.stderr)
    sys.exit(1)
if data[8] != 1 or data[9] != 0x0A:
    print("FAIL: not WP 5.1 document header", data[8:12], file=sys.stderr)
    sys.exit(1)
doc = data[16:]
if b"\xc8" in doc:
    print("FAIL: private C8 just code is not WP 5.1-safe", file=sys.stderr)
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
