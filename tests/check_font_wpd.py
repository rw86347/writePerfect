#!/usr/bin/env python3
"""Check the scripted Font menu wrote Times + LARGE codes."""
import sys

path = "tests/out/font.wpd"
with open(path, "rb") as f:
    data = f.read()
if data[:4] != b"\xffWPC":
    print("FAIL: not a WPD", file=sys.stderr)
    sys.exit(1)
doc = data[16:]
# C7 01 0C = Font Times 12; C3 02 = LARGE on
if b"\xc7\x01\x0c" not in doc:
    print("FAIL: missing [Font:Times 12pt]", file=sys.stderr)
    print(doc, file=sys.stderr)
    sys.exit(1)
if b"\xc3\x02" not in doc:
    print("FAIL: missing [LARGE]", file=sys.stderr)
    sys.exit(1)
if b"Hello" not in doc or b"World" not in doc:
    print("FAIL: missing text", file=sys.stderr)
    sys.exit(1)
print("check_font_wpd: ok")
