#!/usr/bin/env python3
"""Lists uncovered lines and branches per first-party file from an LCOV file (developer aid).
usage: uncovered.py coverage.lcov [path-substring]"""
import os
import sys
sys.path.insert(0, os.path.dirname(__file__))
from coverage_report import parse_lcov, exclusions  # noqa: E402

data = parse_lcov(sys.argv[1])
flt = sys.argv[2] if len(sys.argv) > 2 else "Source/"
for path, d in sorted(data.items()):
    if flt not in path or "/tests/" in path:
        continue
    excluded, _ = exclusions(path)
    try:
        src = open(path, encoding="utf-8", errors="replace").read().split("\n")
    except OSError:
        continue
    lines = sorted(n for n, c in d["lines"].items() if c == 0 and n not in excluded)
    branches = defaultdict = {}
    for (n, b, br), t in d["branches"].items():
        if t == 0 and n not in excluded:
            branches.setdefault(n, 0)
            branches[n] += 1
    if not lines and not branches:
        continue
    print(f"== {os.path.relpath(path)}")
    for n in sorted(set(lines) | set(branches)):
        tag = ("L" if n in lines else " ") + (f"B{branches[n]}" if n in branches else "  ")
        print(f"  {n:5d} {tag:4s} {src[n-1].strip()[:110] if n-1 < len(src) else ''}")
