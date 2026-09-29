#!/usr/bin/env python3
"""Spec-ID traceability report and functional gate (product spec §8.2).

Reads the spec-ID catalogue (Source/tests/spec_catalogue.txt) and one or more trace files
written by `ggui --test --trace=FILE` (one per shard), then:
  * validates the catalogue (format, unique IDs, known phases);
  * rejects spec IDs declared by tests that are not in the catalogue;
  * writes a traceability matrix (Markdown) mapping every ID to its passing tests;
  * fails when a test failed, or an ID of a delivered phase (phase <= --phase) has no passing
    test. A test skipped for an old git (GG_REQUIRE_GIT, status "skipped") covers nothing: pass
    the traces of a run on a newer git too.

Usage:
  traceability.py --check                               # validate the catalogue only
  traceability.py --phase 1 --out matrix.md trace*.json # gate + matrix
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CATALOGUE = os.path.join(ROOT, "Source", "tests", "spec_catalogue.txt")
ID_RE = re.compile(r"^[A-Z0-9]+(-[A-Z0-9]+)+$")


def load_catalogue(path):
    entries, errors = {}, []
    with open(path, encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split("|")]
            if len(parts) != 4:
                errors.append(f"{path}:{n}: expected 4 fields 'ID | phase | section | description'")
                continue
            sid, phase, section, desc = parts
            if not ID_RE.match(sid):
                errors.append(f"{path}:{n}: malformed spec ID '{sid}'")
            if not phase.isdigit() or int(phase) > 4:
                errors.append(f"{path}:{n}: phase must be 0..4, got '{phase}'")
                continue
            if sid in entries:
                errors.append(f"{path}:{n}: duplicate spec ID '{sid}'")
            if not desc:
                errors.append(f"{path}:{n}: empty description for '{sid}'")
            entries[sid] = {"phase": int(phase), "section": section, "description": desc, "line": n}
    return entries, errors


def load_traces(paths):
    tests = []
    for p in paths:
        with open(p, encoding="utf-8") as f:
            data = json.load(f)
        for t in data.get("tests", []):
            t["trace"] = p
            tests.append(t)
    return tests


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("traces", nargs="*", help="trace JSON files from ggui --test --trace=FILE")
    ap.add_argument("--catalogue", default=DEFAULT_CATALOGUE)
    ap.add_argument("--phase", type=int, default=None, help="highest delivered phase to gate on")
    ap.add_argument("--out", default=None, help="write the Markdown traceability matrix here")
    ap.add_argument("--check", action="store_true", help="only validate the catalogue")
    args = ap.parse_args()

    catalogue, errors = load_catalogue(args.catalogue)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 2
    print(f"catalogue: {len(catalogue)} spec IDs "
          + ", ".join(f"phase {p}: {sum(1 for e in catalogue.values() if e['phase'] == p)}" for p in range(5)))
    if args.check:
        return 0

    tests = load_traces(args.traces)
    covered = {}      # id -> [test names] (passing)
    declared = set()
    unknown = []
    for t in tests:
        name = f"{t['category']}/{t['name']}"
        for sid in t.get("specs", []):
            declared.add(sid)
            if sid not in catalogue:
                unknown.append(f"{sid} (declared by {name}, {t.get('file')}:{t.get('line')})")
            elif t.get("status") == "success":
                covered.setdefault(sid, []).append(name)
    failed_tests = [f"{t['category']}/{t['name']}" for t in tests if t.get("status") not in ("success", "not-run", "skipped")]

    phase = args.phase if args.phase is not None else max(e["phase"] for e in catalogue.values())
    required = {sid: e for sid, e in catalogue.items() if e["phase"] <= phase}
    missing = sorted(sid for sid in required if sid not in covered)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(f"# Traceability matrix (phases 0-{phase})\n\n")
            f.write(f"Covered: {len(required) - len(missing)}/{len(required)} required IDs; "
                    f"{len(covered)}/{len(catalogue)} of the whole catalogue.\n\n")
            f.write("| Spec ID | Phase | Section | Description | Passing tests |\n|---|---|---|---|---|\n")
            for sid, e in sorted(catalogue.items(), key=lambda kv: (kv[1]["phase"], kv[1]["line"])):
                tests_text = "<br>".join(sorted(covered.get(sid, []))) or ("**MISSING**" if sid in required else "—")
                desc = e["description"].replace("|", "\\|")
                f.write(f"| {sid} | {e['phase']} | {e['section']} | {desc} | {tests_text} |\n")

    status = 0
    if unknown:
        print("unknown spec IDs declared by tests:\n  " + "\n  ".join(unknown), file=sys.stderr)
        status = 1
    if failed_tests:
        print("failing tests:\n  " + "\n  ".join(failed_tests), file=sys.stderr)
        status = 1
    if missing:
        print(f"{len(missing)} spec ID(s) of phases <= {phase} have no passing test:\n  " + "\n  ".join(missing),
              file=sys.stderr)
        status = 1
    print(f"functional gate (phase <= {phase}): {len(required) - len(missing)}/{len(required)} covered"
          + (" — OK" if status == 0 else " — FAILED"))
    return status


if __name__ == "__main__":
    sys.exit(main())
