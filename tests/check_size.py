#!/usr/bin/env python3
"""Wrong size then wanted size must replace. Normal must close size."""
import sys

ON, OFF = 0xC3, 0xC4
LARGE, FINE = 2, 4

with open("tests/out/size.wpd", "rb") as f:
    data = f.read()
if data[:4] != b"\xffWPC":
    print("FAIL: not a WPD", file=sys.stderr)
    sys.exit(1)
doc = data[16:]
want = (
    bytes([ON, LARGE, ON])
    + b"A"
    + bytes([OFF, LARGE, OFF])
    + b"B"
    + bytes([ON, LARGE, ON])
    + b"C"
    + bytes([OFF, LARGE, OFF, ON, FINE, ON])
    + b"D"
    + bytes([OFF, FINE, OFF])
    + b"E"
)
if doc != want:
    print("FAIL: wpd size stream", file=sys.stderr)
    print(" got", doc, file=sys.stderr)
    print("want", want, file=sys.stderr)
    sys.exit(1)
if bytes([ON, FINE]) + b"A" in doc or doc.startswith(bytes([ON, LARGE, OFF, LARGE])):
    print("FAIL: stacked unused size codes", file=sys.stderr)
    sys.exit(1)

with open("tests/out/size.md", "rb") as f:
    md = f.read().decode("utf-8")
if '<span data-wp-size="LARGE">A</span>B' not in md:
    print("FAIL: md missing LARGE A then normal B\n", md, file=sys.stderr)
    sys.exit(1)
if '<span data-wp-size="LARGE">C</span><span data-wp-size="FINE">D</span>E' not in md:
    print("FAIL: md missing C large / D fine / E normal\n", md, file=sys.stderr)
    sys.exit(1)
if md.count("data-wp-size") != 3:
    print("FAIL: md should have exactly 3 size spans\n", md, file=sys.stderr)
    sys.exit(1)
print("check_size: ok")
