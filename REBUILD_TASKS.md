# ggui clean-room rebuild — implementation task list

Source: `REBUILD_PLAN.md` (2026-09-27). Tasks are in implementation order and grouped by the
plan's phases (§7). Each task lists what it depends on, the plan sections it implements, what
to build, and when it is done. A phase is finished only when its gate task passes.

**Progress legend** (updated as tasks are implemented): `[x]` done and verified locally ·
`[~]` partially done (the Status line says what is missing) · `[!]` blocked (needs something this
environment does not have). The Status line under each heading records the evidence.

---

## Standing rules (apply to every task)

These come from the plan's confirmed decisions. Every task's "done" implicitly includes them.

1. **Clean room.** Work only from `REBUILD_PLAN.md`, the per-panel UI spec and the screenshots.
   Do not read the old `ggui`/`gg` sources or gitfourchette (GPL-3). The project is GPL-2.0-only.
2. **Git transparency.** No plain `git` command may corrupt or confuse ggui's view. Nothing is
   written under `refs/` except normal branches/tags/stash that git itself uses. `.git/gg/`
   holds only the undo journal and disposable caches; deleting it must not change what any
   commit, file or conflict means.
3. **Compatibility.** Works on any repository plain git made, in any state (mid-merge,
   mid-rebase, detached, unborn, bare, linked worktree, SHA-256), and leaves it in a state
   plain git understands.
4. **Git access (G2).** Reads (snapshot, history, diff, blame, reflog, stash list,
   index/status, marker scans) use libgit2 directly. Every mutation plain git has goes through
   the `git` CLI via the libgg git runner. Rewrites git cannot do are built in memory with
   libgit2, then all ref moves go through **one** `git update-ref --stdin` transaction.
   Always parse porcelain/plumbing formats (`-z`, `--porcelain=v2`, `--format`), never
   human text. Batch git calls (Windows process cost).
5. **Responsiveness (§3.1).** The UI thread never calls libgit2 and never waits on a git
   process. `git_repository*` handles never cross threads; results cross only as immutable
   C++ values. Big results arrive in pieces; no event makes the UI do O(repo) work in one
   frame. Every long operation is cancellable. Mutations disable conflicting actions only;
   browsing keeps working on the last snapshot.
6. **No private metadata.** No wrapper API around libgit2 (helpers are fine). libgg holds
   only code shared by `ggui` and `git-gg`.
7. **Testing (§8).** Only Dear ImGui Test Engine integration tests inside the real `ggui`
   binary. No unit tests, no GoogleTest, no mocks, no test-only command backdoors. Tests
   build fixtures with plain `git`, drive the UI like a user, and assert on both UI and
   on-disk repository (`git fsck` after every mutating test). Every test declares the spec
   IDs it covers; keyboard and mouse paths count separately. Failure paths are triggered from
   the scenario, never simulated in code.
8. **Acceptance: every feature and every UI action is tested** (owner decision 2026-09-28,
   replacing the > 90 % line/branch gate). Every implemented feature has a scenario that
   checks its result (UI and on-disk repository). Every UI action the app implements is
   exercised by at least one test: menu items, context-menu items, buttons, toolbar items,
   keyboard shortcuts, drag and drop, dialog controls and options. A feature or action
   counts as done only when its test exists and passes. Line/branch coverage is still
   measured, but only as a report that points at untested code, not as a gate. Any
   `COVERAGE_EXCL` still needs a one-line reason and an allowlist update.
9. **Platforms.** Linux and Windows (MinGW-static and MSVC) for the first release. macOS
   is out of scope.
10. **UI wording.** Git terms: HEAD, branch, commit, staged, unstaged. Never "@ is a change you
    edit", change IDs, aliases, workspaces (except where §4 keeps a label).

## Working notes (for resuming work)

- **Build and test:**
  - Build: `cmake --preset ninja && cmake --build build/ninja`.
  - Tests run inside the real binary: `cd build/ninja && ./bin/ggui --test=FILTER --headless`
    (`--list-tests` lists them).
  - Without `--shard` (or with N = 1) the filter is the test engine's and matches test names
    only. With `--shard=I/N`, N > 1, it matches substrings of `category/name`: run every shard
    I = 0 … N-1 to get all matches (e.g. `--test=ui/ --shard=0/4` … `--shard=3/4`).
  - Failure logs and screenshots go to `build/ninja/test-artifacts/`.
  - `GGUI_KEEP_TEST_DIRS=1` keeps the scenario repositories.
- **Scripts:**
  - Mark tasks: `scripts/mark_task.py ID done|partial "status text"`.
  - Regenerate the traceability matrix:
    `scripts/traceability.py --phase N --out docs/traceability.md trace.json`.
  - Measure coverage: `NO_GATE=1 scripts/run_software_coverage.sh`.
- **Test-engine pitfalls:**
  - Combo labels must not contain `##`, and menu labels must not contain `/`.
  - Items inside tables or child windows need `s.child(...)` or `**/` paths.
  - Wait predicates must not `revParse` a ref that may not exist (use `gitMayFail`).
- **Clean-room exception:** the owner allows reusing graph rendering and graph-loading
  optimizations from `build/ggui-src`. Otherwise rule 1 stands.

---

## Phase 0 — Spec and harness

### [!] P0-01 Verify inputs against the latest upstream
- **Status:** Blocked (2026-09-27): no SSH access to the upstream remotes, and neither `gg` nor a git checkout of `ggui` is available here (`~/src/projects/ggui` is not a git repository and must not be read, per the clean-room rule). The plan still names `ggui@e21feb2` / `gg@03dfdac`; someone with access must pull, diff and fold any behaviour changes into §4.
- **Depends on:** —
- **Refs:** header of plan
- **Do:**
  - Re-run `git pull` for `gg` and `ggui` from an environment with SSH access.
  - Diff against the analysed commits (`ggui@e21feb2`, `gg@03dfdac`) and list any user-facing
    behavior added or changed since.
  - Fold those changes into `REBUILD_PLAN.md` (§4 features, §2.2 pins).
- **Done when:** the plan names the new analysed commits and any behavior delta is in §4.

### [~] P0-02 Freeze the per-panel UI spec
- **Status:** Draft written: `docs/spec/ui-spec.md` (every panel, menu, toolbar item, context menu, dialog and state in Git wording, K/M/N/D legend, stable ImGui IDs). Missing: screenshots of the pre-rebuild app (not available in this clean-room environment; the suite's failure captures give screenshots of the rebuilt app instead) and the review/freeze by the project owner.
- **Depends on:** P0-01
- **Refs:** §7 Phase 0, §4, §10 (clean-room licensing)
- **Do:**
  - Capture screenshots of today's app: welcome, main window with default dock layout, each
    panel, each menu, each context menu, each dialog, toolbar states, error banner, settings.
  - Write a per-panel UI spec (layout, labels, shortcuts, context menus, dialogs, states)
    in Git wording, applying the K/M/N/D legend from §4.
  - Mark this spec plus `REBUILD_PLAN.md` as the only inputs for implementers.
- **Done when:** the spec is reviewed and frozen, and it covers every §4 bullet.

### [x] P0-03 Spec-ID catalogue for §4
- **Status:** Done: `Source/tests/spec_catalogue.txt` (615 IDs, phase-tagged, keyboard and mouse paths split); `scripts/traceability.py --check` validates it (catalogue job in CI).
- **Depends on:** P0-02
- **Refs:** §8.2
- **Do:**
  - Assign a stable spec ID to every feature bullet, menu item, shortcut, context-menu entry,
    dialog and option in §4 (e.g. `HIST-KEY-N`, `STASH-POP-INDEX`). Split keyboard and mouse
    paths into separate IDs where both exist.
  - Store the catalogue in a machine-readable file in the test sources, tagged with the phase
    that delivers each ID.
- **Done when:** every §4 item maps to ≥ 1 ID and the file parses in CI.

### [~] P0-04 Formal spec: first-class conflict marker grammar (M1)
- **Status:** Written: `docs/spec/conflict-markers.md` covers diff3, the N-sided extended form, marker length, in-band no-EOL/CRLF/empty sides, well-formed-only rule, `gg-conflicts=false`, the term algebra, no nesting and Git regions around ggui regions, with worked examples E1–E11. Awaiting review before it counts as frozen.
- **Depends on:** P0-02
- **Refs:** §4.10 marker format, §5 M1/K1, §10
- **Do:** write the formal grammar and semantics, covering:
  - two-sided regions: exact Git diff3 (`<<<<<<<` A / `|||||||` base / `=======` / B /
    `>>>>>>>`), base mandatory
  - N-sided extended form: alternating `+++++++` side and `-------` base sections, side
    count in the opening label
  - marker length ≥ 7 honoring `conflict-marker-size`; automatic longer run when content has
    marker-like lines; the opening marker fixes the length for the region
  - in-band encoding of missing final newline on a side, CRLF vs LF sides, empty sides
  - "conflicted ⇔ at least one well-formed region"; malformed/partial markers are text
  - `gg-conflicts=false` opt-out attribute
  - N-way term algebra: parse each version into terms (plain file = one term), combine,
    simplify (matching removes/adds cancel), write back as plain / diff3 / extended
  - no nesting in files ggui writes
  - precise treatment of a file where plain Git added its own conflict regions around or
    next to ggui regions (native conflict first; remaining ggui regions become first-class
    again after resolution)
- **Done when:** the spec has worked examples for every case above, reviewed.

### [~] P0-05 Formal spec: undo journal format (U1)
- **Status:** Written: `docs/spec/undo-journal.md` (JSON Lines records, header version, per-worktree HEAD keys in one shared journal, locking, torn-line handling, grouping and `GG_OPERATION`, undo/redo selection and refusal rules, gc via reflogs). Awaiting review.
- **Depends on:** P0-02
- **Refs:** §5 U1, §4.12 B
- **Do:** specify `.git/gg/journal`:
  - location: per worktree for HEAD, shared for other refs; linked-worktree layout
  - entry = one operation: id, source (ggui / git-gg / `git <command>`), label, timestamp,
    before/after of every touched ref (HEAD, branches, tags, `refs/stash`, other
    reflog-backed refs), index tree when known, rewrite mappings, parent/grouping info
  - append-only writes, locking rules matching git ref locks, torn/corrupt entries skipped
  - undo = new operation restoring before values; redo = undo of the undo
  - refusal rules (refs no longer at "after"; worktree would lose data → offer stash)
  - `GG_OPERATION=<id>` grouping and how hook-reported updates join an open operation
  - gc protection via ref reflogs only (no private refs)
- **Done when:** reviewed; includes versioning so later format changes are detectable.

### [~] P0-06 New repository skeleton and build
- **Status:** Tree created: CMake + CPM with every pin from §2.2, libgit2 options (SHA-256, OpenSSL-Dynamic/Schannel, ssh exec, `cmake/libgit2-win32-no-console.patch` adding CREATE_NO_WINDOW), targets `libgg`/`ggui_core`/`ggui`/`git-gg`, fonts via `cmake/EmbedFont.cmake` (.incbin on GCC/Clang incl. MinGW, generated array on MSVC), `.rc` + icon, `$ORIGIN` rpath, presets ninja/ninja-debug/coverage/coverage-gcov/release/mingw-x64-static/msvc-x64 plus mingw-x64-cross (Linux-hosted), `-Werror`. Verified: Linux gcc 16 and clang 22; MinGW-w64 cross build links `ggui.exe` and `git-gg.exe` (both run under wine: `--help`, `--list-tests`). Not done: retiring the `gg` repository (not available here, outward-facing); native MSVC build not run (no Windows host).
- **Depends on:** P0-01
- **Refs:** §2.2, §3, §7 Phase 0, §9
- **Do:**
  - Retire the `gg` repository (archive it; note it in its README).
  - Create the new `ggui` tree on the CMake + CPM.cmake skeleton with the same pins:
    libgit2 v1.9.6, CLI11 v2.6.2, SDL3 release-3.4.4, Dear ImGui 84a9d53 (docking),
    ImGuiColorTextEdit fa6fd43 (+ hash-stamped patch re-apply), IconFontCppHeaders 4577f2f,
    nativefiledialog-extended 1.3.0 (`NFD_PORTAL ON`), spdlog v1.17.0, efsw 1.6.3,
    nlohmann/json 3.12.0, imgui_test_engine 3fff435 + stb f1c79c0. No GoogleTest.
  - libgit2 options carried over unchanged: `BUILD_TESTS/CLI/EXAMPLES/FUZZERS OFF`,
    `EXPERIMENTAL_SHA256 ON` + `GIT_EXPERIMENTAL_SHA256`, HTTPS OpenSSL-Dynamic (Schannel on
    Windows), `USE_SSH exec`, Windows patch hiding child consoles; static link by default;
    link the `libgit2package` CPM target.
  - Targets: `libgg` (internal static, not installed), `ggui_core`, `ggui`, `git-gg`
    (CLI11, no SDL/ImGui), test sources compiled into `ggui` behind
    `GGUI_ENABLE_IMGUI_TEST_ENGINE`.
  - ImGui static lib with `IMGUI_USER_CONFIG=Source/imconfig.h`, SDL3 + SDLGPU3 backends,
    `imgui_stdlib`.
  - Embedded fonts via `cmake/EmbedFont.cmake`: MaterialSymbolsOutlined, NotoSansMono (UI),
    JetBrainsMono (diff).
  - Windows `.rc` + icon, `WIN32_EXECUTABLE`; `$ORIGIN` rpath on Linux.
  - Presets: Ninja, `mingw-x64-static` (fully static, Schannel), `msvc-x64`; coverage
    options; `-Werror`.
  - Nothing from the removal checklist (§9): no `find_package(gg)`, no `../gg` fallback, no
    `gg::gg`, no gg shared libs installed.
- **Done when:** all presets build empty `ggui` and `git-gg` executables on Linux and Windows.

### [x] P0-07 Minimal app and test-engine wiring
- **Status:** Done: SDL3 + SDL_GPU + ImGui docking app (`Source/app/main.cpp`, `platform/Platform.cpp`), offscreen render target for screenshots and `--headless`, test engine in the real binary (`--test`, `--test=FILTER`, `--smoke`, `--shard=I/N`, `--trace=FILE`, exit code = result), spdlog with SPDLOG_LEVEL and GGUI_LOG_FILE, test hooks (UI-thread switch, slow-git latency, command log). `ggui --smoke` and the suite pass locally.
- **Depends on:** P0-06
- **Refs:** §4.1 (logging, `--test`), §8.1, §8.3
- **Do:**
  - Minimal `ggui` main: SDL3 window, SDL_GPU renderer, ImGui docking, one frame loop.
  - Integrate imgui_test_engine into the real binary: `--test`, `--test=<filter>`, `--smoke`;
    exit code reflects results; test engine capture for screenshots on failure.
  - spdlog with env-driven levels and `GGUI_LOG_FILE`.
  - Test-only app hooks, and nothing more: stable ImGui IDs, UI-thread assertion switch
    (wired later in P1-01), slow-git latency switch, frame-time probe.
  - Failure output: screenshot, app log, git command log.
- **Done when:** `ggui --smoke` and a trivial `ggui --test` pass in CI.

### [x] P0-08 Test isolation environment
- **Status:** Done: per-test HOME, XDG_CONFIG_HOME, GIT_CONFIG_GLOBAL, GIT_CONFIG_NOSYSTEM, GGUI_PREF_PATH and PATH (git-gg next to ggui); libgit2 search paths reset. Test `harness/isolation…` (HARNESS-ISOLATION) passes.
- **Depends on:** P0-07
- **Refs:** §8.1
- **Do:** each test runs with a temp directory and an isolated `HOME`, `XDG_CONFIG_HOME`,
  `GIT_CONFIG_GLOBAL`, preferences directory (`SDL_GetPrefPath` override) and a `PATH` that
  contains the `git-gg` under test. No network access.
- **Done when:** a test proves that user-level git config and ggui settings are not read.

### [x] P0-09 Fixture builder library
- **Status:** Done: `Source/tests/Fixtures.cpp` has 22 recipes (linear, merges, many refs, worktrees incl. locked/stale, remote with ahead/behind, bare, SHA-256, unborn, mid-merge/rebase -i/rebase apply/cherry-pick/revert, bisect, submodules, filter-based LFS stand-in, CRLF/no-EOL/binary/marker-like text, 2- and 3-sided conflicts, working changes, stashes), plus `startGitDaemon` and an SSH shim. All pass `git fsck` (HARNESS-FIXTURES).
- **Depends on:** P0-08
- **Refs:** §8.3, §7 Phase 0
- **Do:** scripted repository recipes built with plain `git` in the test sources:
  - linear, merges, many refs, linked worktrees, bare remotes (local `file://`)
  - mid-merge, mid-rebase (interactive and apply), mid-cherry-pick, mid-revert, bisecting
  - unborn HEAD, bare repo, SHA-256 repo, submodules
  - LFS with a local test filter
  - CRLF and no-final-newline text, binary files, files with marker-like text
  - conflicted commits with 2 and N sides (hand-crafted per P0-04 until ggui can make them)
  - local `git daemon` / SSH shim for transport tests
- **Done when:** every recipe builds and passes `git fsck`.

### [x] P0-10 Assertion helpers
- **Status:** Done: UI readers (`itemExists`, `itemLabel`, `comboSelect`, `contextMenu`, `waitUntil`, `waitIdle`, `openRepository`), repo readers via plain git, post-test `git fsck --strict` + plain-git validity hook, git/git-gg step helpers, seeded RNG (seed logged, GGUI_TEST_SEED replays). Used by `harness/assertion helpers`.
- **Depends on:** P0-09
- **Refs:** §8.3
- **Do:**
  - UI readers: visible rows, labels, badges, dialog state, error banner.
  - Repo readers via plain git: `fsck`, `status --porcelain=v2 -z`, `for-each-ref`,
    `log --format`, `cat-file`, file bytes on disk.
  - A post-test hook that asserts `git fsck` passes and the repo state is plain-git valid
    for every mutating test.
  - A "run git / git gg as a test step" helper.
  - A seeded randomizer that logs failing seeds for promotion to fixed cases.
- **Done when:** used by at least one sample test.

### [x] P0-11 Large-repository fixture
- **Status:** Done: `Source/tests/LargeFixture.cpp` builds 100,000 commits (with merges), 5,200 refs and more than 50k files through `git fast-import` in about 1.5 min, cached under GGUI_FIXTURE_CACHE; a cached run takes 0.25 s. CI caches the directory (`fixtures-large-v1`); the CI restore is not yet observed.
- **Depends on:** P0-09
- **Refs:** §3.1 acceptance, §8.3
- **Do:** generator for ≥ 100k commits, ≥ 5k refs, ≥ 50k files; generated once and cached in CI.
- **Done when:** CI restores it from cache in reasonable time.

### [x] P0-12 Traceability report and functional gate
- **Status:** Done: `scripts/traceability.py` merges shard traces, rejects unknown IDs, writes a Markdown matrix and fails when any ID of a delivered phase lacks a passing test. Verified locally: it failed while HARNESS-ISOLATION was uncovered and passed (5/5 phase-0 IDs) once covered.
- **Depends on:** P0-03, P0-07
- **Refs:** §8.2
- **Do:** collect spec IDs declared by passing tests, compare against the P0-03 catalogue
  filtered to phases delivered so far, emit a traceability matrix, fail CI on any gap.
- **Done when:** CI fails on a deliberately uncovered ID and passes once covered.

### [~] P0-13 Coverage pipeline and code gate
- **Status:** Pipeline done and runs locally (scripts/run_software_coverage.sh, PARALLEL=1, Xvfb unless GGUI_HEADLESS; coverage_report.py with COVERAGE_EXCL allowlist). The coverage gate was dropped on 2026-09-28 (rule 8), so this is now an informational report. Last run: 96.9 % line / 88.0 % branch. Missing: running it in CI (P0-14).
- **Depends on:** P0-07
- **Refs:** §8.2
- **Do:**
  - Clang source-based coverage (`-fprofile-instr-generate -fcoverage-mapping`,
    `LLVM_PROFILE_FILE=%p-%m.profraw`) so every `git-gg` child writes its own profile.
  - Merge with `llvm-profdata`, report line/branch/region with `llvm-cov`.
  - GCC fallback: gcov with `-fprofile-update=atomic`, gcovr with
    `--exclude-throw-branches --exclude-unreachable-branches`.
  - Adapt `scripts/run_software_coverage.sh` and `coverage_report.py` (re-written from
    the described behavior).
  - Exclude third-party and generated fonts. Count `COVERAGE_EXCL` markers (each needs a
    reason), fail if the count grows without an allowlist change. Start with zero.
  - ~~Gate: > 90 % line and > 90 % branch.~~ Dropped 2026-09-28 (rule 8): coverage is a
    report that points at untested code, not a gate.
- **Done when:** the report runs on every CI build and merges `ggui` + `git-gg` profiles.

### [~] P0-14 CI matrix
- **Status:** Written: `.github/workflows/ci.yml` (catalogue check, git 2.36 built and cached, Linux Xvfb + lavapipe with 4 shards × {git 2.36, latest}, gates job merging traces and profiles, MinGW-static and MSVC jobs). Not run: no remote or CI runner is reachable from this environment.
- **Depends on:** P0-12, P0-13
- **Refs:** §7 Phase 0, §8.3
- **Do:**
  - Linux under Xvfb with software Vulkan; Windows (MinGW-static and MSVC).
  - Test against git 2.36 (pinned minimum) and the latest git.
  - Shard the suite; every shard runs with the UI-thread assertion on.
  - Functional gate (P0-12) switched on; coverage report (P0-13) published, not gating.
- **Done when:** all jobs green on the skeleton.

---

## Phase 1 — Read-only viewer

### [x] P1-01 UI-thread registration and assertion (libgg)
- **Status:** Done: `libgg/Thread` registers the UI thread; `assertNotUiThread` (Off/Abort/Record, compiled in debug and every test build) is called by every libgit2 helper, reader and the git runner. Scenario `threading/UI-thread call…` (APP-UI-THREAD-ASSERT) trips it; the whole suite runs in Abort mode.
- **Depends on:** P0-07
- **Refs:** §3.1
- **Do:** register the UI thread ID at startup. Provide a check that asserts (debug and test
  builds) that the caller is not the UI thread. Every libgit2 helper and the git runner will
  call it.
- **Done when:** a test-build scenario proves a deliberate UI-thread call trips the assertion
  path (via the test-only switch), and the check is compiled into all later helpers.

### [x] P1-02 libgit2 helpers (libgg)
- **Status:** Done: `libgg/Git2.hpp` — RAII handles for every libgit2 type used, `check()` → `git2::Error` with libgit2's message, OID helpers (SHA-1/SHA-256), reference iteration; each helper asserts off-UI-thread. Used by all readers.
- **Depends on:** P1-01
- **Refs:** §3
- **Do:** RAII handles (`unique_ptr` with the matching `git_*_free`), error → `expected`/
  exception check carrying libgit2's message, OID ↔ string (SHA-1 and SHA-256), small
  iterator adaptors. Each asserts off-UI-thread. No wrapper API.
- **Done when:** used by P1-04; covered through later scenarios.

### [x] P1-03 Engine contract and threading
- **Status:** Done: `core::Engine` — commands in, events out (variant of immutable values), cancel tokens, activities with progress; five worker queues (mutation, snapshot/status, history, content with latest-wins slots, network), each with its own `git_repository*`; UI pump bounded to 64 events / ~4 ms per frame. Scenarios: `engine/overlapping diff requests…` (only the newest diff ever shows) and `history/cancel a long history load…`.
- **Depends on:** P1-02
- **Refs:** §3, §3.1
- **Do:**
  - Commands in, immutable snapshots and events out, with cancel tokens and progress.
  - Five queues: mutations (serialized per repo), snapshot/status, history/graph
    (incremental, cancellable), diff/blame/file content (latest wins, stale dropped),
    network (git processes).
  - Each worker opens its own `git_repository*`.
  - UI-side event pump that applies at most a bounded amount of work per frame.
  - Action-lock model: a running mutation publishes which actions are disabled.
- **Done when:** a scenario issues overlapping diff requests and sees only the newest result;
  cancel stops a long history request.

### [x] P1-04 Open repository
- **Status:** Done: open with progress and Cancel (`shell/opening shows progress…`); normal, bare, unborn, linked worktree and SHA-256 repositories (`shell/repository kinds…`); merge, rebase -i/merge backend, rebase apply, cherry-pick, revert, bisect detected from git's state files (`shell/repository state badge`); open errors go to the banner.
- **Depends on:** P1-03
- **Refs:** §3 compatibility rule, §4.1
- **Do:** open command with progress and cancel; supports normal, bare, unborn HEAD, linked
  worktree, SHA-256; reports errors as events. Detect repository state (merging, rebasing
  interactive/apply, cherry-picking, reverting, bisecting) from git state files.
- **Done when:** each fixture recipe opens and reports the correct state.

### [x] P1-05 Snapshot
- **Status:** Done: `readSnapshot` — HEAD (branch/detached/unborn), branches with upstream and ahead/behind (no network), upstream-gone, lightweight/annotated tags, remote-tracking refs, remotes (URL, push URL, prune), worktrees (main, locked with reason, stale), state + progress, MERGE_MSG, stashes, leftover refs/gg/*. Checked against git for-each-ref / worktree list --porcelain in `panels/*` scenarios.
- **Depends on:** P1-04
- **Refs:** §3.1, §4.1 toolbar, §4.7, §4.8 (published state)
- **Do:** immutable snapshot of HEAD (branch or detached, ID), local branches with upstream
  and local ahead/behind (no network), tags (lightweight/annotated), remote-tracking refs,
  remotes (name, URLs), worktrees (main, stale, locked), repository state, stash count.
- **Done when:** snapshot matches `git for-each-ref`/`git worktree list --porcelain` on
  fixtures.

### [x] P1-06 Index-aware status
- **Status:** Done: staged (HEAD→index, renames), unstaged/untracked (index→worktree), conflicted (stages 1–3 with Git's descriptions), intent-to-add; partial results flagged "scanning…" on big worktrees (`engine/partial status…` on a clone of the large fixture). Grouping matches `git status --porcelain=v2` (`changes/working tree groups…`).
- **Depends on:** P1-05
- **Refs:** §4.4, §3.1
- **Do:** staged, unstaged, untracked, conflicted (index stages 1–3), renames/copies,
  intent-to-add. On huge worktrees publish partial results flagged "scanning…".
- **Done when:** grouped results match `git status --porcelain=v2 -z` on fixtures,
  including the large fixture's partial publication.

### [x] P1-07 History walk and graph layout
- **Status:** Done: topological walk (libgit2) with published/in-scope propagation, bounded first page (200 rows) then 2,000-row batches, 5,000-row pages with Show more; lane layout with curved edges on the worker; shortest unique IDs; badges; reveal (extends until found, cancellable); merge collapse; search over message, ID, author and refs on the worker. Large fixture: first page and scrolling with 0 frames > 33 ms.
- **Depends on:** P1-05
- **Refs:** §3.1, §4.2, §4.8, §10 (performance)
- **Do:**
  - Bounded first page, then batches; cancellable; scope = selected refs.
  - Lane layout with curved-edge geometry computed on the worker.
  - Per-row data: shortest unique prefix ID, subject, author, date, ref badges (branch, tag,
    remote, worktree), published flag (reachable from any remote-tracking ref).
  - Reveal a commit: extend until found, cancellable. Collapsed regions ("show more"),
    expand/collapse merge history.
  - Search/filter by message, ID, branch, tag.
- **Done when:** large-fixture history's first page appears without any frame > ~33 ms.

### [x] P1-08 Diff computation
- **Status:** Done: commit vs parent, commit vs commit (compare with HEAD), HEAD→index, index→worktree, worktree vs HEAD, stash parts; whitespace modes, context count, full texts for expandable context, renames/copies, mode changes, binary/image (with dimensions for PNG/GIF/BMP/JPEG)/submodule placeholders, 20k-line cap with Load full. Patches match `git diff` byte-for-byte in `changes/file context menu…`.
- **Depends on:** P1-05
- **Refs:** §4.5, §4.4
- **Do:** tree↔tree, tree↔index, index↔workdir, commit vs parent, "compare with HEAD";
  whitespace modes (normal / ignore changes / ignore all), context-line count, expandable
  context, renames/copies, mode changes, binary / image / submodule placeholders. Build
  diff line tables on the worker; cap large files with a "load full" continuation.
- **Done when:** diffs match `git diff` output for fixture cases.

### [x] P1-09 Blame computation
- **Status:** Done: blame at a commit and on the working tree (uncommitted lines), "blame before" (follows renames into the parent), originating source, 50k-line cap. Matches `git blame --porcelain` in `blame/blame at a commit…`.
- **Depends on:** P1-05
- **Refs:** §4.6
- **Do:** blame at a commit and on the working tree (uncommitted lines marked), "blame
  before this change", originating source; streamed/capped for large files.
- **Done when:** matches `git blame --porcelain` on fixtures.

### [x] P1-10 Reflog and stash list
- **Status:** Done: reflog of HEAD, any branch and refs/stash (`panels/reflog…` matches `git reflog`); stash list with index, message, base, date, index/untracked flags; stash contents split into working-tree, index and untracked parts (`changes/stash contents…`).
- **Depends on:** P1-05
- **Refs:** §4.7 Reflog, §4.9 List
- **Do:** read the reflog of HEAD, any branch, or `refs/stash`. Stash list entries: index,
  message, base commit, date; stash contents split into working-tree, index and untracked
  parts.
- **Done when:** matches `git reflog`/`git stash list` on fixtures.

### [x] P1-11 File watcher
- **Status:** Done: efsw on its own thread, 150 ms debounce, classifies worktree / index / refs+state / journal changes and triggers targeted refreshes (linked-worktree git dirs watched too). Scenario `engine/watcher…` runs plain git commit/branch/edit/add as steps and sees the UI update.
- **Depends on:** P1-03
- **Refs:** §4.1, §3.1
- **Do:** efsw on its own thread, posting debounced "paths changed" notices; watch the
  worktree plus `.git/index`, `HEAD`, refs, `MERGE_HEAD`, `rebase-*`, `refs/stash` (and
  per-worktree gitdirs). Triggers a targeted refresh.
- **Done when:** a scenario runs plain `git` commands as steps and sees the UI update.

### [x] P1-12 App shell, fonts, theme and window settings
- **Status:** Done: thin App shell + Session + per-panel view models; embedded NotoSansMono/JetBrainsMono/Material Symbols; dark/light; UI scale 50–300 %; settings.json + imgui.ini (custom window-placement handler) in the pref dir, all file I/O on a background thread; error banner with dismiss/details; activity spinner, Cancel, task tooltip. `shell/settings persist across restarts` restarts a real ggui process.
- **Depends on:** P0-07
- **Refs:** §2.2, §3, §4.1
- **Do:**
  - Thin shell owning docking, menus, dialogs; each panel has its own view model (no
    god-object).
  - Embedded fonts, Material Symbols icons, dark/light theme, UI scale 50–300 %.
  - `settings.json` (nlohmann/json) and `imgui.ini` with a custom window-settings handler,
    both in `SDL_GetPrefPath("gg","ggui")`; settings I/O off the UI thread.
  - Error banner (dismissable), activity spinner with Cancel and background-task tooltip.
- **Done when:** scale/theme persist across restarts in a scenario.

### [x] P1-13 Welcome screen and recent repositories
- **Status:** Done: Welcome (Open/typed path, Initialize…/Clone… shown disabled until Phase 2, recent list with branch/upstream/ahead-behind from SummaryService, Delete/Forget, progress + Cancel, Git-wording tagline); auto-open argv[1] or the most recent existing repository (tested with real ggui subprocesses).
- **Depends on:** P1-04, P1-12
- **Refs:** §4.1
- **Do:** welcome screen with *Open repository…* (NFD picker), *Initialize…* and *Clone…*
  placeholders (implemented in Phase 2), recent list (Delete forgets an entry), progress and
  cancel while opening, Git-wording tagline. Recent summaries (branch, upstream,
  ahead/behind) as engine commands. Auto-open argv[1], else the most recent existing repo.
- **Done when:** spec IDs for welcome/recent/auto-open covered.

### [x] P1-14 Main menu, toolbar and dock layout (read-only parts)
- **Status:** Done: Repository menu (Open Ctrl+O, Recent ▸ with filter and summaries, Open working directory, Copy path, Close Ctrl+W, Refresh F5, Settings, Quit), View menu (panel toggles, F6/Shift+F6, Reset layout), toolbar (Refresh, switcher, open folder, branch/detached, HEAD reveal/copy, state badge, spinner/Cancel, banner; mutation buttons disabled with a reason), default dock layout. All covered in `shell/*`.
- **Depends on:** P1-13
- **Refs:** §4.1
- **Do:**
  - Repository menu: Open… (Ctrl+O), Recent ▸ (filterable, with branch/upstream/ahead-behind),
    Open working directory, Copy path, Close (Ctrl+W), Refresh (F5), Settings…, Quit.
  - View menu: toggle each panel, Previous/Next changed file (Shift+F6/F6), Reset layout.
  - Toolbar: Prev, Next, Refresh, repository switcher combo, open-folder button, current
    branch or "detached", HEAD ID (click reveals in History; context menu copies), spinner +
    Cancel, error banner. Mutation buttons appear disabled until Phase 2.
  - Default dock layout: left column Branches|Tags over Worktrees|Remotes|Stashes; History
    centre; Changes and Change information top right; Diff, Blame, Reflog, Operations tabbed
    bottom right.
  - Repository-state badge (MERGING / REBASING / CHERRY-PICKING / REVERTING / BISECTING),
    display only.
- **Done when:** related spec IDs covered; Reset layout restores the default.

### [x] P1-15 History panel (read-only)
- **Status:** Done: graph, rows, badges (current outlined), published colouring, Working tree / Index rows, stash badges toggle, scope from side panels, search, reveal, Show more, merge expand/collapse, ↑/↓, Copy ▸ ID / Full description (`history/*`).
- **Depends on:** P1-07, P1-14
- **Refs:** §4.2
- **Do:** graph rendering, rows, badges, pushed/unpushed colouring, virtual **Working tree**
  row and **Index (staged)** row when something is staged, stash badges on base commits
  (toggle), scope following side-panel selection, search/filter, reveal, show more,
  expand/collapse merges, ↑/↓ navigation, Copy ▸ (ID, full description).
- **Done when:** spec IDs covered; large-fixture scroll has no frame stalls.

### [x] P1-16 Changes panel (read-only)
- **Status:** Done: file list (commit, compare with HEAD, stash parts), grouped working tree / index, filter, Ctrl/Shift/Ctrl+A multi-select, keyboard navigation, status icons, renames, Copy ▸ name/relative/absolute, Copy patch, Save patch… (file and selection), Blame file (`changes/*`).
- **Depends on:** P1-06, P1-08, P1-15
- **Refs:** §4.4
- **Do:** file list for the selected commit/comparison with filter, multi-select
  (Ctrl/Shift/Ctrl+A), keyboard navigation, status icons, renames/copies, "Compare with
  HEAD" toggle. For Working tree/Index selection: Staged / Unstaged / Untracked / Conflicted
  groups. Read-only context-menu entries: Copy ▸ (name/relative/absolute path), Copy patch,
  Save patch…, Blame file.
- **Done when:** spec IDs covered.

### [x] P1-17 Change information panel (read-only)
- **Status:** Done: message, author (copy name/email), committer when different, date, published state, commit ID copy, parents (click reveals) (`info/change information…`, `history/published vs unpublished`).
- **Depends on:** P1-15
- **Refs:** §4.4
- **Do:** message, author (copy name/email), committer when different, date, published/lock
  state, commit ID (copy), parents (reveal). No aliases list.
- **Done when:** spec IDs covered.

### [x] P1-18 Diff panel (read-only)
- **Status:** Done: custom unified view (line numbers, selection, Ctrl+C, expandable context with Shift = whole gap, no-EOL marker), side-by-side with TextDiff syntax highlighting, whitespace/context controls, placeholders, compare only this file with HEAD, Load full diff, stash parts, context menu Copy / Blame file (`diff/*`).
- **Depends on:** P1-08, P1-16
- **Refs:** §4.5
- **Do:** unified and side-by-side (TextDiff), whitespace mode, context count, expandable
  context (Shift reveals whole section), syntax highlighting, binary/image/submodule
  placeholders, mode-change line, "Compare only this file with HEAD", Ctrl+C copy, "load
  full" for capped files, stash diff split into working-tree / index / untracked parts.
  Context menu read-only entries: Copy, Blame file.
- **Done when:** spec IDs covered.

### [x] P1-19 Blame panel
- **Status:** Done: working-tree blame with uncommitted marks, filter, back/forward (buttons and mouse buttons 4/5), tooltips, blame before, originating source, reveal/copy commit, select/copy change block (`blame/*`).
- **Depends on:** P1-09, P1-14
- **Refs:** §4.6
- **Do:** everything in §4.6: working-tree blame with uncommitted marks, filter, back/forward
  history (mouse buttons), per-line tooltips, "Blame before this change", originating source,
  reveal/copy commit, select or copy a change block.
- **Done when:** spec IDs covered.

### [x] P1-20 Side panels (read-only)
- **Status:** Done: Branches (filter, visibility click / Ctrl-click only, current outlined, upstream + ahead/behind, Reveal, Copy, remote-tracking branches under their remote), Tags, Worktrees (main/locked/stale, copy name/path, reveal HEAD, open directory), Remotes, Stashes, Reflog (HEAD/branch/stash chooser, filter, copy/reveal old and new) (`panels/*`).
- **Depends on:** P1-05, P1-10, P1-15
- **Refs:** §4.7
- **Do:**
  - Branches: filter, current outlined, click toggles visibility in History (Ctrl-click =
    only), Reveal, Copy name.
  - Tags: filter, visibility toggle, Reveal, Copy.
  - Worktrees: list (main, stale, locked), Copy name/path, Reveal HEAD, Open directory.
  - Remotes: list, Copy name.
  - Stashes: list; selecting shows files and diff in Changes/Diff.
  - Reflog: HEAD/branch/stash reflog chooser, filter, copy/reveal old and new commits.
- **Done when:** spec IDs covered.

### [~] P1-21 Responsiveness acceptance
- **Status:** Scenario `engine/responsiveness on the large repository` (100k commits, 5.2k refs, 50k+ files, slow-git 50 ms): 1,773 frames, worst 2.3 ms, none over 33 ms, with the UI-thread assertion on. Cancel covered for history load, reveal, and slow diffs/blame via the toolbar. Not yet run on Windows CI (no runner available here).
- **Depends on:** P1-15 … P1-20, P0-11
- **Refs:** §3.1 acceptance, §8.4 responsiveness
- **Do:** frame-time scenario on the large fixture with slow-git mode: open, scroll history,
  select commits, diff, blame, filter. Assert no frame > ~33 ms due to repository work.
  Whole suite runs with the UI-thread assertion on. Cancel works for history load, reveal,
  large diff and blame.
- **Done when:** passes on Linux and Windows CI and beats the 30 s+ baseline.

### [x] P1-22 Phase 1 gate
- **Status:** Functional gate green: all phase 0–1 spec IDs have passing tests. The coverage gate was dropped on 2026-09-28 (rule 8). The every-UI-action check for phases 0–3 is done once, in P3-20. Windows CI for P1-21 is still pending.
- **Depends on:** all P1 tasks
- **Do:** browse any fixture repository with today's layout; functional gate for Phase 1 IDs.
- **Done when:** functional gate green. (The coverage gate was dropped on 2026-09-28; the
  every-UI-action check for phases 0–3 is done once, in P3-20.)

---

## Phase 2 — Everyday Git workflow

### [x] P2-01 Git runner (libgg)
- **Status:** Covered through every mutation scenario; cancel (clone cancel, cancel-all) and failure paths (network failure, hook failure, locked ref) have their own scenarios.
- **Depends on:** P1-01
- **Refs:** §3 git runner, §3.1, §10
- **Do:**
  - Spawn `git` with argument vectors (no shell), stdin/stdout/stderr pipes, never on the UI
    thread (assert).
  - Progress parsing from `--progress` stderr (best effort).
  - Cancellation kills the whole process tree.
  - No console windows on Windows.
  - `LC_ALL=C` for parsed output; user-visible messages passed through as-is.
  - `GIT_TERMINAL_PROMPT=0`, `GIT_ASKPASS`/`SSH_ASKPASS` pointing to the askpass endpoint
    (P2-04); `GG_OPERATION` set when an operation is open.
  - Command log for tests.
- **Done when:** used by P2-02; cancel and failure paths covered by later scenarios.

### [x] P2-02 Git presence and version check
- **Status:** test_setup.cpp: git missing / older than 2.36 → blocking "Git required" prompt with Retry (and Quit).
- **Depends on:** P2-01, P1-13
- **Refs:** §4.1, §5 G2
- **Do:** on startup/open, run `git --version` off-thread; if missing or < 2.36 show a
  blocking prompt with a clear error. Record version features (config-defined hooks support,
  `reference-transaction` ≥ 2.28) for later tasks.
- **Done when:** scenarios with no git on `PATH` and with a fake old git cover the prompt.

### [x] P2-03 Mutation pipeline and journal recording
- **Status:** Mutation pipeline + journal: every mutation scenario records one operation; test_undo.cpp undoes each everyday mutation.
- **Depends on:** P2-01, P0-05, P1-03
- **Refs:** §3.1, §5 U1
- **Do:**
  - Implement the journal (libgg) per P0-05: append-only, locked, corrupt entries skipped,
    per-worktree HEAD.
  - Mutation queue: one at a time per repo, locks conflicting actions, opens a journal
    operation with `GG_OPERATION=<id>`, records before/after refs and index tree, closes it,
    triggers refresh.
  - Hook failure output → error banner; failed mutation leaves repo in plain-git state.
- **Done when:** every later mutating action produces exactly one journal operation.

### [x] P2-04 Askpass prompt
- **Status:** test_network.cpp askpass scenario: prompt answered (clone over the ssh shim) and cancelled (clean failure, no directory left). Pending prompts are refused on close/reset, so nothing deadlocks.
- **Depends on:** P2-01
- **Refs:** §4.8 credentials, §5 T1
- **Do:**
  - Decide and document the askpass bridge (e.g. `git-gg askpass` or `ggui --askpass`
    forwarding to the running ggui over a local channel).
  - ggui shows a small prompt dialog for username/password/passphrase; never stores it.
  - Credential helpers, ssh-agent and `~/.ssh/config` work untouched because git handles them.
  - No built-in credential dialog.
- **Done when:** a transport scenario over the SSH/HTTP shim prompts and succeeds; cancel
  aborts cleanly.

### [x] P2-05 Git-config-backed settings
- **Status:** test_setup.cpp Settings ▸ Git: core.editor for User/Repository/Worktree, merge.tool, diff.tool, pull.rebase (unset when emptied); commit default in test_commit.cpp. Fixed: typed values were reset until the configuration loaded, forever when nothing was set.
- **Depends on:** P2-03, P1-12
- **Refs:** §4.1 settings
- **Do:** Settings window additions: `core.editor` per scope (User / Repository / Worktree),
  `merge.tool`, `diff.tool`, `pull.rebase`, default for "commit all when nothing is staged"
  (C1). Writes via `git config`. No max-new-file-size setting.
- **Done when:** spec IDs covered; values visible to plain `git config`.

### [x] P2-06 External tool launcher
- **Status:** test_staging.cpp external editor, folder and diff tools; test_conflicts.cpp merge tool.
- **Depends on:** P2-05
- **Refs:** §4.1, §4.4 context menu
- **Do:** open file in `core.editor`, open containing folder, open working directory, run
  external diff tool (vs HEAD, vs parent) using `diff.tool`, launch three-way merge tool
  using `merge.tool`. All off the UI thread.
- **Done when:** scenarios with a scripted fake tool verify arguments and file contents.

### [x] P2-07 Initialize repository
- **Status:** test_setup.cpp: Initialize from Welcome and the Repository menu.
- **Depends on:** P2-03, P1-13
- **Refs:** §4.1
- **Do:** Repository ▸ Initialize… and welcome entry: pick folder, `git init`, open it.
- **Done when:** spec IDs covered.

### [x] P2-08 File-level staging
- **Status:** test_staging.cpp (stage/unstage/discard, intent-to-add, track/untrack, delete) passing.
- **Depends on:** P2-03, P1-16
- **Refs:** §4.4, §4.2 working-tree menu
- **Do:** Stage / Unstage / Discard per file or selection (`git add`, `git restore --staged`,
  `git restore`/clean for untracked with confirmation); Space/Enter toggles; double-click
  stages; Stage all, Unstage all, Stage all modified (not untracked); intent-to-add; drag
  between Staged and Unstaged; Track (`git add`), Untrack (`git rm --cached`) with optional
  "add to .gitignore"; Working tree row context menu: Stage all, Unstage all, Discard
  changes…; Delete file.
- **Done when:** spec IDs covered; index matches expectations via plain git.

### [x] P2-09 Hunk and line staging and discard
- **Status:** test_linestaging.cpp (hunk and line staging, discard, randomized rounds against a model) passing on the new editor-based diff view.
- **Depends on:** P2-08, P1-18
- **Refs:** §4.5, §10 (line staging risk)
- **Do:** in the Diff panel: Stage/Discard line(s)/hunk on unstaged diffs, Unstage line(s)/
  hunk on staged diffs, buttons in hunk headers. Build one patch per action and apply with
  `git apply --cached` (stage/unstage `-R`) or to the worktree for discard. Handle partial
  hunks, CRLF, no newline at EOF, renames.
- **Done when:** randomized seeded scenario compares ggui's resulting index with
  `git apply --cached` of the same patch; edge-case fixtures pass.

### [x] P2-10 Commit and amend
- **Status:** test_commit.cpp (commit, nothing staged options, hooks, amend, reword HEAD) passing.
- **Depends on:** P2-08
- **Refs:** §4.3 Commit…/Amend…, §4.4, §4.12 A, §5 C1
- **Do:**
  - Commit… commits the index via `git commit`; if nothing staged, apply C1 (prompt "Stage
    all and commit?" or configured default; also "stage the selected files").
  - Amend… amends HEAD with index, message and/or content.
  - Reword HEAD via amend (rewording other commits is P3).
  - "Skip hooks" checkbox (`--no-verify`); blocking hook failure shown in the banner.
  - Change information: Save message for HEAD, "Amend" mode for HEAD with clean index.
  - Toolbar Commit/Amend button label follows selection; Commit menu entries.
- **Done when:** spec IDs covered including a failing `pre-commit` hook scenario.

### [x] P2-11 New commit on HEAD (UI)
- **Status:** test_new_checkout.cpp new commit scenarios passing.
- **Depends on:** P2-10
- **Refs:** §4.3 New commit, §4.2 keys N/Alt+N, §4.1 Ctrl+N, §6
- **Do:** implement `new` in libgg (shared with `git-gg`, P2-26). New (Ctrl+N, toolbar,
  N key) and New detached (Alt+N) on a selected commit: empty commit, advances attached branch, detached leaves branches.
  Multi-parent (merge) selection. Insert before/after is P3.
- **Done when:** spec IDs covered.

### [x] P2-12 Check out / switch and HEAD navigation
- **Status:** test_new_checkout.cpp checkout / switch / Stash and switch / Move HEAD scenarios passing.
- **Depends on:** P2-03, P1-15
- **Refs:** §4.3 Edit/Check out, Move HEAD, §4.2 key E
- **Do:** `git switch` to branch or detach at commit (Check out ▸ branches at commit, E key);
  refuse on overwrite and offer "Stash and switch"; Move HEAD to parent/child (detach unless
  branch tip); toolbar Prev/Next.
- **Done when:** spec IDs covered incl. collision failure path.

### [x] P2-13 Branch management
- **Status:** test_refs.cpp: create/checkout/rename/delete (local, remote, all), move + worktree warning, upstream set/unset/ff — all passing.
- **Depends on:** P2-03, P1-20
- **Refs:** §4.7 Branches, §4.2 context menu
- **Do:** Create branch… (panel and History), Rename…, Delete ▸ (Local / a remote / Local and
  all remotes), Move branch ▸ (to selected commit), Set/unset upstream, Fast-forward to
  upstream, Check out. Warn when moving a branch checked out in another worktree.
- **Done when:** spec IDs covered.

### [x] P2-14 Tag management
- **Status:** test_refs.cpp tags scenario: lightweight, annotated, delete, push, delete on remote.
- **Depends on:** P2-03, P1-20
- **Refs:** §4.7 Tags
- **Do:** Create tag (lightweight or annotated with message), Delete, push tag, delete a
  remote tag.
- **Done when:** spec IDs covered.

### [x] P2-15 Remote management
- **Status:** test_refs.cpp remotes scenario: add, edit URL, prune on fetch, delete; REFLOG-BRANCH also covered.
- **Depends on:** P2-03, P1-20
- **Refs:** §4.7 Remotes
- **Do:** Add remote, Delete, Edit URL, prune-on-fetch option.
- **Done when:** spec IDs covered.

### [x] P2-16 Clone
- **Status:** test_network.cpp: clone from Welcome and the menu, unreachable remote, cancel (no directory left).
- **Depends on:** P2-01, P2-04, P1-13
- **Refs:** §4.1, §4.8
- **Do:** Clone dialog (URL, destination), `git clone --progress` on the network queue with
  progress and Cancel (partial clone directory cleaned up), opens the result.
- **Done when:** spec IDs covered incl. unreachable-remote and cancel paths.

### [x] P2-17 Fetch
- **Status:** test_network.cpp fetch scenario: toolbar, dropdown (one remote, prune, tags), menu, Remotes panel; only remote-tracking refs move; badges; failure reported.
- **Depends on:** P2-15, P2-04
- **Refs:** §4.1 toolbar Fetch, §4.8
- **Do:** Fetch all (toolbar, Repository menu, Remotes panel Fetch / Fetch all); dropdown:
  single remote, Fetch and prune, Fetch tags. Only remote-tracking refs change (no automatic
  local fast-forward). Ahead/behind badges refresh afterwards.
- **Done when:** spec IDs covered incl. network failure.

### [x] P2-18 Marker parser and conflict detection (libgg, read-only)
- **Status:** test_conflicts.cpp marker parsing: diff3 and N-sided regions, marker length via conflict-marker-size, malformed markers are text, gg-conflicts=false opt-out (fixed: the documented =false string form was ignored), deleting .git/gg reports the same.
- **Depends on:** P0-04, P1-02
- **Refs:** §4.10, §5 M1/K1
- **Do:**
  - Parser for the P0-04 grammar (2-sided diff3 and N-sided extended form, marker length,
    in-band edge cases, well-formed-only rule, `gg-conflicts=false`).
  - "Is this tree conflicted" = pure function of the tree; incremental per commit from
    parents' state plus changed files; results in the disposable cache under `.git/gg/`.
  - Worker-side scanning; handles files with plain-Git regions safely.
- **Done when:** crafted fixtures detect correctly; deleting `.git/gg/` changes nothing
  reported.

### [x] P2-19 First-class conflict display (read-only)
- **Status:** test_conflicts.cpp first-class display: History marks (incl. descendants), Conflicted only filter, F7/Shift+F7, Change information file/side list, Changes flags a checked-out conflicted commit's files while git status is clean.
- **Depends on:** P2-18, P1-15 … P1-18
- **Refs:** §4.2, §4.4, §4.10 display
- **Do:** conflict colour/marker on conflicted commits and on a working tree with native
  conflicts; "show only conflicted commits" filter; next/previous conflicted commit keys;
  Change information lists conflicted files and side counts; Changes panel flags conflicted
  files of a checked-out conflicted commit under Conflicted even though `git status` is
  clean.
- **Done when:** spec IDs covered.

### [x] P2-20 Pull
- **Status:** test_network.cpp pull scenarios: config, merge/rebase/ff-only overrides, menu, Branches and Remotes panels, disabled reasons, Stash and pull.
- **Depends on:** P2-17, P2-12
- **Refs:** §4.1 toolbar Pull, §4.7, §4.8
- **Do:** pull current branch from upstream following `pull.rebase`/`pull.ff`; dropdown
  override merge / rebase / ff-only; incoming badge (↓n) from the last fetch; disabled with
  a reason when detached or no upstream; "Stash and pull" when local changes block; Pull from
  Branches (current branch) and Remotes panels; native conflict result goes to P2-24 flow.
- **Done when:** spec IDs covered.

### [x] P2-21 Push
- **Status:** test_network.cpp push scenarios: toolbar/menu/History/Branches, Push to with --set-upstream, force with lease, tags, rejection (Pull then push / Force), refusal of first-class conflicts with Reveal.
- **Depends on:** P2-17, P2-19
- **Refs:** §4.1 toolbar Push, §4.8, §4.10 safety, §5 P1
- **Do:** push current branch to upstream; outgoing badge (↑n); no upstream → Push to… with
  remote prefilled and `--set-upstream` checked; dropdown Push to…, Force with lease (only
  force option), Push tags; History/Branches Push and Push to…; rejected non-fast-forward
  offers "Pull then push" or "Force with lease…" (confirmation). **Always refuse** when
  commits not reachable from the remote's tracking refs contain first-class conflicts:
  error lists commits and files with Reveal buttons, no override.
- **Done when:** spec IDs covered incl. refusal and rejection paths.

### [x] P2-22 Stash
- **Status:** test_stash.cpp: create (message, untracked, keep index, staged only, selected files; toolbar and Working tree menu), apply/pop (+index), apply one file, branch, drop with Undo/Redo, clear, conflicting pop keeps the stash. Fixed: the stash row context menu never opened.
- **Depends on:** P2-03, P1-20, P2-12
- **Refs:** §4.9, §4.1 toolbar Stash/Pop
- **Do:** Create (message; keep index, include untracked, staged only, selected paths);
  Apply/Pop with restore-index option (pop keeps the stash on conflict); Drop with
  confirmation; Clear all; Branch from stash; apply a single file from a stash; toolbar
  Stash/Pop; Working tree "Stash changes…"; "Stash and switch"/"Stash and pull" helpers
  reuse this; journal records push/pop/drop so Undo can restore a dropped stash.
- **Done when:** spec IDs covered.

### [x] P2-23 Patches
- **Status:** test_patches.cpp: Edit ▸ Apply patch from the clipboard/a file to the working tree/index; a failing patch opens an error popup and changes nothing. Copy/Save patch covered in phase 1.
- **Depends on:** P2-03, P1-18
- **Refs:** §4.11
- **Do:** Edit ▸ Apply patch… from clipboard or file with "Apply to index" vs "Apply to
  working tree"; Copy patch and Save patch… for a file or diff selection.
- **Done when:** spec IDs covered incl. a patch that fails to apply.

### [x] P2-24 Native conflict and in-progress operation flow
- **Status:** test_conflicts.cpp native flow: three-way (stage) diffs, take ours, merge tool, mark resolved, MERGE_MSG editing, Continue/Skip/Abort, commit with conflicts (diff3 regions) and binary conflicts not offered (status now flags them).
- **Depends on:** P2-06, P2-08, P2-19
- **Refs:** §4.10 native, §4.1 state badge
- **Do:** for merge, rebase (interactive/apply), cherry-pick, revert, bisect started by plain
  git, stash apply or pull (P2-20): conflicted files from index stages 1–3; Resolve with merge tool, take
  ours/theirs, Mark resolved (`git add`), three-way diff; Continue / Skip / Abort from the
  state badge; show/edit `MERGE_MSG`. "Commit with conflicts" (text-only conflicts): write
  diff3 regions from stages 1–3, stage, finish the operation.
- **Done when:** spec IDs covered for every state; "Commit with conflicts" refused when a
  binary conflict remains.

### [x] P2-25 Undo/Redo and Operations panel
- **Status:** test_undo.cpp: Undo/Redo via menu, keys, toolbar; Operations lists ggui/git-gg sources and restores a row; refusals (nothing to undo, refs moved outside the journal, lossy worktree → Stash and undo, locked ref); corrupt journal lines skipped; undo of every everyday mutation. Fixed: Stash and undo undid the stash instead of the refused operation; undo/redo labels now name the original operation.
- **Depends on:** P2-03
- **Refs:** §4.1 Edit menu, §4.7 Operations, §5 U1
- **Do:** Undo (Ctrl+Z) / Redo (Ctrl+Y) in menu and toolbar; Operations panel listing journal
  entries with source label and Restore; undo = new operation restoring refs and index;
  worktree updated only when lossless, otherwise refuse or offer stash first; refuse when
  refs moved outside the journal; note in UI that without managed hooks Undo covers ggui
  and git-gg only (Reflog remains the recovery path).
- **Done when:** scenarios cover each refusal path, a corrupt journal (skipped, not fatal),
  and undo of every Phase 2 mutation.

### [x] P2-26 `git-gg` executable core
- **Status:** test_cli.cpp: new (HEAD, -m, --detach, merge), undo/redo/op log, conflicts (exit 1), help, Git-style exit codes (128 fatal, 129 usage), ui (starts the ggui next to git-gg with a clean absolute path). Fixed: 'git gg help/undo/...' inside ggui were taken for askpass prompts and hung; help printed the wrong text.
- **Depends on:** P2-03, P2-11, P2-18, P2-25
- **Refs:** §6
- **Do:** CLI11 app, Git-style exit codes (128 fatal) and stderr messages; `git gg help <cmd>`
  / `--help` plain text; fast startup (no repo scan). Commands:
  - `new [-m MSG] [--detach] [PARENT...]` (shared libgg code; `--before/--after` in P3-10)
  - `undo`, `redo`, `op log`
  - `conflicts [REV]` (exit 1 when any)
  - `ui [PATH]`
- **Done when:** scenarios run each command as a test step and verify results in the UI.

### [x] P2-27 Managed hooks: install, uninstall, status
- **Status:** test_hooks.cpp: CLI install/status/uninstall with config-defined hooks (git 2.55) and wrapper scripts (GG_HOOKS_MODE=wrapper); chaining keeps the user's hooks and exit status; uninstall is byte-exact; missing git-gg is silent except pre-push's warning; linked worktrees.
- **Depends on:** P2-26, P2-02
- **Refs:** §4.12 B, §5 H1
- **Do:** `git gg hooks install|uninstall|status` and the shared libgg installer:
  config-defined hooks when git supports them, else wrapper scripts in the active hooks dir
  (`core.hooksPath` or `.git/hooks`) that call `git gg hook <name>` then the previous hook;
  chaining with exit status respected; works in linked worktrees; uninstall restores
  byte-exact; wrapper does nothing silently if `git-gg` is missing (except `pre-push`,
  which warns).
- **Done when:** scenarios cover chaining, exit codes, missing `git-gg` and byte-exact
  uninstall.

### [x] P2-28 Hook entry points and plain-git journal capture
- **Status:** test_hooks.cpp: plain git commit/branch/checkout -b/reset --hard/tag each yield one journal operation that Ctrl+Z restores; amend's post-rewrite context; ggui's own commands join its operation; 3000-ref fetch fast. Fixed: symref updates were journaled with an unknown old value (undo tried to delete HEAD); undo of worktree-updating git commands now carries a clean working tree back.
- **Depends on:** P2-27
- **Refs:** §4.12 B, §8.4 hooks
- **Do:** `git gg hook reference-transaction` (on `committed`, append old/new to journal,
  grouping one git command into one operation labelled with the command via `GIT_*` env and
  process tree); `post-checkout`, `post-merge`, `post-rewrite`, `post-commit` add context;
  `GG_OPERATION` joins the open operation (loop guard); fast exit when journal disabled.
- **Done when:** every ref-changing plain git command in the fixture list yields exactly one
  journal operation that Undo restores; a fetch of thousands of refs stays fast.

### [x] P2-29 First-open prompts and Hooks settings tab
- **Status:** test_hooks.cpp: first-open prompt Install / Not now (asks again) / Never (never again), Settings ▸ Hooks status, remove and install.
- **Depends on:** P2-27, P1-13
- **Refs:** §4.1, §4.12 B, §5 H1
- **Do:** on first open without hooks: "Install ggui hooks (Undo for all git operations,
  block pushing conflicts)?" Install / Not now / Never; answer stored in ggui settings.
  Settings ▸ Hooks tab: install/remove and status.
- **Done when:** spec IDs for all three answers covered.

### [x] P2-30 Old gg refs cleanup (C3)
- **Status:** test_setup.cpp: prompt on open, only commits kept alive by refs/gg are listed, kept under a chosen or backup branch name, one update-ref --stdin, Undo restores everything, Ignore remembered.
- **Depends on:** P2-03, P2-25
- **Refs:** §5 C3
- **Do:** on open, detect `refs/gg/*`; list commits kept alive only by them; per commit offer
  create-branch or keep via a backup branch; delete refs with one `git update-ref --stdin`
  (undoable via journal); "Ignore" remembered per repository.
- **Done when:** spec IDs covered incl. undo of the cleanup.

### [x] P2-31 Phase 2 gate
- **Status:** Functional gate green: all phase 0–2 spec IDs covered and the §8.4 failure paths that apply have scenarios. The coverage gate was dropped on 2026-09-28 (rule 8). The every-UI-action check for phases 0–3 is done once, in P3-20.
- **Depends on:** all P2 tasks
- **Do:** everyday-use exit criterion; all failure paths from §8.4 that apply so far (hook
  rejection, git missing/old, locked refs, checkout collision, network failure, cancel,
  corrupt journal); functional gate green. (The coverage gate was dropped on 2026-09-28; the
  every-UI-action check for phases 0–3 is done once, in P3-20.)

---

## Phase 3 — History editing

### [x] P3-01 Marker writer and N-way term merge (libgg)
- **Status:** libgg/Markers.cpp writer/algebra, exercised byte-exactly by test_conflict_edges.cpp (no newline, CRLF, empty side, marker-like content with longer markers, 3-sided merge and simplification) and the rewrite scenarios.
- **Depends on:** P2-18
- **Refs:** §4.10 rewrites, §5 M1, §10
- **Do:** writer (plain / diff3 / extended form), automatic marker length choice, in-band
  edge cases; N-way term algebra with simplification per P0-04; never nest markers; respect
  `gg-conflicts=false` (conflict becomes non-text → pre-flight).
- **Done when:** exercised by P3-02 scenarios with byte-exact checks.

### [x] P3-02 In-memory rewrite engine
- **Status:** libgg/Rewrite.cpp engine; test_rewrite.cpp (invariants, post-rewrite mapping, one Undo, published warning, failure untouched, pre-rebase veto, post-checkout), test_edit.cpp (no-op keeps ids, text conflicts continue, completion message).
- **Depends on:** P3-01, P2-03
- **Refs:** §3 rewrites, §4.3 intro, §5 R1/R2, §4.12 A
- **Do:**
  - Plan: chosen commits + descendants reachable from local branches containing them, plus a
    detached HEAD; never move remote-tracking refs; confirm before moving branches checked
    out in other worktrees.
  - Build commits in memory; text conflicts become first-class and the rewrite continues;
    collect non-text conflicts (binary, filtered/LFS, mode, type, modify/delete, rename,
    submodule, opt-out files) for the pre-flight dialog.
  - Run `pre-rebase` via `git hook run` for rebase-like rewrites.
  - Apply all ref moves in one `git update-ref --stdin`; update the worktree via git
    (`checkout` / `read-tree -m -u`); run `post-rewrite` (with mapping) and `post-checkout`
    via `git hook run`.
  - One journal operation; completion message lists newly conflicted commits.
  - Failures (locked refs, worktree collision, hook rejection, I/O) leave everything
    untouched. Cancel before apply changes nothing.
  - Published-history warning (commits reachable from remote-tracking refs).
- **Done when:** rewrite invariants hold (§8.4): ancestors/unrelated branches unchanged,
  no-op keeps IDs, failure leaves refs/HEAD/index/worktree untouched, Undo exact.

### [x] P3-03 Pre-flight dialog for non-text conflicts
- **Status:** test_preflight.cpp: binary, modify/delete, symlink, submodule, mode, rename (grouped per rename), filtered, opt-out, file from disk; conflicts listed per commit in order; Cancel leaves .git byte-identical (except disposable caches).
- **Depends on:** P3-02
- **Refs:** §4.10 pre-flight
- **Do:** list conflicts per commit in order; per conflict: take A / take B / take base /
  choose file from disk / keep deleted or present; modes: pick one; step through commits in
  memory since choices affect later commits; Cancel leaves the repo byte-identical.
- **Done when:** a scenario per non-text kind; Cancel verified byte-identical.

### [x] P3-04 Checking out conflicted commits
- **Status:** test_conflicts.cpp: checked-out conflicted commit keeps git status clean; the Expand to index stages setting writes stages 1-3 from the regions and collapses them before switching away.
- **Depends on:** P3-01, P2-12
- **Refs:** §4.10 checking out
- **Do:** worktree gets exactly the committed files with markers, index = HEAD, `git status`
  clean. Optional setting "Expand to index stages on checkout" (default off): write stages
  1–3 for two-sided regions and collapse them back on switch-away or Mark resolved.
- **Done when:** both setting values covered.

### [x] P3-05 First-class conflict resolution
- **Status:** test_conflicts.cpp: per-term Diff view, take side (whole file / one region), Mark resolved refused while regions remain, Amend rebases descendants (their copies resolve), editor + new commit on top, merge tool with stages from the regions.
- **Depends on:** P3-04, P2-06, P3-02
- **Refs:** §4.10 resolving, §4.5
- **Do:** Resolve with merge tool (extract base/ours/theirs from regions); take side N per
  region or whole file; edit in editor; Mark resolved refuses while regions remain; then
  Amend (descendants rebased, their copies resolve) or New commit on top. Diff panel
  per-term view (base → side N) plus raw marker view. Replace the old "Resolve conflict"
  dialog.
- **Done when:** spec IDs covered.

### [x] P3-06 Managed `pre-push` check
- **Status:** test_hooks.cpp: with the managed hooks, plain git push of first-class conflicted commits is refused (commits and files listed); --no-verify bypasses.
- **Depends on:** P2-28, P2-18
- **Refs:** §4.12 B, §4.10 safety, §5 P1
- **Do:** `git gg hook pre-push` refuses commits with first-class conflicts in ranges not yet
  on the remote's tracking refs, listing them; only `--no-verify` bypasses.
- **Done when:** plain `git push` refusal scenario with hooks installed.

### [x] P3-07 Reword any commit and edit author
- **Status:** test_rewrite.cpp: Save message on any commit (reword with descendants), Edit author (author date kept).
- **Depends on:** P3-02
- **Refs:** §4.3 Describe, Edit author, §4.4
- **Do:** Save message on any commit; edit author (Change information); descendants rebased.
- **Done when:** spec IDs covered.

### [x] P3-08 Duplicate and rebase
- **Status:** test_edit.cpp: Duplicate (D) / Duplicate branch (Shift+D) as detached copies; Rebase onto (one commit / with descendants).
- **Depends on:** P3-02
- **Refs:** §4.3 Duplicate, Rebase…, §4.2 keys D/Shift+D
- **Do:** Duplicate commit/branch (cherry-pick onto its parent, detached copy); Rebase… one
  commit or whole branch onto a destination.
- **Done when:** spec IDs covered.

### [x] P3-09 Squash, split, abandon, restore, simplify parents
- **Status:** test_edit.cpp: Squash into parent (S), into an ancestor (fixup), descendants into a commit (Shift+S); Split by files (Alt+S); Abandon (A) / Abandon branch (Shift+A, remote delete); Restore paths in a commit or the working tree; Simplify parents.
- **Depends on:** P3-02
- **Refs:** §4.3, §4.2 keys S/Shift+S/Alt+S/A/Shift+A
- **Do:** Squash into parent or chosen target (fixup/squash), with descendants; Split by
  selected files into two commits; Abandon / Abandon branch (optional remote branch delete);
  Restore paths in a commit from another commit, or restore worktree
  (`git restore --source`); Simplify parents (remove redundant merge parents).
- **Done when:** spec IDs covered.

### [x] P3-10 Insert new commit before/after
- **Status:** UI (test_edit.cpp) and git gg new --before/--after (test_cli.cpp), shared libgg insertPlan; one journal operation, git gg undo.
- **Depends on:** P3-02, P2-11, P2-26
- **Refs:** §4.3 New commit, §6 `--before/--after`
- **Do:** insert before/after in the UI and `git gg new --before/--after REV`, rebasing
  descendants.
- **Done when:** spec IDs covered for UI and CLI.

### [x] P3-11 Reorder and move changes between commits
- **Status:** test_move.cpp: files and lines/hunks to parent, child, active commit, working tree (uncommit) and revert, from the Changes and Diff menus; reorder in test_edit.cpp; A,B→B,A→A,B leaves no nested markers.
- **Depends on:** P3-02, P2-09
- **Refs:** §4.3 Reorder, Move files/hunks/lines, §4.4, §4.5
- **Do:** move a commit before/after another (or copy); move selected files/hunks/lines to
  parent, child, active commit, or working tree ("uncommit"); Revert line/hunk; Changes
  context menu Move to parent/child, Revert.
- **Done when:** spec IDs covered; cancellation scenario (A,B → B,A → A,B) leaves no nested
  markers.

### [x] P3-12 Merge into HEAD and reconcile
- **Status:** test_edit.cpp: Merge into HEAD in memory (and natively), Rebase HEAD onto a branch, Reconcile (merge or rebase).
- **Depends on:** P3-02, P2-24
- **Refs:** §4.3 Merge into @, Rebase @ onto branch / Reconcile, §4.7
- **Do:** in-memory merge commit (text conflicts first-class, non-text via pre-flight) then
  checkout; option "use native `git merge`" (stops with index conflicts); Rebase HEAD onto
  branch and Reconcile with remote/branch (native rebase or merge of diverged branch).
- **Done when:** spec IDs covered.

### [x] P3-13 Drag and drop
- **Status:** test_dnd.cpp: commit→commit with Shift/Ctrl+Shift/Ctrl/Alt and the chooser (Copy after), branch badge→commit, a commit's files→its parent, working tree files→an older commit (absorb). Modifiers documented in ui-spec §2.x.
- **Depends on:** P3-08, P3-09, P3-11, P2-13
- **Refs:** §4.2 drag and drop
- **Do:** commit → commit: Move before/after, Squash, Rebase (modifier keys pick the default;
  otherwise a chooser); branch badge → commit moves branch; files from Changes → commit move
  changes into it.
- **Done when:** spec IDs covered for every drop target and modifier.

### [x] P3-14 "Commit with conflicts" and conflict propagation checks
- **Status:** test_conflict_edges.cpp: round trips byte-exact, N-way (3 sides via merge, simplified by Undo), randomized reorders come back exact with no nesting, plain-git transparency (rebase, cherry-pick, amend, merge, stash, gc, clone, push to bare), git merging files with ggui regions (clean and native); commit with conflicts in test_conflicts.cpp.
- **Depends on:** P3-05, P2-24
- **Refs:** §4.10, §8.4 conflicts
- **Do:** full conflict scenario group: round-trip edge cases (marker-like lines, CRLF,
  missing newline, empty sides, N sides, opt-out) created through UI rewrites and checked
  byte for byte; automatic resolution when reordering back or dropping the cause; plain-Git
  transparency steps (`rebase`, `cherry-pick`, `commit --amend`, `merge`, `stash`,
  `gc --prune=now`, `clone`, `push` to bare) between UI checks; plain Git merging a file
  with ggui regions; deleting `.git/gg/` changes nothing.
- **Done when:** all conflict spec IDs covered; randomized N-way rewrite scenario stable.

### [x] P3-15 Interactive rebase: todo model and validation
- **Status:** Model built in libgg (Todo.hpp/.cpp): parse/format, read, readRemaining, expand, autosquash, squash templates/messages, editorMessages, cleanup, validation, engine choice, exec-each, toPlan. Exercised by P3-16..P3-19: the editor (read, autosquash vs git, messages, validation, engine choice), the randomized differential vs git rebase -i (toPlan), the native engine (format for the prepared todo, parse for the progress view, expand for Edit remaining todo, addExecEach for exec after every commit).
- **Depends on:** P3-02
- **Refs:** §4.13
- **Do:** todo model with actions `pick`, `reword`, `edit`, `squash`, `fixup` (incl. `-C`/
  `-c`), `drop`, `exec`, `break`, `update-ref`; Git-order read/write; validation (first row
  not squash/fixup; warn when dropping sole branch commits; published warning); autosquash
  placement for `fixup!`/`squash!`/`amend!`; squash message assembly the way Git does;
  engine selection (in-memory unless `edit`/`break`/`exec` or user choice) with reason.
- **Done when:** exercised by P3-16 … P3-19.
- **Design notes:**
  - **Todo model (built):** `Source/libgg/include/libgg/Todo.hpp` + `Todo.cpp`, namespace
    `gg::todo`.
    - `parse`/`format`: Git's todo text. Git ≥ 2.44 writes `pick <id> # <subject>`, older
      versions `pick <id> <subject>`, and both parse. There is a blank line after each
      `update-ref`. `.git/rebase-merge/git-rebase-todo` holds full ids with no help text.
      `done` uses the same format.
    - `read(repo, {upstream, tip, onto, updateRefs, autosquash})` returns a `Context`: the
      range, `CommitInfo` (message, author, empty, published), branches at each commit, and
      the starting todo.
      - The starting todo drops merges and leaves out commits already upstream (patch-id).
      - Git lists `update-ref` lines in reverse name order. It skips the rebased branch and
        branches checked out in other worktrees.
    - `expand` resolves abbreviated ids. `autosquash`, `groups`, `squashTemplate`,
      `groupMessage`, `editorText`, `cleanup`, `validate`, `unchangedPrefix`, `chooseEngine`,
      `addExecEach` and `toPlan` are pure functions of the todo and the `Context`.
    - A typed reword/squash message lives on the group's first row (`Item::message`). P3-16
      should reset it when the group's composition changes.
  - **Git behavior checked by hand against git 2.55** (throwaway driver, not committed):
    - The starting todo matches git for linear ranges, ranges with merges, cherry-picked
      commits, several branches on one commit, and a branch checked out in a worktree.
    - Autosquash order matches: repeated prefixes, `<id>` targets, subject-prefix targets,
      `amend!`, and `fixup!X` with no space (not a fixup).
    - Squash templates and final messages match byte for byte for fixup/squash/`-C`/`-c`
      mixes.
      - `fixup -C/-c` skips the earlier messages only when no squash came before it.
      - `amend!` subjects are always commented out. `squash!`/`fixup!` subjects are
        commented out only under squash or after a squash.
      - Fixup-only groups keep the message verbatim, with no strip. Comment lines survive.
    - `toPlan` + `Rewriter` gave the same trees, messages, authors and branch positions as
      `git rebase -i --autosquash --update-refs`. Also checked on SHA-256 with a root
      rebase.
  - **For P3-17/P3-18:**
    - `toPlan` sets `keepBranches`/`keepHead`, so only the tip ref and `update-ref` lines
      move, as in git. `Plan::refsToSteps`/`detachHeadAt` now accept `=<id>` (an update-ref
      before any commit row goes to `onto`). Dropped commits are simply not replayed; they
      are not in `Plan::dropped`.
    - The Rewriter maps squashed commits to their first parent's replacement. Git's
      post-rewrite maps them to the squash result, so fix this before the differential
      test. (Fixed in P3-18: `Result::rewritten`.)
    - "Keep committer date" still needs a committer override on `Step`/`Plan`.
    - The patch-id pass walks all of `tip..upstream`, which can be slow on a very stale
      branch.
  - **For P3-19:**
    - `update-ref` in the todo needs git ≥ 2.38, but the minimum is 2.36. With an older git,
      the native engine must drop those lines or refuse.
    - Git accepts a duplicate `update-ref`, or one for the rebased branch, and then fails
      at the end. `validate` reports both as errors.
    - The stop UI builds on `App::drawStateBadge`, `Actions::continueOperation` and
      `detectState`. `Source/gitgg/SequenceEditor.cpp` is still a stub.
  - **Tests:** `Source/tests/test_rebase_i.cpp`, including the differential test against
    `git rebase -i` on a copy.

### [x] P3-16 Interactive rebase: todo editor UI and entry points
- **Status:** test_rebase_i.cpp: RebasePanel (tab next to History) with header (Start/Cancel, engine and reason, validation), options, rows and badges, newest first, drag and Alt+↑/↓, p r e s f d x b, multi-select, undo/redo, inline reword/squash messages; entry points I / History menu, selection, Commit menu and Branches (asks for a base), Open as interactive rebase from Squash and Rebase onto. Start runs toPlan + Actions::rewrite; native todos are validated but wait for P3-19. Full suite 180/180.
- **Depends on:** P3-15
- **Refs:** §4.13 entry points, todo editor
- **Do:** dockable panel with modal header; rows (action, short ID, subject, author, date,
  branch badges); oldest-first with newest-first toggle; drag or Alt+↑/↓ reorder; keys
  p/r/e/s/f/d/x/b; multi-select; insert exec/break; undo/redo in editor; inline message
  editors for reword/squash with combined squash message; options (onto, autosquash,
  update-refs default on, autostash, exec after every commit, committer date keep/now);
  engine shown with reason. Entry points: History I key / "from here…", range selection,
  Branches "Interactive rebase onto…", Commit menu (asks for base), "Open as interactive
  rebase…" from single actions.
- **Done when:** spec IDs covered.
- **Design notes:**
  - **Panel:** `Source/app/panels/RebasePanel.{hpp,cpp}` (`RebasePanel`, window
    `panel::Rebase` = "Interactive rebase", ui-spec §4.x). `Session::rebase()` owns it; it is drawn
    only while a todo is open and docks as a tab next to History.
    - `open(Request)` reads the range on the mutation queue (`Actions::run`, no journal, no
      refresh): `from` (commit → upstream = its parent), `tipContaining` (HEAD, else the first
      local branch by name), `onto`, `selected`, and `adjust` (the starting todo for "Open as
      interactive rebase…").
    - Always read with update-ref lines and without autosquash. The options edit the list:
      Update refs off removes the rows and on re-inserts them; Autosquash on runs
      `todo::autosquash` and off goes back to `Context::initial`. *Onto* re-reads and keeps the list.
    - Undo steps are `State{todo, context, updateRefs, autosquash}`. A text field gives one step
      per editing session.
    - `resetStaleMessages` drops a typed message when its group's rows or actions change.
  - **Entry points:** `openInteractiveRebase`, `openInteractiveRebaseSelection`,
    `showInteractiveRebaseDialog` in `CommitMenu.cpp`. "Open as…" buttons are in the Squash and
    Rebase onto dialogs.
  - **Start (temporary in-memory path):** `todo::toPlan` + `Actions::rewrite`. The builder
    refuses when the tip moved since the read.
    - `Plan::keepCommitterDate` is new in libgg.
    - `Actions::rewrite(..., autostash)` stashes before apply and pops after, in the same
      operation. If the pop fails, the changes stay in the stash and `ctx.info` is shown as a
      warning.
    - Native todos (edit/break/exec, exec after every commit, Run as git rebase) are edited and
      validated, but Start is disabled ("not available yet").
  - **Tests:** `Source/tests/test_rebase_i.cpp` (8 scenarios). Not covered yet, because they
    need the native engine: IR-ACT-EDIT/EXEC/BREAK, IR-OPT-EXEC-EACH, IR-ENGINE-USER-CHOICE,
    IR-ENTRY-STOPPED.
  - **Test-engine pitfalls:**
    - `BeginCombo` reports no item info, so the panel registers `###ir_action_<id>` itself.
    - The test engine presses Alt and an arrow in the same frame, which toggles the menu layer
      on release. The panel owns Alt while it handles Alt+arrows.
    - An error popup titled like a window collides with it: error titles must differ from
      "Interactive rebase".

### [x] P3-17 Interactive rebase: live preview
- **Status:** test_rebase_i.cpp: preview beside the list (Queue::Preview worker, latest wins, same toPlan + Rewriter::compute as Start, never applied) with first-class conflicts, non-text decisions, empty / becoming-empty commits, moving and staying branches, detached HEAD; each previewed commit's tree, subject, conflicts, emptiness and branches equal Start's result and git rebase -i --update-refs on a clone; pre-flight lists exactly the previewed decisions; 600 ms slow worker: 5 requests, 1 shown, frames < 33 ms; .git byte-identical. Full suite 183/183 (8 shards).
- **Depends on:** P3-16
- **Refs:** §4.13 live preview, §3.1
- **Do:** compute the plan in memory on a worker (latest edit wins); preview graph beside the
  list showing first-class conflicts, non-text conflicts needing decisions, empty commits,
  and moving branches.
- **Done when:** preview matches the executed result in scenarios; no frame stalls.
- **Notes from P3-16:**
  - Start the preview from `RebasePanel::onTodoChanged()`, which runs after every edit, undo,
    option change and re-read. Draw it to the right of the list (`drawList`).
  - The inputs are `todo()`, `context()` and `options()`. They are plain values, so copy them to
    the worker.
- **Design notes:**
  - **Engine:** `Engine::rebasePreview(todo, context, options)` runs on a sixth worker queue,
    `Queue::Preview` (one slot, latest wins), so a slow preview never delays diffs or mutations.
    It answers with `RebasePreviewEvent` (`core::RebasePreview`, plain values in
    `core/Types.hpp`). `Source/core/RebasePreview.cpp` (`readRebasePreview`) runs
    `todo::toPlan(…, replayStops = true)` and `Rewriter::compute(plan, cancel)`, then reads the new
    commits through `Rewriter::repository()`: per result commit its id, tree, subject, sources,
    unchanged, empty / was empty, first-class conflicts (`conflicts::commitConflicts`, memory-only
    cache), "new" conflicts (`Result::conflicted`), non-text decisions (`Result::unresolved`),
    branches ending there (`Plan::refsToSteps`, `detachHeadAt`), moves (`Result::moves`), branches
    ending at the base, and branches that stay on the old commits.
  - **Newest edit wins:** `RebasePanel::startPreview` (from `onTodoChanged`) cancels the pending
    request and sends a new one. The worker slot drops queued requests and cancels the running
    one (`Rewriter::compute` now checks a cancel token between steps). Answers to anything but the
    newest request are ignored. The previous result stays on screen, marked "Updating...". A list
    with errors gets no preview. A cancel from the toolbar ends in "The preview was cancelled.".
  - **Nothing is written (decision):** no throwaway ODB is needed on top of what the Rewriter
    already has. Each preview builds a fresh `Rewriter`, which opens its own repository instance
    and adds a libgit2 mempack backend at the highest priority. Every object `compute` creates
    (blobs with markers, trees, commits) goes into that in-memory backend and is gone when the
    Rewriter is destroyed. `apply` is never called, so refs, index, working tree and object files
    stay as they were. The only on-disk side effect libgit2 may have is refreshing the mtime of an
    object that already exists ("freshen", which git does too). The test compares every byte
    under `.git` before and after.
  - **Native todos** are previewed with `toPlan(…, replayStops = true)`: edit = pick, exec and
    break are skipped (the history when every stop just continues). Label/reset/merge still
    throw, and the preview shows the error.
  - **UI:** the list table gets a fixed width and the preview is a bordered child to its right
    (ui-spec §4.x "Live preview"). History's graph cell drawing moved to
    `panels/Graph.{hpp,cpp}` (`graph::drawCell`, `inset`, `laneX`), which both panels use. The
    preview is always one lane, newest first, with the base last.
  - **Tests:** `test_rebase_i.cpp`, three "live preview" scenarios plus the detached-HEAD one.
    `checkMatches` compares each previewed commit with the executed result: tree, subject,
    first-class conflicted files, emptiness, unchanged id and branch positions. The comparison
    runs after Start, and after `git rebase -i --update-refs` on a clone with the same todo.
    The worker test runs with a 600 ms slow-git latency: 5 requests, 1 shown, frame maximum below
    33 ms (`frameProbe`). Pre-flight lists exactly the previewed decisions. `.git` stays
    byte-identical. `Scenario::gitDirBytes` moved into the harness.

### [x] P3-18 Interactive rebase: in-memory engine
- **Status:** test_rebase_i.cpp: randomized differential vs git rebase -i on a copy (seed 0x1818d1ff, 6 rounds; seeds 1–25 × 25 rounds also pass). Todos are entered through the UI: reorders, drops, squash/fixup/-C/-c, reword, autosquash, update-ref rows anywhere, onto, Keep/Drop/Ask for commits that become empty. Checked: trees, messages, authors, every branch position, post-rewrite mapping, one committed ref transaction, clean worktree, no sequencer state; one Undo restores all refs. Rewriter: squash/amend chains and post-rewrite as git reports them (Result::rewritten). An update-ref row before squash/fixup rows keeps the finished commit (Step::amend), as git does. New option Becoming empty (Keep/Drop/Ask, Ask asks at Start). A moved tip is still refused (decision). Full suite 185/185 (8 shards); coverage 92.4 % line / 78.6 % branch overall.
- **Depends on:** P3-17, P3-03
- **Refs:** §4.13 execution 1, §5 R3
- **Do:** run the todo through the rewrite engine; pre-flight for non-text conflicts; one
  `update-ref --stdin`; worktree update after; `post-rewrite rebase` with mapping; one
  journal operation (one Undo); no sequencer state; cancel before apply = no change.
- **Done when:** randomized differential scenario vs `git rebase -i` on a copy matches trees,
  messages, authors and branch positions; autosquash order verified.
- **Notes from P3-16:**
  - Start already goes through `toPlan` + `Actions::rewrite`: pre-flight, published
    confirmation, one operation. One Undo, Cancel and autosquash order are tested.
  - Still to do:
    - The randomized differential test.
    - The squash mapping for post-rewrite (see P3-15).
    - Deciding whether the tip-moved check should re-read instead.
  - `test_rebase_i.cpp` "autosquash" already compares a fixed case with `git rebase -i` on a
    clone: trees, messages and authors.
- **Notes from P3-17:**
  - The preview and Start share `toPlan` + `Rewriter::compute`. Any change to the plan (the
    squash mapping, empty commits, committer override) shows up in the preview on its own. Keep
    `checkMatches` in `test_rebase_i.cpp` passing, and reuse it in the differential test (it
    compares each commit's tree, subject, conflicts, emptiness and branches with a finished
    rebase).
  - Commits that *become* empty: `git rebase -i` stops on them (interactive default
    `--empty=stop`; checked with git 2.55), while the Rewriter keeps them as empty commits. The
    preview shows "(empty)", and Start currently keeps them. Decide whether to drop, keep or ask
    before the differential test; commits that were empty from the start are kept by both.
  - An `update-ref` row between a pick and its squash/fixup rows moves the branch to the
    squashed result (`toPlan` maps it to the group's step). Git would leave it at the pre-squash
    commit. Validation does not flag this yet.
- **Design notes:**
  - **Git's behavior, checked by hand against git 2.55** (throwaway scripts, not committed), then
    held by the differential test:
    - An exec, break or update-ref row *finishes* the current squash chain (`is_final_fixup`
      only skips drop/noop rows). A squash/fixup after it amends that finished commit as a new
      chain whose template starts with HEAD's message. The update-ref branch keeps the finished
      commit. Post-rewrite maps each chain's commits to that chain's result.
    - Post-rewrite (`rebase`): the leading picks that stay as they are are skipped
      (`skip_unnecessary_picks`, drop rows do not stop it) and not reported, except the last one
      when a squash/fixup comes next. Every later replayed commit is reported, fast-forwarded
      ones as `X X`. Squashed commits map to their chain's result. A commit dropped by
      `--empty=drop` maps to the commit it would have gone onto.
    - `--empty=drop` drops a becoming-empty pick even when fixups follow it; they then amend the
      commit before it. A squash chain that ends up empty stops git ("No changes").
  - **Model (libgg):**
    - `todo::groups` now ends a group at exec/break/update-ref rows. A later squash/fixup starts
      a group with `Group::amends` (the previous group's first row). Its template's first
      message is the previous group's message (`baseMessage`); `keptMessage` and
      `squashTemplate` walk `foldedRows`. This fixes the P3-17 note: git leaves an update-ref
      before squash rows at the pre-squash commit, and so does ggui now.
    - `toPlan` emits such rows as `Step::amend` Squash steps. The Rewriter writes the previous
      commit (the branch can point at it) and amends it: same parents and author, the new key.
    - `Result::rewritten` is what post-rewrite and the journal get, git's list as described
      above. `Result::mapping` (parents, branch moves) is unchanged for other rewrites. So the
      non-interactive squash actions now also report squashed commits → squash result.
    - `Plan::reportUnchanged` / `unreported` carry git's skipped prefix (set by `toPlan`).
  - **Commits that become empty (decision):** a new option *Becoming empty* `###ir_empty`, Keep /
    Drop / **Ask** (default). It maps to git's `--empty=keep|drop|stop`, and stop is git's
    interactive default.
    - `Plan::emptied` + `Result::becameEmpty`. A commit "becomes empty" when its finished commit
      (after its squash/fixup rows) has its parent's tree although some of its original commits
      changed something. Commits empty from the start are always kept, as in git.
    - Drop leaves it out: later rows go onto its parent, and post-rewrite maps it there.
    - Ask: the preview shows it kept ("(empty)", tooltip "Start asks…"). Start's rewrite
      pipeline shows "Commits become empty" (after the pre-flight, before the published
      confirmation) with Keep them / Drop them / Cancel. Drop recomputes; Cancel changes nothing.
    - Divergence kept on purpose: a becoming-empty commit with squash/fixup rows after it is judged
      by the whole group. Git's `--empty=drop` would fold those rows into the commit before it.
  - **Tip moved (decision): still refused**, not re-read. A silent re-read would either drop the
    new commits or change the list the user edited. git would also fail its final ref update on
    a moved branch. The builder checks the tip both when preparing and when applying, and the
    `update-ref --stdin` transaction carries old values, so a move during the pre-flight or a
    last-moment race fails atomically too. The editor stays open with the list.
  - **Preview:** amended rows take the amending step's commit. `RebasePreview::aside` lists
    branches left on a finished pre-squash commit (`###irp_aside_<n>`). `droppedEmpty` lists
    commits dropped because they became empty (`###irp_dropped_empty`). Rows of dropped commits
    are left out.
  - **Tests** (`test_rebase_i.cpp`):
    - "update-ref before squash/fixup rows": a fixed case against `git rebase -i` on a copy.
      Checks the template, preview aside, trees, messages, authors, both branches, post-rewrite
      (3 entries, equal to git's), one committed ref transaction and no sequencer state.
    - "randomized differential": fixture with chains of commits per file, fixup!/squash!/amend!
      commits, a commit that becomes empty onto `up`, an empty commit and two stacked branches.
      Each round picks random options (onto, autosquash, update-refs, Keep/Drop/Ask). It makes a
      random todo: chain-preserving interleaving (no conflicts: git would stop), dropped chain
      tails, random pick/reword/squash/fixup/-C/-c, and update-ref rows anywhere. The todo is
      entered through the UI (Alt+↑, action combos). Every message editor gets the text git's
      editor script produces (" reworded" on the first non-comment line).
      - After Start, the same todo runs through `git rebase -i --empty=…` on a copy with that
        editor (`GIT_EDITOR` set per call: the runner's `GIT_EDITOR=true` beats core.editor).
      - Compared: `checkMatches` on both, aside branches, full history (trees, messages, authors)
        of main/stack1/stack2/up, post-rewrite, HEAD tree, a clean worktree, no sequencer state,
        one committed ref transaction (none when nothing moves). One Undo then restores every ref.
      - Seed `0x1818d1ff`, 6 rounds, logged. `GGUI_IR_SEED` / `GGUI_IR_ROUNDS` replay or
        explore. Seeds 1–25 × 25 rounds all passed.
      - Kept out of the generator: rewords with squash rows after them (git opens its editor
        twice, ggui has one editor per group). Also, under Drop, squash/fixup right after the
        becoming-empty commit.
    - "commits that are or become empty": Ask → Cancel / Keep them / Drop them, and the Drop
      option (preview and Start).
    - IR-OPT-EMPTY added to the catalogue.

### [x] P3-19 Interactive rebase: native engine and stop handling
- **Status:** test_rebase_native.cpp (4 scenarios): git rebase -i through git gg sequence-editor (prepared todo, typed messages by the commit on done's last line); stops at edit, break, failing exec, conflicts and a commit that becomes empty, with Continue/Skip/Abort, Amend and continue, Commit with conflicts and a progress popup; Edit remaining todo saved through git rebase --edit-todo (byte-identical to git on a copy); a plain git rebase -i test step detected, edited and finished in a terminal; one journal operation per rebase (ggui, or plain with hooks) and one Undo. Refuses update-ref rows with git < 2.38; Keep committer date is in-memory only. Fixed: hook recorded a detached symbolic HEAD's old value as an oid; Undo of rebases tried to move HEAD and its branch together. Full suite 189/189; coverage 93.0 % line / 79.2 % branch; traceability 632/648 (phase 3: all but IR-CONFLICTED-INPUT).
- **Depends on:** P3-16, P2-24, P2-26
- **Refs:** §4.13 execution 2, plain rebase -i, §4.10 native
- **Do:** `git gg sequence-editor FILE` helper writing prepared todo/messages; run
  `git rebase -i` with `GIT_SEQUENCE_EDITOR`/`GIT_EDITOR` set to it and `GG_OPERATION`;
  stops (edit, break, failing exec, conflicts) use the native UI plus Amend and continue,
  Edit remaining todo (writes `git-rebase-todo` as `--edit-todo` would), progress view
  (done/current/remaining); follow a rebase finished in a terminal; detect plain
  `git rebase -i` from `.git/rebase-merge/`; whole rebase = one journal operation (ggui-run,
  or plain with hooks installed). Never touch sequencer files otherwise.
- **Done when:** spec IDs covered for each stop type and for a plain rebase started as a test
  step.
- **Notes from P3-16:**
  - Enable Start for `todo::Engine::Native` in `RebasePanel::canStart`/`start`. Apply
    `m_options.execEach` with `todo::addExecEach`.
  - Pass Autostash as `--autostash`. Git has no "keep the original committer date" option
    (`--committer-date-is-author-date` uses the author date), so decide how to offer it natively.
  - For Edit remaining todo, open `RebasePanel` with a todo parsed from `git-rebase-todo`. That
    needs a Request variant that skips `todo::read` and uses `todo::parse` + `expand`.
  - Uncovered spec IDs waiting for this task: IR-ACT-EDIT/EXEC/BREAK, IR-OPT-EXEC-EACH,
    IR-ENGINE-USER-CHOICE, IR-ENTRY-STOPPED.
- **Notes from P3-17:**
  - The preview already handles native todos (`toPlan(…, replayStops = true)`). If "Edit remaining
    todo" opens `RebasePanel` mid-rebase, the Context's onto/range must describe the remaining
    part (onto = the current HEAD), or the preview will show the whole range again.
  - `Queue::Preview` / `RebasePreviewEvent` is the pattern for other "compute in memory, show,
    never apply" features.
- **Notes from P3-18:**
  - *Becoming empty* maps directly to `--empty=keep|drop|stop` (Ask = stop). A stop on a
    becoming-empty commit is another native stop to handle.
  - Git opens its editor once per finished squash chain that needs it (before an exec, break or
    update-ref row, or at the chain's end) and once per reword. Chains after such a row amend
    the finished commit (`Group::amends`). A reword with squash rows after it gets the editor
    twice in git but has one message in ggui. The `GIT_EDITOR` helper must hand the typed text to
    the right invocation and keep the other one as it is.
  - The test runner sets `GIT_EDITOR=true`, which beats `core.editor`. Tests that need git's
    editor set it around the call (`ggui::setEnv`, see `gitRebase` in `test_rebase_i.cpp`).
  - The differential helpers in `test_rebase_i.cpp` can drive native-engine comparisons too:
    `gitRebase`, `rewordAll`, `postRewrite`, `history`, `checkAside`, `installRecordingHooks`.
- **Design notes:**
  - **libgg `NativeRebase.{hpp,cpp}`** (namespace `gg::native`), shared by ggui and git-gg.
    - A rebase's identity is `orig-head onto head-name` from `rebase-merge/` ("" = none).
    - Per-worktree state lives in `$GIT_COMMON_DIR/gg/rebase/<wt>/`. It is disposable.
      - `operation`: the journal group (op id and identity).
      - `prepared.json`: the todo, the typed messages and the identity.
  - **`git gg sequence-editor FILE`** (`gg::native::sequenceEditor`) needs `GG_SEQUENCE_DIR`.
    - A file named `git-rebase-todo` gets the prepared todo.
    - Any other file (`COMMIT_EDITMSG`) gets the message prepared for the commit on the last line of
      `rebase-merge/done`. Without one it stays as git wrote it, which is git's default message.
    - `todo::editorMessages` keys a typed message by the row where git opens its editor for it: the
      reword row, or a group's last squash/fixup row when git asks for the combined message. A
      reword followed by squash rows gets the typed text at the end of the chain and git's text at
      the reword. This matches `groupMessage` (checked by the tests' typed messages).
  - **Start** (`RebasePanel::startNative`, `Actions::nativeRebase`):
    - `todo::addExecEach` adds "exec after every commit", then `todo::format`.
    - It runs `git rebase -i --empty=<keep|drop|stop> [--autostash] [--root] [--onto X] <upstream>
      [<branch>]`, with `GIT_SEQUENCE_EDITOR`/`GIT_EDITOR` = `'<dir>/git-gg' sequence-editor`,
      through the mutation queue (`ctx.gitMayFail`, never on the UI thread).
    - The tip-moved check is the same as in memory.
    - `git version` is checked on the worker: git older than 2.45 gets `--empty=ask`.
    - **update-ref with git < 2.38: refused (decision)** ("turn off Update refs…"). Dropping the
      rows silently would leave stacked branches behind.
    - A `--root` run first writes the empty tree (`git mktree`). git's squash-onto commit refers to
      it without writing it, and `git fsck --strict` then fails.
    - Stopped = `rebase-merge/` exists after the run. That is Ok, not an error (git exits 1 for a
      failing exec or conflicts). The panel closes and shows a notification with git's message.
  - **Keep committer date (decision):** git always sets the committer date to now, and
    `--committer-date-is-author-date` means something else. With the native engine the combo is
    disabled at "Use now" (tooltip). Keep original stays an in-memory option.
  - **Stops** (`ActionsRebase.cpp`):
    - `rebaseStep` runs Continue/Skip/Abort/Commit with conflicts/Amend and continue for any
      `rebase-merge` rebase. git's editor gets the prepared messages when `preparedFor` (identity)
      matches; otherwise `GIT_EDITOR=true`, as before.
    - A step that fails but moved `done` on (the next stop) is information, not an error
      (`Actions::onRebaseStep` notification).
    - Amend and continue: `git commit --amend --no-edit` when the index differs from HEAD, then
      `--continue`. It is offered only without native conflicts.
    - Progress: `Snapshot::rebase` (`RebaseProgress`: `done`, `remaining`, `todoText`,
      `headName`), read with `todo::parse` on the snapshot worker. Missing subjects come from the
      commits. The toolbar popup `##rebase_progress` shows them. Why it stopped comes from the last
      done action and the native conflicts.
  - **Edit remaining todo:** `RebasePanel::Request::remaining`.
    - `todo::readRemaining` does parse + expand: onto = upstream = HEAD, `tipRef` = head-name,
      `Context::continuesHead`. A leading squash/fixup is valid, and `toPlan` previews it as
      amending HEAD.
    - Save = `Actions::editRemainingTodo`. It refuses when `git-rebase-todo` changed since the
      read. It merges the typed messages into `prepared.json`, then runs `git rebase --edit-todo`
      with the helper, so git itself validates and writes `git-rebase-todo` (and `update-refs`).
      ggui never writes sequencer files.
  - **One journal operation** (undo-journal.md §4.1):
    - `OperationRecorder` joins the remembered group op while its rebase is in progress (not
      undo/redo), and withholds `end` until the rebase is gone.
    - An op that saw a rebase start (ggui's Start, or `pull --rebase` stopping) becomes the group.
      The hooks do the same for plain git: a `rebase` record keeps a `src:git` op open after its
      process exits, `post-rewrite rebase` ends it, and later commands join it.
    - `closeFinishedGroup` ends a group whose rebase is gone. It runs in the recorder, the hooks and
      before Undo plans.
    - Fixed along the way (both broke Undo of any rebase):
      - The hook recorded HEAD's old value as the branch's commit when git detached a symbolic
        HEAD. It now records `ref:<branch>`.
      - `planUndo` skips refs with equal old/new values, since HEAD ends on its branch again.
      - Resumed operations of a plain rebase record no index (mid-rebase trees broke Undo's
        worktree carry). Ones ggui opened keep the latest index, and the hook adds the final one.
  - **Tests:** `Source/tests/test_rebase_native.cpp` (4 scenarios).
    - edit / break / failing exec with Amend and continue, the progress popup, a typed reword via
      the helper, one op and one Undo, and the CLI refusals.
    - A conflict stop with Run as git rebase and exec each. Edit remaining todo is compared byte for
      byte with `git rebase --edit-todo` on a copy, a changed list is refused, and Commit with
      conflicts runs twice.
    - A plain `git rebase -i` test step, edited in ggui and finished in a terminal: with hooks one
      `[git]` op that Undo restores; ggui-started and terminal-finished is one op; without hooks
      Undo refuses and the group is closed.
    - Refusals: a moved branch, git 2.37 (a fake `git version`) with update-ref and then
      `--empty=ask`, local changes without Autostash, Abort, `--root` with Keep.

### [x] P3-20 Phase 3 gate
- **Status:** Functional gate green: 238/238 scenarios, 633/633 phase 0–3 spec IDs covered. UI-action audit (rule 8) done: docs/ui-actions.md lists 408 UI actions of phases 0–3 (menus, context menus, toolbar, buttons, shortcuts, drag and drop, dialog controls and options, one row per trigger), each with a passing test that drives it through the UI and checks its effect. The audit added tests for 19 actions (Source/tests/test_ui_actions.cpp) and made 14 existing tests check the effect instead of opening and cancelling a dialog or counting journal entries. Bug fixed: a refused git check left the open pending, so after Git required ▸ Quit the Welcome screen stayed disabled. Nothing removed. The traceability matrix now lists tests in sorted order (the same for any sharding).
- **Depends on:** all P3 tasks
- **Do:**
  - Every action of today's app is available under Git semantics, and the functional gate is
    green.
  - **UI-action audit (rule 8):** list every UI action implemented in phases 0–3 (menus,
    context menus, buttons, toolbar, shortcuts, drag and drop, dialog controls and options).
    Map each one to the test that exercises it, write the list to `docs/ui-actions.md`, and
    add tests for every action that has none.
- **Done when:** the functional gate is green and every entry in `docs/ui-actions.md` has a
  passing test.
- **Notes from P3-19:**
  - All §4.13 IDs except IR-CONFLICTED-INPUT are covered. IR-CONFLICTED-INPUT (rebasing commits
    that already have first-class conflicts) has no scenario yet.
  - Any ggui mutation while a `rebase-merge` rebase is in progress joins that rebase's journal
    operation, and it stays open (not undoable) until the rebase ends. A ggui `pull --rebase` that
    stops is grouped the same way.
- **Design notes (functional half):**
  - **Conflicted input** (`test_rebase_i.cpp` "conflicted input", `test_rebase_native.cpp`
    "conflicted input"): the Rewriter already merges region-carrying files with the term algebra.
    - `Result::conflicted` now compares each new commit with *all* its original commits
      (`Pending::contributors`): a conflicted commit squashed into a clean one is carried, not new.
      `Result::resolved` lists new commits whose originals' conflicts are gone.
    - Preview: `RebasePreview::Row::resolved` (files), summary "R resolve conflicts", tooltip
      "Conflicts resolved in:". Start's completion adds an Info notice "N commit(s) no longer have
      first-class conflicts".
    - Differential: carrying (reorder/fixup/reword around conflicted commits) equals
      `git rebase -i` on a copy (regions are text to git). Resolving by rebasing onto the cause
      is ggui-only: git stops with a text conflict there (checked, then aborted).
  - **Undo of a native rebase (fix):** each step of a rebase group records its index even when it
    left it unchanged (`OperationRecorder::finish`), so the group keeps its first and latest index
    and Undo carries the worktree back after changes staged at an edit stop.
  - **Parity:** the shared commit actions (History row menu, Commit ▸ Selected commit) gained
    *Merge into HEAD…* (any commit; "Merge commit '<short>'") and *Rebase HEAD onto this*.
    `Actions::mergeIntoHead` accepts any revision.
  - **Commit menu without a repository** dereferenced a null session (crash); fixed and checked in
    "harness/smoke".
- **Coverage push (stopped 2026-09-28 when the gate was dropped):**
  - Line 93.1 → 96.9 %, branch 79.5 → 88.0 %, with no new `COVERAGE_EXCL`. By directory,
    branch coverage: app 89.3 %, core 87.4 %, gitgg 89.6 %, libgg 85.6 %.
  - Suite: 223/223 tests (29 new), 57 s in 8 headless shards. Under coverage instrumentation
    only, "history/first rows of a large history appear quickly" goes over its 700 ms limit.
  - Commits: 0a9e8a1 … c538423.
  - Bugs fixed:
    - A rewrite started from a linked worktree warned about the wrong worktree's branch.
    - A journal write failure was silently dropped; it now shows a warning.
    - The Diff panel could show a stale diff after the selection changed.
    - Image sizes showed as "(none)" in commit diffs.
    - Moving or reverting lines of a renamed or mode-changed file failed; an executable file
      lost its mode.
    - The History drag-and-drop chooser could not be closed.
    - Server-side progress lines were reported with the phase "remote".
  - Removed: the `DiffPanel::revealRow` test backdoor, the `ggui --askpass` stub, and several
    pieces of dead code.
  - `scripts/run_software_coverage.sh` has `PARALLEL=1` and runs under Xvfb unless
    `GGUI_HEADLESS` is set.
---

## Phase 4 — Worktrees, polish and parity

**Notes for every Phase 4 task (from P3-20): keep `docs/ui-actions.md` current.**
- A task that adds, changes or removes a UI action (menu or context-menu item, button, toolbar
  item, shortcut, drag and drop, double-click, dialog field or option) adds or updates its rows
  in `docs/ui-actions.md` in the same commit: one row per trigger, keyboard and mouse separately.
- Each row needs a passing test that drives the action through the UI and checks its effect in
  the UI and on disk. Opening a dialog and cancelling it tests only Cancel. A task is not done
  while a row is `missing`.
- Cross-check with the coverage report: uncovered lines inside action handlers
  (`scripts/uncovered.py build/coverage/coverage/coverage.lcov Source/app`) point at actions
  without a test. Dialog options read with `form.checked(...)`/`form.choice(...)` share a line
  with other paths, so grep the tests for the field id as well.

### P4-01 Interactive rebase with `--rebase-merges`
- **Depends on:** P3-19
- **Refs:** §4.13
- **Do:** `label`, `reset`, `merge` in the todo model and editor; route to the native engine
  unless the in-memory engine supports it; extend the differential test.
- **Done when:** spec IDs covered.
- **Notes from P3-19:**
  - The native path already hands any todo to git (`todo::format` writes label/reset/merge back
    as parsed). `RebasePanel::startNative` needs `--rebase-merges` in `Actions::NativeRebase::args`.
  - The preview throws on label/reset/merge (`toPlan`), and so does Edit remaining todo's preview
    for a `--rebase-merges` rebase: the list stays editable and savable.
  - `rebaseStep`/`onRebaseStep` and the progress popup are action-agnostic. The stop reason
    only knows edit/break/exec/conflicts.
- **Notes from P3-20:** new todo actions (label, reset, merge) get rows in the "Interactive rebase
  panel" section of `docs/ui-actions.md` (action combo entries, any new key or button).

### P4-02 `git gg sequence-editor` as Git's `sequence.editor`
- **Depends on:** P3-19
- **Refs:** §4.13, §6
- **Do:** opt-in setting so plain `git rebase -i` opens ggui's todo editor window; saved todo
  goes back to git.
- **Done when:** scenario runs plain `git rebase -i` as a step and edits in ggui.
- **Notes from P3-19:**
  - `git gg sequence-editor FILE` exists (`gg::native::sequenceEditor`). Without
    `GG_SEQUENCE_DIR` it exits 1 with "no prepared todo". P4-02 replaces that branch: open ggui's
    editor on FILE and write the saved list back.
  - `todo::readRemaining(repo, text, headName)` builds the Context from a todo text. For a fresh
    `git rebase -i` the head-name/onto files already exist when the sequence editor runs.
  - `RebasePanel::Request::remaining` is the pattern for opening the editor on git's own list.
- **Notes from P3-20:** the opt-in setting and the editor window's Save/Cancel (and closing the
  window) are new rows in `docs/ui-actions.md` (Settings and Interactive rebase sections).

### P4-03 Full worktree management
- **Depends on:** P2-03, P1-20
- **Refs:** §4.7 Worktrees, §4.1
- **Do:** Add…, Remove…, Lock/Unlock, Prune, Repair, Open here, Open in new window (linked
  worktree in a new ggui window), per-worktree journal. No gg rename/forget.
- **Done when:** spec IDs covered.
- **Notes from P3-20:** these items are drawn disabled today (`disabledMenuItem(..., kLater)` in
  `SidePanels.cpp`) and are listed only in the Worktrees section note of `docs/ui-actions.md`.
  Replace that note with one row per item and its dialog controls, each with a test.

### P4-04 Packaging
- **Depends on:** P0-06
- **Refs:** §2.2, §7 Phase 4
- **Do:** install rules and CPack ZIP bundling `ggui` and `git-gg` (so `git gg` resolves on
  `PATH`); Windows MinGW/MSVC packages with resource/icon; Linux package; release builds
  exclude the test engine.
- **Done when:** installed packages on clean Linux and Windows VMs run `ggui` and `git gg`.

### P4-05 Removal checklist audit
- **Depends on:** all earlier tasks
- **Refs:** §9
- **Do:** verify none of the following exist: `<gg/gg.h>`, `gg::gg`/`ggConfig.cmake`, gg CLI
  binary, any `refs/gg/*` writes, conflict metadata, working-tree auto-snapshot,
  max-new-file-size, revsets/filesets, old gg CLI families, fetch auto-fast-forward, "@ is a
  change you edit" wording.
- **Done when:** a documented audit with grep evidence.

### P4-06 Final release gate
- **Depends on:** all tasks
- **Refs:** §7 Phase 4, §8.2
- **Do:** 100 % of §4 spec IDs covered; every implemented feature and every UI action in
  `docs/ui-actions.md` exercised by a passing test (rule 8) on Linux and Windows CI;
  responsiveness scenario passes on both; `COVERAGE_EXCL` count within allowlist; coverage
  report published (informational).
- **Done when:** all gates green on release candidate.
- **Notes from P3-20:** re-run the audit the way P3-20 did: list the widget calls in
  `Source/app` (`MenuItem`, `Button`, `Selectable`, `Checkbox`, `Combo`, `InputText`,
  `Shortcut`/`IsKeyPressed`, drag-and-drop, `Form` fields and buttons), compare with the rows of
  `docs/ui-actions.md`, and check that every listed test still exists (`ggui --list-tests`).

## User feedback (2026-09-27)

Requested by the user during Phase 2. The old ggui graph layout and graph-loading code may be
reused for UF-06 to UF-09 (explicit exception to the clean-room rule).

### [x] UF-01 Text baselines line up across the UI
- **Status:** done. Badges are one text line tall and gutter buttons no longer shift glyphs.
  `UI-TEXT-BASELINE` maps glyph quads in the draw lists back to their baselines and fails on any
  mismatch (checked at 100/125/150 % scale on Welcome, the repository view, every panel, Settings
  tabs and a dialog).

### [x] UF-02 Icon glyphs centred on text
- **Status:** done. The icon font's glyph offset is corrected; `UI-ICON-ALIGN` measures the baked
  glyphs at several sizes for both fonts.

### [x] UF-03 Diff text selectable; editor widget for unified and side-by-side
- **Status:** done. Both views are read-only TextEditor instances (selection, Ctrl+C, find,
  syntax colours). A gutter keeps line numbers, line handles, hunk stage/discard/unstage buttons
  and context expanders. Side by side uses two scroll-synchronised editors with filler lines.
  (`DIFF-SELECT-TEXT`, `DIFF-EDITOR-VIEWS`.)

### [x] UF-04 Reflog, Operations and Blame hidden by default
- **Status:** done (`LAYOUT-HIDDEN-PANELS`). They open from View, or on demand (Blame file).

### [x] UF-05 Graph renders correctly
- **Status:** done. Fixed row pitch, dynamic graph column width, first-parent chains keep their
  column, the Working tree row leads to HEAD (`HIST-GRAPH-CONTINUOUS` checks line continuity and
  row pitch).

### [x] UF-06 Merge commits collapsed by default; toggled with the merge bubble
- **Status:** done (`HIST-MERGE-COLLAPSED-DEFAULT`, `HIST-MERGE-EXPAND`). Branch tips inside a
  merged side stay visible; revealing a hidden commit shows it.

### [x] UF-07 Fast graph loading
- **Status:** done. An incremental date-ordered walk replaces libgit2's sorted revwalk, which walks
  all history first. Collapsed-merge visibility is propagated instead of computed with a
  hide-walk per merge. First rows appear within 0.35 s of opening on the 100k-commit fixture
  (`HIST-LOAD-FAST`, limit 0.7 s; `GGUI_PERF_REPO` measures any other repository too).

### [x] UF-08 "Load more" at the bottom of the graph; walk stops early
- **Status:** done. Pages of 2000 commits; the last row loads the next page (`HIST-SHOW-MORE`).

### [x] UF-09 Error bar replaced by notifications and error popups
- **Status:** done. Important errors open a modal error popup (OK / Copy message). Warnings and
  information are corner toasts that fade out, stay while hovered and can be closed
  (`APP-ERROR-POPUP`, `APP-ERROR-DISMISS`, `APP-NOTIFY-TOAST`).

## User feedback, round 2 (2026-09-27)

Requested by the user after P3-14 (UF-10 … UF-31). All done.

### [x] UF-10 History tooltips only while the list is not scrolling
- **Status:** done. Tooltips are held back for 0.3 s after any scroll change (wheel, scrollbar, keyboard) (`HIST-TOOLTIP-SCROLL`).
- **Do:** suppress row/graph/badge tooltips while the History list is scrolling (wheel,
  scrollbar drag or keyboard); show them again once scrolling stops.

### [x] UF-11 Side-by-side diff shows only code
- **Status:** done. No hunk header lines side by side; hunk stage/discard/unstage moved to the context menu for both views (`DIFF-SBS-CODE-ONLY`, `DIFF-HUNK-MENU`).
- **Do:** no `@@ -a,b +c,d @@` hunk header lines in the side-by-side view; the
  "… N unchanged lines" expander already marks omitted content.

### [x] UF-12 Remotes in Branches share the Remotes context menu
- **Status:** done. The remote node has the Remotes menu; remote-tracking branches get it as a "Remote <name>" submenu (`BR-REMOTE-MENU`).
- **Do:** a remote (and its remote-tracking branches) in the Branches panel gets the same
  context menu as in the Remotes panel.

### [x] UF-13 Copy ID copies the short ID; Shift copies the full ID
- **Status:** done. Short ID by default, full ID with Shift, in History, toolbar, Info, Blame and Reflog (`APP-COPY-ID-SHIFT`).
- **Do:** every "Copy ID" (menus, toolbar, Info, keyboard) copies the short ID by default and
  the full ID when Shift is held; the menu text/tooltip says so.

### [x] UF-14 Full IDs show the short prefix normally and the rest dimmed
- **Status:** done. Info commit ID and ID tooltips (History, Blame, Stashes, toolbar) dim everything after the short prefix (`APP-ID-DIMMED`).
- **Do:** wherever a full commit ID is displayed, draw the short-ID prefix in the standard
  text colour and the remainder dimmed.

### [x] UF-15 Diff: "Compare with HEAD" on the button row
- **Status:** done. "Compare with HEAD" sits on the diff button row, for a commit's or stash's file (`DIFF-VS-HEAD`).
- **Do:** replace "Compare only this file with HEAD" with a "Compare with HEAD" control on the
  same line as the diff toolbar buttons.

### [x] UF-16 Changes: double-click opens the file
- **Status:** done. Double-click opens new files in the editor, others in the diff tool against the parent; it no longer stages (`CHG-DBLCLICK-OPEN`).
- **Do:** double-clicking a file in Changes opens new files in the configured editor, and
  modified files in the configured diff tool (against the parent).

### [x] UF-17 Changes: Patch submenu
- **Status:** done. Patch ▸ Copy / Save... (`CHG-CTX-COPY-PATCH`, `CHG-CTX-SAVE-PATCH`).
- **Do:** replace "Copy patch" and "Save patch..." with a "Patch" submenu holding "Copy" and
  "Save...".

### [x] UF-18 History graph not clipped at the left edge
- **Status:** done. The graph starts inside its column by the outline's overhang (`HIST-GRAPH-NOT-CLIPPED`).
- **Do:** move the graph slightly to the right so the white outline of the current commit is
  not clipped by the left edge.

### [x] UF-19 Default pull method in Settings
- **Status:** done. Settings ▸ Git ▸ Pull method: Merge / Rebase / Rebase, keeping merges / Fast-forward only (`SET-PULL-METHOD`).
- **Do:** Settings lets the user configure the default pull method (`pull.rebase`, merge or
  fast-forward only).

### [x] UF-20 Name and email in Settings
- **Status:** done. user.name and user.email per scope (`SET-IDENTITY`).
- **Do:** Settings lets the user configure `user.name` and `user.email`.

### [x] UF-21 Settings: one field per option, scopes as tabs
- **Status:** done. Scope tabs User / Repository / Worktree, inherited hints, Inherit clears an override; the Worktree tab needs extensions.worktreeConfig (`SET-SCOPE-TABS`, `SET-SCOPE-HINT`, `SET-SCOPE-INHERIT`).
- **Do:** each option has a single field. The scope (worktree / repository / user) is
  chosen with tabs. On a higher-precedence tab where only a lower-precedence value exists,
  show that value as a hint. Where the higher-precedence scope overrides it, offer a way to
  clear the override and inherit the lower-precedence value again.

### [x] UF-22 Tags without "(annotated)"
- **Status:** done (`TAG-LABEL-PLAIN`).
- **Do:** drop the "(annotated)" suffix from tag labels.

### [x] UF-23 "Set upstream" with a branch filter
- **Status:** done. Filter field in the upstream list; Enter picks the first match (`BR-SET-UPSTREAM-FILTER`).
- **Do:** the branch "Set upstream" dialog or menu gets a filter field for the branch list.

### [x] UF-24 History filter hides the graph
- **Status:** done. Text filter or Conflicted only: a three-column table without the graph (`HIST-FILTER-NO-GRAPH`).
- **Do:** while a History filter is active, hide the graph column, because the graph is
  broken for filtered rows anyway.

### [x] UF-25 History keeps its scroll position
- **Status:** done. The rows in view are captured each frame and restored after any change to the rows (`HIST-SCROLL-ANCHOR`).
- **Do:** any change to History (expanding or collapsing merges, loading more, refreshes)
  keeps the scroll position anchored on the rows being viewed.

### [x] UF-26 Remove Track / Untrack from Changes
- **Status:** done. Menu items, dialog, action, tests and spec IDs removed (`CHG-NO-TRACK-UNTRACK`).
- **Do:** remove the "Track" and "Untrack..." context-menu items in Changes
  (`ChangesPanel.cpp`), together with the Untrack dialog (`Session::showUntrackDialog`),
  `Actions::untrack`, and their tests and spec IDs.

### [x] UF-27 Remove the toolbar Previous / Next buttons
- **Status:** done. Move HEAD to parent/child stay in the Commit menu (`TB-NO-PREV-NEXT`).
- **Do:** remove the toolbar buttons that move HEAD to its parent or child (`##tb_prev`,
  `##tb_next` in `AppChrome.cpp`), with their tests and spec IDs.

### [x] UF-28 Toolbar folder button opens the working directory
- **Status:** done (`TB-OPEN-FOLDER`).
- **Do:** the toolbar folder button (`##tb_open`) opens the current repository's working
  directory in the system file manager instead of "Open repository...". Opening a
  repository stays available from the menu, Ctrl+O and the repository switcher.

### [x] UF-29 Changes header for the working tree
- **Status:** done. Header "0000000 Working tree"; Compare with HEAD disabled in Changes and Diff (`CHG-HEADER-WT`).
- **Do:** when the Working tree is selected, the Changes header shows the zero commit ID
  before "Working tree", and "Compare with HEAD" (see UF-15) is disabled.

### [x] UF-30 Author in Info has no button effect
- **Status:** done. A shared plain-text item keeps hover for tooltips and menus but draws no highlight; also used for the toolbar labels and diff placeholders (`INFO-AUTHOR-PLAIN`).
- **Do:** the author line in the change information (Info panel) keeps its right-click
  context menu (copy name/email, "Edit author...") where it is, but shows no hover or click
  highlight, because clicking it does nothing. General rule: items whose click does nothing
  must not look or behave like buttons.

### [x] UF-31 Toolbar branch label and commit ID are plain text
- **Status:** done. Click no longer reveals; tooltip and Copy ID stay (`TB-HEAD-PLAIN`).
- **Do:** the toolbar's branch label and HEAD commit ID are plain text instead of selectables
  (clicking them does nothing useful). Keep the tooltip if it is still useful.
