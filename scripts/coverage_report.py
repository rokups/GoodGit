#!/usr/bin/env python3
"""First-party coverage report and code gate (REBUILD_PLAN §8.2, task P0-13).

Reads an LCOV tracefile (from `llvm-cov export -format=lcov` or `gcovr --lcov`), keeps only
first-party sources (Source/libgg, Source/core, Source/app, Source/gitgg; tests, third-party
code and generated fonts are excluded), applies COVERAGE_EXCL markers and gates on line and
branch coverage.

Exclusion markers (the rebuild starts with none; every marker needs a one-line reason):
    // COVERAGE_EXCL_START: <reason>
    ...
    // COVERAGE_EXCL_STOP
    code();  // COVERAGE_EXCL_LINE: <reason>
The number of markers per file may not exceed scripts/coverage_excl_allowlist.txt.
"""
import argparse
import os
import re
import sys
from collections import defaultdict

FIRST_PARTY = ("Source/libgg/", "Source/core/", "Source/app/", "Source/gitgg/")
MARKER_RE = re.compile(r"COVERAGE_EXCL_(START|STOP|LINE)(?::\s*(.*))?")


def parse_lcov(path):
    files = {}
    cur = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if line.startswith("SF:"):
                cur = files.setdefault(os.path.normpath(line[3:]), {"lines": defaultdict(int), "branches": {}})
            elif line.startswith("DA:") and cur is not None:
                n, count = line[3:].split(",")[:2]
                cur["lines"][int(n)] += int(float(count))
            elif line.startswith("BRDA:") and cur is not None:
                n, block, branch, taken = line[5:].split(",")
                key = (int(n), block, branch)
                t = 0 if taken == "-" else int(float(taken))
                cur["branches"][key] = cur["branches"].get(key, 0) + t
            elif line == "end_of_record":
                cur = None
    return files


def exclusions(source_path):
    """Returns (excluded line numbers, markers [(line, kind, reason)])."""
    excluded, markers = set(), []
    try:
        with open(source_path, encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except OSError:
        return excluded, markers
    inside = False
    for n, text in enumerate(lines, 1):
        m = MARKER_RE.search(text)
        if m:
            kind, reason = m.group(1), (m.group(2) or "").strip()
            if kind in ("START", "LINE"):
                markers.append((n, kind, reason))
            if kind == "START":
                inside = True
            elif kind == "STOP":
                inside = False
                excluded.add(n)
                continue
            elif kind == "LINE":
                excluded.add(n)
        if inside:
            excluded.add(n)
    return excluded, markers


def load_allowlist(path):
    allow = {}
    if not path or not os.path.exists(path):
        return allow
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if line:
                file_, count = line.split()[:2]
                allow[os.path.normpath(file_)] = int(count)
    return allow


def pct(hit, total):
    return 100.0 if total == 0 else 100.0 * hit / total


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lcov", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--allowlist", default=None)
    ap.add_argument("--min-line", type=float, default=90.0)
    ap.add_argument("--min-branch", type=float, default=90.0)
    ap.add_argument("--out", default=None, help="Markdown summary")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    data = parse_lcov(args.lcov)
    allow = load_allowlist(args.allowlist)

    per_dir = defaultdict(lambda: [0, 0, 0, 0])  # lines hit/total, branches hit/total
    rows, problems = [], []
    marker_counts = {}
    for path, d in sorted(data.items()):
        rel = os.path.relpath(path, root).replace(os.sep, "/")
        if not rel.startswith(FIRST_PARTY):
            continue
        excluded, markers = exclusions(path)
        if markers:
            marker_counts[rel] = len(markers)
        for n, kind, reason in markers:
            if not reason:
                problems.append(f"{rel}:{n}: COVERAGE_EXCL_{kind} needs a one-line reason")
        lines = {n: c for n, c in d["lines"].items() if n not in excluded}
        branches = {k: t for k, t in d["branches"].items() if k[0] not in excluded}
        lh, lt = sum(1 for c in lines.values() if c > 0), len(lines)
        bh, bt = sum(1 for t in branches.values() if t > 0), len(branches)
        rows.append((rel, lh, lt, bh, bt))
        top = "/".join(rel.split("/")[:2])
        acc = per_dir[top]
        acc[0] += lh
        acc[1] += lt
        acc[2] += bh
        acc[3] += bt

    for rel, count in marker_counts.items():
        allowed = allow.get(os.path.normpath(rel), 0)
        if count > allowed:
            problems.append(f"{rel}: {count} COVERAGE_EXCL marker(s), allowlist permits {allowed}")
    total_markers = sum(marker_counts.values())

    tl = [sum(r[1] for r in rows), sum(r[2] for r in rows)]
    tb = [sum(r[3] for r in rows), sum(r[4] for r in rows)]
    line_pct, branch_pct = pct(*tl), pct(*tb)

    out = ["# First-party coverage", "",
           f"Line: **{line_pct:.2f} %** ({tl[0]}/{tl[1]}), branch: **{branch_pct:.2f} %** ({tb[0]}/{tb[1]}), "
           f"COVERAGE_EXCL markers: {total_markers}", "",
           "| Directory | Line % | Branch % |", "|---|---|---|"]
    for top, (lh, lt, bh, bt) in sorted(per_dir.items()):
        out.append(f"| {top} | {pct(lh, lt):.1f} ({lh}/{lt}) | {pct(bh, bt):.1f} ({bh}/{bt}) |")
    out += ["", "| File | Line % | Branch % |", "|---|---|---|"]
    for rel, lh, lt, bh, bt in sorted(rows, key=lambda r: pct(r[1], r[2])):
        out.append(f"| {rel} | {pct(lh, lt):.1f} ({lh}/{lt}) | {pct(bh, bt):.1f} ({bh}/{bt}) |")
    text = "\n".join(out) + "\n"
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
    print(out[2])
    for top, (lh, lt, bh, bt) in sorted(per_dir.items()):
        print(f"  {top:16s} line {pct(lh, lt):6.2f} %   branch {pct(bh, bt):6.2f} %")

    status = 0
    if problems:
        print("\n".join(problems), file=sys.stderr)
        status = 1
    if line_pct <= args.min_line or branch_pct <= args.min_branch:
        if args.min_line > 0 or args.min_branch > 0:
            print(f"code gate FAILED: need > {args.min_line} % line and > {args.min_branch} % branch", file=sys.stderr)
            status = 1
    if status == 0:
        print("code gate OK")
    return status


if __name__ == "__main__":
    sys.exit(main())
