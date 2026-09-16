#!/usr/bin/env python3
"""Confirm the F2/F4/F9/F12 script saved indent, end-field, and center codes."""
import os
import sys

WP_INDENT = 0xC2
WP_CENTER = 0xC5
WP_END_FIELD = 0xB6

here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
path = os.path.join(here, "tests", "out", "keys.wpd")
with open(path, "rb") as f:
    data = f.read()
if data[:4] != b"\xffWPC":
    print("FAIL: not a WP 5.1 file", file=sys.stderr)
    sys.exit(1)
if WP_INDENT not in data:
    print("FAIL: missing indent (F4)", file=sys.stderr)
    sys.exit(1)
if WP_END_FIELD not in data:
    print("FAIL: missing end field (F9)", file=sys.stderr)
    sys.exit(1)
if WP_CENTER not in data:
    print("FAIL: missing center (F12)", file=sys.stderr)
    sys.exit(1)
print("check_keys_wpd: indent, end field, and center present")
