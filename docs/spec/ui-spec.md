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

Tooltips wrap at about 40 font sizes and are cut with "…" after 10 lines.

A tooltip never shows over a list or an editor gutter while it scrolls (wheel, scrollbar, keyboard or
code): a tooltip that is showing when the scrolling starts goes away, and tooltips come back once
the scroll position has been still for 0.3 s. This holds for every scrolling window, also when
an enclosing window is the one that scrolls.

The tooltip of a row in a list or table, or of an editor gutter line, whose text is put together
from several parts is built once when it appears and reused for as long as it stays shown; it is
built again when the row's data changes. (Tooltips of buttons and menu items are built on each
frame.)

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
| Recent list | `##recent` rows `recent_<n>` | click or Enter opens; Delete key forgets the focused entry (the keyboard cursor is the nav cursor; Alt+Space: Forget); each row shows path, current branch, upstream and ↑n/↓n |
| Opening progress | `##opening` + `Cancel##open` | spinner, phase text, Cancel |

Auto-open (K): argv[1] if given, otherwise the most recent repository that still exists.

### 1.3 Main menu
**Repository** (K): Open… `Ctrl+O` · Initialize… · Clone… · Recent ▸ (filter field
`##recent_filter`, entries show branch, upstream and ahead/behind; Delete forgets the hovered or
focused entry other than the current repository) · Open working directory ·
Copy path · Close repository `Ctrl+W` · Refresh `F5` · — · **N** Fetch · Pull · Push · — ·
Settings… · Quit `Ctrl+Q`.

**Commit** (M, renamed from "Change"): New commit `Ctrl+N` · New detached commit · Commit… ·
— · the selected-commit actions of §4
(below) · Interactive rebase… (N, Phase 3: asks for the base in the `Interactive rebase onto` dialog,
then opens the todo editor for HEAD, §4.13).

**Edit** (K): Undo `Ctrl+Z` · Redo `Ctrl+Y` · Apply patch… (N options: to index / to working tree).

**View** (K): one checkbox per panel (History, Changes, Change information, Diff, Blame,
Branches, Tags, Worktrees, Remotes, Stashes, Reflog, Operations) · Previous changed file
`Shift+F6` · Next changed file `F6` · Reset layout.

### 1.4 Toolbar — window `"Toolbar###Toolbar"` (K + N)
Left to right, each button has a tooltip with its shortcut:
New `##tb_new` · Commit `##tb_commit` (always "Commit", whatever is selected; the dialog's
*Amend* checkbox amends HEAD) · Undo `##tb_undo` ·
Redo `##tb_redo` · Refresh `##tb_refresh` · **N** Fetch `##tb_fetch` + `##tb_fetch_menu` ·
Pull `##tb_pull` + `##tb_pull_menu` (badge ↓n) · Push `##tb_push` + `##tb_push_menu` (badge ↑n) ·
**N** Stash `##tb_stash` · Pop `##tb_pop` · repository switcher `##tb_repo` (combo of open and
recent repositories, as wide as the name it shows (12 to 28 font sizes; a longer name loses its start to an ellipsis, so its base name stays, and a tooltip on the closed combo gives the whole name; the list is as wide as its longest row); Enter opens it, Down walks the entries, Enter switches, Delete forgets the focused entry other than the current one) · folder `##tb_open` (opens the working directory in the file manager) ·
current branch label `##tb_branch` (plain text: branch name or "detached") · HEAD ID `##tb_head`
(short ID, clickable text; right-click: the Copy ID item) ·
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

General rules: items whose click does nothing are plain text (no hover or click highlight; a context
menu may still attach). Commit IDs follow one rule wherever one is drawn on its own: in rows, in
tooltips and as standalone text; IDs inside sentences, window titles and dialog previews are plain
7-character text. A short ID is always the first 7 characters and shows the first 3 in the text
colour and the other 4 dimmed; a full ID shows its first 7 characters in the text colour and the
rest dimmed. The context menu of a commit ID offers one "Copy" item that states what it copies; it
depends on where the right click landed and on Shift (held as the menu opens or while it is open). A
full ID: its 7-character prefix gives "Copy a1b2c3d", the rest "Copy full ID". A short ID: with
Shift "Copy full ID"; without it the 3-character prefix gives "Copy a1b" and the rest "Copy
a1b2c3d". A right click on a row outside its ID text counts as the rest. A menu opened with
`Alt+Space` has no click: it gives "Copy a1b2c3d" for a short and for a full ID, and "Copy full ID"
with Shift, never the 3 characters. The item's ID is `###copy_id`. This applies to every ID menu
(the toolbar HEAD, the History row's Copy submenu, Blame's line menus, the reflog, the Stashes
rows, Change information's commit ID and parents, the Changes title's commit ID). ID text on its own
(the toolbar HEAD, the Change information commit ID and the Changes title's commit ID) is the exception to "plain text does nothing on click": clicking its
highlighted part copies that part (3 characters of a short ID, 7 of a full ID) and clicking the
dimmed rest copies the full ID (hand cursor, no highlight); Space/Enter on the focused ID copies
the full ID. IDs inside rows and tooltips are not clickable.
Every item with a right-click context menu also opens it on `Alt+Space` while it has keyboard
focus (the menu opens below the item's bottom-left corner; Space does not also activate it, and
the Alt press does not switch to the menu bar; ignored while text is being typed).
Tapping `Alt` on its own (pressed and released with no other key in between) puts the keyboard
cursor on the main menu bar's first menu: Left/Right walk the menus, Down or Enter opens one;
Escape (or `Alt` again) leaves the bar and returns focus to the window that had it. As a trade-off
`Alt` no longer moves to a docked panel's tab bar (`Ctrl+Tab` still switches windows).
Controls above a list (buttons, filter fields, combos) stay visible: only the list scrolls, never
the whole window (keyboard navigation still crosses between the controls and the list).

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
Tabs: **General** (UI scale slider `##scale` 50–300 %, theme combo `##theme` Dark/Light, *History badge prefix* `##badge_prefix` and *History badge suffix* `##badge_suffix` (number fields, 1–100, default 12, stored in settings.json: the characters kept at the start and at the end of an elided badge in History), *Add GoodGit to PATH* `##add_to_path` (N, Linux with systemd: checked iff `$XDG_CONFIG_HOME/environment.d/60-goodgit.conf` names the executable directory; a warning when it names another directory or no PATH entry, with buttons `Point to this GoodGit##path_repoint` and `Remove the file##path_remove`; "Takes effect at next login"; disabled with a tooltip without systemd or off Linux; errors in the `Add GoodGit to PATH` error popup), *Add "Open in GoodGit" to file manager menus* `##context_menu` (N, Linux: checked iff the Dolphin, Nemo and Nautilus files below `$XDG_DATA_HOME` exist with the content for this executable (the Dolphin file and the Nautilus script executable); a warning when they exist but point to another GoodGit or are incomplete, with buttons `Update##context_menu_update` and `Remove##context_menu_remove`; disabled with a tooltip off Linux; errors in the `File manager context menu` error popup)),
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
|                |                            | Change information        |
+----------------+                            +---------------------------+
| Repositories   |                            | Diff | Blame | Reflog |   |
+----------------+                            | Operations                |
| Remotes |      |                            |                           |
| Stashes |      |                            |                           |
| Worktrees      |                            |                           |
+----------------+----------------------------+---------------------------+
```
Left column ≈ 18 %, right column ≈ 34 %; Changes/Change information ≈ 45 % of the right column;
Remotes/Stashes/Worktrees ≈ 15 % of the left column, at its bottom; Repositories ≈ 30 % of the
part above it, between Branches/Tags and Remotes/Stashes/Worktrees. The layout is built when
imgui.ini has no docking data and on *View ▸ Reset layout*; a saved layout is left as it is, so
there a panel that is new (Repositories) shows floating until a reset or a manual dock.

---

## 2. History panel — window `"History"`
- Header row: filter field `##hist_filter` (hint "Filter: message, ID, author, branch, tag"), button *HEAD*
  `##hist_head` (icon, tooltip "Go to the HEAD commit"; reveals and selects the HEAD commit as the
  *Reveal* items do, with their notice when the scope has no such commit; when the commit is already loaded and
  the text filter hides it, the text filter is cleared; a commit that must load first, or that *Conflicted only*
  hides, is selected with the filter kept, so its row may stay hidden; disabled when HEAD has no commit),
  toggle *Conflicted only*
  `##hist_conflicted` (N; a view setting like *Stashes*, stored in imgui.ini as
  `[GGUIView][History] ConflictedOnly`, so it stays on across repositories and restarts; in the next
  repository the graph is hidden at once and the conflicted commits appear as the scan finds them),
  toggle *Stashes* `##hist_stashes` (N), *Show more* when truncated.
- Table `##hist_table`: columns Graph, Description (ID prefix, badges, subject), Author, Date.
  While a filter is active (text or *Conflicted only*) the graph column is hidden
  (`##hist_table_filtered`, three columns). The graph starts slightly inside its column so the
  current commit's outline is not clipped. Changes to the rows (refresh, expanding or
  collapsing merges, loading more, the Index row appearing) keep the rows in view in place.
  Row IDs: `row_wt` (Working tree), `row_index` (Index, N, only when something is staged),
  `row_<full commit id>` for commits.
- Badges: local branch (outlined when checked out), remote-tracking branch, tag, worktree HEAD,
  stash (N), keep (a commit a ggui or git-gg operation made on a detached HEAD that no branch or tag reaches: a pin icon and the short ID, in the HEAD
  badge's colour, `###badge_<full commit id>`; hidden like a branch, by the ref `refs/gg/keep/<full
  commit id>`; not drawn on the detached HEAD's own row, which has the HEAD badge). A badge's name longer than prefix + suffix + 1 characters (the General settings,
  default 12 + 12) is shown as its first prefix characters, "…" and its last suffix characters, for
  every kind of badge but keep; the badge's ID (`###badge_<name>`) keeps the full name. Published commits (reachable from a remote-tracking ref) use the normal text colour;
  unpublished commits are highlighted. Conflicted commits: conflict colour and ⚠ icon.
- Selection: a click selects one commit; the row is the anchor. Ctrl-click adds a commit or removes it (it
  selects the commit as the primary one when nothing, the Working tree row or the Index row is selected).
  Shift-click selects the commits from the anchor to the clicked row, both included, in the order of the
  list now (filter and collapsed merges apply; the Working tree, Index and Show more rows are never in it).
  The anchor is the row of the last plain click, the last Ctrl-click that added a commit or the last plain
  arrow key, while that commit is selected and in the list; else the primary commit; with none, Shift-click is a plain click.
  The clicked row is the primary commit (the Changes panel follows it), the others are the extra selection.
  With exactly two commits selected, Changes and Diff show the diff between them.
  A range of adjacent commits enables *Interactive rebase selection…* and Squash. Shift with an arrow key onto the
  Working tree or Index row does not select it (the selection stays); a Shift-click on it does.
- Keys: ↑/↓ select, Shift with ↑/↓, Page Up/Down, Home and End extends the range from the anchor (only when the cursor stops on a commit row, not on the Working tree, Index or Show more row), `N` new, `Alt+N` new detached, `E` edit commit, `D`/`Shift+D` duplicate
  commit/branch, `S`/`Shift+S`/`Alt+S` squash/with descendants/split, `A`/`Shift+A` drop
  commit/drop commit and descendants, `I` interactive rebase (N), `F7`/`Shift+F7` next/previous conflicted commit (N).
- Double click on a row (outside its badges; not with Ctrl or Shift, not on the Working tree and Index rows, only
  while no task runs) checks out, as in traditional git software. One local branch at the commit: it is checked out.
  Several: a popup (`##dblclick_checkout`) lists them and, after a separator, *Detached*. None: a dialog *Checkout
  detached* names the short ID, says that HEAD becomes detached and has *Checkout* and *Cancel*; *Detached* in the
  popup asks the same way. Nothing happens when HEAD is on a branch of the commit or, when the commit has no local branch, HEAD is detached at it.
- Double click on a badge: a local branch badge checks that branch out (nothing for the current branch); a
  remote branch badge checks out the local branch of the short name, or opens *Create branch* with the remote
  branch as the start point and the short name as the name; a tag badge asks as above, then detaches at the tag's
  commit; any other badge acts as the row.
- Row context menu, in groups with separators: New · New detached · Check out ▸ (the branches at the commit, then after a
  separator *Detached* (no dialog: chosen by name); enabled with a single selected commit, also without a branch) ·
  Create branch… · Create tag… · Move branch ▸ | Merge into HEAD… · Rebase onto… · Interactive rebase… ·
  *Reset \<branch\> to here…* (id `###reset_here`; "Reset to here…" and disabled with a detached or unborn HEAD, also disabled while a merge, rebase, cherry-pick, revert or bisect is in progress) ·
  *Interactive rebase selection…* (Phase 3, when several adjacent commits are selected with Ctrl-click or Shift-click:
  the list starts at the oldest selected commit and the selected commits start selected in the editor) |
  Cherry-pick · Cherry-pick (no commit) · Revert · Revert (no commit) (with several selected commits: "Cherry-pick 3 commits" and so on) | Edit commit (checkout detached) · Duplicate · Squash… · Split… · Simplify parents ·
  Drop commit… | *Go to parent* · *Go to child* · *Filter by author* · Copy ▸ (ID, Full description) · Expand / Collapse merged history. The menu has no branch actions
  (Delete branch, Push): the menu of a branch badge has them (right click on the badge).
  *Go to parent* and *Go to child* (single selected commit) reveal and select the commit: one parent or child is
  a plain item, more (a merge commit; a commit with several children among the loaded rows) are a submenu with
  an item "\<short ID\> \<subject\>" each (ids `###go_<ID>`). A parent that a collapsed merge hides (it has no
  row) expands the merge first, then is revealed. *Go to parent* is disabled for a root commit
  ("The commit has no parent."), *Go to child* when no loaded row has the commit as a parent ("No child in the
  loaded history."). *Filter by author* (single selected commit) puts the author's name in the filter field and
  filters as a typed text does (the search text has name and email).
- Badge context menu: a right click on a badge selects the row (as a right click on the row does) and shows the
  menu of the badge's ref, with the items of the ref's menu in its side panel (the same code): a local branch the
  Branches panel's branch menu (`##badge_branch_menu`), a remote-tracking branch the remote-tracking branch menu
  (`##badge_rbranch_menu`), a tag the Tags panel's tag menu (`##badge_tag_menu`), a stash the Stashes panel's menu
  (`##badge_stash_menu`), a worktree the Worktrees panel's menu (`##badge_worktree_menu`), a keep badge the menu of
  the commit in the Detached node of Branches (`##badge_kept_menu`). The menu is drawn after the table, so it stays
  when its row scrolls out of view. The HEAD badge has no menu of its own: it shows the row context menu. A right
  click on the row outside its badges shows the row context menu. The left click and the drag of a badge are as before.
- Working tree context menu: Commit… · Discard changes… · N: Stash changes… ·
  Stage all · Unstage all.
- Drag and drop (Phase 3): commit→commit (Move before/after, Squash, Rebase; Shift = move
  before, Ctrl = squash, Alt = rebase, none = chooser popup `##dnd_chooser`), branch
  badge→commit or badge (a menu `##branch_drop_chooser`: Merge into, Rebase onto, Move here; Shift = move at once),
  files from Changes→commit (move changes).
- Reveal: loads more history until the commit is found; progress in the activity area,
  cancellable.
- Scope: branch/tag/remote visibility from the side panels.

### 2.x Drag and drop (Phase 3)
- **Commit row → commit row.** The modifier held at the drop picks the action: Shift = Move after,
  Ctrl+Shift = Move before, Ctrl = Squash into (messages combined), Alt = Rebase onto (with
  descendants). Without a modifier a chooser pops up: Move before / Move after / Copy after /
  Squash into / Rebase onto.
- **Branch badge X → commit row or badge:** (the drag starts on the badge of a local branch; the tooltip
  shows the name of X.) Without a modifier a small menu `##branch_drop_chooser` pops up, with the items
  "Merge X into Y" (`###merge`), "Rebase X onto Y" (`###rebase`) and "Move X here" (`###move`, shortcut text
  "Shift"). Y is the target: the local branch of the badge the drop is on; else the only local branch at the
  commit; else the short ID of the commit. Merge X into Y is enabled when Y is the current branch: it opens
  the "Merge into HEAD" dialog with X. Rebase X onto Y is enabled when X is the current branch: it replays
  the commits of X onto Y (the branch or the commit). Move X here moves X to the commit; when another worktree has X checked out, it opens the "Move branch" dialog (with the warning) instead. A disabled item has
  a hint with the reason. Shift at the drop moves the branch at once, without the menu (the same rule for a branch of another worktree). A drop on the commit of
  X, or on its badge, does nothing. The menu closes without an action when X or the target goes away (a
  reload); its items are off while a task runs.
- **Files from Changes → commit row:** a commit's files go to its parent, its child or the
  checked-out commit (other targets are refused with a notification); working tree files are
  folded into the target commit (descendants rebased, the working tree kept as it is).

## 3. Changes panel — window `"Changes"`
- Header `###changes_title`: title of the selection ("0000000 Working tree" — the zero ID —,
  "Index", short ID and commit subject, stash message; the ID follows the ID rule, its own item
  `###changes_title_id`; a commit's ID copies on click and has the Copy ID item, the zero ID does neither), filter `##changes_filter`, text field
  *Compare with* `##compare_with` (HEAD, a commit ID or ref, or Work Tree; enabled for commits only). While two
  commits are selected in History, the field is replaced by the disabled field `##compare_pair` that shows
  `<lower short ID>..<upper short ID>`: the files and the diff are those between the two commits (Before: the
  lower row in the list, After: the upper row, whichever is the primary commit), the typed target comes back
  with one commit or three or more, and the History-editing and Restore items of the file menu are off.
  In that state the `D` key does nothing and a file row cannot be dragged onto a commit. The pair ends when one of the
  two commits is no longer in the loaded history (a reload rewrote it).
- For a commit: flat list `##files` of rows `file_<path>` with status icon (A, M, D, R, C, T, U),
  renames shown `old → new`; files holding first-class conflicts are in the conflict colour with
  "N-sided conflict", and double-clicking one at HEAD opens the editor. Selecting a commit
  selects a file, as a click on the row does, and the diff shows it; the keyboard focus stays in History.
  The file is the one that the user last selected (same path, or the old path of a renamed file) when
  the commit has it and the filter shows it. Otherwise it is the first visible file. The selection of a
  file by the panel does not change the remembered path: a commit without the file shows its first file,
  and a later commit that has the file selects it again. The list scrolls once to the row that the panel
  selects, so the row is visible; the panel adds no scroll for a selection by the user. A new compare
  target keeps the current file when the comparison still has it.
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
  Move to parent · Revert (commit files: a new commit on HEAD; id `###revert`) · Revert (no commit) (index and working tree; id `###revert_no_commit`) · Discard (D; commit files: rewrites the commit, one Undo, published commits ask first) · Delete file.

## 4. Commit actions (Commit menu and History context menu)
New commit, Check out (a branch, or detached), Edit commit (checkout detached) (E; detaches at the commit, after making a local branch for a commit only a remote branch has; a toolbar banner *Editing \<id\> of \<branch\>* with Return / Stop editing, and Amend restacks the descendants atomically), Commit…, Describe (Save message), Edit author, Duplicate
commit/branch, Rebase…, Interactive rebase…, Reset \<branch\> to here… (dialog *Reset branch*: the commit and a *Mode* Soft, Mixed (default) or Hard; Hard asks first in the dialog *Discard changes* (buttons *Reset hard* and *Cancel*) when it would discard staged, unstaged or conflicted changes, or an untracked file that the commit also has; other untracked files stay), Squash…/with descendants, Split…, Restore…,
Drop commit…/Drop commit and descendants…, Simplify parents, Reorder, Move
files/hunks/lines, Merge into HEAD, Rebase HEAD onto branch / Reconcile with remote. Meaning:
plan §4.3 table. *Interactive rebase from here…* (key `I`) opens the todo editor (§4.x) for the
commit and its descendants up to HEAD, or up to the first local branch (by name) that contains it
when HEAD does not; a commit on neither is refused. The *Squash* and *Rebase onto* dialogs have
*Open as interactive rebase…*: the editor opens with the action as its starting todo (the commit
moved after the target as squash or fixup; the new base as *Onto*).
*Merge into HEAD…* and *Rebase HEAD onto this* in the shared commit actions act on the selected commit
(disabled on HEAD itself; Rebase needs an attached HEAD); a commit is merged with Git's default message
"Merge commit '<full ID>'" (the dialogs' revision fields are prefilled with the full ID, which Git
runs as typed). Branches offers the same two for a branch (§8).

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
- It follows the history selection, except while the Blame panel has a line selected: then it shows that line's
  change (§7). A history selection made meanwhile is shown once the Blame selection is gone. Clicking a parent
  ID reveals it in History and ends the Blame selection, so the parent is what is shown.
- Message editor `##message` with *Save message* `###save_message`; for HEAD the button is red and reads *Amend HEAD*
  and asks for confirmation (a dialog titled `Amend HEAD`, buttons Amend / Cancel; it notes when staged changes
  are not included).
  The message fields (`##message`, `##commit_message`, `##merge_message`) have a context menu with a checkable
  *Word wrap* option (popup `<field id>_menu`, e.g. `##message_menu`); it is persistent,
  stored in imgui.ini (`[GGUIView][Info] WrapMessage`). A horizontal sizer below the message field
  (`<field id>_sizer`, e.g. `##message_sizer`) resizes it by whole lines (2–40, double click resets to 6); the
  height is shared by the fields and persistent (`[GGUIView][Info] MessageLines`).
- Author line (plain text) with menu Copy name / Copy email / Edit author… (Phase 3); Committer line when it
  differs (N); Date; published/lock state ("Published" / "Not published").
- Commit ID `###commit_id_text` (a short ID: 3 characters in the text colour, 4
  dimmed; clickable text, see the general rules; no Copy button) with the one copy-ID context
  menu item; Parents list
  `parent_<n>` (short IDs; click reveals; right click: the same item for that parent).
- N: conflicted files list with side counts.
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
  copied text (both views). Ctrl+X and Shift+Delete copy as well (the text cannot be cut).
- Expandable context rows `expand_<n>` (click: 10 lines; Shift+click: whole gap).
- Placeholders: binary, image (dimensions), submodule (old → new commit), mode change line.
- Capped large files: "Load full diff" `##load_full`.
- Context menu: Copy · Blame file · Stage/Discard/Unstage line(s) and hunk(s) (N) · Move line(s)/hunk to
  child (or working tree on the HEAD commit, which has no child) / parent / active commit (Phase 3) · Revert line(s) (a commit's lines: a new commit on HEAD; id `###revert`) · Revert line(s) (no commit) (into the index and working tree; id `###revert_no_commit`) · Discard line(s)/hunk(s) of a commit (rewrite).

## 7. Blame panel — window `"Blame"`
- **Toolbar:** Back `##blame_back` / Forward `##blame_fwd` (also mouse buttons 4/5), the file and where it is blamed
  ("tale.txt at 1a2b3c4", "at working tree"), "loading..." while a blame runs, and the filter `##blame_filter`.
  "Large file: only the first lines are blamed." is shown above the code of a capped blame.
  Back and Forward move only within the blames of one file: blaming another file (from outside the panel) or closing
  the panel clears the history, and closing clears the filter too. The reopened panel is empty, with the hint "Use
  "Blame file" on a file ..." and Back and Forward disabled. "Another file" is a path that is none of the history's
  entries: the old name of a renamed file, reached by *Show originating source*, is the same file, and blaming it by
  its new name adds to the history. Blaming what is already shown adds nothing; a working-tree blame is run again,
  and keeps its position (first visible line, cursor line, selection). A blame reached by Back or Forward is shown
  at the first visible line, with the cursor line and the selection it was left with.
- **Code:** the whole file in a read-only text editor `##blame_editor` with line numbers. The editor does the
  syntax highlighting, chosen by the file name's extension (no highlighting for other files; the colours follow the
  theme and change with it). Text can be selected with the mouse and keys and copied (Ctrl+C); typing, paste, Delete,
  Backspace, Enter and Tab change nothing. Ctrl+X and Shift+Delete copy as well.
- **Gutter:** one per line, `blame_line_<n>`, between the line numbers and the code: the commit ID (3 characters
  highlighted, the rest dimmed), author and date on the first line of each change block; "Not committed" for an
  uncommitted block. Every second change block has the alternate row background in its gutter, and each block
  after the first starts under a 1 px separator line. The author column is as wide as the longest author present, up
  to the width of 20 digits; a longer name is cut with an ellipsis. The gutter is measured again when a blame loads
  and when the font or its size changes. The tooltip (full commit summary, author, date, original file and line)
  is on the gutter only. The selected lines have the code's selection colour in the gutter too, in place of
  the alternate background.
- **Opening at a line:** *Show originating source* and *Blame before this change* open the blame with the cursor on the
  corresponding line. When they are used on a line, that line stays on the same row of the editor; without a known row
  it is scrolled to the middle of the code. That is not a selection: Down in the window selects it.
- **Menus:** a right click on a line's gutter, on its line number or on its code, and Alt+Space (for the cursor line), open the
  menu of that line. The code's menu has Copy (enabled with a selection) and Select all first, a separator and then the
  line items; the other three have the line items only. Line items: Blame before this change ·
  Show originating source · Reveal commit · Copy (the one ID item: on the gutter's ID its 3
  highlighted characters give 3, anywhere else the 7, Shift the full ID; a line whose ID is not
  shown counts as the rest) · Select change block · Copy change block. The first four act on the
  line's commit and are disabled for an uncommitted line. A right click acts on the line
  hit and does not move the cursor or change the selection (*Select change block* does select).
- **Filter:** marks the lines matching it (text, author, ID, summary) and brings the first match into view when it is
  outside the view; it hides nothing and does not move the cursor or select. The marks follow the theme. After the field
  the panel shows "n of m" (the current match and the count), "No matches" when nothing matches, nothing for an empty
  filter. Enter in the filter (it stays in the field, its text selected) and F3 / Shift+F3 anywhere in the window (they move the keyboard
  to the code) go to the next / previous match, wrapping; the first step after a new filter or blame goes to the first
  match (the last one for Shift+F3). The cursor and the selection move to the match line, which is scrolled to the middle of the code
  when it is outside the view, and Change information shows its change. A new filter text puts the position on the first match without
  moving the cursor or selecting. Esc in the filter restores the text it had when the field was entered and leaves the
  field. Ctrl+F focuses the filter (the editor's own find window is never opened).
- **Keys:** Down in the window (outside a text field) moves the keyboard into the code, selecting the cursor line
  (line 1 of a fresh blame) when nothing is selected, without scrolling when it is in view; from there the arrows,
  Shift+arrows and the editor's other keys apply, and the selection follows the cursor. Alt+Space (with the keyboard
  in the code and a line selected) opens the line menu of the cursor line, below its gutter; when that line is out of
  view it is scrolled into view first. Ctrl+A selects the whole text. Esc clears the selection (see below), the cursor and
  the keyboard stay in the code; an Esc that closes a menu does not.
- **Selection:** a line is selected by a press on its gutter (Shift extends the range from the line the selection
  started on; dragging from the press selects the whole rows down or up to the row under the mouse, at most to the
  first or last row in view), or by the cursor or a text selection made in the code (mouse, arrows, Ctrl+A, Select all);
  the cursor a blame opens with is not a selection. A selection that ends at the start of a later line does not include
  that line, except a selection of the whole text (Ctrl+A), which includes an empty last line.
- **Change information:** while the Blame window is visible and a line is selected, it shows the change of the line
  selected last (the one pressed last in a Shift range, the line at the moving end of a text selection (the one the cursor is on, or the line before it when the cursor is at
  the start of a line), the one the menu was
  opened on for *Select change block*; an uncommitted line gives the working tree form); the history selection,
  Changes and Diff do not change. Esc in the Blame window (when a line is selected) clears the selection; so do
  loading another blame and a blame result arriving. Without a selection, with the window closed, collapsed or hidden
  behind another tab, or while a blame loads, Change information follows the history selection again (§5).

## 8. Side panels
- **Hover actions**: while the mouse is on a row of Branches, Tags, Remotes, Stashes or Worktrees, its most
  common actions show as icon buttons at the row's right edge (over the row's end), buttons `###act_<name>` in
  the row's ID scope, each the same as its menu item (same enabled state; the tooltip is the item's name); a
  click on a button is not a click on the row. Mouse only (not keyboard stops). Local branch: Check out (not
  on HEAD's branch) `act_checkout` · Push `act_push`; remote-tracking branch: Check out ("Check out..." without a local branch); kept commit:
  Check out · Create branch `act_branch`; tag: Reveal
  `act_reveal`; remote: Fetch `act_fetch` · Pull `act_pull`; stash: Apply `act_apply` · Pop `act_pop`;
  worktree: Open here `act_open_here` (not on the current one) · Open directory `act_open_dir`.
- **Branches** `"Branches"`: filter, Create branch… `##create_branch`, rows `branch_<name>`
  (click toggles visibility in History, Ctrl-click = only this; keyboard: Space toggles,
  Ctrl+Space = only this; the eye is mouse-only), current outlined. Branches are grouped by folder and
  remote-tracking ones by remote; a group row's eye (`###eye` in the group's scope) shows/hides the branches
  listed under it (all visible: hide, else show; mixed drawn dimmed; Ctrl-click = only these; mouse only). Ctrl-click
  (on a row's or a group's eye) hides every other branch, tag and kept commit too. Context:
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
  Check out on a remote-tracking branch (the menu item, the row's `act_checkout` button and the double click on its
  badge in History) has one result: a local branch of the short name is checked out; without one, the *Create branch*
  dialog opens (name: the short name, at: the remote-tracking branch, *Check out after creating* on), and nothing is
  created before the user confirms it. The menu item is then "Check out…" (`###check_out`); else "Check out".
  The **Detached** node `detached_group` sits between the local and the remote branches. It is shown only
  while the snapshot lists kept commits (commits a ggui or git-gg operation made on a detached HEAD that no branch or tag reaches,
  kept by `refs/gg/keep/<id>`; a detached HEAD merely on a commit does not list it) that pass the filter; open by default. Its eye (`###eye` in the node's scope) acts on the listed rows
  like a group's eye. One row per kept commit, in the snapshot's order, `detached_<full commit id>`: the short ID
  (7 characters, the last 4 dimmed), two spaces, the summary, and `[<worktree>]` when the commit is another
  worktree's detached HEAD. The row's eye toggles `refs/gg/keep/<id>` in History. This worktree's detached HEAD
  is outlined as the current branch is. Hover actions: Check out `act_checkout` (not on the current row) ·
  Create branch `act_branch` (the `Create branch` dialog at the commit). Double-click checks the commit out.
  Context: Check out (detached; disabled on the current row) · Create branch here… · Reveal · Copy <ID> (as in
  History) · Drop commit… (History's `Drop commit` dialog). The filter matches the commit ID or the summary,
  ignoring case. Show all / Hide all at the top include the kept commits.
- **Tags** `"Tags"`: filter, Create tag… (N annotated with message), rows `tag_<name>` (the
  name only; the tooltip of an annotated tag shows its message)
  (visibility toggle, keys as Branches). Context: Reveal · Copy name · Delete · N: Push tag · Delete on remote.
- **Repositories** `"Repositories"` (N): the permanent repository list of the settings (`repositories`:
  `[{path, alias}]`, no limit; each opened or dropped repository is added, except a linked worktree,
  see below; a settings file without the key starts with the recent list) as a tree. An alias `group/subgroup/name` puts a repository in groups
  (tree nodes `###group_<group path, "/" as ":">`, open by default) under the label `name`; a repository
  without an alias is at the top level with its deduplicated folder name. Each level lists the groups
  first, then the repositories, each part by label (ignoring case). Rows `repo_<label, "/" as ":">/###row` in the
  scope of their groups (equal labels among siblings: `repo_<label>#n/###row`, n from 1 for the second); the tooltip is the full path. The alias
  shows only here: the toolbar switcher, Welcome and the title bar keep the deduplicated folder names.
  A click selects a row; a double-click, or Enter while the panel has the focus, opens the repository in
  this window. The open repository has the text colour of the current branch and is not opened again.
  When it has more than its main worktree its row is a tree node, closed by default (the arrow toggles
  it, a click on the label does not), with one row per worktree, `worktree_<name>/###row` in the row's
  scope: the name, "(main)", then the branch or the short ID; the current one has the text colour of the
  current branch, a missing one is dimmed; a double-click or Enter opens a worktree, not the current or
  a missing one. A linked worktree that this window shows is not a repository entry of its own: the
  application adds its main repository to the list (when that one has a `.git` entry and is not in the
  list yet; a dropped linked worktree folder is not added itself), and the row of the main repository is the open row, with the worktrees below it and the
  current worktree marked. A worktree entry that is in the list stays as a plain row (no automatic
  removal); a double-click on it opens that worktree. When the main repository is bare, or has no `.git`
  entry (a `--separate-git-dir` or submodule repository), the worktree folder is the entry and the open row. The Recent list gets the worktree folder as before.
  The other repositories have no arrow. Context (repository rows): Open (disabled on the
  open one) · Set alias… · Copy path · Remove from list (also the open one; it comes back without its
  alias at its next open).
  Drag and drop: a repository row (not a worktree row) dragged onto a group node goes into that group:
  the alias becomes the group path plus the last segment of the alias, or plus the last "/" segment of
  the label when there is no alias. The empty area below the rows (`###top_level`) is the top level: the alias keeps only its
  last segment, and no alias stays no alias. A drop on the own group changes nothing.
  - `Set alias`: *Alias for <path>* `##alias` (the alias now) · a note ("/" makes groups; empty removes
    the alias) · *Set* / *Cancel*. The text is stored trimmed, also each "/" segment, without empty
    segments.
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
  Apply (restore index) · Pop · Pop (restore index) · Branch from stash… · Drop… · Copy (the one ID item, after a separator: on
  the drawn base ID, the base's; off it, and when opened with `Alt+Space`, the stash's commit, followed by
  a second item for the base, labelled "Copy base a1b2c3d", "Copy base full ID" with Shift; the item's ID
  is `###copy_id`, the base's `base/###copy_id`) · Header: Stash changes… · Clear all….
- **Reflog** `"Reflog"`: chooser `##reflog_ref` (HEAD, branches, stash), filter, rows with old →
  new, message. Context: Copy (the one ID item: for the ID the right click was on; off the IDs, and
  when opened with `Alt+Space`, the new ID's, followed by a second item for the old ID when there
  is one, labelled "Copy old a1b2c3d", "Copy old full ID" with Shift; the item's ID is
  `###copy_id`, the old one's `old/###copy_id`) · Reveal old/new ·
  Create branch from old/new….
- **Operations** `"Operations"` (M): rows `op_<id>` (time, source label, description). Context:
  Restore (undo back to before this operation). The tooltip lists the refs the operation changed;
  a keep ref reads `detached <first 10 characters of the ID>`, not `refs/gg/keep/<id>`. Plain git commands appear as `git <command>` rows (source `git`), journaled by the reconciler
  (undo-journal §4); there is no footer note.

## 9. Dialogs
Each dialog is a modal popup with the given name and OK/Cancel buttons `OK##<dialog>` and
`Cancel##<dialog>`:
`Commit`, `Create branch`, `Rename branch`, `Delete branch`, `Move branch`,
`Create tag`, `Add remote`, `Edit remote URL`, `Clone repository`, `Initialize repository`,
`Push to`, `Force push`, `Stash changes`, `Drop stash`, `Branch from stash`, `Discard changes`,
`Apply patch`, `Save patch`, `Stash and switch`, `Stash and pull`, `Push refused`,
`Credentials` (askpass), `Rewrite published history`, `Amend HEAD` (Change information, §5:
*Amend* / *Cancel*), `Non-text conflicts` (pre-flight, Phase
3), `Interactive rebase onto` (Phase 3: field `##base`, buttons *Open* / *Cancel*; the todo editor
itself is the dockable window `Interactive rebase`, §4.x), `Replace sequence.editor` (Phase 4:
*Replace* / *Cancel*, §1.6), `Add worktree`, `Remove worktree`, `Remove worktree with changes`,
`Lock worktree`, `Prune worktrees`, `Repair worktree` (Phase 4, §8 Worktrees), `Set alias`
(§8 Repositories), `Settings`.
The `Commit` dialog has the message field `##message`, an *Amend* checkbox `##amend` (unchecked;
disabled with a tooltip on an unborn HEAD or during a merge, cherry-pick or revert), *Skip hooks*
`##skip_hooks` and, while Amend is ticked, *Change the message only* `##message_only`. Ticking
Amend swaps the field to HEAD's message (whatever is selected) and the button to *Amend*; the
typed commit message and the (edited) amend message are each kept across toggles. The
conflict-marker warning shows in both modes (not with *message only*). Amending commits that are already on a remote (HEAD or a restacked descendant, also *message only*) asks `Rewrite published history` first; Cancel reopens the Commit dialog as it was.

## 10. States
- **Busy:** conflicting actions disabled with reason tooltip; browsing stays enabled.
- **Scanning:** Changes header shows "scanning…" while partial status results arrive.
- **Loading history:** History footer shows "Loading…" plus Cancel.
- **Detached HEAD:** branch label "detached", Pull disabled with reason.
- **Unborn HEAD:** History shows only the Working tree row; branch label shows the unborn branch.
- **Bare repository:** no Working tree row; Changes shows commit files only.
