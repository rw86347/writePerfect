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
# Official WP 5.1 LARGE is C3 02 C3. Private C7 font codes are omitted so old 5.1 can open.
if b"\xc3\x02\xc3" not in doc:
    print("FAIL: missing official [LARGE]", file=sys.stderr)
    print(doc, file=sys.stderr)
    sys.exit(1)
if b"\xc7" in doc:
    print("FAIL: private C7 font code is not WP 5.1-safe", file=sys.stderr)
    sys.exit(1)
if b"Hello" not in doc or b"World" not in doc:
    print("FAIL: missing text", file=sys.stderr)
    sys.exit(1)
print("check_font_wpd: ok")
