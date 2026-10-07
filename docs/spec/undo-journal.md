# Undo journal — format and semantics (U1)

Status: **draft for review**. Implements product spec §5 U1 and §4.12 B.
Implementation: `Source/libgg/Journal.cpp` and `Source/libgg/Reconcile.cpp`; used by ggui and
`git gg undo|redo|op log`. Plain git is recorded by the reconciler (§4), without hooks.

The journal is **history only**. Deleting it disables Undo for past operations and changes
nothing else: no commit, file, ref or conflict means something different without it.

---

## 1. Location and layout

| Path | Content |
|---|---|
| `$GIT_COMMON_DIR/gg/journal` | The journal (one file, shared by all worktrees) |
| `$GIT_COMMON_DIR/gg/journal.lock` | Lock file while appending or reconciling |
| `$GIT_COMMON_DIR/gg/reconcile.json` | Reconciler state: baseline and HEAD reflog cursors (§4) |
| `$GIT_COMMON_DIR/gg/rebase/<W>/` | The operation that spans a native rebase in worktree *W* (§4.1) |
| `$GIT_COMMON_DIR/gg/cache/` | Disposable caches (not part of this spec) |

`$GIT_COMMON_DIR` is `.git` of the main worktree (or the bare repository). A linked worktree
uses the same journal.

**Per-worktree HEAD.** Every `HEAD` value is recorded with the worktree it belongs to: the key is
`HEAD` for the main worktree and `worktrees/<id>/HEAD` for a linked worktree `<id>` (the directory
name under `$GIT_COMMON_DIR/worktrees/`). Index trees are recorded per worktree the same way.
Other refs (`refs/heads/*`, `refs/tags/*`, `refs/stash`, `refs/remotes/*`, any other
reflog-backed ref) are shared. Undo run from worktree *W* considers only operations that touched
a shared ref or *W*'s own HEAD/index (§5.1); a keep ref (`refs/gg/keep/*`) does not count. An operation that added, removed, locked or unlocked
a linked worktree (`worktree` records) belongs to the worktree that ran it only (§5.4).

## 2. Encoding

- UTF-8 text, one **record** per line (JSON Lines). Every record is a JSON object on a single
  line, terminated by `\n`.
- The first line of a new journal is the header record `{"gg-journal":1}`. The number is the
  format version. A reader that finds a higher major version stops reading and reports "journal
  written by a newer ggui".
- Every record has `"v":1` (record version) and `"t"` (type). Unknown types and unknown fields
  are ignored, so later minor additions are compatible.
- Object IDs are full lowercase hex (40 or 64 characters). The null ID (all zeros) means "did not
  exist". A symbolic value is written `"ref:<target>"` (e.g. `"ref:refs/heads/main"`).

### 2.1 Record types

| `t` | Fields | Meaning |
|---|---|---|
| `begin` | `op`, `src`, `label`, `time`, `wt`, optional `undoes`, optional `cmd`, optional `pid`, `pstart` | Opens operation `op` |
| `refs` | `op`, `u`: list of `[ref, old, new]` | Ref updates that happened in `op` |
| `index` | `op`, `wt`, `before`, `after` | Index tree of worktree `wt` before/after (tree IDs) |
| `map` | `op`, `m`: list of `[old commit, new commit]` | Rewrite mapping (post-rewrite) |
| `rebase` | `op` | `op` spans a native rebase (§4.1): open until its `end` record |
| `worktree` | `op`, `do`, `path`, optional `head`, `branch`, `locked`, `reason` | `op` added, removed, locked or unlocked the linked worktree at `path` (§5.4) |
| `end` | `op`, optional `ok` (false = failed) | Closes operation `op` |

- `op` — operation ID: `<unix-ms>-<8 hex>` for everything written now (ggui, git-gg and the
  reconciler). `git-<pid>-<start>` only appears in journals written by older versions, whose hooks
  opened such operations for a plain git command; readers accept both.
- `src` — `ggui`, `git-gg`, or `git` (plain git, recorded by the reconciler, §4).
- `label` — human text shown in the Operations panel, e.g. `commit`, `git rebase -i`,
  `undo "commit"`.
- `time` — Unix time in milliseconds. For a reconciler operation made from a reflog entry: that
  entry's time (1 s granularity).
- `pid`, `pstart` — process ID and start time (as `/proc` or the Windows process times give it) of
  the ggui or git-gg process that opened the operation. Absent in older journals and on reconciler
  operations. They tell the reconciler whether an open operation is still being written (§4).
- `wt` — worktree key of the process that opened the operation (`main` or the linked id).
- `undoes` — on an undo operation: the `op` it reverts. Redo is an undo of an undo.
- `cmd` — for `src:"git"`: the reflog message of the command (e.g. `commit: fix typo`,
  `checkout: moving from main to feat`); empty for a lump (§4). Operations of older
  versions hold the git command line instead.
- `do` — `add`, `remove`, `lock` or `unlock`. `path` is the worktree's directory as
  `git worktree list` shows it. For `add`/`remove`: `head` is the commit it had checked out,
  `branch` its branch (`refs/heads/<name>`, absent when detached), `locked`/`reason` its lock at
  the time. For `lock`/`unlock`: `reason` is the lock's reason. One record per worktree change,
  in the order they happened.

An operation is the set of all records with the same `op`. Records of different operations may
interleave (concurrent processes). An operation without `end` is **open**. Readers close open
`src:"git"` operations only: a reconciler operation at once, a legacy `git-<pid>-<start>` one after
10 minutes or when its process is gone. Open ggui and git-gg operations are never closed by readers;
the reconciler stops deferring to one (§4) when its `pid`/`pstart` process is gone, or, without
`pid`, after 10 minutes. A rebase operation (§4.1) stays open until its `end`.

### 2.2 Example
```
{"gg-journal":1}
{"v":1,"t":"begin","op":"1759000000000-a1b2c3d4","src":"ggui","label":"commit","time":1759000000000,"wt":"main"}
{"v":1,"t":"index","op":"1759000000000-a1b2c3d4","wt":"main","before":"4b82…","after":"4b82…"}
{"v":1,"t":"refs","op":"1759000000000-a1b2c3d4","u":[["refs/heads/main","9f1e…","c0ff…"]]}
{"v":1,"t":"end","op":"1759000000000-a1b2c3d4"}
{"v":1,"t":"begin","op":"1759000005000-0badcafe","src":"ggui","label":"undo \"commit\"","time":1759000005000,"wt":"main","undoes":"1759000000000-a1b2c3d4"}
{"v":1,"t":"refs","op":"1759000005000-0badcafe","u":[["refs/heads/main","c0ff…","9f1e…"]]}
{"v":1,"t":"end","op":"1759000005000-0badcafe"}
```

## 3. Writing

- **Append-only.** Records are only ever appended. Nothing is rewritten in place.
- **Locking** follows Git's ref-lock rules: create `journal.lock` with `O_CREAT|O_EXCL`; if it
  exists, retry for up to 1 s (10 ms steps), then give up: ggui reports "journal busy", a reconcile
  pass is skipped (the next event retries). A lock older than 10 minutes is stale and is removed.
- The reconciler's `Journal::Transaction` holds the lock across its whole pass: it reads the
  journal, the refs and the reflogs, appends its records and writes `reconcile.json` before
  releasing it, so it serialises with every other writer. It never refreshes the lock, so a pass
  stays short.
- Each record is written with a single `write` of the complete line to a file opened with
  `O_APPEND`, then the lock is deleted. `fsync` is not required (history only).
- **Torn or corrupt lines** (no trailing `\n`, invalid JSON, missing `op`) are skipped by readers.
  The next writer first appends a `\n` if the file does not end in one, so a torn line never
  merges with a new record.

## 4. Recording plain git: the reconciler

ggui and git-gg open an operation (`begin`), record ref and index state themselves, and close it
(`end`). Plain git (terminal, other tools) runs no code of ours. The **reconciler**
(`gg::reconcile::run`) finds what it did afterwards, from the refs and their reflogs, and writes
it to the journal as operations with `src:"git"`. No hooks are involved.

- **Known value.** The journal says what every ref should be: *Known(ref)* is the last `new` value
  of the ref over all operations (undo and open ones included), else the baseline value from
  `reconcile.json`, else the null ID. After a pass, Known equals the current value of every ref:
  all `refs/*` (except `refs/gg/*`, but `refs/gg/keep/*`, the keep refs, are tracked, whichever
  operation or worktree created them) and this worktree's `HEAD`. Other worktrees' `HEAD` keys are
  not judged by a pass of this one.
- **State file** `$GIT_COMMON_DIR/gg/reconcile.json`:
  `{"v":1,"baseline":{"<ref>":"<value>"},"journal":"<first op id>","cursors":{"<HEAD key>":{"n","old","new","time","msg"}}}`.
  Written to a temp file and renamed, only when it changed. A rename that fails is retried for a
  short time: on Windows it fails while a reader has the file open. Disposable, like the journal.
  - The **first run** (no file) writes the baseline (every ref's current value) and the cursors
    (reflog tips). Nothing is replayed: what happened before ggui first looked is not history.
  - `journal` is the ID of the journal's first operation. When it no longer matches (the journal
    was deleted or replaced) the baseline and cursors are made again from the current refs: no
    operations, no replay. A ref the reconciler sees for the first time after the first run counts
    as created (Known = null ID); the baseline gets the value of a worktree's `HEAD` the first
    time that worktree is seen.
- **When it runs.** On repository open and on every Watcher ref or journal change (ggui); at the
  start of every ggui and git-gg operation (`OperationRecorder::begin`); before Undo and Redo plan;
  in `git gg op log`. Under the journal lock (§3); when the lock is busy or the journal unreadable
  the pass is skipped and the next event retries.
- **Deferral.** A pass does nothing while another ggui or git-gg operation is open and being
  written: `src` not `git`, not spanning a rebase (§4.1), and its `pid`/`pstart` are alive (no
  `pid` recorded: it is younger than 10 minutes). Its refs are not recorded yet, so a snapshot now
  would call them external. That operation's `OperationRecorder` records its own changes; its
  `finish` moves the HEAD cursor past the reflog entries of the git commands it ran, so they are
  never journaled twice.
- **HEAD reflog, one operation per git command.** The reflog of this worktree's `HEAD` is read
  from the cursor (the entry with the stored fingerprint, searched where it would be if nothing
  expired) to the tip. Every entry is one plain git command and becomes one operation:
  - `label`: `git commit`, `git checkout feat`, `git reset HEAD~1`, `git merge x`, `git rebase`,
    `git pull`, `git cherry-pick`, `git revert`, else `git <action word>`; `cmd`: the reflog
    message; `time`: the entry's time.
  - The HEAD value is derived, not read: `checkout: moving from A to B` puts HEAD on branch `B`
    when it exists (else detached); a rebase start detaches it; `rebase (finish): returning to
    refs/heads/x` puts it back on `x`. It is recorded as `ref:<branch>` when symbolic. While HEAD
    is on a branch, a HEAD entry that is not a checkout implies the same change of that branch,
    in the same operation. A branch created by `checkout -b` / `switch -c` is created in it too.
  - Entries that changed nothing (`git stash`'s `reset: moving to HEAD`, a rebase that ended where
    it started) are dropped. The derived HEAD is forced to equal the actual HEAD.
  - No match for the cursor (reflog expired, rewritten or deleted) or no reflog: HEAD is handled
    by the snapshot difference (the lump below).
- **Branch chain-walk.** For each `refs/heads/*` and `refs/stash` whose value differs from Known
  after the HEAD operations, its own reflog is walked newest to oldest while it is chain-consistent
  (each entry's `new` is the younger one's `old`, the newest's `new` is the ref now) until an
  entry starts at Known (at most 1000 entries). Each step is one operation: `git branch -f x`,
  `git branch x`, `git branch -m`, `git branch -c`, `git stash`. A step with the same old, new and
  message as a HEAD operation joins that operation; a rename step joins by its message alone.
- **Lump.** What is left is written as **one** operation per pass: remote-tracking refs, tags,
  other refs, every deletion, refs whose reflog chain does not reach Known, and HEAD without a
  usable cursor. Label `git fetch`, `git push` or `git pull` when the newest reflog entry of a
  changed remote-tracking ref says so (at most three reflogs are read), else `external changes`;
  `cmd` empty, `time` now. It restores to Known, so Undo of a lump puts every ref back at once.
- **Keep refs (invariant K, product spec K2).** `refs/gg/keep/*` is derived, repository-wide state.
  `keep::maintain` runs in `OperationRecorder::finish`, before it reads the after-values, and at the
  end of each reconcile pass that was not deferred or skipped. `finish` skips it while a native
  rebase is stopped, unless the operation passed keep ids of its own; the pass does not skip. Its
  candidates are the existing keep refs and the commits the caller names; `keep::maintain` reads no HEAD. A commit is kept because a ggui or git-gg operation created it on a detached
  HEAD and named it, never because a detached HEAD sits on it: a checkout, a commit made by plain
  git and a rebase finished in a terminal keep nothing, and a pass or an operation that names
  nothing keeps no commit that was not kept (it still deletes, and repairs: a misnamed ref's commit
  gets its ref under the right name). A failure never fails the operation or
  the pass.
  - *Who names.* An operation that creates commits (`OperationRecorder::setCreatesCommits`,
    `MutationSpec::createsCommits`: Commit, also of the working tree and with
    conflicts, amend, also Amend and continue, New commit, merge, pull then push, interactive
    rebase, Continue, Skip, `git gg new`) names the commit it leaves its worktree's detached HEAD
    on, when the operation succeeded, HEAD moved in it and no native operation (merge, cherry-pick,
    revert, rebase, bisect) is in progress afterwards (a rebase
    stopped when the operation began and over when it ends counts even if HEAD did not move in the
    last step). A rewrite names what `Result::keepExtra` holds (below), and Undo what
    `UndoPlan::keepExtra` holds.
  - *Ownership.* The creation of a keep ref is recorded in an operation exactly when the
    operation named the commit; this holds also for a recorder that joined an open rebase group (a
    rebase finished through ggui), unless an operation begun after the group already records that
    keep ref: the creation is then written as housekeeping, since the next pass's Known takes a
    ref's value from the last operation in begin order. A deletion, or a move, is recorded in the
    operation the maintenance ran for (in a pass: only an operation begun in that pass), also for a
    recorder that joined an open rebase group, unless an operation begun after the group already
    records that keep ref (housekeeping then, as for a creation). Every other keep change is written
    as an operation of its own: `src` `gg`, label `keep refs`, holding only keep refs (`Operation::keepOnly()`,
    `journal::writeKeepHousekeeping`), so the next pass finds it in Known: a deletion by a pass
    that began no operation to give it to, the keep changes of a joined recorder named above, and a
    keep ref changed by something else while an operation was open (a repair, another process). A keep ref that exists already when the
    operation that made its commit is journaled stays with the operation that wrote it; undoing the
    operation that made the commit leaves the commit kept.
  - *Rewrites.* A rewrite (reword, amend, squash, move, drop, ...) carries a keep ref the way it
    carries a local branch. The commits kept by a keep ref count among the descendants a rewrite
    replays. For each keep ref whose commit the rewrite replaced (a key of `Result::mapping`, which
    maps a dropped commit to its replacement parent), `Rewriter::apply` deletes `refs/gg/keep/<id>` in the
    transaction that moves the branches, and `Result::keepExtra` holds the replacement. The caller
    that owns the recorder passes it to `OperationRecorder::setKeepExtra` (`MutationContext::keepExtra`
    in ggui, the recorder itself in git-gg), so the one maintenance of `finish` keeps the replacement
    unless a branch, remote-tracking branch or tag reaches it or it is not a tip; a rewrite also
    names the commit it puts a detached HEAD on, not one it puts a branch on. Dropping a
    kept tip whose parent is on a branch leaves no keep ref; above another unreachable commit it
    keeps that commit. The deletion and the creation are the rewrite's own entries: no `keep refs`
    operation, and Undo and Redo restore them as any other keep entries (below).
    An amend without descendants (the commit dialog), when no native rebase is in progress,
    replaces its commit without a rewrite: it deletes `refs/gg/keep/<id>` of the amended commit itself, in the same
    operation, and the new commit is kept as any commit that an operation creates on a detached HEAD.
  - *Undo and Redo.* A keep ref does not make an operation visible from another worktree (§5.1).
    Only a `keep refs` operation (`src` `gg`, `Operation::keepOnly()`) is passed over by Undo and
    Redo; an operation of a user that changed only keep refs is a target like any other (its
    restore may hold keep deletions only, or nothing but commits to keep again). The keep entries of the undone
    operation are never a reason for "refs moved outside the journal" and are not restored
    literally: a keep ref the operation created is deleted if it still exists; the commit of one
    it deleted is handed to the maintenance (`UndoPlan::keepExtra`,
    `OperationRecorder::setKeepExtra`), which keeps it again unless something reaches it. So Undo of
    an operation drops the keep refs it created, and Redo of that Undo keeps the commit again.
  - A `keep refs` operation stays in the journal and in the Operations panel; Undo and Redo pass
    over it, and the Restore item of its row is disabled.
- **Dedupe.** `OperationRecorder::finish` may advance the cursor without the lock when that stays
  busy, and ggui may crash between its append and the cursor. An operation derived from reflog entries whose
  ref changes already are the newest journal changes of those refs is not written again.
- **Result.** Each plain git command is one operation in the Operations panel, labelled as typed
  (`git commit`, `git checkout feat`), also for commands run while ggui was closed. One
  operation per command, and a rebase from start to finish one operation (§4.1). Operations with no
  restorable change are not written.
- **Limits.** Plain-git steps on refs without a reflog (tags, deleted branches) made between two
  passes are lumped into one operation. `checkout --detach <branch>` cannot be told from a checkout
  of the commit in the reflog. A chain longer than 1000 entries, or rewritten (`stash drop`),
  falls back to the lump. The reflog is read around the refs and a pass retries when the tip moved
  between the reads. Reftable repositories are not supported (libgit2). `git worktree add|remove|lock|unlock` are never
  recorded by the reconciler: only ggui and git-gg write `worktree` records.

### 4.1 Native rebases (several git commands, one operation)
A `git rebase` (either backend) that stops runs as several git commands
(`git rebase -i`, `git rebase --continue`, …). From `rebase (start)` to `rebase (finish)` or
`(abort)` it is **one** operation:
- The HEAD reflog entries of a rebase are grouped by the reconciler: from `rebase … (start)` (also
  `pull --rebase … (start)`) to `(finish)` / `(abort)`, whatever git commands the user ran in
  between (`commit --amend` at an edit stop, the commit that resolves a conflict), however many
  passes it takes. A plain rebase is one operation from start to finish, also when it ran while
  ggui was closed.
  The apply backend of older git writes `rebase: checkout <onto>`, `rebase finished: …` and
  `rebase: updating HEAD` instead; these are recognised as the start, the finish and the abort.
  (`git rebase <upstream> <branch>` with an up-to-date branch writes only `rebase: checkout
  <branch>`, with any git: a start without an end, which the entries after it in the same pass join.)
- While the rebase is in progress the operation is **open** (and cannot be undone) until its
  `end`. It is remembered in `$GIT_COMMON_DIR/gg/rebase/<W>/operation` with the rebase's identity
  (`orig-head`, `onto` and `head-name` from `rebase-merge/`, or, for the apply backend
  (`git rebase --apply`), from `rebase-apply/` when it holds the `rebasing` marker; `git am` has
  `applying` instead and is not a rebase) and the `src` of the operation's
  opener, and a `rebase` record is appended. A rebase that ggui started (its Start step opens the
  operation) and that is finished in a terminal stays that one operation: the reconciler appends
  the rest of its ref changes and the final index tree.
- While that rebase is in progress, ggui and git-gg operations (other than undo/redo) join it
  instead of opening their own, and the reconciler appends the plain git commands' ref changes to
  it.
- It ends when its `finish` or `abort` entry is seen (ggui's step that finished it writes `end`
  itself). A remembered operation whose rebase is no longer in progress and that has no such entry
  (`git rebase --quit`, an expired reflog) gets its `end` the next time a pass runs or before Undo
  plans. A pass that sees a rebase's entries that change something, without the finish, opens the
  operation also when the rebase is no longer in progress (git writes the finish entry before it
  removes `rebase-merge/`: a pass that read the reflog just before it finds the rebase gone and
  no finish): the finish entry joins it in the next pass, which ends it, or ends it without one.
  The rebase's own move of its branch (the branch's reflog entry `rebase (finish): refs/heads/<b>
  onto …`) is written before HEAD's finish entry: a pass that sees it first puts it into that same
  operation, when the branch ends where HEAD stands after the rebase's last step (another
  worktree's rebase finishing is not part of it).
- **Undo refuses while a rebase is in progress** in the worktree: a rebase of either backend ("finish or abort the
  rebase first") or `git am` (`rebase-apply/` without `rebasing`; "... git am first"), for every operation, older ones included: undoing what came before would pull refs
  out from under git.
- Deleting `gg/rebase/` only splits the rebase into several operations.
- When git detaches a symbolic HEAD (a rebase starting), HEAD is recorded as `ref:<branch>`, never
  as the branch's commit. Undo skips refs whose recorded old and new values are equal (HEAD back
  on its branch at the end of a rebase).

## 5. Undo and redo

### 5.1 Which operation
Let *W* be the current worktree. The *visible* operations are those that touched a shared ref,
or *W*'s HEAD/index. A keep ref (`refs/gg/keep/*`) is repository-wide housekeeping and does not
count as a shared ref here: it never makes an operation visible from another worktree. An
operation that changed no HEAD and no ref but keep refs (a rewrite of commits only a keep ref
reaches) is visible from the worktree it ran in and from no other, unless it is a `keep refs`
operation (`src` `gg`), which is visible from none.

- Only operations with something to restore count: a ref whose recorded old and new values
  differ, an index tree, or a `worktree` record (a no-op `git reset --hard` is passed over).
- **Undo** picks the newest visible operation that is not an undo operation and is not already
  undone (no later operation has `undoes` equal to it, unless that undo was itself undone).
- **Redo** picks the newest visible undo operation that is newer than every non-undo operation
  and whose effect is still in place (it has not been undone). Redo = undo of that undo.
- A new normal operation after some undos makes those undos non-redoable.

### 5.2 Effect
Undo of operation *X* is a **new operation** (`undoes: X`) that:
1. checks every ref *X* touched: its current value must equal *X*'s last recorded `new`
   value. If any differs, Undo is **refused**: "refs moved outside the journal" (lists them);
2. checks the working tree of *W*: if restoring HEAD would overwrite uncommitted changes, Undo is
   refused with an offer to stash first;
3. applies all ref changes back to their first recorded `old` values in **one**
   `git update-ref --stdin` transaction (old values verified), creating reflog entries. A symbolic
   ref (`HEAD` on a branch, `refs/remotes/<r>/HEAD`) is restored by name with `git symbolic-ref`
   after that transaction, each on its own; HEAD is detached first, on its own, when it goes back
   to a detached commit while its branch is restored too;
4. restores *W*'s index to *X*'s recorded `before` index tree when known (`git read-tree`), and
   updates the working tree only when that is lossless (`git read-tree -m -u` between the two
   HEAD trees). A reconciler operation (`src:"git"`, no index recorded) carries a clean index and
   working tree back along with HEAD when its reflog action word (from `cmd`) is `checkout`,
   `rebase`, `merge`, `pull`, `cherry-pick`, `revert`, `am` or `reset`; `commit` never carries
   (its changes stay in the working tree). Legacy `git-<pid>-<start>` operations keep the
   command-line parser.

Before it plans, Undo runs a reconcile pass (§4), so plain git since the last pass is undoable.
Undo and Redo are refused while a rebase is in progress (§4.1).

A branch the restore would delete must not be checked out in any worktree (git refuses to delete
one too): Undo is refused ("the branch x is checked out in <path>"), unless that worktree is *W*
itself and its HEAD is restored as well, or the same undo removes that worktree first (§5.4).

### 5.4 Worktrees
An operation's `worktree` records are undone by the opposite change, last record first:

| Record | Undo | Refused (nothing changed) when |
|---|---|---|
| `add` | `git worktree remove <path>` (unlocked first when locked, locked again if that fails) | the worktree is no longer registered, it is the worktree Undo runs in, its HEAD moved on (another branch, or a detached HEAD at another commit), or it has uncommitted changes or untracked files (`git status --porcelain`; a `--no-checkout` worktree counts as changed until its files are checked out) |
| `remove` | `git worktree add [--lock [--reason r]] <path> <branch>` (or `--detach <path> <head>`) | a worktree is registered at the path, the directory exists and is not empty, the branch no longer exists (and is not brought back by the same undo) or is checked out elsewhere, or the detached commit no longer exists |
| `lock` | `git worktree unlock <path>` | the worktree is gone or not locked |
| `unlock` | `git worktree lock [--reason r] <path>` | the worktree is gone or locked already |

- Every opposite change is checked before anything is changed. Removals and unlocks run before
  the index and ref restore (a branch the added worktree had checked out can then be deleted);
  adds and locks run after it (a branch the worktree needs exists again). When an add or lock
  fails at that point, the refs are put back and the undo operation is recorded as failed.
- The undo operation records what it did as `worktree` records, so Redo (an undo of the undo)
  works the same way.
- What Undo cannot bring back: uncommitted changes and untracked files that a forced removal
  deleted (ggui asks before forcing, and says so), and files git ignores (`git worktree remove`
  deletes them). Undo of a removal re-creates the worktree at its branch as the branch is now, or
  at the recorded detached commit.
- `git worktree prune` and `git worktree repair` only rewrite git's administrative files. ggui
  journals them without anything to restore: they appear in the Operations panel, and Undo passes
  over them.
- **Visibility:** an operation with `worktree` records is visible only from the worktree that ran
  it (its `wt`), even when it also changed shared refs (a branch created with the worktree). Undo
  in another worktree never removes or re-creates worktrees it did not manage.

### 5.5 Garbage collection
The journal holds no refs. Commits that are reachable only through the journal stay alive
through the reflogs that `git update-ref` writes (Git keeps unreachable reflog entries for
`gc.reflogExpireUnreachable`, 30 days by default). ggui passes `--create-reflog` so bare
repositories keep them too. After reflog expiry, Undo of an old operation fails with "commit no
longer exists" and changes nothing.

## 6. Versioning
- Header `{"gg-journal":1}` = major version 1. A new major version is only for incompatible
  changes; the writer then starts a new file `journal.v2` and leaves `journal` as is.
- Record field `"v"` lets individual records evolve; readers skip records with a higher `v` than
  they understand.
