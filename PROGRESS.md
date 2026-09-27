# Progress — ggui clean-room rebuild

Where the work stands and how to pick it up again. The task list with per-task evidence is
`REBUILD_TASKS.md` (`[x]` done, `[~]` partial with a Status line); the design is
`REBUILD_PLAN.md`.

## Status (2026-09-27)

- **Phases 0–2:** implemented. Functional gate green: 134/134 scenarios, 452/452 phase 0–2
  spec IDs (`docs/traceability.md`). Code gate: line 91.8 % (met), branch 76.9 % (target
  > 90 %; P2-31 stays `[~]`).
- **Phase 3:** P3-01 … P3-14 done. These cover the rewrite engine, pre-flight, first-class
  conflicts, pre-push, reword/author, duplicate/rebase, squash/split/abandon/restore,
  insert, reorder/move changes, merge into HEAD/reconcile, drag and drop, and commit with
  conflicts.
- **UI feedback round:** UF-01 … UF-09 done:
  - baselines and icon offset
  - editor-based diff views
  - hidden-by-default panels
  - graph rendering, collapsed merges, fast incremental walk and "Load more"
  - toasts and error popups instead of the error bar
- **Partial items that need outside resources:**
  - P0-02 / P0-04 / P0-05: the specs need the owner's review and freeze.
  - P0-06: retiring the old repo; a native MSVC build.
  - P0-13 / P0-14 / P1-21: CI has not been run, because no runner is reachable here.

## Next steps

1. **P3-15 … P3-19, interactive rebase** (§4.13, spec IDs `IR-*` in
   `Source/tests/spec_catalogue.txt`). Not started. Planned design:
   - Todo model in libgg (`Todo.hpp/.cpp`) so `git-gg` can share it:
     - parse and format Git's todo, with validation and autosquash
       (`fixup!`/`squash!`/`amend!` → `fixup -C`)
     - squash message assembly: Git's commented template, then `cleanup=strip`; the
       `squash!` subject line is commented out as in Git ≥ 2.32
     - engine choice with a reason (native for edit/break/exec or on request)
   - In-memory engine: map the todo to `gg::rewrite::Plan`:
     - pick/reword → Pick with a message override; squash/fixup → `Step::Kind::Squash`
     - drop → `dropped`; update-ref → `refsToSteps`
     - run it through `Actions::rewrite` (pre-flight, one update-ref, one Undo)
     - "keep committer date" needs a committer override added to `Step`/`Plan`
       (the Rewriter currently always signs with the default committer)
   - Native engine: `Source/gitgg/SequenceEditor.cpp` is a stub. It should write the
     prepared todo and messages, and run `git rebase -i` with
     `GIT_SEQUENCE_EDITOR`/`GIT_EDITOR` and `GG_OPERATION`. The stop UI builds on the
     existing toolbar state badge (`App::drawStateBadge`, `Actions::continueOperation`)
     and `detectState` (`Source/core/Readers.cpp`, which already reads
     `.git/rebase-merge/`).
   - Tests: a new `Source/tests/test_rebase_i.cpp`, including the differential test
     against `git rebase -i` on a copy.
2. **P3-20, Phase 3 gate**, then the branch-coverage push. The largest gaps are Readers,
   DiffPanel, Markers, ChangesPanel and Journal.
3. **User feedback round 2** (UF-10 … UF-25 in REBUILD_TASKS.md), not started.
4. **Phase 4** (P4-01 … P4-06).

## Working notes

- Build: `cmake --preset ninja && cmake --build build/ninja`. Tests run inside the real
  binary: `cd build/ninja && ./bin/ggui --test=FILTER --headless` (`--list-tests` lists
  them). Failure logs and screenshots go to `build/ninja/test-artifacts/`.
  `GGUI_KEEP_TEST_DIRS=1` keeps the scenario repositories.
- Mark tasks with `scripts/mark_task.py ID done|partial "status text"`. Regenerate the
  traceability matrix with `scripts/traceability.py --phase N --out docs/traceability.md
  trace.json`. Measure coverage with `NO_GATE=1 scripts/run_software_coverage.sh`.
- Test-engine pitfalls:
  - Combo labels must not contain `##`, and menu labels must not contain `/`.
  - Items inside tables or child windows need `s.child(...)` or `**/` paths.
  - Wait predicates must not `revParse` a ref that may not exist (use `gitMayFail`).
- Permissions from the owner: graph rendering and graph-loading optimizations may be reused
  from `build/ggui-src`. Otherwise the clean-room rule stands (see REBUILD_TASKS.md).
