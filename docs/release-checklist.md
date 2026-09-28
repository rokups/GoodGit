# Release checklist (P4-06, final release gate)

The release gate (REBUILD_PLAN §7 Phase 4, §8.2; standing rule 8): every §4 spec ID has a passing
test, every implemented feature and every UI action in `docs/ui-actions.md` is exercised by a
passing test on Linux **and** Windows CI, the responsiveness scenario passes on both, the
`COVERAGE_EXCL` count is within its allowlist, and the coverage report is published
(informational). The packages install and run on clean Linux and Windows machines.

Status on 2026-09-28: **the local (Linux) gate is green.** Everything that needs a CI runner, a
Windows machine or a clean VM is still open (section 4). P4-06 stays partial until those pass on
the release candidate.

## 1. Local gate results (Linux, 2026-09-28)

Host: Arch-based Linux (kernel 7.2.5), GCC 16 (C++23, `-Werror`), 32 cores, headless (SDL
offscreen, Vulkan radv). Tree: the P4-06 commits on top of `4c11693` (P4-05).

| Check | Result |
|---|---|
| Clean build (`rm -rf build/ninja`, preset `ninja`) | 768 steps in 1 min 42 s, no warnings |
| Suite, git 2.55.0 (system git), 4 shards | **257/257 passed** (84 s) |
| Suite, git 2.36.0 (pinned minimum), 4 shards | **254 passed, 3 skipped, 0 failed** (86 s); the skips need a newer git, see §3 |
| Traceability, both git versions merged (as CI does) | **660/660** spec IDs of phases 0–4 (`docs/traceability.md`) |
| Traceability, git 2.55.0 alone | 660/660 |
| Traceability, git 2.36.0 alone | 659/660: only HOOK-CONFIG-DEFINED (hooks defined in the configuration need git 2.54) |
| UI actions (`docs/ui-actions.md`) | **450 rows, all `tested`, each with a passing test** (191 distinct tests); also on each git version alone |
| UI actions, reverse direction | every `MenuItem`/`Button`/`Checkbox`/`Selectable`/`InputText`/`IsKeyPressed`/`Form` field and button added since P3-20 (P4-01 … P4-05) has a row; a label sweep of all of `Source/app` finds no action missing from the file. P4-04 added CLI options only, P4-05 changed two labels |
| Responsiveness (P1-21), `engine/responsiveness on the large repository` | 1,763 frames, worst 2.0 ms, **none over 33 ms** (100k commits, 5.2k refs, 50k+ files, slow-git 50 ms, UI-thread assertion on); passes in the git 2.36.0 run too |
| Removal audit (`scripts/removal_audit.sh --packages build/packages`) | **clean** (20 checks, 4 packages) |
| `COVERAGE_EXCL` | **2 markers** (`Source/app/shell/App.cpp`, native pickers), allowlist 2 |
| Coverage (informational, clang, 4 shards under Xvfb) | **95.95 % line** (18,355/19,130), **86.51 % branch** (9,905/11,450); app 97.47/89.14, core 96.17/86.31, gitgg 93.42/80.97, libgg 93.30/82.57 |
| Linux packages (`scripts/package_smoke.sh`, fresh release build) | **81 checks passed** on the .tar.gz, .zip and .deb (files, desktop file, no test engine, ldd/RUNPATH, runtime checks in a clean user+mount namespace incl. `git gg new/undo` and `ggui --smoke`) |
| MinGW package (`package-mingw-cross`, `scripts/package_smoke_windows.sh`) | built; 8 file/import/string checks passed; runtime checks need Windows. The MinGW test build (`mingw-x64-cross`) also builds, and `ggui.exe --list-tests` lists 257 tests under wine |

Notes:
- git 2.36.0 was built the way CI's `git-min` job does (`make prefix=~/git-2.36 NO_GETTEXT=1
  NO_TCLTK=1 all install`); GCC 15 and later also need `CFLAGS="-O2 -std=gnu17"` (C23 makes
  `unreachable` a macro). CI's Ubuntu 24.04 has GCC 13, so the job is unchanged.
- The Linux packages built here need glibc 2.43 (this host). Release packages must come from the
  CI job (Ubuntu 24.04, glibc 2.39).
- `xvfb-run` on this host sometimes exits 1 after a passing run (its cleanup finds Xvfb gone);
  `run_software_coverage.sh` now takes the result from the trace files.

## 2. Found and fixed by this gate

The suite had never run against git 2.36, the pinned minimum. The first run had 21 failing tests.
Fixes to the app:
- **Merge tool on a first-class conflict** used `git ls-files --format` (git 2.38): now
  `ls-files -s -z`.
- **post-rewrite hook after an in-memory rewrite** used `git hook run --to-stdin` (git 2.40): before
  2.40 the hook runs the way git's sequencer does (hook path from `git rev-parse --git-path`, only
  when executable, the list on stdin), through a one-shot `git -c alias.…=!…` so git's shell runs
  it on every platform (removal-audit allowlist entry with the reason).
- **Hooks in wrapper mode** (every git before 2.54, which has no config-defined hooks) took the
  hook script's shell for the git command: every hook call opened its own journal operation,
  labelled "/bin/sh .git/hooks/…", so one plain git command could need several Undos. The hook now
  walks up to the nearest git process (Linux `/proc`, Windows toolhelp).
- **HEAD switches on older git**: git 2.36 points HEAD at another branch (`checkout <branch>`,
  `checkout -b`, `switch`) and reattaches it at the end of a rebase without a ref transaction, so
  the journal never saw it and Undo refused ("the branch is checked out", "HEAD moved outside the
  journal"). The post-checkout hook records a branch switch when the operation has no HEAD change
  yet (from `@{-1}`), and a rebase's operation records HEAD's final value when its end is seen.
- **Big fetches on older git** (one ref transaction per ref before 2.51) split into several
  operations once the `begin` record left the 64 KiB tail the hook searches: any record of the
  operation in the tail now counts.
- `docs/spec/undo-journal.md` (U1) describes the three hook changes.

Fixes to the tests and gates:
- `GG_REQUIRE_GIT(major, minor, why)` ends a test as **skipped** (trace status `skipped`, not a pass)
  when its subject needs a newer git; tests where an `update-ref` row was incidental turn Update
  refs off on git before 2.38 (as ggui's refusal message asks a user to), and oracles run git
  without the options it lacks (`--update-refs`, `--empty=stop`) and forgive git 2.36's octopus
  label names. The wrapper-mode hooks test now checks the operation label and a command with two ref
  transactions.
- New `scripts/ui_actions_check.py`: the UI-action gate (every row `tested`, every named test in the
  traces and not failed, at least one passing test per row).
- CI: `GGUI_DELIVERED_PHASE` was still 0 (now 4); the coverage step still gated on > 90 %
  (`coverage_report.py` default, now report-only; COVERAGE_EXCL allowlist still enforced); the
  UI-action gate is new; the Windows jobs now gate their own traces.

## 3. Tests skipped on git 2.36 (need a newer git)

| Test | Needs | Why |
|---|---|---|
| `rebase-i/update-ref before squash/fixup rows: …` | 2.38 | subject is update-ref rows; git rebase -i with them is the reference |
| `rebase-native/typed squash messages reach git's editor, also for a squash that amends after an update-ref row; …` | 2.38 | subject is a squash after an update-ref row in git rebase -i |
| `hooks/config-defined hooks: a repository path with a quote, a partial installation completed` | 2.54 | hooks defined in the configuration |

Their spec IDs and UI rows are covered by the latest-git leg. ggui's refusal of update-ref rows on
git before 2.38 is itself tested (`rebase-native/git rebase -i refusals and options: …`, with a
faked `git version`).

## 4. Remaining steps (need CI, Windows or clean VMs)

### 4.1 CI (`.github/workflows/ci.yml`) on the release-candidate commit

Push the branch to a GitHub remote (none is reachable from the rebuild environment). Every job
must be green:

| Job | What must pass |
|---|---|
| `catalogue` | `traceability.py --check`; `removal_audit.sh` |
| `git-min` | builds and caches git 2.36.0 |
| `linux` (8 jobs: git minimum/latest × shards 0–3) | the suite under Xvfb + lavapipe with coverage instrumentation; first CI restore of the large fixture cache (`fixtures-large-v1`, P0-11) |
| `gates` | functional gate 660/660 with no failing test; UI-action gate; coverage report uploaded (`reports` artifact: `traceability.md`, `coverage.md`), COVERAGE_EXCL within the allowlist |
| `windows-mingw` | first run of the suite on Windows (headless, D3D12/WARP) incl. the responsiveness scenario (P1-21); gates on its own trace |
| `windows-msvc` | first native MSVC build (P0-06) and suite run; gates on its own trace |
| `package-linux` | `package_smoke.sh --container ubuntu:24.04` (the .deb installed with apt in a clean container); `removal_audit.sh --packages` |
| `package-mingw`, `package-msvc` | `package_smoke_windows.sh` incl. its runtime checks on Windows |

Watch: the Windows gates see one git version. With git older than 2.54 on the runner the
config-defined hooks test is skipped there and the traceability step reports HOOK-CONFIG-DEFINED
missing. MSYS2's git is current; for the MSVC job check the logged `git --version` and install a
current Git for Windows if needed. Timing tests (`HIST-LOAD-FAST` 0.7 s, the 33 ms frame limit,
`HOOK-FAST` 5 s) have not been seen on shared runners yet.

### 4.2 Manual checks on clean machines

Linux: a fresh Ubuntu 24.04 desktop VM, plus one other glibc ≥ 2.39 distribution for the tar.gz.
1. `sudo apt install ./ggui_<version>_amd64.deb`; the dependencies (git, Vulkan ICD, X11) come in.
2. Start ggui from the application menu (icon, window class); open a repository; stage, commit,
   Undo; interactive rebase with an edit stop; fetch/push to a local bare remote.
3. In a terminal: `ggui --version`, `git gg --version`, `git gg hooks install`, a plain
   `git commit`, then Undo in ggui.
4. `sudo apt remove ggui` leaves nothing under /usr. Unpack the tar.gz in `$HOME` on the second
   distribution and repeat step 2 from `bin/ggui`.

Windows: a clean Windows 10 or 11 VM with Git for Windows (one with git 2.36–2.37 if possible, one
with the current release), for the MinGW ZIP and the MSVC ZIP each.
1. Unzip; start `ggui.exe` from Explorer: no console window, the icon, and version details
   (Properties ▸ Details) on `ggui.exe` and `git-gg.exe`.
2. From cmd and PowerShell: `ggui --version` prints in the console; with the folder on `PATH`,
   `git gg --version`, `git gg hooks install`, a plain `git commit`, Undo in ggui.
3. A repository on another drive and in a path with spaces and non-ASCII characters; Worktrees ▸
   Open in new window (the `GetModuleFileNameW`/`CreateProcessW` path, P4-03); ggui as
   `sequence.editor` for a `git rebase -i` started in Git Bash (P4-02).
4. Delete the folder: nothing else is left besides ggui's settings.

### 4.3 Outside the build

- P0-01: pull `gg` and `ggui` upstream with SSH access, diff against the analysed commits, fold any
  behaviour change into §4.
- P0-02, P0-04, P0-05: the owner reviews and freezes the UI spec and the M1/U1 specs.
- P0-06: retire the `gg` repository (archive it, note it in its README).

## 5. How to run each check

```sh
# Clean build and the suite (4 shards; run every shard I = 0..3)
rm -rf build/ninja && cmake --preset ninja && cmake --build build/ninja
cd build/ninja && for i in 0 1 2 3; do ./bin/ggui --test --headless --shard=$i/4 --trace=/tmp/t-latest-$i.json & done; wait

# The same against git 2.36.0 (built like CI's git-min job)
curl -sSLO https://mirrors.edge.kernel.org/pub/software/scm/git/git-2.36.0.tar.xz && tar xf git-2.36.0.tar.xz
make -C git-2.36.0 -j"$(nproc)" prefix="$HOME/git-2.36" NO_GETTEXT=1 NO_TCLTK=1 CFLAGS="-O2 -std=gnu17" all install
cd build/ninja && for i in 0 1 2 3; do PATH=$HOME/git-2.36/bin:$PATH ./bin/ggui --test --headless --shard=$i/4 --trace=/tmp/t-min-$i.json & done; wait

# Gates (both git versions merged, as CI does)
scripts/traceability.py --phase 4 --out docs/traceability.md /tmp/t-latest-*.json /tmp/t-min-*.json
scripts/ui_actions_check.py /tmp/t-latest-*.json /tmp/t-min-*.json

# Responsiveness alone, with its frame numbers in the log
GGUI_LOG_FILE=/tmp/resp.log build/ninja/bin/ggui --test='responsiveness on the large repository' --headless
grep 'frames' /tmp/resp.log

# Removal audit (-v lists the allowed hits and why), with the packages
scripts/removal_audit.sh --packages build/packages

# Coverage report (informational) and the COVERAGE_EXCL allowlist check
NO_GATE=1 SHARDS=4 PARALLEL=1 scripts/run_software_coverage.sh   # build/coverage/coverage/summary.md

# Packages
scripts/package_smoke.sh                        # add --container ubuntu:24.04 where docker/podman works
cmake --workflow --preset package-mingw-cross && scripts/package_smoke_windows.sh build/packages/ggui-*-windows-x64-mingw.zip
```

The UI-action reverse check (P3-20 notes in P4-06): list the widget calls in `Source/app`
(`git grep -nE 'MenuItem|Button|Selectable|Checkbox|Combo|InputText|Shortcut|IsKeyPressed|DragDrop|buttons.push_back' -- Source/app`)
and compare them with the rows of `docs/ui-actions.md`; `git diff <last audit>..HEAD -- Source/app`
limits it to what changed.
