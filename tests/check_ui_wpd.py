#!/usr/bin/env python3
import sys

path = "tests/out/ui.wpd"
data = open(path, "rb").read()
if data[:4] != b"\xffWPC":
    print("FAIL: magic", data[:4], file=sys.stderr)
    sys.exit(1)
if data[8] != 1 or data[9] != 0x0A or data[10] != 0 or data[11] != 1:
    print("FAIL: not WP 5.1 document header", data[8:12], file=sys.stderr)
    sys.exit(1)
if b"Hello" not in data:
    print("FAIL: missing Hello", file=sys.stderr)
    sys.exit(1)
if b"\xc3\x0c\xc3Bold\xc4\x0c\xc4" not in data:
    print("FAIL: missing official bold codes", list(data), file=sys.stderr)
    sys.exit(1)
if b"\xc3\x0e\xc3Und\xc4\x0e\xc4" not in data:
    print("FAIL: missing official underline codes", list(data), file=sys.stderr)
    sys.exit(1)
if data[-1] != 0x0A:
    print("FAIL: expected hard return at end", file=sys.stderr)
    sys.exit(1)
print("check_ui_wpd: %s is a WP 5.1 file (%d bytes)" % (path, len(data)))
