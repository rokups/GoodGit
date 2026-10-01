# ggui per-panel UI specification

Status: **draft for review**. Inputs: `docs/spec/product.md` §4 only. Legend from §4:
**K** keep, **M** keep the entry point with Git semantics, **N** new, **D** dropped (listed only
so implementers know it is intentionally absent).

Screenshots of the pre-rebuild app are **not** attached: the analysed checkouts were not
available to the implementer (clean room). The layout below is taken
from the product spec's written description. Screenshots of the rebuilt app are produced by the test
suite (`ggui --test=screens` writes `test-artifacts/screens/*.png`) and replace them.

Wording rule: HEAD, branch, commit, staged, unstaged, working tree, index, stash, worktree.
Never "change", "@", "alias", "workspace" (the §4 label "Compare with @" is written
"Compare with HEAD").

Stable ImGui identifiers are part of this spec: tests address widgets by these paths, so a
rename is a spec change. Window names are fixed (`"History"`, `"Changes"` …); widgets inside use
`##id` suffixes where the visible label is dynamic.

---

## 1. Application shell

### 1.1 Main window
- Title: `ggui` when no repository is open; `<repo name> — ggui` otherwise.
- Top to bottom: main menu bar, toolbar (window `"Toolbar###Toolbar"`, titled "Toolbar" in the window navigator), (no error banner: see 1.5),
  dock space (`"##DockSpace"`).
- Repository-state badge and prompts appear in the toolbar row.

### 1.2 Welcome screen (K; shown when no repository is open) — window `"Welcome"`
| Element | ID | Behavior |
|---|---|---|
| Title + tagline "A Git client with undo and first-class conflicts" | — | text only |
| Button *Open repository…* | `Open repository...` | native folder picker (NFD); opens the chosen folder |
| Button *Initialize repository…* | `Initialize repository...` | folder picker, `git init`, opens it (Phase 2) |
| Button *Clone repository…* | `Clone repository...` | opens the Clone dialog (Phase 2) |
| Path field + *Open* | `##welcome_path`, `Open` | opens a typed path (keyboard path for tests and users without a picker) |
| Recent list | `##recent` rows `recent_<n>` | click opens; Delete key forgets the focused entry; each row shows path, current branch, upstream and ↑n/↓n |
| Opening progress | `##opening` + `Cancel##open` | spinner, phase text, Cancel |

Auto-open (K): argv[1] if given, otherwise the most recent repository that still exists.

### 1.3 Main menu
**Repository** (K): Open… `Ctrl+O` · Initialize… · Clone… · Recent ▸ (filter field
`##recent_filter`, entries show branch, upstream and ahead/behind) · Open working directory ·
Copy path · Close repository `Ctrl+W` · Refresh `F5` · — · **N** Fetch · Pull · Push · — ·
Settings… · Quit `Ctrl+Q`.

**Commit** (M, renamed from "Change"): New commit `Ctrl+N` · New detached commit · Commit… ·
Amend… · — · the selected-commit actions of §4
(below) · Interactive rebase… (N, Phase 3: asks for the base in the `Interactive rebase onto` dialog,
then opens the todo editor for HEAD, §4.13).

**Edit** (K): Undo `Ctrl+Z` · Redo `Ctrl+Y` · Apply patch… (N options: to index / to working tree).

**View** (K): one checkbox per panel (History, Changes, Change information, Diff, Blame,
Branches, Tags, Worktrees, Remotes, Stashes, Reflog, Operations) · Previous changed file
`Shift+F6` · Next changed file `F6` · Reset layout.

### 1.4 Toolbar — window `"Toolbar###Toolbar"` (K + N)
Left to right, each button has a tooltip with its shortcut:
New `##tb_new` · Commit/Amend `##tb_commit` (label "Commit" when the working tree/index is
selected, "Amend" when HEAD is selected) · Undo `##tb_undo` ·
Redo `##tb_redo` · Refresh `##tb_refresh` · **N** Fetch `##tb_fetch` + `##tb_fetch_menu` ·
Pull `##tb_pull` + `##tb_pull_menu` (badge ↓n) · Push `##tb_push` + `##tb_push_menu` (badge ↑n) ·
**N** Stash `##tb_stash` · Pop `##tb_pop` · repository switcher `##tb_repo` (combo of open and
recent repositories) · folder `##tb_open` (opens the working directory in the file manager) ·
current branch label `##tb_branch` (plain text: branch name or "detached") · HEAD ID `##tb_head`
(plain text, short ID; tooltip with the full ID; right-click: Copy ID) ·
**N** repository-state badge `##tb_state` (MERGING, REBASING, CHERRY-PICKING, REVERTING,
BISECTING) with Continue / Skip / Abort (and *Commit with conflicts* for text-only conflicts) ·
while a `git rebase -i` is stopped (from ggui or plain git, detected from `.git/rebase-merge/`):
*Amend and continue* `##tb_amend_continue` (no native conflicts left: `git commit --amend
--no-edit` when something is staged, then `git rebase --continue`), *Edit remaining todo*
`##tb_edit_todo` (the todo editor on the rest of git's list, §4.x) and *Progress*
`##tb_rebase_progress`, a popup `##rebase_progress`: "Rebasing <branch>: N done, M remaining"
`###rp_title`, the done rows (dimmed; merge rows as "merge -C <short ID> <labels> # <subject>"),
"Stopped at" the current row `###rp_current` with why and
what to do `###rp_reason` (conflicts, edit, break, a failed exec), and the remaining rows
`###rp_next_<n>` · activity spinner `##tb_activity` (tooltip lists the background tasks) +
`Cancel##tb_cancel`. Continue / Skip / Amend and continue that stop again further on (the next
edit, break, failing exec or conflict) are not errors: a notification "Interactive rebase
stopped" shows git's message. While a plain `git rebase -i` waits for its list in the todo editor
(ggui as `sequence.editor`, §4.x) the badge's buttons are disabled (tooltip: "git rebase -i waits
for the list in the todo editor: Save or Cancel it first").

Mutation buttons are disabled (with a tooltip "Not available yet" until their phase, and
"Busy: <operation>" while a conflicting mutation runs).

General rules: items whose click does nothing are plain text (no hover or
click highlight; a context menu may still attach). Full commit IDs show the short prefix in the
text colour and the rest dimmed. Every "Copy ID" copies the short ID, or the full ID while Shift
is held: the menu item reads "… short ID", and "… full ID" while Shift is held (no shortcut hint).
Every item with a right-click context menu also opens it on `Alt+Space` while it has keyboard
focus (the menu opens below the item's bottom-left corner; Space does not also activate it, and
the Alt press does not switch to the menu bar; ignored while text is being typed).

### 1.5 Errors and notifications (K)
- Important errors (failed git commands, open/clone failures, hook failures, journal errors) open
  a modal error popup titled after the action: error icon, the git/hook output verbatim, buttons
  `OK` (dismiss) and `Copy message`.
- Low-importance information and warnings show as notifications in the bottom-right corner
  (windows `##toast_<n>`, newest at the bottom): icon, title, message, close button
  `###toast_close`, right-click `Copy message`. They fade out after 6 s (information) or 12 s
  (warnings), not counting time under the mouse; at most five are shown, and a repeated notice
  replaces the older one.

### 1.6 Settings window `"Settings"` (K/N)
Tabs: **General** (UI scale slider `##scale` 50–300 %, theme combo `##theme` Dark/Light, *Add GoodGit to PATH* `##add_to_path` (N, Linux with systemd: checked iff `$XDG_CONFIG_HOME/environment.d/60-goodgit.conf` names the executable directory; a warning when it names another directory or no PATH entry, with buttons `Point to this GoodGit##path_repoint` and `Remove the file##path_remove`; "Takes effect at next login"; disabled with a tooltip without systemd or off Linux; errors in the `Add GoodGit to PATH` error popup)),
**Git** (N: "When nothing is staged" default: Ask / Stage all tracked / Stage selected; git
configuration with scope tabs `##config_scope` User / Repository / Worktree, one field per option:
`user.name`, `user.email`, `core.editor`, `merge.tool`, `diff.tool`, *Pull method*
`##pull_method` (Merge / Rebase / Rebase, keeping merges / Fast-forward only = `pull.rebase` or
`pull.ff=only`). An unset field shows the inherited lower-scope value as a hint; a field that
overrides a lower scope has *Inherit* `Inherit##<key>` to clear the override. Fields apply on
Enter or when they lose focus; an empty field unsets. The Worktree tab is off until
`extensions.worktreeConfig` is set (*Enable worktree settings*). Each scope tab also has *Use
ggui's todo editor for git rebase -i* `##sequence_editor` (checked when that
scope's `sequence.editor` is ggui's, `git gg sequence-editor`): on sets it; when the scope already
has a `sequence.editor` of the user's own, the dialog `Replace sequence.editor` shows it with
*Replace* (keeps it in `gg.previousSequenceEditor` at the same scope) / *Cancel*; off removes only
ggui's value and puts a kept one back. Below it, dimmed: "git rebase -i here uses: ggui's todo
editor | '<command>' | git's editor (sequence.editor is not set)" and the scope it comes from.),
**Conflicts** (N, Phase 3: "Expand to
index stages on checkout"). **D** max-new-file-size.

### 1.7 One-time prompts on open (N)
- "git not found / too old" (blocking modal `Git required`): shows the found version and the
  minimum 2.36; buttons *Retry*, *Quit*.
- No hooks prompt: managed hooks of older versions are removed silently on open (product spec §4.12 B).

### 1.8 Default dock layout (K + N)
```
+----------------+----------------------------+---------------------------+
| Branches | Tags|                            | Changes                   |
|                |          History           +---------------------------+
+----------------+                            | Change information        |
| Worktrees |    |                            +---------------------------+
| Remotes |      |                            | Diff | Blame | Reflog |   |
| Stashes        |                            | Operations                |
+----------------+----------------------------+---------------------------+
```
Left column ≈ 18 %, right column ≈ 34 %; Changes/Change information ≈ 45 % of the right column.

---

## 2. History panel — window `"History"`
- Header row: filter field `##hist_filter` (message, ID, branch, tag), toggle *Conflicted only*
  `##hist_conflicted` (N), toggle *Stashes* `##hist_stashes` (N), *Show more* when truncated.
- Table `##hist_table`: columns Graph, Description (ID prefix, badges, subject), Author, Date.
  While a filter is active (text or *Conflicted only*) the graph column is hidden
  (`##hist_table_filtered`, three columns). The graph starts slightly inside its column so the
  current commit's outline is not clipped. Changes to the rows (refresh, expanding or
  collapsing merges, loading more, the Index row appearing) keep the rows in view in place.
  Row, graph and badge tooltips are held back while the list scrolls.
  Row IDs: `row_wt` (Working tree), `row_index` (Index, N, only when something is staged),
  `row_<full commit id>` for commits.
- Badges: local branch (outlined when checked out), remote-tracking branch, tag, worktree HEAD,
  stash (N). Published commits (reachable from a remote-tracking ref) use the normal text colour;
  unpublished commits are highlighted. Conflicted commits: conflict colour and ⚠ icon.
- Keys: ↑/↓ select, `N` new, `Alt+N` new detached, `E` check out, `D`/`Shift+D` duplicate
  commit/branch, `S`/`Shift+S`/`Alt+S` squash/with descendants/split, `A`/`Shift+A` drop/drop
  branch, `I` interactive rebase (N), `F7`/`Shift+F7` next/previous conflicted commit (N).
- Row context menu: New · New detached · Check out ▸ (branches at the commit, "Detached HEAD") ·
  Create branch… · Move branch ▸ · Delete branch ▸ · Push · Push to… · Copy ▸ (ID, Full
  description) · shared commit actions (§4) · *Interactive rebase selection…* (Phase 3, when
  several commits are selected with Ctrl-click: the list starts at the oldest selected commit and
  the selected commits start selected in the editor).
- Working tree context menu: Commit… · Amend into HEAD… · Discard changes… · N: Stash changes… ·
  Stage all · Unstage all.
- Drag and drop (Phase 3): commit→commit (Move before/after, Squash, Rebase; Shift = move
  before, Ctrl = squash, Alt = rebase, none = chooser popup `##drop_chooser`), branch
  badge→commit (move branch), files from Changes→commit (move changes).
- Reveal: loads more history until the commit is found; progress in the activity area,
  cancellable.
- Scope: branch/tag/remote visibility from the side panels.

### 2.x Drag and drop (Phase 3)
- **Commit row → commit row.** The modifier held at the drop picks the action: Shift = Move after,
  Ctrl+Shift = Move before, Ctrl = Squash into (messages combined), Alt = Rebase onto (with
  descendants). Without a modifier a chooser pops up: Move before / Move after / Copy after /
  Squash into / Rebase onto.
- **Branch badge → commit row:** moves the branch there (the drag starts on the badge).
- **Files from Changes → commit row:** a commit's files go to its parent, its child or the
  checked-out commit (other targets are refused with a notification); working tree files are
  folded into the target commit (descendants rebased, the working tree kept as it is).

## 3. Changes panel — window `"Changes"`
- Header `###changes_title`: title of the selection ("0000000 Working tree" — the zero ID —,
  "Index", short ID and commit subject, stash message), filter `##changes_filter`, toggle
  *Compare with HEAD* `##compare_head` (enabled for commits only).
- For a commit: flat list `##files` of rows `file_<path>` with status icon (A, M, D, R, C, T, U),
  renames shown `old → new`; files holding first-class conflicts are in the conflict colour with
  "N-sided conflict", and double-clicking one at HEAD opens the editor.
- For Working tree / Index (N): groups `Staged`, `Unstaged`, `Untracked`, `Conflicted`, each a
  collapsible header with a count and group buttons (Stage all / Unstage all). Space/Enter
  toggles staging of the selection; drag rows between Staged and Unstaged.
- Double-click opens a file: new files in the editor, other files in the diff tool against the
  parent (HEAD for the working tree and index).
- Multi-select: Ctrl-click, Shift-click, Ctrl+A. ↑/↓ navigation.
- File context menu: Open working-copy file · Open containing folder · Copy ▸ (Name, Relative
  path, Absolute path) · Stage/Unstage/Discard (N) · Intent to add (N) · Resolve with merge
  tool · Mark resolved · Patch ▸ (Copy, Save…) ·
  Blame file · External diff ▸ (vs HEAD, vs parent) · Move to child (Move to working tree on the HEAD commit, which has no child) /
  Move to parent · Revert (commit files: index and working tree; Shift: Revert and commit, a new commit on HEAD) · Discard (D; commit files: rewrites the commit, one Undo, published commits ask first) · Delete file.

## 4. Commit actions (Commit menu and History context menu)
New commit, Edit/Check out, Edit commit (E; a toolbar banner *Editing \<id\> of \<branch\>* with Return / Stop editing, and Amend restacks the descendants atomically), Commit…, Amend…, Describe (Save message), Edit author, Duplicate
commit/branch, Rebase…, Interactive rebase…, Squash…/with descendants, Split…, Restore…,
Abandon/Abandon branch, Simplify parents, Reorder, Move
files/hunks/lines, Merge into HEAD, Rebase HEAD onto branch / Reconcile with remote. Meaning:
plan §4.3 table. *Interactive rebase from here…* (key `I`) opens the todo editor (§4.x) for the
commit and its descendants up to HEAD, or up to the first local branch (by name) that contains it
when HEAD does not; a commit on neither is refused. The *Squash* and *Rebase onto* dialogs have
*Open as interactive rebase…*: the editor opens with the action as its starting todo (the commit
moved after the target as squash or fixup; the new base as *Onto*).
*Merge into HEAD…* and *Rebase HEAD onto this* in the shared commit actions act on the selected commit
(disabled on HEAD itself; Rebase needs an attached HEAD); a commit is merged with Git's default message
"Merge commit '<short ID>'". Branches offers the same two for a branch (§8).

## 4.x Interactive rebase todo editor — window `"Interactive rebase"` (N, Phase 3)
Dockable; opens as a tab next to History while a todo is open and closes on Start (after success)
or Cancel; its close button cancels. The range is read on a worker; everything else is edited in
the panel without touching the repository.
- **Header** (acts like a modal dialog's head): title `###ir_title` ("Rebase N commit(s) [and M
  merge(s)] of <branch> onto <short ID | the root>"), *Start* `###ir_start` (disabled with the reason as
  tooltip: errors in the list, busy, or an engine not available yet), *Cancel* `###ir_cancel`;
  engine line `###ir_engine` ("Engine: in memory" or "git rebase", with the reason); one line per
  validation issue `###ir_issue_<n>` (error or warning icon, "Row N: …").
- **Options:** *Onto* `###ir_onto` (Enter applies; empty = the upstream; an unknown revision is an
  error and the list stays), *Autosquash* `###ir_autosquash` (on: `fixup!`/`squash!`/`amend!`
  rows are placed and marked; off: back to Git's starting list), *Update refs* `###ir_update_refs`
  (default on; off removes the `update-ref` rows, on puts them back after their commit's (or
  merge's) squash/fixup rows), *Rebase merges* `###ir_rebase_merges` (off by
  default: on replaces the list with Git's `--rebase-merges` starting list, with the Update refs
  and Autosquash options applied, and off with the straight list; the edits so far go, Undo brings
  them back), *Autostash* `###ir_autostash`, *Run as git rebase* `###ir_native`, *Exec
  after every commit* `###ir_exec_each`, *Committer date* `###ir_committer_date` (Use now / Keep
  original; disabled at "Use now" when the engine is git rebase, which always sets the committer
  date to now), *Becoming empty* `###ir_empty` (Keep / Drop / Ask, default Ask: what happens to
  commits whose changes are already in the new base, like `git rebase --empty=keep|drop|stop`).
- **Tools:** Undo `###ir_undo` (Ctrl+Z), Redo `###ir_redo` (Ctrl+Y, Ctrl+Shift+Z), *Insert exec*
  `###ir_insert_exec`, *Insert break* `###ir_insert_break` (after the last selected row, else at the
  end); with Rebase merges on (or label/reset/merge rows in the list) also *Insert label*
  `###ir_insert_label` (a name no row uses yet: "label", "label-2", …), *Insert reset*
  `###ir_insert_reset` ("onto") and *Insert merge* `###ir_insert_merge` (the nearest label above);
  *Newest first* `###ir_newest_first`. Undo/Redo apply to the list and its options only; the
  repository's Undo is untouched while the editor has focus.
- **List** `##ir_table` (oldest first, Git's order; columns Action, ID, Subject, Author, Date).
  Commit rows `###ir_<full id>` with the action combo `###ir_action_<full id>` (pick, reword,
  edit, squash, fixup, fixup -C, fixup -c, drop) and branch badges `###ir_badge_<branch>`; other
  rows `###ir_row_<index>`: `exec` with its command field `###ir_exec_<index>`, `break`,
  `update-ref` with a badge `###ir_ref_<branch>`. Dropped rows are dimmed; rows with an issue show
  its icon (tooltip: the message).
- **--rebase-merges rows**: `label` with its name field `###ir_label_<index>`;
  `reset` with its target field `###ir_reset_<index>` and, dimmed, where it goes ("(the new base)"
  for `onto`, "(a new root commit)" for `[new root]`, else the target's subject); `merge` rows made
  from a merge `###ir_merge_<full id>` with the ID, the merge's author and date, branch badges, an
  action combo `###ir_action_merge_<full id>` (*merge -C*: recreate it with its message and author,
  reused as it is when its parents stay; *merge -c*: the same with the message edited; *merge*: a
  new merge with Git's message "Merge branch '<labels>'"), a field `###ir_merge_<index>` for the
  labels (or revisions) it merges, separated by spaces, and its subject (for *merge*, dimmed, the
  message Git will use). Merge rows typed as `merge <labels>` are `###ir_row_<index>` without a
  combo. Validation: a label needs a name Git accepts as `refs/rewritten/<name>`; a reset or merge
  needs a name that an earlier `label` row defines, a label of the stopped rebase, or a revision the
  list was read with (Git's abbreviated ids for commits outside the range); a name defined only
  further down is an error (a warning when it is also such a revision: Git then takes the revision);
  a merge needs at least one label.
- **Selection:** click, Ctrl-click (toggle), Shift-click (range from the last clicked row).
- **Keys** (list focused, not typing): `p r e s f d` set the action of the selected commit rows;
  `x` / `b` insert exec / break; `l` / `t` / `m` insert label / reset / merge (Git's letters, as the
  Insert buttons); `Delete` removes selected exec, break, update-ref, label, reset and merge rows;
  `Alt+↑`/`Alt+↓` move the selected rows one place as displayed. Rows can also be dragged: dropped
  below the dragged rows they go after the target, above it before it; a selection moves together.
- **Messages:** a reword row, and the first row of a group with a squash or `fixup -c`, get an
  inline editor `###ir_msg_<full id>` below the row, prefilled with Git's text (the commit message,
  or the commented "This is a combination of N commits." template). A typed message is cleaned
  like Git's editor output (comments and extra blank lines removed) and goes back to Git's text
  when the group's rows or actions change. A `merge -c` row gets the same editor for the merge's
  message (kept while the row stays `merge -c`).
- **Start:** the in-memory engine (plan §4.13 R3) through the rewrite pipeline: pre-flight for
  non-text conflicts, then (with *Becoming empty* = Ask and commits that become empty) the dialog
  "Commits become empty" listing them (`empty_<n>`: short ID and subject) with *Keep them* / *Drop
  them* / *Cancel*, the published-history confirmation, one operation (one Undo). Refused (error
  popup, the editor stays) when the branch moved since the list was read. With *Autostash* tracked
  local changes are stashed before and popped after, in the same operation; when they no longer
  apply they stay in the stash (warning notification). Todos needing `git rebase -i` (edit, break,
  exec, label/reset/merge, exec after every commit, Run as git rebase) start the native engine
  (a list with label, reset or merge rows adds `--rebase-merges`; the engine line says "row N is
  label: --rebase-merges lists are replayed by git rebase"): `git rebase -i` with
  the list (exec after every commit added after each commit row) and the typed messages handed over
  by `git gg sequence-editor`, *Autostash* as `--autostash`, *Becoming empty* as `--empty`. The
  editor closes; a stop shows the notification "Interactive rebase stopped" (git's message) and the
  toolbar's stop handling (§1.4). Refused (error popup, the editor stays) when the branch moved, when
  git refuses (local changes without Autostash), or when the list has update-ref rows and git is
  older than 2.38.
- **For a plain `git rebase -i`** (`sequence.editor = git gg sequence-editor`,
  §1.6): git runs `git gg sequence-editor <git dir>/rebase-merge/git-rebase-todo` and waits. A ggui
  with that repository (worktree) open shows git's list here and comes to the front; with none,
  git gg starts `ggui <worktree>` (`GG_GGUI` names the program; default the `ggui` next to
  `git-gg`) and waits for it to open the repository; without a display (no `DISPLAY` /
  `WAYLAND_DISPLAY` on Linux) or without a ggui program git's own editor (`git var GIT_EDITOR`)
  edits the list, with a note on stderr. The editor: title "git rebase -i: Rebase N commit(s) …",
  engine line "git rebase (git rebase -i is waiting for this list)", the line "git opens its own
  editor for reword, squash and merge -c messages." `###ir_git_note` (no inline message editors),
  no options row (git's options are the command line's), merges mode when git's list has
  label/reset/merge rows (`--rebase-merges`), the preview on the fresh list (onto = git's `onto`).
  *Save* (`###ir_start`) hands the list to git, which goes on with it; *Cancel* and closing the
  panel hand back an empty list, so git stops with "nothing to do" and nothing changes (git's own
  "empty todo aborts"). `git rebase --edit-todo` from a terminal opens the remaining list the same
  way (title "Remaining todo of the rebase of …", engine "git rebase --edit-todo is waiting for
  this list"); there Cancel keeps git's list as it was. When another todo is open in the editor,
  git waits and a notification "git rebase -i is waiting" says so; the list shows when that todo
  is started or cancelled; while git's list is shown, opening another todo is refused with the
  warning "Todo editor in use". When git stops waiting (interrupted in its terminal) the panel closes
  with the warning "git rebase -i stopped waiting". Saving needs no free mutation queue.
- **Edit remaining todo** (toolbar, while a `git rebase -i` is stopped): the same editor on the
  rest of git's list (`git-rebase-todo`, ids resolved), with the title "Remaining todo of the rebase
  of <branch>: N commit(s) onto HEAD <short ID>", the engine line "git rebase (the rest of the
  rebase in progress)", no options row, and *Save* (`###ir_start`) instead of Start. Rows, keys,
  messages, validation (a squash/fixup may come first: it folds into HEAD) and the preview (onto
  HEAD) work as above. Save writes the list through `git rebase --edit-todo` (so git writes
  `git-rebase-todo` exactly as it would for its own editor) and keeps the typed messages for the
  later Continue steps; refused ("The rebase moved on…") when git's list changed since it was read.
  Errors are titled `Save the remaining todo`.
- **Live preview** `##ir_preview` (right of the list, a bordered child; plan §4.13): "Result", and
  while a newer result is computed a spinner with "Updating..." (the previous result stays). A
  summary line `###irp_summary` ("N commit(s), K with conflicts, R resolve conflicts, M need a decision, E empty"; R = commits whose
  original commits had first-class conflicts that are gone, the tooltip lists the files),
  `###irp_moves` ("Moves: <branches>", a detached HEAD as `HEAD`), `###irp_staying` (warning
  colour: "Stay on the old commits: <branches>", branches in the range without an update-ref row),
  `###irp_aside_<n>` ("<branch>: <short ID> <subject>, before the squash": an update-ref row before
  squash/fixup rows leaves the branch on the commit as it was then, beside the result that amends
  it, as `git rebase -i` does) and `###irp_dropped_empty` ("Dropped, became empty: <subjects>",
  with *Becoming empty* = Drop).
  The graph `##irp_table` is one lane for a straight list, newest first, the base last (dimmed:
  short ID and subject, or "(the root)"); a --rebase-merges list gets a lane per branch, merges drawn
  as bubbles with an edge to each parent in the result (parents outside it have no edge); rows `###irp_row_<k>` (k = result commit, oldest = 0) show the conflict icon
  and conflict colour for first-class conflicts, a help icon (warning colour) for non-text
  conflicts Start will ask about (pre-flight), "(empty)" for commits that are or become empty,
  badges `###irp_badge_<branch>` for branches (and `HEAD`) ending there, and the subject (dimmed
  when the commit is unchanged and keeps its ID). HEAD's future commit gets History's outline. The
  row tooltip names the source commits and lists conflicted files, decisions and why it is empty;
  clicking a row selects its rows in the list. With errors in the list there is no result ("Fix
  the errors in the list to see the result."); an engine failure shows "Cannot compute the
  result: …"; a preview cancelled from the toolbar says so. Todos for `git rebase -i` are
  previewed as if every stop continued at once (edit = pick, exec/break change nothing), and
  label/reset/merge rows as `git rebase -i --rebase-merges` runs them: a merge whose heads are
  already in HEAD's history makes no commit, `merge -C` keeps the merge's message and author (and
  the merge itself when its parents stay), `merge -c` takes the typed message, a plain `merge`
  Git's "Merge branch '…'" (or the text after `#` on its row); a merge that conflicts shows
  first-class conflicts (git stops there). A list the in-memory replay cannot model (a
  squash/fixup right after a reset or merge row, a merge onto a new root) shows "No preview: …"
  `###irp_unsupported`, not an error; Start still runs it.
  Computed on the preview worker by the same plan and in-memory engine as Start, never applied:
  nothing is written to the repository, the newest edit cancels older computations.
- Errors are titled `Open interactive rebase` / `Start interactive rebase` (never the panel's
  name).

## 5. Change information panel — window `"Change information"`
- Message editor `##message` with *Save message* `##save_message` (HEAD only until Phase 3).
- Author line (plain text) with menu Copy name / Copy email / Edit author… (Phase 3); Committer line when it
  differs (N); Date; published/lock state ("Published" / "Not published").
- Commit ID (full, dimmed after the short prefix) with Copy `##commit_id`; Parents list
  `parent_<n>` (click reveals).
- N: conflicted files list with side counts. N: "Amend" mode for HEAD with a clean index.
- D: aliases list.

## 6. Diff panel — window `"Diff"`
- Toolbar: view `##diff_view` (Unified / Side by side), whitespace `##diff_ws` (Normal / Ignore
  changes / Ignore all), context lines `##diff_context` with `-`/`+`, hunk navigation
  `##diff_prev_hunk` / `##diff_next_hunk` (icon buttons "Previous hunk (Alt+Up)" / "Next hunk
  (Alt+Down)", also bound to `Alt+Up` / `Alt+Down` while the Diff window is focused and no text
  field is active; they scroll the previous / next hunk to the top of the view, in unified
  (the hunk header row) and side by side (the hunk's first code line, both sides together),
  relative to the hunk last navigated to while it is still on screen (the last hunks cannot
  reach the top) or else the first visible line; they stop at the first / last hunk, where the
  button is disabled), *Compare with HEAD* `##diff_vs_head`
  (this file of a commit or stash; on the same row), for stashes a part selector `##stash_part` (Working tree / Index /
  Untracked).
- Body: hunks with headers; per hunk buttons (N) `Stage hunk`, `Discard hunk`, `Unstage hunk`; in a commit's diff `Discard hunk` (rewrites the commit without that hunk).
  A hunk row shows only the hunk's function context (the text after the closing `@@`, dimmed;
  empty when there is none) and no `@@ -a,b +c,d @@` range: the editor text never contains range
  text, so it cannot be selected or copied. Its gutter is blank apart from the buttons.
  Side by side shows only code: no hunk rows at all (gap placeholders mark omitted lines).
  Line selection with click/Shift-click; Ctrl+C and the menu's Copy copy only code lines: hunk
  rows, the "... N unchanged lines" placeholders and side-by-side fillers are left out of the
  copied text (both views).
- Expandable context rows `expand_<n>` (click: 10 lines; Shift+click: whole gap).
- Placeholders: binary, image (dimensions), submodule (old → new commit), mode change line.
- Capped large files: "Load full diff" `##load_full`.
- Context menu: Copy · Blame file · Stage/Discard/Unstage line(s) and hunk(s) (N) · Move line(s)/hunk to
  child (or working tree on the HEAD commit, which has no child) / parent / active commit (Phase 3) · Revert line(s) (a commit's lines, into the index and working tree; Shift: Revert line(s) and commit) · Discard line(s)/hunk(s) of a commit (rewrite).

## 7. Blame panel — window `"Blame"`
Filter `##blame_filter`, Back `##blame_back` / Forward `##blame_fwd` (also mouse buttons 4/5),
table of lines (commit prefix, author, date, text); uncommitted lines marked "Not committed";
tooltip per line (full commit summary); context menu: Blame before this change · Show
originating source · Reveal commit · Copy commit ID · Select change block · Copy change block.

## 8. Side panels
- **Branches** `"Branches"`: filter, Create branch… `##create_branch`, rows `branch_<name>`
  (click toggles visibility in History, Ctrl-click = only this), current outlined. Context:
  Reveal · Copy name · Check out · Merge into HEAD · Rebase HEAD onto branch · Push · Push to… ·
  Reconcile with remote/branch… · Rename… · Delete ▸ (Local / on <remote> / Local and all
  remotes) · N: Set upstream… · Unset upstream · Fast-forward to upstream · Pull (current branch) ·
  Interactive rebase onto… (Phase 3: the `Interactive rebase onto` dialog asks for the base; the
  branch is the tip and only it and its update-ref branches move, HEAD stays) · Check out in new
  worktree… (Phase 4: the `Add worktree` dialog with *An existing branch* and this branch; disabled
  for a branch checked out in any worktree). Remote-tracking branches are listed under their remote;
  the remote has the Remotes panel's context menu, and each remote-tracking branch has Reveal ·
  Copy name · Remote <name> ▸ (the same menu). Set upstream… has a filter field (Enter picks
  the first match).
- **Tags** `"Tags"`: filter, Create tag… (N annotated with message), rows `tag_<name>` (the
  name only; the tooltip of an annotated tag shows its message)
  (visibility toggle). Context: Reveal · Copy name · Delete · N: Push tag · Delete on remote.
- **Worktrees** `"Worktrees"` (M): header Add worktree… `###add_worktree` (disabled while HEAD
  is unborn); rows `worktree_<name>` (git's id, the directory name) with "(main)", "(bare)", a lock
  icon, "(missing)" (directory gone; dimmed), "(prunable)" (git worktree prune would remove it:
  missing and not locked), then the branch or the detached commit. The current worktree is
  selected. Tooltip: path, "Shown in this window", lock reason, what Prune/Repair do for a missing
  one. Context: Copy name · Copy path · Reveal HEAD · Open directory (not for missing) · Open here
  (switches this window; not for the current or a missing one) · Open in new window (starts
  another ggui process on the worktree, detached; `GG_GGUI` names the program, default this ggui;
  not for a missing one) · Add… · Remove… (not for the main worktree or the one this window shows)
  · N: Lock… / Unlock (not for the main worktree) · Prune… · Repair…. D: gg rename/forget.
  Every change runs `git worktree add|remove|lock|unlock|prune|repair`. Add, Remove, Lock and
  Unlock are undoable from the worktree whose window made them (undo-journal.md §5.4); Prune and
  Repair are listed in Operations but cannot be undone.
  - `Add worktree`: *Path* `##path` (prefilled `<main worktree>-worktree` next to the main
    worktree; relative paths start at this worktree) · *Check out* `##checkout`: A new branch /
    An existing branch / A commit (detached HEAD) · *New branch* `##branch` (new branch) · *Branch*
    `##existing` (existing branch; filterable, Enter picks the first match) · *Start at* `##start`
    (new branch and detached, default HEAD) · *Force …* `##force` (`--force`: a branch checked out
    elsewhere) · *Do not check out the files* `##no_checkout` (`--no-checkout`) · *Lock the new
    worktree* `##lock` (`--lock`) · *Lock reason* `##reason` (with Lock; `--reason`) · note on Undo ·
    *Add* / *Cancel*.
  - `Remove worktree`: what is removed (a missing one: only git's records), the lock ("removing
    unlocks it first"), what Undo brings back · *Remove* / *Cancel*. When git refuses because of
    uncommitted changes or untracked files: `Remove worktree with changes` (git's message; the
    changes are deleted for good, Undo re-creates the worktree without them) · *Delete changes and
    remove* (`--force`) / *Cancel*.
  - `Lock worktree`: *Reason* `##reason` (optional) · *Lock* / *Cancel*. Unlock has no dialog.
  - `Prune worktrees`: git's `prune --dry-run --verbose` lines, "cannot be undone" · *Prune* /
    *Cancel*. With nothing to prune a notification says so instead; after pruning a notification
    shows git's lines.
  - `Repair worktree`: *Location* `##path` (the worktree's path; the new one if it was moved by
    hand) · *Repair* / *Cancel*; a notification shows git's repair lines.
- **Remotes** `"Remotes"`: rows `remote_<name>` with URL. Context: Copy name · Fetch · Pull ·
  Delete · N: Edit URL… · Prune on fetch (checkbox) · Fetch all. Header: Add remote…
- **Stashes** `"Stashes"` (N): rows `stash_<n>` (index, message, base, date). Context: Apply ·
  Pop · Apply (restore index) · Drop… · Branch from stash… · Header: Stash changes… · Clear all….
- **Reflog** `"Reflog"`: chooser `##reflog_ref` (HEAD, branches, stash), filter, rows with old →
  new, message. Context: Copy old/new ID · Reveal old/new · Create branch from old/new….
- **Operations** `"Operations"` (M): rows `op_<id>` (time, source label, description). Context:
  Restore (undo back to before this operation). Plain git commands appear as `git <command>` rows (source `git`), journaled by the reconciler
  (undo-journal §4); there is no footer note.

## 9. Dialogs
Each dialog is a modal popup with the given name and OK/Cancel buttons `OK##<dialog>` and
`Cancel##<dialog>`:
`Commit`, `Amend`, `Create branch`, `Rename branch`, `Delete branch`, `Move branch`,
`Create tag`, `Add remote`, `Edit remote URL`, `Clone repository`, `Initialize repository`,
`Push to`, `Force push`, `Stash changes`, `Drop stash`, `Branch from stash`, `Discard changes`,
`Apply patch`, `Save patch`, `Stash and switch`, `Stash and pull`, `Push refused`,
`Credentials` (askpass), `Rewrite published history`, `Non-text conflicts` (pre-flight, Phase
3), `Interactive rebase onto` (Phase 3: field `##base`, buttons *Open* / *Cancel*; the todo editor
itself is the dockable window `Interactive rebase`, §4.x), `Replace sequence.editor` (Phase 4:
*Replace* / *Cancel*, §1.6), `Add worktree`, `Remove worktree`, `Remove worktree with changes`,
`Lock worktree`, `Prune worktrees`, `Repair worktree` (Phase 4, §8 Worktrees), `Settings`.

## 10. States
- **Busy:** conflicting actions disabled with reason tooltip; browsing stays enabled.
- **Scanning:** Changes header shows "scanning…" while partial status results arrive.
- **Loading history:** History footer shows "Loading…" plus Cancel.
- **Detached HEAD:** branch label "detached", Pull disabled with reason.
- **Unborn HEAD:** History shows only the Working tree row; branch label shows the unborn branch.
- **Bare repository:** no Working tree row; Changes shows commit files only.
