# Removal checklist audit (REBUILD_PLAN §9, task P4-05)

Audit date: 2026-09-28. Scope: this repository's first-party, tracked files: `Source/`, `cmake/`
(except the vendored `cmake/CPM.cmake`), `CMakeLists.txt`, `CMakePresets.json`, `scripts/`,
`docs/`, `res/`, `.github/`, plus the install rules and the built packages. Build directories and
fetched third-party code are out of scope. No old `ggui`/`gg` sources were consulted (clean room).

**Re-run:** `scripts/removal_audit.sh` repeats every grep check below and exits 1 on a violation
(`-v` prints the allowed hits with their reason; `--packages build/packages` also lists the
archives). CI runs it in the `catalogue` job, and with `--packages` in the `package-linux` job.

**Behavioral checks** (in the test suite, run inside the real `ggui`):

- **Post-test hook, every test** (`Source/tests/TestRunner.cpp`, `Scenario::gitTransparent` in
  `Source/tests/Harness.cpp`). After each of the ~400 scenarios, every repository the test
  touched must meet two conditions:
  - it has no ref under `refs/gg/` except the ones the test itself planted with plain git (the
    C3 fixtures);
  - `$GIT_COMMON_DIR/gg/` holds only `journal` (and its `.lock`), `cache/`, `hooks/`, `rebase/`
    and `symref-*`.
- **`removal/ggui, git gg and plain git leave no refs/gg; …`** (`REMOVAL-NO-GG-STATE`,
  `Source/tests/test_removal.cpp`). With the managed hooks installed, it runs:
  - through the UI: an in-memory rebase that produces a first-class conflict, stash and pop from
    the toolbar, a reword, then Undo and Redo;
  - `git gg new`, `undo`, `redo` and `op log`;
  - plain `git commit`, `tag` and `branch`, journaled by the hooks.

  It then asserts that `git for-each-ref refs/gg` is empty and that every ref is under
  `refs/heads/`, `refs/tags/` or is `refs/stash`. `.git/gg` must contain only `journal`, `cache`
  and `hooks`. Finally it uninstalls the hooks, deletes `.git/gg` and reopens. The refs, the
  status, `git gg conflicts` output and the History conflict mark are unchanged; only the
  Operations list is empty.
- **`removal/git gg has none of the old gg command families`** (`REMOVAL-NO-OLD-CLI`).
- Existing scenarios cited below: `conflicts/…` (`test_conflicts.cpp:258-264`, deleting
  `.git/gg` changes no conflict), `network/fetch: … only remote-tracking refs move`
  (`REMOTE-FETCH-NO-FF`), and the C3 cleanup tests (`GGREFS-*`).

A shell run of the same kind (git-gg from `build/ninja/bin`, clean `GIT_CONFIG_GLOBAL`):

```
$ git gg hooks install; git gg new -m "from git gg"; git gg undo; git gg redo
$ git commit -q --allow-empty -m plain; git branch topic HEAD~1; git tag v1
$ echo b > a; git stash -q; git stash pop -q; git checkout -q -- a
$ git for-each-ref refs/gg
(nothing)
$ git for-each-ref --format='%(refname)'
refs/heads/main
refs/heads/topic
refs/tags/v1
$ find .git/gg | sort
.git/gg
.git/gg/hooks
.git/gg/hooks/run
.git/gg/journal
$ git gg hooks uninstall; rm -rf .git/gg; git fsck --strict --no-progress && echo fsck-ok
dangling commit 1321a8de…      (the popped stash; plain git behaviour)
fsck-ok
```

## Summary

| # | Item (§9 / P4-05) | Verdict |
|---|---|---|
| 1 | `<gg/gg.h>` | Absent |
| 2 | `gg::gg` target, `ggConfig.cmake`, `find_package(gg)` | Absent |
| 3 | `gg` CLI binary in install bundles | Absent |
| 4 | `refs/gg/*` writes (workspaces, commit-aliases, visible-heads, alias GC) | Absent. **Fixed:** the journal skipped `refs/gg/cache*` |
| 5 | Conflict metadata (`refs/gg/conflicts/*` or other) | Absent |
| 6 | Working-tree auto-snapshot into `@` | Absent |
| 7 | max-new-file-size setting | Absent |
| 8 | Revset and fileset languages | Absent |
| 9 | Old gg CLI families | Absent |
| 10 | Agent skill docs | Absent |
| 11 | Fetch auto-fast-forward of local branches | Absent |
| 12 | "@ is a change you edit" wording; change IDs, aliases, workspaces (rule 10) | Absent. **Fixed:** "@" in the Blame header, a "Change" column in Reflog |

Grep commands below use `git grep` over the scope above, with
`FP="Source cmake CMakeLists.txt CMakePresets.json scripts docs res .github ':!cmake/CPM.cmake'"`
and the audit's own two files excluded.

## 1. `<gg/gg.h>`

```
$ git grep -n -E 'gg/gg\.h' -- $FP
(no output)
```

Verdict: **absent**. There is no public header; libgg's headers are `Source/libgg/include/libgg/*.hpp`,
used only inside the build (REBUILD_PLAN §0).

## 2. `gg::gg` / `ggConfig.cmake` / `find_package(gg)`

```
$ git grep -n -E 'gg::gg([^A-Za-z0-9_]|$)|ggConfig|gg-config\.cmake|find_package\(gg[ )]|install\(EXPORT|export\(TARGETS|configure_package_config_file|write_basic_package_version_file' -- $FP
(no output)
$ git grep -n -E 'add_library' -- CMakeLists.txt Source cmake ':!cmake/CPM.cmake'
Source/core/CMakeLists.txt:3:add_library(ggui_core STATIC ${CORE_SOURCES})
Source/libgg/CMakeLists.txt:3:add_library(libgg STATIC ${LIBGG_SOURCES})
cmake/Dependencies.cmake:40:add_library(ggui_libgit2 INTERFACE)
cmake/Dependencies.cmake:85:add_library(imgui STATIC
cmake/Dependencies.cmake:144:add_library(imgui_color_text_edit STATIC ${GGUI_CTE_SOURCES})
cmake/Dependencies.cmake:148:add_library(icon_font_headers INTERFACE)
```

(`gg::gguiProgram()` in `ActionsWorktrees.cpp` and `SequenceEditor.cpp` is a C++ function in
namespace `gg`, not the old target, hence the word boundary.) `libgg` is static, named
`gg_internal`, and never installed.

Verdict: **absent**.

## 3. `gg` CLI binary in install bundles

```
$ git grep -n -E 'add_executable' -- CMakeLists.txt Source cmake
Source/app/CMakeLists.txt:7:add_executable(ggui ${APP_SOURCES})
Source/gitgg/CMakeLists.txt:3:add_executable(git-gg ${GITGG_SOURCES})
$ git grep -n -E 'install\(' -- CMakeLists.txt cmake ':!cmake/CPM.cmake'
cmake/Packaging.cmake:27:install(TARGETS git-gg RUNTIME DESTINATION ${GGUI_INSTALL_BINDIR} COMPONENT ggui)
cmake/Packaging.cmake:29:    install(TARGETS ggui RUNTIME DESTINATION ${GGUI_INSTALL_BINDIR} COMPONENT ggui)
cmake/Packaging.cmake:31:        install(FILES ${PROJECT_SOURCE_DIR}/res/ggui.desktop DESTINATION ...
cmake/Packaging.cmake:33:        install(FILES ${PROJECT_SOURCE_DIR}/res/ggui.png ...
cmake/Packaging.cmake:38:    install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION ${GGUI_INSTALL_DOCDIR} RENAME LICENSE.txt ...
cmake/Packaging.cmake:41:    install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION ${GGUI_INSTALL_DOCDIR} COMPONENT ggui)
```

Installed package contents (`build/packages`, built by P4-04):

```
$ tar tzf ggui-0.1.0-linux-x86_64.tar.gz | grep -v '/$'
ggui-0.1.0-linux-x86_64/bin/ggui
ggui-0.1.0-linux-x86_64/bin/git-gg
ggui-0.1.0-linux-x86_64/share/applications/ggui.desktop
ggui-0.1.0-linux-x86_64/share/doc/ggui/LICENSE
ggui-0.1.0-linux-x86_64/share/icons/hicolor/256x256/apps/ggui.png
$ (deb data.tar) usr/bin/ggui usr/bin/git-gg usr/share/applications/ggui.desktop
                 usr/share/doc/ggui/LICENSE usr/share/icons/hicolor/256x256/apps/ggui.png
$ unzip -l ggui-0.1.0-windows-x64-mingw.zip
ggui-0.1.0-windows-x64-mingw/LICENSE.txt
ggui-0.1.0-windows-x64-mingw/ggui.exe
ggui-0.1.0-windows-x64-mingw/git-gg.exe
```

`scripts/package_smoke.sh` already fails on any other installed file, and
`removal_audit.sh --packages` rejects headers, CMake configs, libraries and a `gg` binary.

Verdict: **absent**. The only executables are `ggui` and `git-gg` (`git gg`, §6).

## 4. `refs/gg/*` writes

```
$ git grep -n 'refs/gg' -- $FP ':!Source/tests' ':!docs'
Source/app/shell/SessionDialogs.cpp:477:    // Commits kept alive only by refs/gg/* (read with libgit2 on a worker).
Source/app/shell/SessionDialogs.cpp:520:                + " refs under refs/gg/ were left by the old gg tool. ggui does not use them.\n"
Source/core/Readers.cpp:309:        } else if (name.rfind("refs/gg/", 0) == 0) {
Source/core/Readers.cpp:314:    // Symbolic refs under refs/gg are also leftovers.
Source/core/include/core/Types.hpp:150:    std::vector<std::string> oldGgRefs; // leftover refs/gg/* (C3)
Source/libgg/include/libgg/Operation.hpp:19:// leftovers of the old gg under refs/gg/ are journaled like any other ref and their cleanup is undoable).
$ git grep -n -E '(update-ref|git_reference_create|git_reference_symbolic_create|"create ").*refs/gg' -- Source ':!Source/tests'
(no output)
$ git grep -n -i -E 'workspaces/|commit-aliases|visible-heads|alias.?gc|alias.?prefix' -- $FP
(no output)
```

Why each allowed hit is fine (all belong to the C3 one-time cleanup, P2-30):

- `Readers.cpp:309/314` and `Types.hpp:150`: the snapshot **reads** leftover refs into
  `oldGgRefs`, so the dialog can offer the cleanup. It is read-only (libgit2 iteration).
- `SessionDialogs.cpp:477/520`: the "Old gg data found" dialog, with its message and a comment.
  It lists commits only those refs keep alive (a read on a worker).
- The cleanup itself (`Actions::cleanUpOldGgRefs`, `Actions.cpp:937-949`) never names `refs/gg`.
  It creates the branches the user chose with `git branch`, then sends `delete <ref>` for every
  leftover in one `git update-ref --stdin`. Undo of the cleanup restores the user's own leftovers
  through the journal. That is putting back what the user had, not a ggui write.
- `Operation.hpp:19`: a comment (see the fix below).
- Tests (`test_setup.cpp`, `test_ui_actions.cpp`) plant old refs with plain `git update-ref` to
  drive C3. `Harness.cpp/.hpp` assert after every test that no other `refs/gg` ref exists.
  `docs/traceability.md` and `spec_catalogue.txt` are spec text.

**Violation found and fixed.** `readRefValues` (`Source/libgg/Operation.cpp:39`), which gives the
journal every ref's before and after values, skipped `refs/gg/cache*` ("except refs/gg/ caches").
Nothing in the code writes such refs; ggui's caches are files under `.git/gg/cache/`. The
exclusion implied a private ref namespace, and it had a real effect: an old-gg leftover under
`refs/gg/cache…` would have been deleted by the C3 cleanup without being journaled, so Undo could
not bring it back. The exclusion is removed, and the journal now tracks every ref under `refs/`.
Test: the C3 scenario (`setup/old gg refs: …`, `GGREFS-UNDO`) now also plants
`refs/gg/cache/heads` and checks that Undo restores the full `for-each-ref` output.

Verdict: **absent** (after the fix).

## 5. Conflict metadata

```
$ git grep -n -i -E 'refs/gg/conflicts|conflict[-_ ]?metadata|git_note_|"notes"' -- $FP ':!docs/spec' ':!Source/tests'
(no output)
$ git grep -n -E '"gg" / ' -- Source ':!Source/tests'
Source/core/Engine.cpp:487:   ... / "gg" / "hooks" / "run", ec);
Source/libgg/Conflicts.cpp:114:        m_file = commonDir / "gg" / "cache" / "conflicts-v1";
Source/libgg/Hooks.cpp:94:    p.runner = p.commonDir / "gg" / "hooks" / "run";
Source/libgg/Hooks.cpp:353:        const fs::path pending = commonDir / "gg" / ("symref-" + operationId(false));
Source/libgg/NativeRebase.cpp:84:    return ... / "gg" / "rebase" / worktreeKey(repo);
Source/libgg/Rewrite.cpp:897:        const fs::path mapFile = ... / "gg" / ("rewrite-" + journal::Journal::newOperationId());
```

Plus `Journal.cpp:109` (`<common>/gg/journal`, `journal.lock`). What each entry is:

| Entry | What it is | If deleted |
|---|---|---|
| `journal`, `journal.lock` | undo history (U1) | past Undo is lost |
| `cache/conflicts-v1` | disposable conflict-scan cache | rebuilt from file content |
| `hooks/run` | the managed-hook runner (H1) | the hooks do nothing, safely (`HOOK-MISSING-SILENT`) |
| `rebase/<worktree>/` | groups a native rebase's steps into one journal operation, and hands the todo to the sequence editor | the rebase ends as separate operations |
| `symref-<id>` | a hook's note between the prepared and committed reference-transaction calls | one journal entry loses a symbolic old value |
| `rewrite-<id>` | stdin for `git hook run post-rewrite`, removed right after | nothing |

First-class conflicts are self-describing markers in file content (M1, `docs/spec/conflict-markers.md`).
`test_conflicts.cpp:258-264` deletes `.git/gg` and gets the same conflicts, and so does
`REMOVAL-NO-GG-STATE` (CLI and History).

Verdict: **absent**.

## 6. Working-tree auto-snapshot into `@`

```
$ git grep -n -i -E 'auto-?snapshot|autosnapshot|auto_snapshot|snapshot(_|-| )?(the )?working(_|-| )?(tree|copy)|working(_|-| )?copy(_|-| )?snapshot|snapshotWorking' -- $FP
(no output)
```

"Snapshot" in the code means the immutable read model of refs and HEAD (`core::Snapshot`, P1-05),
not a commit of the working tree. The working tree is only committed by an explicit Commit or Amend
(`git commit`), and the tests check the result against `git status --porcelain=v2`.

Verdict: **absent**.

## 7. max-new-file-size setting

```
$ git grep -n -i -E 'max.?new.?file|maxNewFile|snapshot\.max' -- $FP
docs/spec/ui-spec.md:122:index stages on checkout"). **D** max-new-file-size.
```

The one hit is the UI spec recording the setting as **D**ropped. Settings (`Settings.hpp`) has no
such key.

Verdict: **absent**.

## 8. Revset and fileset languages

```
$ git grep -n -i -E 'revsets?|filesets?' -- $FP
docs/traceability.md:666:| REMOVAL-NO-OLD-CLI | ... (generated from the catalogue line below)
Source/tests/spec_catalogue.txt:701:REMOVAL-NO-OLD-CLI | 4 | §9 | git gg has none of the old gg CLI families (...) and no revsets
Source/tests/test_removal.cpp:152:    // The old families and a revset argument: usage errors, and nothing changes.
Source/tests/test_removal.cpp:168:    for (const char* gone : {..., "revset", "fileset"})
```

Revisions are Git revisions everywhere: `git rev-parse`, and `git_revparse_single` in libgit2.
`git gg new -r @-` is a usage error (`REMOVAL-NO-OLD-CLI`).

Verdict: **absent**.

## 9. Old gg CLI families

```
$ git grep -n -E 'add_subcommand\("' -- Source/gitgg
main.cpp:275: "new"   286: "undo"   287: "redo"   288: "op"   290: op → "log"   292: "conflicts"
main.cpp:296: "hooks" (install|uninstall|status)   302: "hook"   309: "ui"   313: "sequence-editor"   317: "help"
$ git grep -n -E 'add_subcommand\("(branch|file|util|workspace|config|operation|restore|next|prev)"|"--what"' -- Source/gitgg
(no output)
```

That is exactly the §6 command list. `REMOVAL-NO-OLD-CLI` runs `git gg branch list`, `file list`,
`util gc`, `workspace list`, `config list`, `operation restore --what repo`, `op restore`, `next`,
`prev`, `log -r all()` and `new -r @-`, the way a user runs them from a shell. Each exits 129
(usage) with a message on stderr, HEAD and refs are unchanged, and `git gg help` names none of them.

Note: ggui's own git children get `GG_ASKPASS_ENDPOINT`. There, `git-gg` with one unknown
argument is `GIT_ASKPASS` receiving a prompt (`main.cpp:262-267`, P2-04). This is by design, and a
user's shell never has that variable.

Verdict: **absent**.

## 10. Agent skill docs

```
$ git ls-files | grep -i -E '(^|/)(skills?|\.claude|agents?)/|SKILL\.md$|(^|/)AGENTS?\.md$'
(no output)
```

Verdict: **absent**.

## 11. Fetch auto-fast-forward of local branches

```
$ git grep -n -E '"fetch".*refs/heads/|--update-head-ok' -- Source ':!Source/tests'
Source/app/shell/Actions.cpp:476:            ctx.git({"fetch", "-q", ".", up + ":refs/heads/" + branch});
```

`Actions::fetch` (`Actions.cpp:536-553`) runs only `git fetch --progress [--prune] [--tags]
<remote>|--all` and has no follow-up step. The one hit is `Actions::fastForward`, the explicit
**N** action "Fast-forward to upstream" (§4.7), chosen by the user for one branch that isn't
checked out. It fetches from `.` so git refuses anything that isn't a fast-forward. Behavioral
test: `network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs
move` (`REMOTE-FETCH-NO-FF`).

Verdict: **absent**.

## 12. "@ is a change you edit" and rule-10 wording

```
$ git grep -n -i 'change you edit' -- Source ':!Source/tests'
(no output)
$ git grep -n -i -E 'change[ _-]?ids?([^A-Za-z]|$)|(^|[^A-Za-z])alias(es)?([^A-Za-z]|$)|workspaces?|jujutsu|(^|[^A-Za-z])jj([^A-Za-z]|$)' -- Source/app Source/gitgg Source/core Source/libgg
(no output)
$ git grep -n -E '"[^"]*[ (]@([ ).,:!?][^"]*)?"|"@ [^"]*"' -- Source/app Source/gitgg
Source/app/panels/CommitMenu.cpp:53:    // HEAD and this commit (plan §4.3 "Merge into @", "Rebase @ onto"): also in Branches.
```

The last hit is a comment quoting the plan. Since P4-06 the alias pattern has two allowed hits in
`Source/libgg/Rewrite.cpp`: on git before 2.40 (no `git hook run --to-stdin`) ggui runs the
post-rewrite hook with a one-shot `git -c alias.gg-post-rewrite=!… gg-post-rewrite`, so that git's
own shell runs it on every platform. That is a git command alias, not the old gg's commit aliases. The menu items themselves read "Merge into HEAD..." and
"Rebase HEAD onto this/branch" (`CommitMenu.cpp:57-59`, `SidePanels.cpp:96-98`). The §4 labels that
named jj concepts are written in Git terms, as `docs/spec/ui-spec.md` ("Wording rule") requires:
"Workspaces" → "Worktrees" (panel), the Settings scope tab "Workspace" → "Worktree"
(`git config --worktree`, `AppSettings.cpp:24-25`), and the dropped
**D** "Aliases list". Other `@` literals are Git syntax (`@{upstream}`, `@@` hunk headers, `"$@"`)
or the internal drag-and-drop payload `"@<commit>"`, which is never shown.

**Violations found and fixed** (the UI spec forbids "@" and "change" as UI terms):

- The Blame panel header read `"<file> @ <commit>"` (`BlamePanel.cpp:141`). It now reads
  `"<file> at <commit>"` / `"<file> at working tree"`. Test: `blame/blame at a commit and on the
  working tree` checks the header text in both cases and that no "@" is drawn.
- The Reflog table's first column was titled "Change" for an old → new commit pair
  (`SidePanels.cpp:542`). It is now "Commits". Test: `panels/reflog: …` checks the header.

Other uses of "change" are verbs or the §4 labels the plan keeps ("Changes" panel, "Change
information", "Blame before this change", "Select/Copy change block").

Verdict: **absent** (after the fixes).
