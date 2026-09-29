#!/usr/bin/env python3
"""UI-action gate (standing rule 8).

Reads docs/ui-actions.md and one or more trace files written by `ggui --test --trace=FILE`
(one per shard) and fails when
  * a row's Status is not `tested`,
  * a row names no test,
  * a row names a test that is not in the traces (renamed or removed),
  * a named test failed, or
  * none of a row's tests passed (skipped for an old git or not run everywhere).

Usage:
  ui_actions_check.py trace*.json                 # gate
  ui_actions_check.py --doc docs/ui-actions.md trace*.json
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DOC = os.path.join(ROOT, "docs", "ui-actions.md")
TEST_RE = re.compile(r"`([^`]+/[^`]+)`")


def load_rows(path):
    rows, section = [], ""
    with open(path, encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if line.startswith("## "):
                section = line[3:].strip()
                continue
            if not line.startswith("| ") or line.startswith("| Action | Trigger |") or line.startswith("|---"):
                continue
            cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
            if len(cells) != 4:
                rows.append({"line": n, "section": section, "action": line, "tests": [], "status": "malformed"})
                continue
            action, trigger, tests, status = cells
            rows.append({"line": n, "section": section, "action": f"{action} ({trigger})",
                         "tests": TEST_RE.findall(tests), "status": status})
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("traces", nargs="+", help="trace JSON files from ggui --test --trace=FILE")
    ap.add_argument("--doc", default=DEFAULT_DOC)
    args = ap.parse_args()

    results = {}
    for p in args.traces:
        with open(p, encoding="utf-8") as f:
            for t in json.load(f).get("tests", []):
                name = f"{t['category']}/{t['name']}"
                status = t.get("status")
                # A shard's trace lists the other shards' tests as not-run. A test that ran
                # in two traces (e.g. two git versions) must pass in both, or be skipped in one.
                # Worst status wins; "skipped" (GG_REQUIRE_GIT on an old git) and "not-run" only
                # count when the test ran nowhere else.
                rank = {"not-run": 0, "skipped": 1, "success": 2}
                prev = results.get(name)
                if prev is None or rank.get(status, 3) > rank.get(prev, 3) or \
                        (prev == "success" and status not in ("not-run", "skipped")):
                    results[name] = status

    rows = load_rows(args.doc)
    problems = []
    for r in rows:
        where = f"{os.path.relpath(args.doc, ROOT)}:{r['line']} [{r['section']}] {r['action']}"
        if r["status"] != "tested":
            problems.append(f"{where}: status '{r['status']}'")
        if not r["tests"]:
            problems.append(f"{where}: no test named")
        for t in r["tests"]:
            if t not in results:
                problems.append(f"{where}: test not in the traces: {t}")
            elif results[t] not in ("success", "skipped", "not-run"):
                problems.append(f"{where}: test {results[t]}: {t}")
        # A test skipped for an old git (or not run) is no failure, but some named test must pass.
        if r["tests"] and not any(results.get(t) == "success" for t in r["tests"]):
            problems.append(f"{where}: no named test passed ("
                            + ", ".join(f"{t}: {results.get(t, 'missing')}" for t in r["tests"]) + ")")

    named = {t for r in rows for t in r["tests"]}
    print(f"ui actions: {len(rows)} rows, {len(named)} distinct tests named, "
          f"{len(results)} tests in the traces")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        print(f"ui-action gate: {len(problems)} problem(s) — FAILED")
        return 1
    print("ui-action gate: every row tested by a passing test — OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
