# ggui — product specification

Note: §2 (analysis of the pre-rebuild app), §7 (phased delivery) and §10 (risks) have been
removed as no longer relevant; the remaining section numbers are unchanged because
scripts and code comments cite them.

---

## 0. Confirmed decisions
- The old libgg (public C API over libgit2) and the `gg` repository are dropped. ggui uses
  libgit2 **directly**, with no wrapper layer, plus the `git` CLI (below).
- **New `libgg`** is just the name of the internal static library that holds code
  **shared by `ggui` and `git-gg`**. It has no public API, is not installed, has no ABI,
  and does not wrap libgit2. Its functions take a `git_repository*` directly.
- **libgit2 and git work never blocks the UI thread** (§3.1).
- First-class conflicts are kept, but **only text content conflicts**. They live entirely
  in the file, with no metadata. All other conflicts are resolved immediately (K1).
- **Hybrid Git access:** the `git` CLI does anything a plain git command does, including
  transport, commit, checkout, stash, merge, branch/tag, worktree and staging patches.
  libgit2 does reads, the graph, diff and blame, and in-memory rewrites and conflict merging.
  `git` ≥ 2.36 is a runtime requirement (G2).
- **No managed hooks** (H1): plain `git` is journaled by the reconciler from reflogs and ref
  snapshots (§4.12 B). Hooks of older installs are removed silently on open.
- **Pushing commits with first-class conflicts is always refused** by ggui. Plain `git push` is
  not guarded (P1).
- **Undo** restores refs and the index. It updates the working tree only when nothing is
  lost; otherwise it refuses or offers to stash first (U1).
- Leftover `refs/gg/*` from the old gg: **deleted automatically and silently** on open, in ggui and git-gg (C3).
- Platforms for the first release: **Linux and Windows**. macOS comes later.
- **Tests are complete integration tests only, run through Dear ImGui Test Engine in the
  real `ggui` binary.** There are no unit tests and no GoogleTest. The goals are **every
  feature in §4 covered** and **> 90 % line and > 90 % branch coverage** of first-party
  code: `ggui`, `ggui_core`, `libgg` and `git-gg`. Both are CI gates (§8).
- **Interactive rebase** (§4.13) with two engines (R3). By default it runs in memory: one
  atomic step, one Undo, and conflicts end up inside the files. It uses native
  `git rebase -i` when the todo has `edit`/`break`/`exec` or when the user asks.

## 1. Goals and scope

| Keep | Drop | Add |
|---|---|---|
| The `ggui` desktop app (Dear ImGui + SDL3) | The `gg` CLI utility and its command surface | A minimal `git gg` subcommand (`git-gg` executable), starting with `new` |
| The third-party dependencies and how they are pulled in (CPM, pinned versions). libgit2 moves into ggui and is used **directly** | The whole `gg` repository, including the **old libgg** (C API `<gg/gg.h>`, `gg::gg` target, `find_package(gg)` / sibling-checkout fallback) | Staging: index-aware status, stage/unstage per file, hunk and line |
| **First-class conflicts**: a commit can contain unresolved text conflicts. They live **entirely inside the conflicted file** as self-describing markers, with no metadata. Binary and other non-text conflicts must be resolved immediately, as in Git (§4.10) | The Jujutsu-style model: change IDs/aliases, **all** `refs/gg/*` storage (workspaces, aliases, visible heads, conflict metadata), auto-snapshotting the working tree into `@` | |
| UI layout: docked panels, menus, toolbar, dialogs, shortcuts, drag and drop | The "max new file size" snapshot setting and other snapshot-only concepts | Stash: list, create, apply, pop, drop, inspect, branch from stash |
| Every user-facing function (mapped to Git semantics where it was jj-specific; see §4) | | A Git-centric workflow: native index, native conflicts, native merge/rebase states, native worktrees |
| | | **Git hooks integration**: ggui runs the repository's standard hooks itself (libgit2 does not), and Undo covers plain `git` operations without hooks (§4.12) |

**Principle:** the rebuild is specified by *behavior*, not by the existing code. The existing
architecture (monolithic `Application` class spread over ~20 files, a 1.4k-line worker,
jj semantics leaking into every layer) is not carried forward.

**Hard rule, Git transparency:** no plain `git` command, run at any time, may corrupt,
desynchronize or confuse ggui's view of the repository. Everything ggui understands must
be derivable from objects, refs, the index and the working tree alone. Local files under
`.git/gg/` may only hold **history or caches**: deleting them may lose undo history, but
must never change what a commit, file or conflict means.

---

## 3. Target architecture (features first, just enough structure)

A single repository (`ggui`). There is no separate library project, no public API and no
layer that wraps libgit2.

```
+---------------------------+          +-----------------------------+
| ggui (ImGui app, UI thread)|          |  git-gg (tiny CLI, CLI11)   |
|  panels read snapshots,   |          |  no SDL/ImGui, fast startup |
|  send commands            |          +--------------+--------------+
+-------------+-------------+                         |
              | commands / events (queues)            |
+-------------v-------------+                         |
| ggui_core (engine)        |                         |
| worker threads, watcher,  |---- libgit2 directly ---+
| snapshots, graph, diff,   |---- git CLI runner -----+
| blame, rewrites           |                         |
+-------------+-------------+                         |
              +--------------+------------------------+
                             |
               +-------------v-------------+
               | libgg (internal static    |  shared code only: journal,
               | lib, not installed)       |  conflict markers, `new`,
               | plain functions taking    |  reconciler, PATH setup,
               | git_repository*           |  git runner, libgit2 helpers
               +---------------------------+
```

- **libgit2 is used directly** by `ggui_core` and `libgg`. There is no wrapper API.
  **Helpers are welcome:** RAII handles (`unique_ptr` with the matching `git_*_free`), an
  error-to-exception or error-to-`expected` check, OID/string conversion, and small
  iterator adaptors. They are conveniences, not an abstraction boundary.
- **libgg holds only code that both `ggui` and `git-gg` need**: the undo journal,
  conflict-marker parser/writer and N-way term merge, `new`, the reconciler (plain git into the journal), the legacy hook removal, the git process
  runner, and the libgit2 helpers. Everything else lives in `ggui_core`.
- **Git access rules (decision G2):**
  - **Reads use libgit2:** snapshot, history walk, diff, blame, reflog, stash list,
    index/status, and scanning for conflict markers.
  - **Mutations that plain git has use the `git` CLI:** commit, amend, switch/checkout,
    merge, stash push/apply/pop/drop/branch, branch/tag, fetch/pull/push/clone,
    worktree, `apply --cached` for hunk and line staging, `restore`, `reset`, `rm --cached`,
    `add`. Git then runs hooks, filters/LFS, sparse checkout, credential helpers, SSH
    config, `insteadOf` and proxies natively, so none of it is reimplemented.
  - **Rewrites git cannot do use libgit2 in memory:** rebase, reorder, squash, split, drop,
    move lines between commits, and N-way conflict merges. Commits are built with libgit2,
    then all ref moves are applied in **one `git update-ref --stdin` transaction**, so
    `reference-transaction` hooks fire natively and the update is atomic. The worktree is
    then updated with `git` (`checkout`/`read-tree -m -u`). Afterwards, hooks git would not
    run on its own (`post-rewrite`, `post-checkout` when needed) are run through
    `git hook run`.
  - **The git runner (in libgg)** handles progress parsing (`--progress` on stderr),
    cancellation (killing the process tree), no console windows on Windows, locale forced
    to C for parsing (user-visible messages are shown as-is), and `GIT_TERMINAL_PROMPT=0`
    plus `GIT_ASKPASS`. It never waits on the UI thread.
- **No private refs, no semantic metadata.** Nothing is added to `refs/`. `.git/gg/` holds only:
  1. the undo journal (history; losing it only disables Undo for past operations)
  2. disposable caches, for example "does this tree contain conflicts", which can always
     be rebuilt from the objects
  3. `edit/<worktree>`: ggui's edit-session state (§4.3 Edit commit: the commit and the branch
     to return to); losing it only ends the session, HEAD and every ref stay as they are
- **Hooks:** the user's hooks are handled by git itself (G2). There are no managed hooks: the
  undo journal learns about plain git from the refs and reflogs (§4.12 B).
- **The UI talks to a small engine with an explicit contract**: commands in, immutable
  snapshots and events out, with cancel and progress, embodied in `Commands.hpp`/`Events.hpp`.
  See §3.1 for the threading rules.
- **The UI is split by panel**, with a thin shell that owns docking, menus and dialogs. Replace
  the single god-object `Application` with per-panel view models.
- **Compatibility rule:** ggui must work on any repository that plain `git` created or changed,
  in any state (mid-merge, mid-rebase, detached, unborn, bare, linked worktree), and must
  leave it in a state plain `git` understands.

### 3.1 Responsiveness: libgit2 and git never block the UI
The UI thread renders every frame at the display's refresh rate, whatever the repository
size or network state. The rules:

- **The UI thread never calls libgit2 and never waits on a git process.** This includes
  "small" calls such as reading HEAD, looking up a commit for a tooltip, checking whether
  a path is ignored, or summarizing recent repositories. Every such need is either already
  in the current snapshot, or requested as a command whose answer arrives as an event.
- **Enforced in code:** the libgit2 helpers and the git runner in libgg assert in debug
  builds and tests that they are not on the UI thread (the thread ID is registered at
  startup). A UI test runs the whole action suite with the assertion enabled.
- **Threads and handles:**
  - libgit2 objects are not safe to share across threads, so each worker thread opens its
    own `git_repository*`. Handles are never passed between threads. Results cross threads
    only as plain immutable C++ values (snapshots, graph rows, diff text, blame lines).
  - Separate queues so slow work can't starve fast work:
    1. **mutations:** serialized, one at a time per repository
    2. **snapshot/status**
    3. **history/graph:** incremental and cancellable
    4. **diff/blame/file content:** latest request wins, and stale requests are dropped
    5. **network:** git processes
  - The file watcher (efsw) runs on its own thread and only posts debounced "paths changed"
    notices.
- **Big results arrive in pieces:**
  - History is published as a first bounded page, then extended in batches.
  - Diffs and blame for large files are streamed or capped, with a "load full" action.
  - Status on huge worktrees is published as partial results, marked "scanning…".
  - No single event makes the UI do O(repository) work in one frame. Large conversions,
    such as the graph lane layout and diff line tables, are done on the worker.
- **Every long operation can be cancelled:**
  - libgit2 work checks a cancel flag between steps and through progress callbacks.
  - git processes are terminated, and a mutation is undone or left in a state plain git
    understands.
  - The UI shows progress and a Cancel button (existing toolbar spinner and Cancel).
- **Mutations lock actions, not the UI:** while a mutation runs, conflicting actions are
  disabled, but browsing, scrolling, selecting, diffing and blame keep working on the last
  snapshot.
- **Recent-repository summaries, opening, cloning and settings I/O** run off-thread too,
  as engine commands (`std::async` internally).
- **Acceptance criteria:**
  - A performance test opens a large fixture repository (≥ 100k commits, ≥ 5k refs,
    ≥ 50k files) and drives the UI, asserting that no frame takes longer than
    about 33 ms because of repository work, well under the 30 s+ history loads of a naive
    implementation.

---

## 4. Feature specification

Legend: **K** = keep as is. **M** = keep the UI entry point but map it to Git semantics.
**N** = new. **D** = drop.

### 4.1 Application shell
- **K** Welcome screen: *Open repository…*, *Initialize repository…*, *Clone repository…*,
  recent list (Delete key forgets an entry), progress and cancel while opening.
  Update the tagline to Git wording.
- **K** Auto-open argv[1], otherwise the most recent repository that still exists.
- **N** On open, show the one-time prompt "git not found / too old" (G2), which is blocking.
  Managed hooks of older versions (H1) and old gg refs (C3) are removed silently, without a prompt.
- **K** Main menu:
  - **Repository:** Open… (Ctrl+O), Initialize…, Clone…, Recent ▸ (filterable; shows
    branch, upstream and ahead/behind), Open working directory, Copy path, Close repository
    (Ctrl+W), Refresh (F5), **N** Fetch / Pull / Push (same behavior as the toolbar),
    Settings…, Quit.
  - **Change** → rename to **Commit**: New commit (Ctrl+N), Commit…/Amend…, and the
    selected-commit actions (§4.3).
  - **Edit:** Undo (Ctrl+Z), Redo (Ctrl+Y), Apply patch….
  - **View:** toggle each panel, Previous/Next changed file (Shift+F6/F6), Reset layout.
- **K** Toolbar: New, Commit/Amend (label follows the selection), Prev, Next, Undo, Redo,
  Refresh, repository switcher combo, open-folder button, current branch or "detached",
  HEAD ID (click reveals it in History; context menu copies it), activity spinner with
  cancel, background-task tooltip, error popups for important errors and corner notifications
  for warnings/information (no error banner).
- **N** Toolbar **Fetch / Pull / Push** group, placed after Refresh. All three run through
  the `git` CLI (T1), on a worker, with progress and Cancel in the activity area (§3.1).
  - **Fetch:** fetches all remotes. The dropdown arrow offers: a single remote, "Fetch and
    prune", and "Fetch tags".
  - **Pull:** updates the current branch from its upstream, following `pull.rebase` /
    `pull.ff`.
    - Shows an **incoming count badge** (↓n), taken from the last fetch.
    - The dropdown overrides the strategy for this pull: merge / rebase / fast-forward only.
    - Disabled, with a tooltip saying why, when HEAD is detached or the branch has no
      upstream.
    - If local changes block the pull, it offers "Stash and pull" (§4.9).
  - **Push:** pushes the current branch to its upstream.
    - Shows an **outgoing count badge** (↑n).
    - With no upstream, it opens **Push to…** with the remote prefilled and
      `--set-upstream` checked.
    - The dropdown offers: Push to…, Force with lease, and Push tags.
    - Refused when the pushed range contains first-class conflicts (P1). The error lists the
      commits.
    - A rejected non-fast-forward push offers "Pull then push" or "Force with lease…"
      (confirmation required).
  - The buttons are disabled while a conflicting mutation runs. Browsing stays available.
  - Ahead/behind badges refresh after every fetch, pull, push and local ref change, with no
    network access of their own.
  - The same three actions appear in the **Repository** menu (Fetch, Pull, Push) for
    keyboard and menu access.
- **N** Toolbar and status additions: **Stash** and **Pop** buttons, and a repository-state
  badge (MERGING / REBASING / CHERRY-PICKING / REVERTING / BISECTING) with
  **Continue / Skip / Abort**.
- **K** Default dock layout: a left column with Branches|Tags on top and Workspaces|Remotes
  below. History in the centre. Changes and Change information top right. Diff, Blame,
  Reflog and Operations tabbed bottom right. **N** Stashes docks with Workspaces|Remotes.
- **K** Settings window: UI scale (50–300%), theme (dark/light), `core.editor` per scope
  (User / Repository / Workspace). **D** max-new-file-size. **N** merge tool and diff tool
  selection (`merge.tool`, `diff.tool`), pull strategy (`pull.rebase`), and the default for
  "commit all when nothing is staged".
- **N** Settings > General "Add GoodGit to PATH" (Linux with systemd only): puts the ggui
  executable directory, which holds `git-gg` (so `git gg` works in a terminal), on the login PATH by
  writing `$XDG_CONFIG_HOME/environment.d/60-goodgit.conf` (default `~/.config`; one line
  `PATH=<dir>:${PATH}` under a comment saying ggui manages it); unchecking deletes the file. The
  state is read from the file, not stored: checked iff it exists and names the current
  directory. A file naming another directory (or no PATH entry) shows a warning, in the warning
  colour, that the git-gg found there may be stale or missing, with **Point to this GoodGit**
  (rewrites it; checking does the same) and **Remove the file**. Written atomically; a directory containing `$ \ " ' :` or a line break is refused with an
  error. Takes effect at next login. Systemd is detected as `/run/systemd/system` being a
  directory; elsewhere the control is disabled with an explanatory tooltip.
- **K** Open a linked worktree in a new window. Open files in the external editor, open
  folders, run the external diff tool (vs HEAD, vs parent) and the three-way merge tool.
- **K** File watcher: debounced refresh of the paths that changed. Also watch `.git/index`,
  `HEAD`, refs, `MERGE_HEAD`, `rebase-*` and `refs/stash`.
- **K** Logging (`GGUI_LOG_FILE`, spdlog env levels). `--test`, `--test=`, `--smoke` for the
  test engine.

### 4.2 History panel (commit graph)
- **K** Lane-based graph with curved edges. Rows show ID prefix, subject, author, date,
  branch/tag/remote/worktree badges, and pushed vs unpushed colouring. Commits with
  first-class conflicts get the conflict colour and marker, and so does a working tree with
  native conflicts. **N** Filter: "show only conflicted commits". Keyboard: jump to the
  next or previous conflicted commit.
- **K** Virtual **Working tree** row at the top, parented on HEAD. **N** A separate
  **Index (staged)** row between Working tree and HEAD when anything is staged, so a
  selection can show "staged" vs "unstaged" diffs.
- **N** Optional stash rows, shown as badges on their base commits (toggle).
- **K** Scope follows the branch/tag/remote selection in the side panels. Search/filter by
  message, ID, branch or tag. Reveal a commit (loads more history until found, cancellable).
  "Show more" for collapsed regions. Expand and collapse merge history. Unique shortest-prefix
  IDs.
- **K** Keyboard: ↑/↓ navigation, N (new), Alt+N (new detached), E (edit
  commit; check out is in the context menu and the Branches window), D / Shift+D
  (duplicate commit / branch → cherry-pick), S / Shift+S / Alt+S (squash / with descendants /
  split), A / Shift+A (drop / drop branch). **N** I (interactive rebase from the selected
  commit, §4.13).
- **K** Row context menu: New, New detached, Check out ▸ (branches at this commit), Create
  branch…, Move branch ▸, Delete branch ▸, Push, Push to…, Copy ▸ (ID, full description), plus
  the shared commit actions below.
- **K** Working-tree context menu: Commit…, Amend into HEAD…, Discard changes….
  **N** Stash changes…, Stage all, Unstage all.
- **K** Drag and drop:
  - commit → commit: Move before, Move after, Squash, Rebase (modifier keys pick the default;
    otherwise a chooser pops up)
  - branch badge → commit: move the branch
  - file(s) from Changes → commit: move the file changes into that commit
- **K** Warn before rewriting published history (commits reachable from remote-tracking refs).

### 4.3 Commit actions (shared by the History menu and the Commit menu)
Each rewrite rewrites the chosen commits, rebases the descendants on the current branch, and
moves affected local branches. **A rewrite never stops because of a text conflict.**
The resulting commits contain first-class conflicts in the file content (§4.10), and
descendants are rebased on top of them. **Binary and other non-text conflicts** are
resolved up front, in a pre-flight dialog, before anything is written (§4.10). A later rewrite can resolve a conflict automatically; for example, moving a
commit back or dropping the change that caused the conflict. The completion message and
the History panel list newly conflicted commits. See §5, decision R1.

| Action | Status | Git-centric meaning |
|---|---|---|
| New commit | **M** | Empty commit on the selection (`git gg new`). Advances the branch when HEAD is attached. "New detached" leaves branches alone. More than one parent gives a merge commit. Insert before/after rebases the descendants onto it |
| Edit / Check out | **M** | `git switch` a branch or detach at a commit. Refuse if it would overwrite local changes; offer "stash and switch" (**N**) |
| Edit commit (E) | **M** | Detach HEAD at any commit and save an edit session (the commit and the local branch to return to) in `.git/gg/edit/<worktree>`. A toolbar banner shows *Editing \<id\> of \<branch\>* with **Return to \<branch\>** and **Stop editing**. Amending it restacks its descendants and moves every branch ref that pointed at them, all or nothing: the restack is computed first, so an amend that cannot be restacked (a non-text conflict) is refused before the commit, and one that fails after it is rolled back. Text conflicts become first-class conflicts and are reported. A descendant merge keeps its parents and its own resolution, and the changes of every rewritten parent are carried into it (a clash with the merge's resolution becomes a first-class conflict). Amend and restack are one operation, undone in one step. The session follows the amended commit and is dropped when HEAD is no longer detached or the branch is gone |
| Commit… | **M** | Commit the **index**. If nothing is staged, offer "commit all tracked changes" (`-a`) or "stage the selected files" |
| Amend… | **M** | Amend HEAD with the index (message and/or content) |
| Describe (Save message, Change information panel) | **M** | Reword any commit. Rebases descendants |
| Edit author | **M** | Change the author of any commit |
| Duplicate commit / branch | **M** | Cherry-pick one commit or a range onto its parent, making a detached copy |
| Rebase… | **K** | Rebase one commit or the whole branch onto a destination |
| Interactive rebase… | **N** | Open the rebase todo editor for a range: from the selected commit to HEAD or the branch tip, optionally onto a new base (§4.13) |
| Squash… / with descendants | **M** | Fold into a parent or a chosen target (fixup/squash) |
| Split… | **M** | Split a commit by selected files into two commits |
| Restore… | **M** | Restore paths in a commit from another commit (rewrite), or restore the working tree from a commit (`git restore --source`) |
| Abandon / Abandon branch | **M** | Drop the commit(s) and rebase the descendants. Optionally delete the remote branch too |
| Simplify parents | **M** | Remove redundant merge parents |
| Move @ to previous/next | **M** | Check out the parent/child commit. Detach if it is not a branch tip |
| Reorder (drag) | **M** | Move a commit before or after another in the same chain, or copy it there |
| Move files/hunks/lines between commits | **K** | Move selected changes to the parent or child commit, or to the working tree ("uncommit") |
| Merge into @ | **M** | Create a merge commit in memory. Text conflicts become first-class conflicts in the new merge commit, which is then checked out. Non-text conflicts go through the pre-flight dialog (§4.10). Option: "use native `git merge`", which stops with index conflicts instead |
| Rebase @ onto branch / Reconcile with remote | **M** | Native rebase or merge of a diverged local branch with its upstream |
| Revert | **N** | `git revert --no-commit`: the inverse of the commit into the index and working tree. The message `Revert "<subject>"` + `This reverts commit <id>.` waits in MERGE_MSG, and the Commit dialog starts with it. Conflicts stop natively (Reverting: Continue/Abort). Refused with staged changes, which Abort would drop |
| Revert and commit | **N** | A new commit on HEAD that undoes the commit, built in memory like Merge into @: text conflicts become first-class conflicts, one ref update advances the branch (or the detached HEAD), one Undo. Local changes in the way refuse it, as for the other HEAD-moving rewrites |
| Cherry-pick | **N** | `git cherry-pick --no-commit`: the commit's change into the index and working tree, with the message plus `(cherry picked from commit <id>)` (added once) in MERGE_MSG. A conflict leaves the CherryPicking state (ggui writes CHERRY_PICK_HEAD, which `--no-commit` does not) so Continue/Abort work. Disabled for HEAD, refused for an ancestor of HEAD and with staged changes |
| Cherry-pick and commit | **N** | A copy of the commit on HEAD built in memory, as Revert and commit, keeping the original author (as git does). A merge commit's change is always taken against its first parent (`-m 1`), for all four actions |

### 4.4 Changes panel and Change information panel
- **K** File list for the selected commit (or comparison), with filter, multi-select
  (Ctrl/Shift/Ctrl+A), keyboard navigation, status icons, renames and copies, and a
  "Compare with @" toggle for the whole commit.
- **N** When the Working tree or Index is selected, show a **Staged / Unstaged / Untracked /
  Conflicted** grouped list. Actions:
  - **Stage**, **Unstage** and **Discard** for a file or selection. Space or Enter toggles
    staging. Double-click stages.
  - Stage all, unstage all, and "stage all modified (not untracked)".
  - Intent-to-add. Mark conflict resolved (`git add`).
  - When a checked-out commit has first-class conflicts, its conflicted files appear under
    Conflicted. Resolve a file (so it has no markers left), then stage and amend. The file
    is then no longer conflicted (§4.10).
  - Drag a file between the Staged and Unstaged groups.
- **K** File context menu: Open working-copy file, Open containing folder, Copy ▸
  (name / relative / absolute path), Resolve with merge tool, Mark resolved, Copy patch,
  Save patch…, Blame file, External diff ▸ (vs @, vs parent), Move to child (on the HEAD
  commit, which has no child, "Move to working tree" takes its place; both use the up arrow) / Move to parent (down arrow),
  Discard (**D**; commit files: rewrites the commit so it no longer changes the files, descendants
  rebased, one Undo; published commits ask "Rewrite published history?" first; no other confirmation
  since Undo restores it; discarding every change may leave an empty commit), Delete file.
- **M** Track → `git add` for untracked files. Untrack → `git rm --cached`, with an optional
  "add to .gitignore" (**N**).
- **K** Change information: editable message with *Save message*, author (copy name/email,
  edit), date and lock (pushed) state, commit ID (copy), parents (reveal). **D** Aliases
  list. **N** Committer shown when it differs from the author. For HEAD and a clean
  index, an "Amend" mode.

### 4.5 Diff panel
- **K** Unified and Side-by-side views. Whitespace mode (normal / ignore changes / ignore
  all). Context-line count. Expandable context (Shift reveals the whole section). Syntax
  highlighting. Binary, image and submodule placeholders. Mode-change line. "Compare only
  this file with @". Ctrl+C copies the selection: code lines only (no hunk rows or gap
  placeholders).
- **K** Line/hunk context menu: Copy, Blame file, Move line(s)/hunk to child (up arrow;
  on the HEAD commit "working tree" replaces it, since HEAD has no child) / parent (down arrow) / active commit, Discard line(s)/hunk(s) (a commit rewrite like Discard on files; the hunk row button "Discard hunk" does the same).
- **N** When viewing unstaged changes: **Stage line(s) / Stage hunk / Discard line(s) /
  Discard hunk**. When viewing staged changes: **Unstage line(s) / Unstage hunk**. Buttons
  appear in each hunk's row, as in gitfourchette. A hunk row shows only the function context,
  never the `@@` line ranges.
- **N** Diff of a stash entry: its working-tree part and its index part. Show untracked files
  as a third part when the stash has them.

### 4.6 Blame panel
- **K** Everything: working-tree blame (uncommitted lines marked), filter,
  back/forward history (mouse buttons), per-line tooltips, "Blame before this change",
  originating source, reveal/copy commit, select or copy a change block.

### 4.7 Side panels
- **K Branches:** Create branch, filter. Click toggles whether the branch is visible in
  History (Ctrl-click makes it the only one). The current branch is outlined. Context menu:
  Reveal, Copy name, Check out, Merge into @, Rebase @ onto branch, Push, Push to…,
  Reconcile with remote/branch…, Rename…, Delete ▸ (Local / a given remote / Local and all
  remotes). **N** Set/unset upstream, Fast-forward to upstream, Pull (for the current
  branch).
- **K Tags:** Create tag, filter, visibility toggle, Reveal, Copy, Delete.
  **N** Annotated tags (message), push tag, delete a remote tag.
- **M Workspaces → Worktrees:** native `git worktree`: list (stale, locked, main), Add…,
  Open here, Open in new window, Open directory, Copy name/path, Reveal HEAD, Remove….
  **N** Lock/unlock, prune, repair (Git offers them; the UI adds them). **D** gg rename
  and forget, which only managed gg metadata. A Git worktree name is its directory.
- **K Remotes:** list, Add remote, Delete, Copy name, Fetch, Pull. **N** Edit URL, prune
  on fetch, Fetch all.
- **N Stashes panel** (§4.9).
- **K Reflog panel:** HEAD reflog with filter. Copy/reveal old and new commits, and create
  a branch from either. **N** Choose a reflog: HEAD, any branch, or stash.
- **M Operations panel:** list of operations from the undo journal, with Restore (§5,
  decision U1). This includes plain `git` operations (§4.12 B), each labelled with its source (ggui, git-gg, or `git <command>`).

### 4.8 Remote operations
- **K** Clone (with progress and cancel). Fetch (one remote or all). Push (branch, force
  with lease, choose remote/branch via Push to…). Pull. Delete a remote branch. Transfer
  progress. **All of these run through the `git` CLI.**
- **M** Credentials: replace the built-in credential dialog (user/pass, SSH agent, SSH key)
  with Git's own mechanisms: credential helpers, ssh-agent and `~/.ssh/config`. If a prompt
  is still needed, ggui acts as `GIT_ASKPASS`/`SSH_ASKPASS` and shows a small prompt
  dialog. Passwords are never stored by ggui.
- **M** Pull follows `pull.rebase` / `pull.ff`, with a per-action override (merge / rebase
  / ff-only). Remove the gg rule that fetch fast-forwards local branches automatically. A
  plain `git fetch` changes only remote-tracking refs. Show ahead/behind so the user
  decides.
- **N** Push `--set-upstream` for a new branch, push tags, and Force-with-lease as the only
  force option.
- **M** Pushed/locked state: a commit counts as published when a remote-tracking ref
  reaches it.

### 4.9 Stash (new)
- **Create:** message; options: keep index, include untracked, staged only (`--staged`),
  selected paths only (`push -- <paths>`).
- **List:** `refs/stash` reflog entries with index, message, base commit and date.
- **Apply / Pop:** option to restore the index. Conflicts end in the normal native
  conflict state. Pop keeps the stash when a conflict happens, as Git does.
- **Drop** (with confirmation). **Clear all.**
- **Branch from stash** (`git stash branch`).
- **Inspect:** selecting a stash shows its files and diff in the Changes and Diff panels.
  Apply a single file from a stash.
- "Stash and switch" / "Stash and pull" helpers when local changes block an operation.
- Undo: the journal records stash push/pop/drop so Undo can bring a dropped stash back.

### 4.10 Conflicts: first-class (in-file) and native

**Principle.** A first-class conflict is **only file content**. The conflicted file's blob
contains every side and base needed to redo or undo the merge. There is no ref, note,
commit header, sidecar file or cache that the meaning depends on. Checking whether a
commit is conflicted is a pure function of its tree.

**Which conflicts may be first-class (decision K1)**

| Conflict kind | Handling |
|---|---|
| Text content conflict (both sides are text blobs, same mode) | **First-class**, written into the file as markers |
| Binary content conflict | **Resolve immediately**, as Git does |
| Modify/delete, add/add with different types, file/directory, symlink, submodule (gitlink) | **Resolve immediately** |
| Mode conflicts, e.g. the executable bit | **Resolve immediately**; the content may still be a first-class conflict |
| Rename/rename, rename/delete | **Resolve immediately** |
| Text with filters (LFS or other `filter=` attributes) | **Resolve immediately**, because the stored blob is not the text the user sees |

**Marker format (decision M1)**
- **Two-sided conflicts use Git's standard `diff3` style exactly:**
  `<<<<<<<` side A / `|||||||` base / `=======` / side B / `>>>>>>>`. Editors, merge tools,
  `git diff` and people all already understand it. The base section is **mandatory**,
  because it makes the file self-describing: ours, base and theirs can all be rebuilt from
  the text.
- **N-sided conflicts use an extended form inside `<<<<<<<` … `>>>>>>>`:** alternating
  side (`+++++++`) and base (`-------`) sections, with the number of sides in the opening
  label. This is only produced when a conflicted file is merged again. For example, rebasing
  a conflicted commit onto a changed base yields 3 sides and 2 bases.
- **Marker length:** at least 7 (Git's `conflict-marker-size`, respecting the attribute). A
  longer run is chosen automatically when the content itself has marker-like lines, so
  parsing is never ambiguous. The opening marker sets the length for the whole region.
- **Region scope:** markers cover only the conflicting hunks. Unconflicted parts of the file
  are plain text.
- **Edge cases encoded in-band, defined in the spec:** a missing final newline on a side,
  CRLF vs LF sides, and empty sides.
- **A file is conflicted exactly when it contains at least one well-formed region.**
  Malformed or partial markers are ordinary text. Any edit that leaves no well-formed region
  resolves the file.
- **Opt-out attribute** for files that legitimately contain marker-like text, such as test
  fixtures and docs: for example `gg-conflicts=false` in `.gitattributes`. A text conflict
  in such a file must be resolved immediately.

**How rewrites treat conflicts**
- **Merging:** when an in-memory rebase, cherry-pick or merge meets a text file, each input
  version is parsed into terms (a plain file is one term). Terms are combined with N-way
  merge algebra and simplified, since matching removes and adds cancel out. The result is
  written back:
  - as plain text if it resolved
  - as a diff3 region if it has 2 sides
  - in the extended form if it has more
- **No nesting:** markers never nest in files written by ggui.
- **Automatic resolution:** reordering back, dropping the change that caused the conflict,
  or fixing a parent makes the conflict disappear in descendants.
- **`gg.sameChange`** (repo config: `accept`, the default, or `keep`): whether a hunk every
  side changed the same way resolves (Git's rule) or stays a first-class conflict (the
  exact term algebra). Read per write, together with the `conflict-marker-size` attribute,
  by `gg::conflicts::writeOptions()`.
- **Pre-flight dialog for non-text conflicts:**
  - The whole operation is computed in memory first. If any non-text conflict turns up, a
    dialog lists them per commit, in order. For each one: take side A / take side B /
    take base / choose a file from disk / keep deleted or keep present. For modes:
    pick one.
  - Choices can change later commits, so the dialog steps through commits in order, still
    in memory.
  - **Cancel leaves the repository untouched.** Nothing is written until every non-text
    conflict has a resolution.
  - `git gg` never produces non-text conflicts: `new` merges nothing.

**Display**
- History: conflict colour and icon on commits whose tree has conflicted files, with a
  filter and next/previous navigation.
  - To keep this cheap, a commit's state is computed from its parents' state plus the files
    it changed. Results go in the disposable cache.
  - A descendant that keeps an unresolved conflicted file is also shown as conflicted.
- Change information lists the conflicted files and the number of sides.
- The Diff panel has a per-term view (base → side N) as well as the raw marker view.

**Checking out a conflicted commit**
- Only the file content is written, so the working tree holds exactly the committed files
  with markers. The index matches HEAD, and plain `git status` is clean.
- The Changes panel still flags the conflicted files, because it scans content.
- **Optional (setting): "Expand to index stages on checkout".** For two-sided regions,
  stages 1–3 are also written to the index, so `git mergetool` works on them. This needs
  care: Git then considers the index unmerged. ggui must collapse the stages back to the
  committed blob when switching away, or on "Mark resolved". The default is **off**, to
  keep plain Git fully consistent.

**Resolving**
- Resolve with merge tool: ggui extracts base, ours and theirs from the regions and runs
  the configured `merge.tool`. On an N-sided conflict (N ≥ 3), a submenu offers each
  adjacent pair of sides; resolving pair k folds sides k and k+1 into one term, leaving
  the file a first-class conflict with one side fewer. Take side N (per region or whole
  file). Edit in editor. Mark resolved means the file has no regions left, and ggui
  refuses if it still does.
- Then either:
  - **Amend** the commit: descendants are rebased, and their copies of the conflict
    resolve too, or
  - **New commit** on top.
- The menu items are "Resolve with merge tool" and "Mark current file resolved".

**Plain Git transparency**
- `git log`, `diff`, `show`, `rebase`, `cherry-pick`, `commit --amend`, `stash`, `push`,
  `clone`, `gc`: conflicted commits are ordinary commits with ordinary blobs, so everything
  works and nothing is lost.
- The conflict travels with the commit to any clone, and another ggui user sees it as a
  conflict too.
- **When plain Git merges a file that already has ggui regions:**
  - If no Git conflict results, the file keeps its regions: still a valid first-class
    conflict.
  - If Git conflicts on it, the file ends up with Git's regions around or next to ggui's.
    ggui shows it as a native conflict (index stages). After the user resolves that, any
    ggui regions left are just a first-class conflict again. The parser must handle this
    safely, and the spec has to define it precisely (see `docs/spec/conflict-markers.md`).

**Safety**
- **ggui always refuses to push** when the pushed range contains commits with
  first-class conflicts. The error lists the commits and conflicted files, with a
  "Reveal" button for each. There is no override in ggui.
- Plain `git push` in a terminal is **not guarded**: there are no managed hooks (§4.12 B, P1).
  Undo can still put the refs back.
- The check is limited to commits not yet reachable from the remote's tracking refs, so
  it stays cheap.
- **ggui also refuses to push commits that left broken conflict markers** (docs/spec/conflict-markers.md §4.10): a commit whose file's first-parent
  version held a first-class conflict, and whose own version has leftover marker lines
  instead of a resolution (`gg::markers::brokenMarkers`). Same message style, same
  "Reveal", no override. The same check warns (never blocks) earlier, at commit time, for staged
  files the commit touches — both a broken region and a staged file that is itself still a
  first-class conflict. ggui's Commit and Amend dialogs and the Info panel's Commit button (on
  the Index) show the warning (`gg::outgoing::stagedConflictWarnings`, read off the UI thread and
  refreshed on every status change); it never blocks committing.

**Native in-progress operations: N**
- Detect merge, rebase (interactive and apply), cherry-pick, revert and bisect states.
  These come from plain Git or from stash apply. ggui's own operations never leave these
  states.
- Conflicted files come from index stages 1–3. The same resolution UI as above: merge tool,
  take ours/theirs, mark resolved, three-way diff.
- Continue / Skip / Abort for the active operation. For a stopped interactive rebase,
  also **Edit remaining todo** in the §4.13 editor, **Amend and continue**, and a progress
  view (done / current / remaining).
- **"Commit with conflicts" (N):** turns the current stopped text conflicts into
  first-class conflicts. It writes the diff3 regions from stages 1–3, stages them, and
  finishes the native operation. It is only offered when every remaining conflict is text,
  since binary conflicts must be resolved first.
- While an operation is in progress, show the message being edited (`MERGE_MSG`).

### 4.11 Patches
- **K** Apply patch (from clipboard or file). Copy patch. Save patch… (file or selection).
- **N** "Apply to index" vs "apply to working tree" option (`git apply --cached`).

### 4.12 Hooks and plain git (new)
**A. The user's hooks (always on).** Mutations go through the `git` CLI, so git runs the
user's hooks natively. For in-memory rewrites, git runs `reference-transaction` natively
through `git update-ref --stdin`. ggui runs `post-rewrite` and `post-checkout` (and
`pre-rebase` before a rebase-like rewrite) through `git hook run`, with Git's arguments and
stdin.
- A failing blocking hook aborts the operation, and its output is shown in an error popup.
- Commit dialogs get a "Skip hooks" checkbox (`--no-verify`).

**B. Plain git in Undo (no managed hooks).** ggui installs no hooks and asks nothing. Undo and
the Operations panel cover plain `git` commands (commits, rebases, resets, merges, branch moves
and deletes, stash, fetch, push) because the **reconciler** reads what happened from the refs and
their reflogs: on repository open, when refs change, before every ggui or git-gg operation, before
Undo, and in `git gg op log`. Format and rules: `docs/spec/undo-journal.md` §4.
- Each plain git command in HEAD's reflog is one operation labelled as typed (`git commit`,
  `git checkout feat`), also when it ran while ggui was closed. A plain rebase is one operation
  from start to finish. Fetches, pushes, tags and deletions are one operation per change pass.
- Undo carries the working tree back for checkout, rebase, merge, pull, cherry-pick, revert, am
  and reset when it is clean. A commit never carries.
- Undo refuses while any rebase or `git am` is in progress: finish or abort it
  first.
- **Limits:** steps on refs without a reflog (tags, deleted branches) made between two passes
  are one operation. `checkout --detach <branch>` looks like a checkout of a commit. Reftable
  repositories are not supported. The first time a repository is opened there is nothing to
  replay: Undo starts from then. Plain `git push` is not guarded (§4.10, P1).
- **Migration of older installs (H1).** On open (ggui, and every `git gg` command that opens a
  repository), managed hooks of older versions are uninstalled silently: previous hooks are
  restored byte-exact (config-defined and wrapper modes), and `refs/gg/*` is deleted (C3). No
  prompt, nothing is kept. `git gg hook …` stays as a silent exit-0 no-op for stale installs;
  `git gg hooks install|status` print "managed hooks were removed; Undo covers plain git without
  them"; `git gg hooks uninstall` runs the uninstall.
- The journal is append-only and locked, with the same lock rules as Git's ref locks. A
  corrupt or partly written journal is skipped, never fatal. Plain git never depends on ggui:
  nothing of ggui runs inside it.
- The Reflog panel stays the recovery path for anything Undo refuses.

### 4.13 Interactive rebase (new)
**Entry points**
- History: select a commit and choose **Interactive rebase from here…** (key I). This
  covers that commit and its descendants up to HEAD, or up to the tip of the selected
  branch.
- Select a range, then **Interactive rebase selection…**.
- Branches panel: **Interactive rebase onto…** (a new base plus the todo editor).
- Commit menu: **Interactive rebase…** (asks for a base).
- When a native rebase is stopped (from ggui or plain `git rebase -i`): **Edit
  remaining todo**.
- Optional: `git gg sequence-editor` can be set as Git's `sequence.editor`. Plain
  `git rebase -i` then opens ggui's todo editor window instead of a text editor, and the
  saved todo goes back to git.

**Todo editor (dockable panel with a modal header)**
- **Rows:** one per commit, oldest first (Git's order), with a toggle for newest-first
  display. Each row shows the action, short ID, subject, author, date, and badges for
  branches that point at the commit.
- **Actions:**
  - `pick`, `reword`, `edit`, `squash`, `fixup` (including `fixup -C` / `fixup -c`),
    `drop`
  - `exec` (shell command lines), `break`
  - `update-ref` (branch moves in stacked branches)
  - with `--rebase-merges`: `label`, `reset`, `merge`
- **Editing:** drag rows to reorder, or use Alt+↑/↓. Git's single-letter keys set the
  action: p, r, e, s, f, d, x, b. Multi-select changes several rows at once. Insert
  `exec`/`break` lines. Undo and redo apply inside the editor.
- **Messages:**
  - `reword` and `squash` rows get an inline message editor.
  - A squash group shows the combined message, prefilled the way Git does.
  - Messages are all collected **before** the rebase starts, so no external editor pops up
    during the run.
- **Options:**
  - onto (new base)
  - `--autosquash`: `fixup!`/`squash!`/`amend!` commits are placed and marked
    automatically, and can be toggled
  - `--update-refs`: default on. Branches in the range move with their commits.
  - `--rebase-merges`
  - `--autostash`
  - "exec after every commit" (for example running tests)
  - committer date handling: keep the original or use now
- **Live preview:**
  - The planned result is computed **in memory** on a worker (§3.1) and shown as a preview
    graph next to the list.
  - It shows which commits will get **first-class text conflicts**, which commits have
    **non-text conflicts** that need a decision, which commits become empty, and which
    branches move.
  - The preview updates as the list is edited: the newest edit wins and older previews are
    dropped.
- **Validation:**
  - The first row cannot be `squash`/`fixup`.
  - Warn when dropped commits are the only ones reachable from a branch.
  - Show the published-commit (locked) warning (§4.2).
  - Rebasing commits that already have first-class conflicts is allowed; their conflict
    terms are carried along and may resolve (§4.10).

**Execution (decision R3): two engines, picked automatically and shown in the editor**
1. **In-memory engine (default).** Used when the todo has only `pick`, `reword`, `squash`,
   `fixup`, `drop`, `update-ref`, and reorders.
   - Runs entirely in memory with libgit2, like every other ggui rewrite (R1).
     - Text conflicts become first-class conflicts in the file.
     - Non-text conflicts go through the pre-flight dialog.
   - Everything is then applied with **one `git update-ref --stdin` transaction**, and the
     worktree is updated afterwards.
   - `post-rewrite` runs with Git's `rebase` argument and the old→new mapping. One journal
     operation covers it, so **one Undo reverts the whole interactive rebase**.
   - It never leaves a sequencer state and never touches the worktree mid-way. Cancel at
     any point before applying leaves everything unchanged.
2. **Native engine (`git rebase -i`).** Required when the todo has `edit`, `break` or
   `exec`, or when the user chooses "Run as git rebase" (for example to use their own
   Git workflow).
   - ggui supplies the todo through `GIT_SEQUENCE_EDITOR`, and the collected messages
     through `GIT_EDITOR`. Both use a tiny `git gg` helper that writes prepared files.
   - Git does the actual rebase, so hooks, `rebase.*` config, rerere, autostash and
     filters all behave exactly as in plain Git.
   - Stops (`edit`, `break`, failing `exec`, conflicts) are ordinary native rebase states.
     §4.10 native UI applies: Continue / Skip / Abort, Amend and continue, Edit remaining
     todo, "Commit with conflicts" for text-only conflicts.
   - Because it is plain Git, the user can also finish in a terminal with
     `git rebase --continue`, and ggui follows along.
   - Undo: the whole native rebase, from `rebase (start)` to `rebase (finish)`, becomes one
     journal operation. That holds when ggui started it and for plain git rebases (the
     reconciler groups them from HEAD's reflog; undo-journal §4.1). Undo refuses while any rebase
     or `git am` is in progress.

**Plain `git rebase -i` started outside ggui**
- It is detected from `.git/rebase-merge/`. ggui shows the done, current and remaining
  todo lists and offers the same stop handling. **Edit remaining todo** writes
  `git-rebase-todo` exactly as `git rebase --edit-todo` would.
- ggui never rewrites Git's sequencer files in any other way.

**Relationship to single actions:** squash, reorder, drop, reword, move-before/after and
the drag-and-drop actions in §4.2/§4.3 are one-step shortcuts for the in-memory engine. Each
can be opened in the editor as a starting todo ("Open as interactive rebase…") when the
user wants to adjust more.

---

## 5. Design decisions the plan depends on

| ID | Decision | Recommendation |
|---|---|---|
| U1 (confirmed) | How Undo/Redo and the Operations panel work without the gg operation log | Scope: **refs plus index; worktree updated only when lossless**, otherwise refuse or offer to stash. A **ref-state journal** in `.git/gg/journal` (per worktree for HEAD, shared for other refs). Each entry is one operation: its source (ggui / git-gg / plain git via the reconciler), the before and after values of every ref it touched (HEAD, branches, tags, `refs/stash` and other reflog-backed refs), and the index tree when known. **Undo** is itself a new operation that restores the before values, like jj and gg, so Redo = undo the undo. Undo refuses if the refs no longer match the entry's after values (moved outside the journal, for example by a tool that changed refs since the last pass). It also refuses if the working tree would lose data, and offers to stash first. Commits that are only reachable from the journal are protected from `gc` by using the refs' reflogs (Git's `gc` keeps reflog entries until they expire), so there are no private refs. The journal is history only: deleting it disables past Undo and nothing else |
| R1 | Rewriting history that conflicts | Do rewrites in memory. **Text conflicts** become in-file first-class conflicts and the rewrite continues. **Non-text conflicts** are resolved in the pre-flight dialog before anything is written (§4.10). A ggui rewrite never leaves a native sequencer state, **except** an interactive rebase the user runs with the native engine (R3). Only failures outside content conflicts abort the rewrite with nothing changed: locked refs, worktree collisions, hook rejection, I/O errors |
| K1 (confirmed) | Which conflicts may be first-class | Text content only. Binary, filtered (LFS), mode, type, delete and rename conflicts must be resolved immediately (§4.10 table) |
| M1 | Marker format | Standard Git diff3 markers for two-sided conflicts, with the base always present. Extended alternating side and base sections for N sides. Marker length grows to avoid ambiguity. Edge cases (final newline, CRLF, empty sides) are encoded in-band. The spec defines the grammar formally; there is **no** separate storage |
| H1 (confirmed) | Hooks | The user's hooks run through git natively (G2). **No managed hooks**: plain git is journaled from reflogs and ref snapshots by the reconciler (§4.12 B, undo-journal §4). Older installs' hooks are removed silently on open |
| R3 (confirmed) | Interactive rebase engine | **In-memory by default** (pick, reword, squash, fixup, drop, update-ref, reorder): atomic, undoable as one operation, conflicts become first-class. **Native `git rebase -i`** when the todo has `edit`/`break`/`exec` or the user asks for it: the todo and messages are fed through `GIT_SEQUENCE_EDITOR`/`GIT_EDITOR`, and stops use the native in-progress UI. The editor always shows which engine will run and why |
| R2 | Which descendants get rebased | Descendants reachable from local branches that contain the rewritten commit, plus a detached HEAD. Never move remote-tracking refs. Refuse to move branches checked out in other worktrees unless the user confirms |
| C1 | Commit when nothing is staged | Configurable. Default: prompt "Stage all and commit?" |
| T1 (confirmed) | Transport | **`git` CLI** for clone, fetch, pull and push. The built-in credential dialog goes away; ggui is only an askpass prompt when needed |
| G1 | Filter drivers (LFS etc.) | **Not needed.** Checkout, add and staging go through `git`, which runs the filters. In-memory rewrites work on stored (clean) blobs, and conflicts in filtered files must be resolved immediately (K1) |
| G2 (confirmed) | Git access | Hybrid: libgit2 for reads and in-memory rewrites; `git` CLI for every mutation plain git has; ref moves after a rewrite go through one `git update-ref --stdin`. Minimum **git 2.36** (for `git hook run`), checked on startup with a clear error |
| P1 (confirmed) | Push with conflicts | Always refused by ggui; plain `git push` is not guarded |
| C3 (confirmed) | Leftover `refs/gg/*` from the old gg | On open (ggui and git-gg), delete them automatically and silently with one `git update-ref --no-deref --stdin`, with no prompt, no backup branches and no setting. The deletion is not journaled (the journal never records `refs/gg/*`), so it cannot be undone. Nothing is kept |
| X1 (confirmed) | Platforms | First release: **Linux and Windows** (MinGW-static and MSVC presets). macOS later: it needs the SDL_GPU MSL/Metal path, signing and a preset |
| I1 | CLI name | Ship the executable as `git-gg` so Git's subcommand lookup finds it as `git gg` |

---

## 6. `git gg` subcommand (deliberately minimal)

A `git-gg` binary on `PATH`, built in the ggui tree and linked to `libgg`. It uses CLI11 and has no revsets, filesets or
config wrapper.

```
git gg new [-m MSG] [--detach] [--before REV | --after REV] [PARENT...]
           # empty commit; default parent HEAD; advances the attached branch;
           # multiple parents create an empty merge commit
git gg undo                # undo last journal operation (ggui, git-gg, or plain git,
                           # read from the reflogs)
git gg redo
git gg op log              # list journal entries
git gg hooks uninstall     # legacy: removes managed hooks of older versions (§4.12 B);
                           # `install` and `status` only say they were removed
git gg hook <hook-name> [ARGS...]
                           # legacy: silent no-op (exit 0) for stale installs whose
                           # hooks still call it
git gg conflicts [REV]     # list files with first-class conflicts in REV (default
                           # HEAD); exit status 1 when there are any. Scriptable
git gg ui [PATH]           # optional: launch ggui on the repo
git gg sequence-editor FILE
                           # internal: used as GIT_SEQUENCE_EDITOR/GIT_EDITOR by ggui's
                           # native interactive rebase (writes prepared todo/messages).
                           # Optional: set as sequence.editor so plain
                           # `git rebase -i` opens ggui's todo editor
```

Rules:
- Exit codes and error text follow Git conventions (messages go to stderr, exit 128 on
  fatal errors).
- `git gg help <cmd>` / `--help` print plain text. No man-page or completion generator at
  first. `git gg` completion can be added later via a `git-completion` hook.
- Anything Git already does (commit, rebase, stash, worktree…) is **not** duplicated.
- `git gg hook` must return immediately, before opening the repository: stale hooks of older
  versions run it on every ref update.

---

## 8. Testing strategy

### 8.1 Policy
- **Only complete integration tests.** Every test is a Dear ImGui Test Engine test that runs
  inside the real `ggui` binary (`ggui --test[=filter]`). It drives the UI the way a user
  does: menus, shortcuts, context menus, drag and drop, dialogs, typing. Results are checked
  through the UI **and** against the real repository on disk.
- **No unit tests, no GoogleTest, no engine-only harness, no mocks** of libgit2, git or the
  filesystem. Internal functions are covered only by the user-visible behavior that needs
  them.
- **`git-gg` is tested inside the same scenarios.** A test runs `git gg …` or plain `git`
  commands as steps (for example a plain `git commit`, then Undo in
  ggui). `git-gg` then runs as a real child process, and its coverage data is collected too.
- **Real processes, real repositories:** each test builds its fixture repository with plain
  `git` in a temporary directory. It uses an isolated `HOME`, `XDG_CONFIG_HOME`,
  `GIT_CONFIG_GLOBAL`, preferences directory and `PATH` containing the `git-gg` under test.
  No network: remotes are local bare repositories, served over `file://` and a local
  `git daemon`/SSH shim where transport matters.
- **Test-only hooks in the app stay minimal:** stable ImGui IDs, the UI-thread assertion,
  a slow-git latency switch and a frame-time probe. There is no test-only way to call
  commands that skips the UI.

### 8.2 Coverage goals (CI gates)
- **Code coverage: > 90 % line and > 90 % branch** for first-party sources (`ggui`,
  `ggui_core`, `libgg`, `git-gg`), from the integration suite alone. Third-party code and
  generated fonts are excluded.
- **Tooling:**
  - Clang source-based coverage (`-fprofile-instr-generate -fcoverage-mapping`,
    `LLVM_PROFILE_FILE=%p-%m.profraw`, so every child `git-gg` process writes its own
    file). Merged with `llvm-profdata` and reported with `llvm-cov`: line, branch and
    region.
  - Fallback: GCC gcov with `-fprofile-update=atomic`, reported by gcovr with
    `--exclude-throw-branches --exclude-unreachable-branches`.
  - Keep and adapt `scripts/run_software_coverage.sh` and `coverage_report.py`.
- **Exclusions are rare and justified.**
  - `COVERAGE_EXCL` markers need a one-line reason, for example "defensive ImGui frame
    rejection" or "native picker integration".
  - CI reports how many there are and fails if the number grows without the allowlist being
    updated.
- Coverage is never left for the end.

### 8.3 Harness and fixtures
- **Fixture builder:** a scripted library of repository recipes run with plain `git`.
  Examples: linear, merges, many refs, linked worktrees, bare remotes, mid-merge,
  mid-rebase, mid-cherry-pick, unborn HEAD, SHA-256, LFS with a local test filter, submodules,
  CRLF/no-final-newline text, binary files, files containing marker-like text, conflicted
  commits with 2 and N sides. It lives in the test sources.
- **Large-repository fixture**, for responsiveness: ≥ 100k commits, ≥ 5k refs, ≥ 50k files.
  Generated once and cached in CI.
- **Assertion helpers:**
  - Read the UI (visible rows, labels, badges, dialog state), repository state through
    plain `git` (`fsck`, `status --porcelain=v2 -z`, `for-each-ref`, `log --format`,
    `cat-file`), and files on disk.
  - After every mutating test, check that `git fsck` passes and that the repository is in
    a state plain git understands.
- **CI runs** on Linux under Xvfb with software Vulkan, and on Windows. The suite
  is split into shards. Every shard runs with the UI-thread assertion on (§3.1).
- **Randomized scenarios**, used where the spec calls for property or differential
  checking. The test generates inputs (repository histories, file contents, todo lists) from a
  fixed seed, performs the actions **through the UI**, and compares the result with a
  reference. Failing seeds are logged and added as fixed cases.

### 8.4 Required scenario groups (each maps to §4 spec IDs)
- **Git compatibility:** after every action, compare the result with plain `git`.
- **Rewrite invariants** (the ideas come from `docs/graph-action-audit.md`, re-derived from
  the spec):
  - Ancestors and unrelated branches keep their IDs, trees and refs.
  - No-op rewrites keep IDs.
  - A failed operation leaves refs, HEAD, index and worktree untouched.
  - Undo restores exactly.
- **Conflicts:**
  - Round-trip and edge cases with crafted files: marker-like lines, CRLF, missing final
    newline, empty sides, N sides, and the opt-out attribute. These are created through
    rewrites in the UI and checked byte for byte.
  - Cancellation sequences (reorder A,B → B,A → A,B; conflict then drop). No nested markers.
  - Pre-flight dialog for every non-text kind; Cancel leaves everything byte-identical.
  - **Plain-Git transparency:** `git rebase`, `cherry-pick`, `commit --amend`, `merge`,
    `stash`, `gc --prune=now`, `clone` and `push` to a bare repo, run as test steps
    between UI checks. Deleting `.git/gg/` changes nothing ggui reports.
  - Push refusal from ggui (conflicted and broken-marker commits). Plain `git push` is not
    guarded, and Undo restores what it moved.
- **Interactive rebase:**
  - A randomized differential scenario: the same todo list is entered in the editor and run
    with the in-memory engine, and fed to `git rebase -i` on a copy of the repository.
    Trees, messages, authors and branch positions must match.
  - Autosquash order, native stops (edit/break/exec/conflict), Edit remaining todo, one-step
    Undo, and following a plain `git rebase -i` started as a test step.
- **Staging:** randomized hunk and line selections staged through the Diff panel, compared
  with the index that `git apply --cached` produces for the same patch.
- **Plain git without hooks (reconciler):**
  - Every plain Git ref-changing command in the fixture list becomes exactly one journal
    operation, labelled as typed, and Undo restores it: commit, amend, reset, checkout/switch,
    merge, cherry-pick, branch -f/-m/-c/-D, stash, tag, fetch, push.
  - Commands run while ggui is closed are journaled on open; `git gg undo` in a terminal
    reconciles first; ggui's own operations are never journaled twice.
  - A plain rebase (also `-i` with an edit stop, or run while ggui is closed) is one operation;
    abort leaves nothing to undo; Undo refuses while a rebase is in progress.
  - Undo carry by action word: reset --hard carries the clean tree, commit keeps the changes.
  - No replay on the first run, after the state file is deleted, or after the journal is
    deleted; an expired reflog falls back to the snapshot difference without duplicates; a stale
    cursor never duplicates an operation; a fetch of thousands of refs is one fast operation.
  - Linked worktrees: a plain commit is journaled from that worktree's own reflog; another
    worktree's HEAD is never seen as created or deleted.
  - Undo of a symbolic ref restores it by name.
- **Hooks and migration:**
  - User hooks run and their exit codes are respected.
  - Managed hooks of older versions (config mode, wrapper scripts, relative `core.hooksPath`,
    linked worktrees) are uninstalled silently on open in ggui and in `git gg`, previous hooks
    restored byte-exact, and plain git is journaled afterwards; `git gg hook` exits 0 silently;
    `git gg hooks install|status|uninstall` behave as in §4.12 B.
- **Stash, remotes, worktrees, blame, reflog, settings, recent repositories, and old gg
  refs cleanup:** at least one scenario per spec ID.
- **Responsiveness:**
  - The whole suite runs with the UI-thread assertion on.
  - A frame-time scenario on the large fixture, with slow-git mode, asserts no frame
    exceeds ~33 ms because of repository work.
  - Cancel works for every long operation.
- **Failure paths are required for branch coverage:** user-hook rejection, git missing or too
  old, locked refs, checkout collisions, network failure (unreachable remote), cancelled
  operations, and a corrupt journal. Each is triggered from the scenario, never simulated
  inside the code.

---

## 9. Removal checklist (jj-specific parts that must not come back)
- The old libgg and the `gg` repository: `<gg/gg.h>`, the `gg::gg` / `ggConfig.cmake` packaging, the
  `gg` CLI binary in install bundles. There is no stable library ABI to maintain.
- `refs/gg/workspaces/*`, `refs/gg/commit-aliases`, `refs/gg/visible-heads/*`, the
  alias-prefix namespace and alias GC. **`refs/gg/conflicts/*` and any other conflict
  metadata**: conflicts live only in file content.
- Automatic snapshot of the working tree into `@` and the max-new-file-size setting.
- Revset and fileset languages. `gg` CLI families (`branch`, `file`, `util`, `workspace`,
  `config`, `operation restore --what`, `next/prev`). Agent skill docs.
- Fetch rule that automatically fast-forwards local branches.
- The "@ is a change you edit" wording in the UI. The UI should say HEAD, branch, commit,
  staged and unstaged instead.
