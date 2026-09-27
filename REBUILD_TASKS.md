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
8. **Coverage.** First-party code (`ggui`, `ggui_core`, `libgg`, `git-gg`) stays > 90 % line
   and > 90 % branch from the integration suite. Any `COVERAGE_EXCL` needs a one-line reason
   and an allowlist update. Coverage is never deferred to the end.
9. **Platforms.** Linux and Windows (MinGW-static and MSVC) for the first release. macOS
   is out of scope.
10. **UI wording.** Git terms: HEAD, branch, commit, staged, unstaged. Never "@ is a change you
    edit", change IDs, aliases, workspaces (except where §4 keeps a label).

## Working notes (for resuming work)

- **Build and test:**
  - Build: `cmake --preset ninja && cmake --build build/ninja`.
  - Tests run inside the real binary: `cd build/ninja && ./bin/ggui --test=FILTER --headless`
    (`--list-tests` lists them).
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
- **Status:** Pipeline done: `scripts/run_software_coverage.sh` (clang profiles with `%p-%m` per process, llvm-profdata merge, llvm-cov lcov for ggui + git-gg; gcov/gcovr fallback) and `scripts/coverage_report.py` (first-party filter, COVERAGE_EXCL with mandatory reasons and `scripts/coverage_excl_allowlist.txt`, > 90 % gate). Runs locally; not yet run in CI. Current numbers (49 % line / 29 % branch) are below the gate until the Phase 1 scenarios exist.
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
  - Gate: > 90 % line and > 90 % branch.
- **Done when:** the gate runs on every CI build and merges `ggui` + `git-gg` profiles.

### [~] P0-14 CI matrix
- **Status:** Written: `.github/workflows/ci.yml` (catalogue check, git 2.36 built and cached, Linux Xvfb + lavapipe with 4 shards × {git 2.36, latest}, gates job merging traces and profiles, MinGW-static and MSVC jobs). Not run: no remote or CI runner is reachable from this environment.
- **Depends on:** P0-12, P0-13
- **Refs:** §7 Phase 0, §8.3
- **Do:**
  - Linux under Xvfb with software Vulkan; Windows (MinGW-static and MSVC).
  - Test against git 2.36 (pinned minimum) and the latest git.
  - Shard the suite; every shard runs with the UI-thread assertion on.
  - Both gates (P0-12, P0-13) switched on.
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

### [~] P1-22 Phase 1 gate
- **Status:** Functional gate: green — 163/163 phase 0–1 spec IDs have passing tests (56 scenarios, all passing in one 2m41s run). Code gate: not met yet — 88.4 % line / 72.8 % branch (target > 90 % each). Remaining gaps are mostly the git runner's cancel/progress/askpass paths and placeholder menus that Phase 2 replaces; the coverage push is scheduled after P2's UI lands (see P2-31).
- **Depends on:** all P1 tasks
- **Do:** browse any fixture repository with today's layout; functional gate for Phase 1 IDs
  and > 90 % line/branch coverage.
- **Done when:** both gates green.

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

### [~] P2-31 Phase 2 gate
- **Status:** Functional gate green: 134/134 scenarios pass, 452/452 phase 0-2 spec IDs covered (docs/traceability.md). Code gate: line 91.8 % (met), branch 76.9 % (target > 90 %: in progress — largest gaps Readers, DiffPanel, Markers, ChangesPanel, Journal).
- **Depends on:** all P2 tasks
- **Do:** everyday-use exit criterion; all failure paths from §8.4 that apply so far (hook
  rejection, git missing/old, locked refs, checkout collision, network failure, cancel,
  corrupt journal); both gates green.

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

### P3-15 Interactive rebase: todo model and validation
- **Depends on:** P3-02
- **Refs:** §4.13
- **Do:** todo model with actions `pick`, `reword`, `edit`, `squash`, `fixup` (incl. `-C`/
  `-c`), `drop`, `exec`, `break`, `update-ref`; Git-order read/write; validation (first row
  not squash/fixup; warn when dropping sole branch commits; published warning); autosquash
  placement for `fixup!`/`squash!`/`amend!`; squash message assembly the way Git does;
  engine selection (in-memory unless `edit`/`break`/`exec` or user choice) with reason.
- **Done when:** exercised by P3-16 … P3-19.
- **Design notes (not started):**
  - **Todo model:** put it in libgg (`Todo.hpp/.cpp`) so `git-gg` can share it.
    - Parse and format Git's todo, with validation.
    - Autosquash, with `amend!` placed as `fixup -C`.
    - Squash messages: fill Git's commented template, then apply `cleanup=strip`. The
      `squash!` subject line is commented out, as in Git ≥ 2.32.
  - **In-memory engine:** map the todo onto `gg::rewrite::Plan`.
    - pick/reword → Pick with a message override.
    - squash/fixup → `Step::Kind::Squash`.
    - drop → `dropped`; update-ref → `refsToSteps`.
    - Run it through `Actions::rewrite` (pre-flight, one update-ref, one Undo).
    - "Keep committer date" needs a committer override on `Step`/`Plan`. The Rewriter
      currently always signs with the default committer.
  - **Native engine:**
    - `Source/gitgg/SequenceEditor.cpp` is a stub. It should write the prepared todo and
      messages.
    - Run `git rebase -i` with `GIT_SEQUENCE_EDITOR`/`GIT_EDITOR` and `GG_OPERATION`.
    - The stop UI builds on `App::drawStateBadge`, `Actions::continueOperation` and
      `detectState` (`Source/core/Readers.cpp`, which already reads `.git/rebase-merge/`).
  - **Tests:** `Source/tests/test_rebase_i.cpp`, including the differential test against
    `git rebase -i` on a copy.

### P3-16 Interactive rebase: todo editor UI and entry points
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

### P3-17 Interactive rebase: live preview
- **Depends on:** P3-16
- **Refs:** §4.13 live preview, §3.1
- **Do:** compute the plan in memory on a worker (latest edit wins); preview graph beside the
  list showing first-class conflicts, non-text conflicts needing decisions, empty commits,
  and moving branches.
- **Done when:** preview matches the executed result in scenarios; no frame stalls.

### P3-18 Interactive rebase: in-memory engine
- **Depends on:** P3-17, P3-03
- **Refs:** §4.13 execution 1, §5 R3
- **Do:** run the todo through the rewrite engine; pre-flight for non-text conflicts; one
  `update-ref --stdin`; worktree update after; `post-rewrite rebase` with mapping; one
  journal operation (one Undo); no sequencer state; cancel before apply = no change.
- **Done when:** randomized differential scenario vs `git rebase -i` on a copy matches trees,
  messages, authors and branch positions; autosquash order verified.

### P3-19 Interactive rebase: native engine and stop handling
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

### P3-20 Phase 3 gate
- **Depends on:** all P3 tasks
- **Do:** every action of today's app available under Git semantics; both gates green.

---

## Phase 4 — Worktrees, polish and parity

### P4-01 Interactive rebase with `--rebase-merges`
- **Depends on:** P3-19
- **Refs:** §4.13
- **Do:** `label`, `reset`, `merge` in the todo model and editor; route to the native engine
  unless the in-memory engine supports it; extend the differential test.
- **Done when:** spec IDs covered.

### P4-02 `git gg sequence-editor` as Git's `sequence.editor`
- **Depends on:** P3-19
- **Refs:** §4.13, §6
- **Do:** opt-in setting so plain `git rebase -i` opens ggui's todo editor window; saved todo
  goes back to git.
- **Done when:** scenario runs plain `git rebase -i` as a step and edits in ggui.

### P4-03 Full worktree management
- **Depends on:** P2-03, P1-20
- **Refs:** §4.7 Worktrees, §4.1
- **Do:** Add…, Remove…, Lock/Unlock, Prune, Repair, Open here, Open in new window (linked
  worktree in a new ggui window), per-worktree journal. No gg rename/forget.
- **Done when:** spec IDs covered.

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
- **Do:** 100 % of §4 spec IDs covered; > 90 % line and branch coverage on Linux and Windows
  CI; responsiveness scenario passes on both; `COVERAGE_EXCL` count within allowlist.
- **Done when:** all gates green on release candidate.

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

Requested by the user after P3-14. Not started.

### UF-10 History tooltips only while the list is not scrolling
- **Do:** suppress row/graph/badge tooltips while the History list is scrolling (wheel,
  scrollbar drag or keyboard); show them again once scrolling stops.

### UF-11 Side-by-side diff shows only code
- **Do:** no `@@ -a,b +c,d @@` hunk header lines in the side-by-side view; the
  "… N unchanged lines" expander already marks omitted content.

### UF-12 Remotes in Branches share the Remotes context menu
- **Do:** a remote (and its remote-tracking branches) in the Branches panel gets the same
  context menu as in the Remotes panel.

### UF-13 Copy ID copies the short ID; Shift copies the full ID
- **Do:** every "Copy ID" (menus, toolbar, Info, keyboard) copies the short ID by default and
  the full ID when Shift is held; the menu text/tooltip says so.

### UF-14 Full IDs show the short prefix normally and the rest dimmed
- **Do:** wherever a full commit ID is displayed, draw the short-ID prefix in the standard
  text colour and the remainder dimmed.

### UF-15 Diff: "Compare with HEAD" on the button row
- **Do:** replace "Compare only this file with HEAD" with a "Compare with HEAD" control on the
  same line as the diff toolbar buttons.

### UF-16 Changes: double-click opens the file
- **Do:** double-clicking a file in Changes opens new files in the configured editor, and
  modified files in the configured diff tool (against the parent).

### UF-17 Changes: Patch submenu
- **Do:** replace "Copy patch" and "Save patch..." with a "Patch" submenu holding "Copy" and
  "Save...".

### UF-18 History graph not clipped at the left edge
- **Do:** move the graph slightly to the right so the white outline of the current commit is
  not clipped by the left edge.

### UF-19 Default pull method in Settings
- **Do:** Settings lets the user configure the default pull method (`pull.rebase`, merge or
  fast-forward only).

### UF-20 Name and email in Settings
- **Do:** Settings lets the user configure `user.name` and `user.email`.

### UF-21 Settings: one field per option, scopes as tabs
- **Do:** each option has a single field. The scope (worktree / repository / user) is
  chosen with tabs. On a higher-precedence tab where only a lower-precedence value exists,
  show that value as a hint. Where the higher-precedence scope overrides it, offer a way to
  clear the override and inherit the lower-precedence value again.

### UF-22 Tags without "(annotated)"
- **Do:** drop the "(annotated)" suffix from tag labels.

### UF-23 "Set upstream" with a branch filter
- **Do:** the branch "Set upstream" dialog or menu gets a filter field for the branch list.

### UF-24 History filter hides the graph
- **Do:** while a History filter is active, hide the graph column, because the graph is
  broken for filtered rows anyway.

### UF-25 History keeps its scroll position
- **Do:** any change to History (expanding or collapsing merges, loading more, refreshes)
  keeps the scroll position anchored on the rows being viewed.
