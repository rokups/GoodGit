#!/usr/bin/env python3
"""Marks a task in REBUILD_TASKS.md: mark_task.py ID STATE "status text"
STATE: done | partial | blocked. Idempotent: replaces an earlier mark and status line."""
import re
import sys

ICON = {"done": "[x]", "partial": "[~]", "blocked": "[!]"}
path = "REBUILD_TASKS.md"
tid, state, text = sys.argv[1], sys.argv[2], sys.argv[3]
lines = open(path, encoding="utf-8").read().split("\n")
out, i, found = [], 0, False
while i < len(lines):
    line = lines[i]
    m = re.match(r"^### (\[[x~!]\] )?(" + re.escape(tid) + r")\b(.*)$", line)
    if m:
        found = True
        out.append(f"### {ICON[state]} {m.group(2)}{m.group(3)}")
        i += 1
        if i < len(lines) and lines[i].startswith("- **Status:**"):
            i += 1
            while i < len(lines) and lines[i].startswith("  ") and not lines[i].startswith("- **"):
                i += 1
        out.append(f"- **Status:** {text}")
        continue
    out.append(line)
    i += 1
if not found:
    sys.exit(f"task {tid} not found")
open(path, "w", encoding="utf-8").write("\n".join(out))
