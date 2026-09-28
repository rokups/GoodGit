# Undo journal — format and semantics (U1)

Status: **draft for review** (P0-05). Implements REBUILD_PLAN §5 U1 and §4.12 B.
Implementation: `Source/libgg/Journal.cpp`; used by ggui, `git gg undo|redo|op log` and
`git gg hook …`.

The journal is **history only**. Deleting it disables Undo for past operations and changes
nothing else: no commit, file, ref or conflict means something different without it.

---

## 1. Location and layout

| Path | Content |
|---|---|
| `$GIT_COMMON_DIR/gg/journal` | The journal (one file, shared by all worktrees) |
| `$GIT_COMMON_DIR/gg/journal.lock` | Lock file while appending |
| `$GIT_COMMON_DIR/gg/cache/` | Disposable caches (not part of this spec) |

`$GIT_COMMON_DIR` is `.git` of the main worktree (or the bare repository). A linked worktree
uses the same journal.

**Per-worktree HEAD.** Every `HEAD` value is recorded with the worktree it belongs to: the key is
`HEAD` for the main worktree and `worktrees/<id>/HEAD` for a linked worktree `<id>` (the directory
name under `$GIT_COMMON_DIR/worktrees/`). Index trees are recorded per worktree the same way.
Other refs (`refs/heads/*`, `refs/tags/*`, `refs/stash`, `refs/remotes/*`, any other
reflog-backed ref) are shared. Undo run from worktree *W* considers only operations that touched
a shared ref or *W*'s own HEAD/index (§5.1). An operation that added, removed, locked or unlocked
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
| `begin` | `op`, `src`, `label`, `time`, `wt`, optional `undoes`, optional `cmd` | Opens operation `op` |
| `refs` | `op`, `u`: list of `[ref, old, new]` | Ref updates that happened in `op` |
| `index` | `op`, `wt`, `before`, `after` | Index tree of worktree `wt` before/after (tree IDs) |
| `map` | `op`, `m`: list of `[old commit, new commit]` | Rewrite mapping (post-rewrite) |
| `rebase` | `op` | `op` spans a native rebase (§4.1): open until its `end` record |
| `worktree` | `op`, `do`, `path`, optional `head`, `branch`, `locked`, `reason` | `op` added, removed, locked or unlocked the linked worktree at `path` (§5.4) |
| `end` | `op`, optional `ok` (false = failed) | Closes operation `op` |

- `op` — operation ID: `<unix-ms>-<8 hex>` for ggui/git-gg; `git-<pid>-<start>` for operations
  opened by the hooks for a plain git command (§4).
- `src` — `ggui`, `git-gg`, or `git`.
- `label` — human text shown in the Operations panel, e.g. `commit`, `git rebase -i`,
  `undo "commit"`.
- `time` — Unix time in milliseconds.
- `wt` — worktree key of the process that opened the operation (`main` or the linked id).
- `undoes` — on an undo operation: the `op` it reverts. Redo is an undo of an undo.
- `cmd` — for `src:"git"`: the git command line when known.
- `do` — `add`, `remove`, `lock` or `unlock`. `path` is the worktree's directory as
  `git worktree list` shows it. For `add`/`remove`: `head` is the commit it had checked out,
  `branch` its branch (`refs/heads/<name>`, absent when detached), `locked`/`reason` its lock at
  the time. For `lock`/`unlock`: `reason` is the lock's reason. One record per worktree change,
  in the order they happened.

An operation is the set of all records with the same `op`. Records of different operations may
interleave (concurrent processes). An operation without `end` is **open**; readers treat an open
operation older than 10 minutes, or whose process no longer exists, as closed.

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
  exists, retry for up to 1 s (10 ms steps), then give up. Git-gg hook callers give up silently
  (the operation is not recorded); ggui reports "journal busy". A lock older than 10 minutes is
  stale and is removed.
- Each record is written with a single `write` of the complete line to a file opened with
  `O_APPEND`, then the lock is deleted. `fsync` is not required (history only).
- **Torn or corrupt lines** (no trailing `\n`, invalid JSON, missing `op`) are skipped by readers.
  The next writer first appends a `\n` if the file does not end in one, so a torn line never
  merges with a new record.

## 4. Grouping and the loop guard

- ggui and git-gg open an operation (`begin`), set `GG_OPERATION=<op>` in the environment of
  every child git process, record ref and index state themselves, and close it (`end`).
- The managed `reference-transaction` hook (`git gg hook reference-transaction committed`)
  appends a `refs` record with the updates read from stdin:
  - if `GG_OPERATION` is set, the updates join that operation (no `begin`): this is the loop
    guard that prevents duplicates;
  - otherwise the hook computes a command key from the git process that runs it (its parent
    PID and that process's start time; on Linux from `/proc`, on Windows best effort). If the
    journal's tail (last 64 KiB) has an open operation with op `git-<pid>-<start>`, the updates
    join it; otherwise it appends `begin` (src `git`, label from the command line, e.g.
    `git rebase -i`) followed by the `refs` record.
- `post-checkout`, `post-merge`, `post-commit` and `post-rewrite` hooks add context to the same
  operation: `map` records (post-rewrite stdin), index trees, and `end` when the command is known
  to be finished (post-merge, post-commit and post-checkout of a top-level command; post-rewrite
  of `rebase`).
- One plain git command therefore yields exactly one operation.
- `git worktree add` creates the new worktree's HEAD from the worktree it runs in, and git names
  it `HEAD` in the transaction all the same. The hook keeps a `HEAD` update only when this
  worktree's HEAD has that value after the transaction; otherwise it belongs to another worktree
  and is left out (so a plain `git worktree add` never looks like the main worktree switching
  branches). The hooks cannot see worktrees being added or removed: only ggui and git-gg write
  `worktree` records.

### 4.1 Native rebases (several git commands, one operation)
A `git rebase` (merge backend, `rebase-merge/`) that stops runs as several git commands
(`git rebase -i`, `git rebase --continue`, …). From `rebase (start)` to `rebase (finish)` it is
**one** operation:
- The operation that first changes refs while a rebase is in progress in worktree *W* (ggui's
  Start, or the hooks for the plain `git rebase -i` process) is remembered in
  `$GIT_COMMON_DIR/gg/rebase/<W>/operation` with the rebase's identity (`orig-head`, `onto` and
  `head-name` from `rebase-merge/`), and a `rebase` record is appended: the operation stays open
  (and cannot be undone) until its `end`, even after the git process that began it is gone.
- While that rebase is in progress, ggui and git-gg operations (other than undo/redo) and the hooks
  of later git commands (`git rebase --continue` in a terminal) join it instead of opening their own.
- It ends when the rebase is gone: ggui's step that finished it writes `end`; the
  `post-rewrite rebase` hook of a terminal command does (adding the final index tree for an
  operation ggui opened). A remembered operation whose rebase is no longer in progress (aborted,
  or finished without the hooks) gets its `end` the next time ggui, git-gg or a hook records
  anything, and before Undo plans.
- Deleting `gg/rebase/` only splits the rebase into several operations.
- When git detaches a symbolic HEAD (a rebase starting), the transaction reports the branch's commit
  as HEAD's old value; the hook records `ref:<branch>` instead (the branch is not in the same
  transaction). Undo skips refs whose recorded old and new values are equal (HEAD back on its
  branch at the end of a rebase).

## 5. Undo and redo

### 5.1 Which operation
Let *W* be the current worktree. The *visible* operations are those that touched a shared ref,
or *W*'s HEAD/index.

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
   `git update-ref --stdin` transaction (old values verified), creating reflog entries;
4. restores *W*'s index to *X*'s recorded `before` index tree when known (`git read-tree`), and
   updates the working tree only when that is lossless (`git read-tree -m -u` between the two
   HEAD trees).

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
