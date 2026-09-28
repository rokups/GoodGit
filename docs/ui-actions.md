# UI actions and their tests

Standing rule 8 (`REBUILD_TASKS.md`): every UI action the app implements is exercised by at
least one test that checks the action's effect on the UI and on the repository. This file lists
every action of phases 0–3 with the test that covers it. It was built in P3-20 from
`Source/app` (every `MenuItem`, `Button`, `Selectable`, `Checkbox`, `Combo`, input field,
`Shortcut`/`IsKeyPressed`, drag-and-drop source/target, double-click, mouse-button and
mouse-wheel handler, and every `Form` dialog's fields and buttons) and checked against line
coverage of the action handlers.

**What a row is.** One way to trigger one action. Keyboard and mouse paths of the same action
are separate rows. A menu that shows the same items in two places (the History row menu and
Commit ▸ Selected commit) is listed once, with a row for the second place. Not listed: tooltips,
hover highlights, scrolling without an effect, ImGui's own window chrome (docking, resizing,
tab dragging), and Phase 4 placeholders that are drawn disabled.

**Columns.** *Test(s)* names tests as `category/name` (as printed by `ggui --list-tests`; run one
with `ggui --test=... --headless`), at most two per row. *Status* is `tested` (a passing test
drives the action through the UI and checks its effect) or `missing`.

**Keeping it up to date (Phase 4 and later).**
- A task that adds, changes or removes a UI action updates this file in the same commit: one
  row per trigger, in the section of the panel, menu or dialog that shows it.
- A row is `tested` only when its test drives the action like a user (click, key, drag, typing;
  no direct calls into `Actions`, no test-only backdoors) and asserts the result, on disk
  (`git` output, files, settings.json) as well as in the UI. Opening a dialog and pressing
  Cancel tests the Cancel row, not the action behind the dialog.
- The keyboard path of an action needs its own row and test even when the mouse path is tested.
- A task is not done while any row is `missing`.
- To find actions that lost their test, run the coverage report
  (`NO_GATE=1 scripts/run_software_coverage.sh`) and look for uncovered lines inside action
  handlers (`scripts/uncovered.py build/coverage/coverage/coverage.lcov Source/app`). Options
  read in the same line as another path (dialog checkboxes, `form.checked(...)`) do not show up
  there: grep the tests for the field id instead.

**Audit result (P3-20).** 408 rows, all `tested`. The audit added tests for 19 rows that had
none and made 14 existing tests check the effect instead of only opening a dialog or counting
journal entries (see `Source/tests/test_ui_actions.cpp` and the P3-20 commits). One bug was
found and fixed: after Git required ▸ Quit (or any refused git check) the open stayed pending, so
the Welcome screen stayed disabled and no repository could be opened without Retry. No action was
removed as dead UI.

## Main menu ▸ Repository

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Open... | menu | `shell/open with the picker: Welcome, menu, Ctrl+O, toolbar` | tested |
| Initialize... | menu | `setup/initialize a repository from Welcome and the menu` | tested |
| Clone... | menu | `network/clone from Welcome and the menu; unreachable remote fails cleanly` | tested |
| Recent ▸ filter field | type in the submenu | `shell/recent repositories: Welcome list, Recent menu, switcher` | tested |
| Recent ▸ <repository> | menu | `shell/recent repositories: Welcome list, Recent menu, switcher` | tested |
| Open working directory | menu | `shell/Repository menu: copy path, refresh, working directory, settings, quit` | tested |
| Copy path | menu | `shell/Repository menu: copy path, refresh, working directory, settings, quit` | tested |
| Close repository | menu | `shell/open by typed path, default layout, close from the menu`<br>`shell/recent repositories: Welcome list, Recent menu, switcher` | tested |
| Refresh | menu | `shell/Repository menu: copy path, refresh, working directory, settings, quit` | tested |
| Fetch | menu | `network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move` | tested |
| Pull | menu | `network/pull follows pull.rebase; dropdown overrides; menu and panels` | tested |
| Push | menu | `network/push: toolbar, menu, History and Branches; no upstream prefills Push to`<br>`edit/dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults` | tested |
| Settings... | menu | `shell/Repository menu: copy path, refresh, working directory, settings, quit`<br>`ui/Settings ▸ Hooks: the ask-on-open checkbox turns the first-open prompt on and off` | tested |
| Quit | menu | `shell/Repository menu: copy path, refresh, working directory, settings, quit` | tested |

## Main menu ▸ Commit

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| New commit | menu | `new/new commit on HEAD advances the branch (toolbar, menu, keys)` | tested |
| New detached commit | menu | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Commit... | menu | `commit/nothing staged: stage all tracked or the selected files`<br>`commit/default for nothing staged comes from Settings` | tested |
| Amend... | menu | `commit/amend content and message, message only, Amend into HEAD`<br>`conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too` | tested |
| Move HEAD to parent | menu | `checkout/move HEAD to parent and child`<br>`conflicts/checking out a conflicted commit: clean status by default, index stages when asked` | tested |
| Move HEAD to child | menu | `checkout/move HEAD to parent and child`<br>`conflicts/checking out a conflicted commit: clean status by default, index stages when asked` | tested |
| Selected commit ▸ (the commit-editing items below) | menu | `edit/no-op rewrites keep ids; the Commit menu carries the selected commit's actions`<br>`edit/History and Commit menus: merge a commit into HEAD, rebase HEAD onto a commit` | tested |
| Interactive rebase... | menu | `rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches`<br>`ui/Interactive rebase: Cancel while the commits are being read` | tested |

## Main menu ▸ Edit

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Undo | menu | `undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores` | tested |
| Redo | menu | `undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores` | tested |
| Apply patch... | menu | `patches/apply from the clipboard or a file, to the working tree or the index` | tested |

## Main menu ▸ View

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| <panel> (show/hide each of the 12 panels) | menu | `ui/View menu: every panel hides and shows again; the choice is saved`<br>`shell/View menu: panels, next/previous changed file, reset layout` | tested |
| Previous changed file | menu | `shell/View menu: panels, next/previous changed file, reset layout` | tested |
| Next changed file | menu | `shell/View menu: panels, next/previous changed file, reset layout` | tested |
| Reset layout | menu | `shell/View menu: panels, next/previous changed file, reset layout` | tested |

## Global keyboard shortcuts

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Ctrl+O open | key | `shell/open with the picker: Welcome, menu, Ctrl+O, toolbar` | tested |
| Ctrl+Q quit | key | `shell/Repository menu: copy path, refresh, working directory, settings, quit` | tested |
| Ctrl+W close repository | key | `shell/open with the picker: Welcome, menu, Ctrl+O, toolbar`<br>`shell/recent repositories: Welcome list, Recent menu, switcher` | tested |
| F5 refresh | key | `shell/Repository menu: copy path, refresh, working directory, settings, quit`<br>`shell/unusual repository states: sequences between commits, detached rebase, odd remotes, tags, stash and worktrees` | tested |
| F6 next changed file | key | `shell/View menu: panels, next/previous changed file, reset layout`<br>`shell/activity spinner, task tooltip and Cancel` | tested |
| Shift+F6 previous changed file | key | `shell/View menu: panels, next/previous changed file, reset layout` | tested |
| Ctrl+N new commit on the selection | key | `new/new commit on HEAD advances the branch (toolbar, menu, keys)`<br>`shell/toolbar details: force with lease, push tags, HEAD tooltip, a merge from the selection, a detached rebase's progress` | tested |
| Ctrl+Z undo | key | `hooks/plain git commands are journaled one operation each and Undo restores them`<br>`setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable` | tested |
| Ctrl+Y redo | key | `undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores`<br>`undo/failed operations are passed over by Undo and Redo` | tested |

## Toolbar

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| New (+) | click | `new/new commit on HEAD advances the branch (toolbar, menu, keys)` | tested |
| Commit (index) | click | `commit/commit the index from the toolbar; hooks run natively`<br>`commit/failing pre-commit hook goes to the banner; Skip hooks` | tested |
| Amend (HEAD selected) | click | `ui/toolbar Amend with HEAD selected; Skip hooks on Amend` | tested |
| Undo | click | `rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo`<br>`rebase-native/failure paths: pre-rebase veto leaves no rebase; a corrupt journal during a stop keeps one Undo` | tested |
| Redo | click | `ui/Redo that would overwrite local changes offers Stash and redo`<br>`stash/apply, pop with the index, apply one file, branch, drop, undo, clear` | tested |
| Refresh | click | `shell/Repository menu: copy path, refresh, working directory, settings, quit` | tested |
| Fetch | click | `network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move`<br>`network/remote actions are disabled while a mutation runs; browsing still works` | tested |
| Fetch options ▸ Fetch <remote> | dropdown | `network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move`<br>`network/fetch from an unreachable remote reports the failure` | tested |
| Fetch options ▸ Fetch and prune | dropdown | `network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move` | tested |
| Fetch options ▸ Fetch tags | dropdown | `network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move` | tested |
| Pull | click | `network/pull follows pull.rebase; dropdown overrides; menu and panels`<br>`network/pull disabled when detached or without upstream; Stash and pull` | tested |
| Pull options ▸ Pull (merge) | dropdown | `network/pull follows pull.rebase; dropdown overrides; menu and panels` | tested |
| Pull options ▸ Pull (rebase) | dropdown | `network/pull follows pull.rebase; dropdown overrides; menu and panels` | tested |
| Pull options ▸ Pull (fast-forward only) | dropdown | `network/pull follows pull.rebase; dropdown overrides; menu and panels` | tested |
| Push | click | `network/push: toolbar, menu, History and Branches; no upstream prefills Push to`<br>`network/rejected push: Pull then push, Force with lease; push tags` | tested |
| Push options ▸ Push to... | dropdown | `network/push: toolbar, menu, History and Branches; no upstream prefills Push to` | tested |
| Push options ▸ Force with lease... (confirm: Force push) | dropdown | `ui/toolbar Push options ▸ Force with lease overwrites the upstream after confirming` | tested |
| Push options ▸ Force with lease... (no upstream: opens Push to) | dropdown | `shell/toolbar details: force with lease, push tags, HEAD tooltip, a merge from the selection, a detached rebase's progress` | tested |
| Push options ▸ Push tags | dropdown | `network/rejected push: Pull then push, Force with lease; push tags` | tested |
| Stash | click | `stash/create: message, untracked, keep index, staged only, selected files`<br>`undo/every everyday mutation can be undone` | tested |
| Pop | click | `stash/create: message, untracked, keep index, staged only, selected files` | tested |
| Repository switcher | combo | `shell/recent repositories: Welcome list, Recent menu, switcher` | tested |
| Open the working directory (folder) | click | `shell/Repository menu: copy path, refresh, working directory, settings, quit` | tested |
| HEAD ID ▸ Copy ID (Shift: full) | context menu | `shell/toolbar HEAD: plain text, copy short or full ID` | tested |
| Cancel (running task) | click | `shell/activity spinner, task tooltip and Cancel`<br>`history/cancel a long history load and a reveal` | tested |

## Toolbar ▸ operation in progress (state badge)

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Continue | click | `conflicts/native merge: three-way diff, take ours, edit the message, continue`<br>`conflicts/native: resolve by editing, mark resolved, continue` | tested |
| Skip (rebase, bisect) | click | `conflicts/toolbar for other operations: abort a revert and an apply-backend rebase, skip and reset a bisect; merge tool on a first-class conflict`<br>`conflicts/native: abort a merge, skip a rebase step` | tested |
| Skip (cherry-pick, revert) | click | `ui/a stopped cherry-pick: Skip, and Commit with conflicts` | tested |
| Abort / Reset (bisect) | click | `conflicts/toolbar for other operations: abort a revert and an apply-backend rebase, skip and reset a bisect; merge tool on a first-class conflict`<br>`conflicts/native: abort a merge, skip a rebase step` | tested |
| Commit with conflicts (merge) | click | `conflicts/commit with conflicts records diff3 regions; not offered for binary conflicts` | tested |
| Commit with conflicts (cherry-pick) | click | `ui/a stopped cherry-pick: Skip, and Commit with conflicts` | tested |
| Amend and continue | click | `rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo`<br>`rebase-native/conflicted input: git rebase -i stops at edit on a commit with first-class conflicts; they are carried along` | tested |
| Edit remaining todo | click | `rebase-native/conflict stop, Edit remaining todo like git rebase --edit-todo, exec after every commit, Run as git rebase`<br>`rebase-native/Edit remaining todo reads a hand-edited list (short commands, CRLF, abbreviated ids, fixup -C/-c); refuses unreadable ones` | tested |
| Progress | click | `shell/toolbar details: force with lease, push tags, HEAD tooltip, a merge from the selection, a detached rebase's progress` | tested |

## Welcome screen

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Open repository... | click | `shell/open with the picker: Welcome, menu, Ctrl+O, toolbar` | tested |
| Initialize repository... | click | `setup/initialize a repository from Welcome and the menu` | tested |
| Clone repository... | click | `network/clone from Welcome and the menu; unreachable remote fails cleanly`<br>`network/cancel a clone: no directory left behind` | tested |
| Path field + Enter | type, Enter | `setup/git versions ggui reads: newer major, vendor suffix, no number; a typed path with a trailing slash; copying a notice`<br>`history/first rows of a large history appear quickly` | tested |
| Open (button) | click | `shell/opening shows progress and can be cancelled`<br>`history/first rows of a large history appear quickly` | tested |
| Cancel (while opening) | click | `shell/opening shows progress and can be cancelled` | tested |
| Cancel (while cloning) | click | `network/cancel a clone: no directory left behind` | tested |
| Recent entry | click | `shell/recent repositories: Welcome list, Recent menu, switcher` | tested |
| Recent entry: Delete key forgets it | key | `shell/recent repositories: Welcome list, Recent menu, switcher` | tested |
| Recent entry ▸ Forget | context menu | `shell/recent repositories: Welcome list, Recent menu, switcher` | tested |

## Settings window

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| General ▸ UI scale | slider | `shell/settings persist across restarts` | tested |
| General ▸ Theme | combo | `diff/edge cases: GIF, BMP, JPEG and unknown images; CRLF without a final newline; light theme; side-by-side scroll sync; text menu; term views of a conflicted commit`<br>`shell/settings persist across restarts` | tested |
| Git ▸ When nothing is staged | combo | `commit/nothing staged: stage all tracked or the selected files`<br>`commit/default for nothing staged comes from Settings` | tested |
| Git ▸ Expand to index stages on checkout | checkbox | `conflicts/checking out a conflicted commit: clean status by default, index stages when asked` | tested |
| Git ▸ scope tabs (User, Repository, Worktree) | tab | `setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides` | tested |
| Git ▸ user.name, user.email, core.editor, merge.tool, diff.tool | text field | `setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides` | tested |
| Git ▸ Pull method | combo | `setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides` | tested |
| Git ▸ Inherit | click | `setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides` | tested |
| Git ▸ Enable worktree settings | click | `setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides` | tested |
| Hooks ▸ Ask to install the ggui hooks when opening a repository | checkbox | `ui/Settings ▸ Hooks: the ask-on-open checkbox turns the first-open prompt on and off` | tested |
| Hooks ▸ Install hooks | click | `hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab` | tested |
| Hooks ▸ Remove hooks | click | `hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab` | tested |

## Dialogs: shared behaviour

Every dialog is a Form (`Source/app/shell/Dialogs.cpp`). A Cancel button that only closes the dialog is covered by these rows, not listed per dialog.

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Cancel button closes without an effect | click | `edit/dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults`<br>`network/askpass: answer and cancel a credentials prompt` | tested |
| Escape = the last button (Cancel) | key | `ui/dialogs: Escape cancels; Enter in a text field confirms when the button is enabled` | tested |
| Enter in a single-line field = the first button (only when enabled) | key | `ui/dialogs: Escape cancels; Enter in a text field confirms when the button is enabled` | tested |
| Filterable combo: type to filter, Enter picks the first match | type, Enter | `refs/upstream: set, unset, fast-forward` | tested |

## Dialogs: controls

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Commit ▸ Message, Commit | dialog | `commit/commit the index from the toolbar; hooks run natively`<br>`commit/nothing staged: stage all tracked or the selected files` | tested |
| Commit ▸ Skip hooks (--no-verify) | checkbox | `commit/failing pre-commit hook goes to the banner; Skip hooks` | tested |
| Commit ▸ Nothing is staged (stage all tracked / selected files) | combo | `commit/nothing staged: stage all tracked or the selected files`<br>`commit/default for nothing staged comes from Settings` | tested |
| Amend ▸ Message, Amend | dialog | `commit/amend content and message, message only, Amend into HEAD`<br>`conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too` | tested |
| Amend ▸ Skip hooks | checkbox | `ui/toolbar Amend with HEAD selected; Skip hooks on Amend` | tested |
| Amend ▸ Change the message only | checkbox | `commit/amend content and message, message only, Amend into HEAD`<br>`undo/every everyday mutation can be undone` | tested |
| Stash changes ▸ Message, Stash | dialog | `stash/create: message, untracked, keep index, staged only, selected files`<br>`panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation` | tested |
| Stash changes ▸ Keep the index | checkbox | `stash/create: message, untracked, keep index, staged only, selected files` | tested |
| Stash changes ▸ Include untracked files | checkbox | `stash/create: message, untracked, keep index, staged only, selected files`<br>`panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation` | tested |
| Stash changes ▸ Staged changes only | checkbox | `stash/create: message, untracked, keep index, staged only, selected files` | tested |
| Stash changes ▸ Selected files only | checkbox | `stash/create: message, untracked, keep index, staged only, selected files` | tested |
| Push to ▸ Remote | combo | `ui/Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease` | tested |
| Push to ▸ Remote branch, Push | dialog | `ui/Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease`<br>`network/push: toolbar, menu, History and Branches; no upstream prefills Push to` | tested |
| Push to ▸ Set as upstream | checkbox | `ui/Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease` | tested |
| Push to ▸ Force with lease | checkbox | `ui/Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease` | tested |
| Force push (confirmation) ▸ Force push | dialog | `network/rejected push: Pull then push, Force with lease; push tags`<br>`ui/toolbar Push options ▸ Force with lease overwrites the upstream after confirming` | tested |
| Push rejected ▸ Pull then push | dialog | `network/rejected push: Pull then push, Force with lease; push tags` | tested |
| Push rejected ▸ Force with lease... | dialog | `network/rejected push: Pull then push, Force with lease; push tags` | tested |
| Push refused ▸ Reveal | dialog | `network/push is refused when outgoing commits hold first-class conflicts` | tested |
| Create branch ▸ Name, Create | dialog | `refs/create, check out, rename and delete branches`<br>`edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Create branch ▸ Check out after creating | checkbox | `refs/create, check out, rename and delete branches` | tested |
| Create tag ▸ Name, Create | dialog | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch`<br>`edit/dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults` | tested |
| Create tag ▸ Annotated, Message | checkbox | `refs/tags: lightweight, annotated, delete, push, delete on remote`<br>`edit/dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults` | tested |
| Add remote ▸ Name, URL, Add | dialog | `refs/remotes: add, edit URL, prune on fetch, delete` | tested |
| Edit remote URL ▸ URL, Save | dialog | `refs/remotes: add, edit URL, prune on fetch, delete` | tested |
| Delete remote ▸ Delete | dialog | `refs/remotes: add, edit URL, prune on fetch, delete` | tested |
| Discard changes (files) ▸ Discard | dialog | `staging/stage, unstage and discard files`<br>`staging/discard all changes from the Working tree menu` | tested |
| Discard changes (all) ▸ Also delete untracked files | checkbox | `staging/discard all changes from the Working tree menu` | tested |
| Apply patch ▸ Source (Clipboard, File), Patch file, Apply to (Working tree, Index), Apply | dialog | `patches/apply from the clipboard or a file, to the working tree or the index` | tested |
| Rename branch ▸ Name, Rename | dialog | `refs/create, check out, rename and delete branches`<br>`undo/every everyday mutation can be undone` | tested |
| Delete branch ▸ Delete | dialog | `refs/create, check out, rename and delete branches`<br>`refs/delete a branch on its remote, and everywhere` | tested |
| Delete branch ▸ Delete even if not merged (-D) | checkbox | `refs/delete a branch on its remote, and everywhere` | tested |
| Move branch ▸ Move | dialog | `refs/move a branch; warning for a branch checked out elsewhere` | tested |
| Set upstream ▸ Upstream, Set | dialog | `refs/upstream: set, unset, fast-forward` | tested |
| Branch from stash ▸ Name, Create | dialog | `stash/apply, pop with the index, apply one file, branch, drop, undo, clear` | tested |
| Drop stash ▸ Drop | dialog | `stash/apply, pop with the index, apply one file, branch, drop, undo, clear` | tested |
| Clear stashes ▸ Clear all | dialog | `stash/apply, pop with the index, apply one file, branch, drop, undo, clear` | tested |
| Delete files ▸ Delete | dialog | `staging/intent to add, delete; no Track / Untrack` | tested |
| Install ggui hooks? ▸ Install | dialog | `hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab` | tested |
| Install ggui hooks? ▸ Not now | dialog | `hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab`<br>`ui/Settings ▸ Hooks: the ask-on-open checkbox turns the first-open prompt on and off` | tested |
| Install ggui hooks? ▸ Never | dialog | `hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab` | tested |
| Old gg data found ▸ Keep <commit> | checkbox | `ui/old gg data: Not now asks again next time; an unchecked commit is not kept` | tested |
| Old gg data found ▸ Branch for <commit> | text field | `setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable` | tested |
| Old gg data found ▸ Clean up | dialog | `ui/old gg data: Not now asks again next time; an unchecked commit is not kept`<br>`setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable` | tested |
| Old gg data found ▸ Ignore | dialog | `setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable` | tested |
| Old gg data found ▸ Not now | dialog | `ui/old gg data: Not now asks again next time; an unchecked commit is not kept` | tested |
| Resolve conflicts before rewriting ▸ Resolution (per conflict) | combo | `preflight/every non-text conflict kind asks for a decision, then the rewrite goes through` | tested |
| Resolve conflicts before rewriting ▸ Continue | dialog | `preflight/every non-text conflict kind asks for a decision, then the rewrite goes through` | tested |
| Resolve conflicts before rewriting ▸ Cancel (repository byte-identical) | dialog | `rebase-i/live preview: non-text conflicts that need a decision, commits that are or become empty` | tested |
| Commits become empty ▸ Keep them | dialog | `rebase-i/live preview: non-text conflicts that need a decision, commits that are or become empty` | tested |
| Commits become empty ▸ Drop them | dialog | `rebase-i/live preview: non-text conflicts that need a decision, commits that are or become empty` | tested |
| Rewrite published history? ▸ Rewrite | dialog | `rewrite/published history asks first; a locked ref leaves everything untouched`<br>`rewrite/in a linked worktree: its own branch follows quietly, the main worktree's branch asks first` | tested |
| Rewrite published history? ▸ Cancel | dialog | `rewrite/published history asks first; a locked ref leaves everything untouched` | tested |
| Stash and switch ▸ Stash and switch | dialog | `checkout/local changes block a switch: Stash and switch` | tested |
| Stash and pull ▸ Stash and pull | dialog | `network/pull disabled when detached or without upstream; Stash and pull` | tested |
| Undo would lose changes ▸ Stash and undo | dialog | `undo/refusals: nothing to undo, refs moved outside the journal, local changes in the way` | tested |
| Undo would lose changes ▸ Stash and redo | dialog | `ui/Redo that would overwrite local changes offers Stash and redo` | tested |
| Credentials ▸ answer, OK | dialog | `network/askpass: answer and cancel a credentials prompt` | tested |
| Credentials ▸ Cancel (git gets no answer) | dialog | `network/askpass: answer and cancel a credentials prompt` | tested |
| Clone repository ▸ URL, Destination, Clone | dialog | `network/clone from Welcome and the menu; unreachable remote fails cleanly`<br>`network/cancel a clone: no directory left behind` | tested |
| Git required ▸ Retry | dialog | `setup/git missing or too old: a blocking prompt with Retry`<br>`setup/git versions ggui reads: newer major, vendor suffix, no number; a typed path with a trailing slash; copying a notice` | tested |
| Git required ▸ Quit | dialog | `ui/Git required: Quit asks the app to quit; the refused open does not block later opens` | tested |
| Error ▸ OK | dialog | `rebase-i/open as interactive rebase from the Squash and Rebase onto dialogs`<br>`shell/errors open a popup; warnings are corner notifications` | tested |
| Error ▸ Copy message | dialog | `shell/errors open a popup; warnings are corner notifications` | tested |
| Take side in a region ▸ Region, Side, Take | dialog | `conflicts/first-class: take a side in one region; resolve in the editor and commit on top` | tested |
| Edit author ▸ Name, Email, Save | dialog | `rewrite/edit the author of any commit` | tested |
| Reconcile ▸ With, How: Rebase my commits onto it | combo | `ui/Reconcile by rebasing onto the upstream` | tested |
| Reconcile ▸ How: Merge it in | combo | `edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile` | tested |
| Rebase onto ▸ Destination, Rebase | dialog | `edit/rebase one commit, and a commit with its descendants, onto another branch`<br>`rewrite/pre-rebase can veto a rebase; post-checkout runs when HEAD moves` | tested |
| Rebase onto ▸ With its descendants | checkbox | `edit/rebase one commit, and a commit with its descendants, onto another branch` | tested |
| Rebase onto ▸ Open as interactive rebase... | dialog | `rebase-i/open as interactive rebase from the Squash and Rebase onto dialogs` | tested |
| Squash ▸ Into, Squash | dialog | `edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S)`<br>`edit/refusals: nothing to squash or move, unknown or descendant destinations, nothing redundant, already merged` | tested |
| Squash ▸ Combine the messages | checkbox | `edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S)`<br>`rebase-i/open as interactive rebase from the Squash and Rebase onto dialogs` | tested |
| Squash ▸ Open as interactive rebase... | dialog | `rebase-i/open as interactive rebase from the Squash and Rebase onto dialogs` | tested |
| Split ▸ file checkboxes, Message, Split | dialog | `edit/split a commit by files (Alt+S)`<br>`edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Abandon branch ▸ Delete the branches that only point into it | checkbox | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Abandon branch ▸ Also delete them on their remote | checkbox | `edit/abandon a commit (A) and a branch (Shift+A)` | tested |
| Abandon branch ▸ Abandon | dialog | `edit/abandon a commit (A) and a branch (Shift+A)`<br>`edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Restore ▸ From, Restore into (this commit / the working tree), Restore | dialog | `edit/restore paths in a commit or the working tree; simplify parents` | tested |
| Merge into HEAD ▸ Message, Merge | dialog | `edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile`<br>`edit/History and Commit menus: merge a commit into HEAD, rebase HEAD onto a commit` | tested |
| Merge into HEAD ▸ Use native git merge | checkbox | `edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile`<br>`edit/more refusals and edges: reorder across branches, reorder on a detached HEAD, fold onto another branch or a deletion, a native merge git refuses` | tested |
| Interactive rebase onto ▸ Base, Open | dialog | `ui/Interactive rebase: Cancel while the commits are being read`<br>`rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches` | tested |

## History panel

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Select a commit | click | `ui/History: Ctrl-click drops a commit from the selection; New merge commit from the row menu`<br>`edit/no-op rewrites keep ids; the Commit menu carries the selected commit's actions` | tested |
| Add/remove a commit to the selection | Ctrl+click | `ui/History: Ctrl-click drops a commit from the selection; New merge commit from the row menu`<br>`new/several parents make a merge commit` | tested |
| Working tree / Index rows | click | `history/Working tree and Index rows`<br>`diff/side by side across files: switching, a binary file, Shift+click first, blame from the working tree; an added submodule` | tested |
| Expand/collapse a merge | click the merge node | `history/merges start collapsed; expand and collapse merged history` | tested |
| Filter field | type | `conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes`<br>`history/search by message, ID, branch and tag; no graph while filtering` | tested |
| Conflicted only | checkbox | `conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes`<br>`history/search by message, ID, branch and tag; no graph while filtering` | tested |
| Stashes | checkbox | `history/stash badges on base commits` | tested |
| Show all refs | click | `history/scope follows the side panels` | tested |
| Load more | click | `history/large history: first page, Show more, reveal, cancel` | tested |
| Scroll (tooltips wait, scroll position anchored) | mouse wheel | `history/tooltips wait until scrolling stops` | tested |
| Up/Down move the selection | key | `history/keyboard navigation`<br>`history/scroll position stays anchored on the rows in view` | tested |
| F7 / Shift+F7 next/previous conflicted commit | key | `conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes` | tested |
| N new commit on the selection | key | `new/new commit on HEAD advances the branch (toolbar, menu, keys)`<br>`new/several parents make a merge commit` | tested |
| Alt+N new detached commit | key | `new/new detached and new on another commit` | tested |
| E check out | key | `checkout/switch to a branch, detach, E key` | tested |
| Row ▸ New / New merge commit | context menu | `ui/History: Ctrl-click drops a commit from the selection; New merge commit from the row menu`<br>`new/new commit on HEAD advances the branch (toolbar, menu, keys)` | tested |
| Row ▸ New detached | context menu | `new/new detached and new on another commit` | tested |
| Row ▸ Check out ▸ <branch> / Detached HEAD | context menu | `checkout/switch to a branch, detach, E key`<br>`conflicts/checking out a conflicted commit: clean status by default, index stages when asked` | tested |
| Row ▸ Create branch... | context menu | `refs/create, check out, rename and delete branches`<br>`ui/dialogs: Escape cancels; Enter in a text field confirms when the button is enabled` | tested |
| Row ▸ Create tag... | context menu | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Row ▸ Move branch ▸ <branch> | context menu | `refs/move a branch; warning for a branch checked out elsewhere` | tested |
| Row ▸ Delete branch ▸ <branch> | context menu | `refs/create, check out, rename and delete branches` | tested |
| Row ▸ Push (branch with upstream) | context menu | `network/push: toolbar, menu, History and Branches; no upstream prefills Push to` | tested |
| Row ▸ Push (no upstream: opens Push to) | context menu | `ui/Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease` | tested |
| Row ▸ Push to... | context menu | `network/push: toolbar, menu, History and Branches; no upstream prefills Push to` | tested |
| Row ▸ Copy ▸ ID (Shift: full ID) | context menu | `history/copy ID and full description; tooltip ID` | tested |
| Row ▸ Copy ▸ Full description | context menu | `history/copy ID and full description; tooltip ID` | tested |
| Row ▸ Expand/Collapse merged history | context menu | `history/merges start collapsed; expand and collapse merged history` | tested |
| Row ▸ Interactive rebase selection... | context menu | `rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches` | tested |
| Working tree row ▸ Commit... | context menu | `commit/nothing staged: stage all tracked or the selected files` | tested |
| Working tree row ▸ Amend into HEAD... | context menu | `commit/amend content and message, message only, Amend into HEAD` | tested |
| Working tree row ▸ Discard changes... | context menu | `staging/discard all changes from the Working tree menu` | tested |
| Working tree row ▸ Stash changes... | context menu | `stash/create: message, untracked, keep index, staged only, selected files` | tested |
| Working tree row ▸ Stage all | context menu | `staging/stage all, unstage all, stage modified` | tested |
| Working tree row ▸ Unstage all | context menu | `staging/stage all, unstage all, stage modified` | tested |
| Drag commit onto commit + Shift: move after | drag | `dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser` | tested |
| Drag commit onto commit + Ctrl+Shift: move before | drag | `dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser` | tested |
| Drag commit onto commit + Ctrl: squash into | drag | `dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser` | tested |
| Drag commit onto commit + Alt: rebase onto | drag | `dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser` | tested |
| Drag commit onto commit: chooser ▸ Move before / Move after / Copy after / Squash into / Rebase onto | drag, menu | `dnd/the chooser's choices and Escape; a commit's files onto its child, HEAD or elsewhere; a commit onto itself` | tested |
| Drag commit: chooser closed by Escape or a click elsewhere | key / click | `ui/the drop chooser closes with Escape and changes nothing`<br>`dnd/the chooser's choices and Escape; a commit's files onto its child, HEAD or elsewhere; a commit onto itself` | tested |
| Drag a branch badge onto a commit (move branch) | drag | `dnd/a branch badge onto a commit moves the branch` | tested |
| Drag a commit's files onto its parent, child or HEAD | drag | `dnd/the chooser's choices and Escape; a commit's files onto its child, HEAD or elsewhere; a commit onto itself`<br>`dnd/files onto a commit: a commit's files into its parent; working tree files into any commit` | tested |
| Drag working tree files onto a commit (absorb) | drag | `dnd/files onto a commit: a commit's files into its parent; working tree files into any commit` | tested |

## Commit editing items

The same items appear in the History row menu and in Commit ▸ Selected commit; keys work on the selected History row.

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| New commit before | context menu | `edit/insert a new commit before or after one` | tested |
| New commit after | context menu | `edit/insert a new commit before or after one`<br>`edit/no-op rewrites keep ids; the Commit menu carries the selected commit's actions` | tested |
| Duplicate | context menu | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Duplicate | D | `edit/duplicate a commit (D) and a branch (Shift+D) as detached copies` | tested |
| Duplicate branch | context menu | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Duplicate branch | Shift+D | `edit/duplicate a commit (D) and a branch (Shift+D) as detached copies` | tested |
| Rebase onto... | context menu | `edit/rebase one commit, and a commit with its descendants, onto another branch`<br>`rebase-i/open as interactive rebase from the Squash and Rebase onto dialogs` | tested |
| Interactive rebase from here... | context menu | `rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches` | tested |
| Interactive rebase from here... | I | `rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches` | tested |
| Merge into HEAD... | context menu | `edit/History and Commit menus: merge a commit into HEAD, rebase HEAD onto a commit`<br>`edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile` | tested |
| Rebase HEAD onto this | context menu / Selected commit | `edit/History and Commit menus: merge a commit into HEAD, rebase HEAD onto a commit`<br>`edit/refusals: nothing to squash or move, unknown or descendant destinations, nothing redundant, already merged` | tested |
| Squash... | context menu | `edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S)`<br>`rebase-i/open as interactive rebase from the Squash and Rebase onto dialogs` | tested |
| Squash... | S | `edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S)` | tested |
| Squash descendants into this | context menu | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Squash descendants into this | Shift+S | `edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S)` | tested |
| Split... | context menu | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Split... | Alt+S | `edit/split a commit by files (Alt+S)` | tested |
| Restore from... | context menu / Selected commit | `edit/restore paths in a commit or the working tree; simplify parents`<br>`edit/dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults` | tested |
| Simplify parents | context menu | `edit/restore paths in a commit or the working tree; simplify parents`<br>`edit/refusals: nothing to squash or move, unknown or descendant destinations, nothing redundant, already merged` | tested |
| Abandon | context menu | `edit/abandon a commit (A) and a branch (Shift+A)`<br>`edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Abandon | A | `edit/abandon a commit (A) and a branch (Shift+A)` | tested |
| Abandon branch... | context menu | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Abandon branch... | Shift+A | `edit/abandon a commit (A) and a branch (Shift+A)` | tested |

## Changes panel

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Select a file (shows its diff) | click | `commit/nothing staged: stage all tracked or the selected files`<br>`stash/create: message, untracked, keep index, staged only, selected files` | tested |
| Multi-select | Ctrl+click, Shift+click, Ctrl+A | `changes/multi-select with Ctrl, Shift and Ctrl+A; keyboard navigation` | tested |
| Up/Down move the current file | key | `changes/multi-select with Ctrl, Shift and Ctrl+A; keyboard navigation` | tested |
| Space / Enter toggle staging | key | `staging/Space and Enter toggle staging` | tested |
| Double-click: open in the editor or the diff tool | double-click | `staging/double-click opens new files in the editor, others in the diff tool` | tested |
| Filter field | type | `changes/commit files, filter, compare with HEAD, header` | tested |
| Compare with HEAD (commit) | checkbox | `changes/commit files, filter, compare with HEAD, header` | tested |
| Stage all / Stage modified / Unstage all (group buttons) | click | `staging/stage all, unstage all, stage modified` | tested |
| Drag files between Staged and Unstaged | drag | `staging/drag files between Staged and Unstaged` | tested |
| File ▸ Open working-copy file | context menu | `conflicts/first-class: take a side in one region; resolve in the editor and commit on top`<br>`staging/external editor, folder and diff tools` | tested |
| File ▸ Open containing folder | context menu | `staging/external editor, folder and diff tools` | tested |
| File ▸ Copy ▸ Name / Relative path / Absolute path | context menu | `changes/file context menu: copy, patch, save patch, blame` | tested |
| File ▸ Stage | context menu | `staging/stage, unstage and discard files` | tested |
| File ▸ Unstage | context menu | `staging/stage, unstage and discard files` | tested |
| File ▸ Discard... | context menu | `staging/stage, unstage and discard files` | tested |
| File ▸ Intent to add | context menu | `staging/intent to add, delete; no Track / Untrack` | tested |
| File ▸ Resolve with merge tool | context menu | `conflicts/native: resolve with the configured merge tool`<br>`conflicts/first-class: resolve with the merge tool (stages from the regions)` | tested |
| File ▸ Take side ▸ Side N (whole file) | context menu | `conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too` | tested |
| File ▸ Take side ▸ In one region... | context menu | `conflicts/first-class: take a side in one region; resolve in the editor and commit on top` | tested |
| File ▸ Take ours | context menu | `conflicts/native merge: three-way diff, take ours, edit the message, continue`<br>`edges/plain git keeps first-class conflicts: rebase, cherry-pick, amend, merge, stash, gc, clone, push` | tested |
| File ▸ Take theirs | context menu | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| File ▸ Mark resolved | context menu | `conflicts/native: resolve by editing, mark resolved, continue`<br>`conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too` | tested |
| File ▸ Patch ▸ Copy | context menu | `changes/file context menu: copy, patch, save patch, blame` | tested |
| File ▸ Patch ▸ Save... | context menu | `changes/file context menu: copy, patch, save patch, blame` | tested |
| File ▸ Blame file | context menu | `changes/file context menu: copy, patch, save patch, blame` | tested |
| File ▸ External diff ▸ vs HEAD / vs parent | context menu | `staging/external editor, folder and diff tools` | tested |
| File (commit) ▸ Move to parent | context menu | `move/files: to the parent, to the child, to the working tree, revert` | tested |
| File (commit) ▸ Move to child | context menu | `move/files: to the parent, to the child, to the working tree, revert` | tested |
| File (commit) ▸ Move to the working tree | context menu | `move/files: to the parent, to the child, to the working tree, revert` | tested |
| File (commit) ▸ Revert | context menu | `move/files: to the parent, to the child, to the working tree, revert` | tested |
| File (stash) ▸ Apply this file | context menu | `stash/apply, pop with the index, apply one file, branch, drop, undo, clear` | tested |
| File ▸ Delete file... | context menu | `staging/intent to add, delete; no Track / Untrack` | tested |

## Diff panel

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| View: Unified / Side by side | combo | `diff/side-by-side view with syntax highlighting`<br>`diff/edge cases: GIF, BMP, JPEG and unknown images; CRLF without a final newline; light theme; side-by-side scroll sync; text menu; term views of a conflicted commit` | tested |
| Whitespace mode | combo | `diff/whitespace modes` | tested |
| Context lines | number field | `diff/unified view, context lines, expandable context` | tested |
| First-class conflict: Raw markers / Base → side N | combo | `conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too`<br>`diff/edge cases: GIF, BMP, JPEG and unknown images; CRLF without a final newline; light theme; side-by-side scroll sync; text menu; term views of a conflicted commit` | tested |
| Native conflict: Working tree / Base → ours / Base → theirs / Ours → theirs | combo | `conflicts/native merge: three-way diff, take ours, edit the message, continue` | tested |
| Compare with HEAD (this file) | checkbox | `diff/renames, compare this file with HEAD, large diffs` | tested |
| Load full diff | click | `diff/renames, compare this file with HEAD, large diffs` | tested |
| Select lines in the gutter (Shift+click extends) | click | `diff/select lines, Ctrl+C and the context menu`<br>`linestaging/hunks from the context menu in the side-by-side view` | tested |
| Select text with the mouse | drag | `diff/text is selectable with the mouse in both views` | tested |
| Ctrl+C copies the selection | key | `diff/select lines, Ctrl+C and the context menu`<br>`diff/text is selectable with the mouse in both views` | tested |
| Show 10 more unchanged lines (Shift+click: all) | click | `diff/unified view, context lines, expandable context` | tested |
| Hunk buttons: Stage / Discard / Unstage hunk | click | `linestaging/stage, discard and unstage hunks`<br>`linestaging/CRLF lines, missing final newline, new files` | tested |
| Side by side scroll stays in sync | mouse wheel | `diff/edge cases: GIF, BMP, JPEG and unknown images; CRLF without a final newline; light theme; side-by-side scroll sync; text menu; term views of a conflicted commit` | tested |
| Line menu ▸ Copy | context menu | `diff/select lines, Ctrl+C and the context menu` | tested |
| Line menu ▸ Blame file | context menu | `diff/select lines, Ctrl+C and the context menu`<br>`diff/more edges: a copied file (and blame before it), files over the text limit, an untracked image, blame of an untracked file` | tested |
| Line menu ▸ Stage line(s) / Discard line(s) | context menu | `linestaging/CRLF lines, missing final newline, new files`<br>`linestaging/randomized line staging matches the content model` | tested |
| Line menu ▸ Stage hunk(s) / Discard hunk(s) | context menu | `linestaging/hunks from the context menu in the side-by-side view` | tested |
| Line menu ▸ Unstage line(s) / Unstage hunk(s) | context menu | `linestaging/hunks from the context menu in the side-by-side view`<br>`linestaging/randomized line staging matches the content model` | tested |
| Line menu (commit) ▸ Move line(s) to parent | context menu | `move/lines: a hunk to the parent and to the active commit`<br>`move/lines of an added file, a renamed file, a CRLF file and a mode change: revert them in a commit` | tested |
| Line menu (commit) ▸ Move line(s) to child | context menu | `move/lines of an added file, a renamed file, a CRLF file and a mode change: revert them in a commit` | tested |
| Line menu (commit) ▸ Move line(s) to active commit | context menu | `move/lines: a hunk to the parent and to the active commit` | tested |
| Line menu (commit) ▸ Move line(s) to working tree | context menu | `move/lines: a hunk to the parent and to the active commit` | tested |
| Line menu (commit) ▸ Revert line(s) | context menu | `move/lines of an added file, a renamed file, a CRLF file and a mode change: revert them in a commit`<br>`move/lines: a hunk to the parent and to the active commit` | tested |

## Change information panel

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Message editor + Save message (HEAD: amend --only; other commits: reword) | type, click | `commit/reword HEAD from Change information (amend mode)`<br>`rewrite/reword a commit in the middle: descendants rebased, the rest untouched, one Undo` | tested |
| Merge message (MERGE_MSG) + Save message | type, click | `conflicts/native merge: three-way diff, take ours, edit the message, continue` | tested |
| Author ▸ Copy name / Copy email | context menu | `info/change information: message, author, committer, date, ID, parents` | tested |
| Author ▸ Edit author... | context menu | `rewrite/edit the author of any commit` | tested |
| Copy commit ID (Shift: full) | click | `info/change information: message, author, committer, date, ID, parents` | tested |
| Conflicted file → its blame | click | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Parent ID → reveal it | click | `info/change information: message, author, committer, date, ID, parents` | tested |

## Blame panel

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Back / Forward | click | `blame/filter, history, tooltips`<br>`blame/line menu: before, originating source, reveal, copy, blocks` | tested |
| Back / Forward | mouse buttons 4 and 5 | `blame/filter, history, tooltips` | tested |
| Filter field | type | `blame/filter, history, tooltips` | tested |
| Select lines (Shift+click extends) | click | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Line ▸ Blame before this change | context menu | `blame/line menu: before, originating source, reveal, copy, blocks`<br>`diff/more edges: a copied file (and blame before it), files over the text limit, an untracked image, blame of an untracked file` | tested |
| Line ▸ Show originating source | context menu | `blame/line menu: before, originating source, reveal, copy, blocks` | tested |
| Line ▸ Reveal commit | context menu | `blame/line menu: before, originating source, reveal, copy, blocks` | tested |
| Line ▸ Copy commit ID | context menu | `blame/line menu: before, originating source, reveal, copy, blocks` | tested |
| Line ▸ Select change block | context menu | `blame/line menu: before, originating source, reveal, copy, blocks` | tested |
| Line ▸ Copy change block | context menu | `blame/line menu: before, originating source, reveal, copy, blocks` | tested |

## Branches panel

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Create branch (+) | click | `refs/create, check out, rename and delete branches`<br>`undo/every everyday mutation can be undone` | tested |
| Filter field | type | `panels/branches: filter, current, upstream, reveal, copy`<br>`dnd/the chooser's choices and Escape; a commit's files onto its child, HEAD or elsewhere; a commit onto itself` | tested |
| Branch row: show/hide in History (Ctrl+click: only this one) | click | `history/scope follows the side panels` | tested |
| Remote-tracking row: show/hide in History | click | `panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation` | tested |
| Branch ▸ Reveal | context menu | `panels/branches: filter, current, upstream, reveal, copy` | tested |
| Branch ▸ Copy name | context menu | `panels/branches: filter, current, upstream, reveal, copy` | tested |
| Branch ▸ Check out | context menu | `checkout/local changes block a switch: Stash and switch`<br>`undo/in a linked worktree: its HEAD and branch are undone; the main worktree's HEAD is left to it` | tested |
| Branch ▸ Merge into HEAD... | context menu | `edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile` | tested |
| Branch ▸ Rebase HEAD onto branch | context menu | `edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile` | tested |
| Branch ▸ Interactive rebase onto... | context menu | `rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches` | tested |
| Branch ▸ Push (with upstream) | context menu | `network/push: toolbar, menu, History and Branches; no upstream prefills Push to` | tested |
| Branch ▸ Push (no upstream: opens Push to) | context menu | `ui/Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease` | tested |
| Branch ▸ Push to... | context menu | `network/push: toolbar, menu, History and Branches; no upstream prefills Push to` | tested |
| Branch ▸ Pull | context menu | `network/pull follows pull.rebase; dropdown overrides; menu and panels` | tested |
| Branch ▸ Reconcile with remote or branch... | context menu | `edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile`<br>`ui/Reconcile by rebasing onto the upstream` | tested |
| Branch ▸ Rename... | context menu | `refs/create, check out, rename and delete branches`<br>`undo/every everyday mutation can be undone` | tested |
| Branch ▸ Delete ▸ Local | context menu | `refs/create, check out, rename and delete branches`<br>`undo/every everyday mutation can be undone` | tested |
| Branch ▸ Delete ▸ On its remote | context menu | `refs/delete a branch on its remote, and everywhere` | tested |
| Branch ▸ Delete ▸ Local and all remotes | context menu | `refs/delete a branch on its remote, and everywhere` | tested |
| Branch ▸ Set upstream... | context menu | `refs/upstream: set, unset, fast-forward` | tested |
| Branch ▸ Unset upstream | context menu | `refs/upstream: set, unset, fast-forward` | tested |
| Branch ▸ Fast-forward to upstream | context menu | `refs/upstream: set, unset, fast-forward` | tested |
| Remote group / remote-tracking row ▸ remote items (as in Remotes) | context menu | `network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move` | tested |
| Remote-tracking row ▸ Reveal / Copy name | context menu | `panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation` | tested |

## Tags panel

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Create tag (+) | click | `edit/dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults`<br>`refs/tags: lightweight, annotated, delete, push, delete on remote` | tested |
| Filter field | type | `panels/tags: filter, visibility, reveal, copy`<br>`history/large history: first page, Show more, reveal, cancel` | tested |
| Tag row: show/hide in History (Ctrl+click: only this one) | click | `panels/tags: filter, visibility, reveal, copy` | tested |
| Tag ▸ Reveal | context menu | `panels/tags: filter, visibility, reveal, copy` | tested |
| Tag ▸ Copy name | context menu | `panels/tags: filter, visibility, reveal, copy` | tested |
| Tag ▸ Delete | context menu | `refs/tags: lightweight, annotated, delete, push, delete on remote` | tested |
| Tag ▸ Push tag ▸ <remote> | context menu | `refs/tags: lightweight, annotated, delete, push, delete on remote` | tested |
| Tag ▸ Delete on remote ▸ <remote> | context menu | `refs/tags: lightweight, annotated, delete, push, delete on remote` | tested |

## Worktrees panel

Phase 4 (P4-03) items are drawn disabled with a "later" tooltip and are not actions yet: Open here, Open in new window, Add..., Remove..., Lock/Unlock, Prune, Repair.

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Worktree ▸ Copy name | context menu | `panels/worktrees: main, locked, stale; copy, reveal, open` | tested |
| Worktree ▸ Copy path | context menu | `panels/worktrees: main, locked, stale; copy, reveal, open` | tested |
| Worktree ▸ Reveal HEAD | context menu | `panels/worktrees: main, locked, stale; copy, reveal, open` | tested |
| Worktree ▸ Open directory | context menu | `panels/worktrees: main, locked, stale; copy, reveal, open` | tested |

## Remotes panel

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Add remote (+) | click | `edit/dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults`<br>`refs/remotes: add, edit URL, prune on fetch, delete` | tested |
| Fetch all | click | `network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move` | tested |
| Remote ▸ Copy name | context menu | `panels/remotes: list and copy` | tested |
| Remote ▸ Fetch | context menu | `network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move` | tested |
| Remote ▸ Pull | context menu | `network/pull follows pull.rebase; dropdown overrides; menu and panels` | tested |
| Remote ▸ Prune on fetch | context menu | `refs/remotes: add, edit URL, prune on fetch, delete` | tested |
| Remote ▸ Edit URL... | context menu | `refs/remotes: add, edit URL, prune on fetch, delete` | tested |
| Remote ▸ Delete | context menu | `refs/remotes: add, edit URL, prune on fetch, delete` | tested |

## Stashes panel

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Select a stash (shows its parts) | click | `changes/stash contents: working tree, index and untracked parts`<br>`staging/double-click opens new files in the editor, others in the diff tool` | tested |
| Stash changes... | click | `panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation` | tested |
| Clear all... | click | `stash/apply, pop with the index, apply one file, branch, drop, undo, clear` | tested |
| Stash ▸ Apply | context menu | `stash/apply, pop with the index, apply one file, branch, drop, undo, clear` | tested |
| Stash ▸ Apply (restore index) | context menu | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Stash ▸ Pop | context menu | `ui/Stashes ▸ Pop applies an older stash and drops only that one`<br>`stash/a conflicting pop keeps the stash and leaves plain git conflicts` | tested |
| Stash ▸ Pop (restore index) | context menu | `stash/apply, pop with the index, apply one file, branch, drop, undo, clear` | tested |
| Stash ▸ Branch from stash... | context menu | `stash/apply, pop with the index, apply one file, branch, drop, undo, clear` | tested |
| Stash ▸ Drop... | context menu | `stash/apply, pop with the index, apply one file, branch, drop, undo, clear` | tested |

## Reflog panel

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Ref combo (HEAD, branches, stash) | combo | `panels/reflog: HEAD, branch, stash; filter; copy; reveal` | tested |
| Filter field | type | `panels/reflog: HEAD, branch, stash; filter; copy; reveal`<br>`panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation` | tested |
| Entry ▸ Copy new ID / Copy old ID | context menu | `panels/reflog: HEAD, branch, stash; filter; copy; reveal` | tested |
| Entry ▸ Reveal new commit / Reveal old commit | context menu | `panels/reflog: HEAD, branch, stash; filter; copy; reveal` | tested |
| Entry ▸ Create branch from new... | context menu | `edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch` | tested |
| Entry ▸ Create branch from old... | context menu | `refs/create a branch from a reflog entry` | tested |

## Operations panel

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Undo / Redo buttons | click | `panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation` | tested |
| Operation ▸ Restore (undo this operation) | context menu | `undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores` | tested |
| Operation ▸ Copy operation ID | context menu | `panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation` | tested |

## Interactive rebase panel

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Start / Save (remaining todo) | click | `rebase-native/conflict stop, Edit remaining todo like git rebase --edit-todo, exec after every commit, Run as git rebase`<br>`rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo` | tested |
| Cancel | click | `rebase-i/validation warnings, Cancel changes nothing, a branch moved meanwhile is refused` | tested |
| Cancel while the commits are being read | click | `ui/Interactive rebase: Cancel while the commits are being read` | tested |
| Close the panel (cancels) | window close | `rebase-i/validation warnings, Cancel changes nothing, a branch moved meanwhile is refused` | tested |
| Onto (Enter applies) | text field | `rebase-i/options: onto, update-refs, autostash, committer date`<br>`rebase-i/conflicted input: carried along like git rebase -i, resolved by rebasing onto the cause` | tested |
| Autosquash | checkbox | `rebase-i/autosquash places fixup!/squash!/amend! like git rebase -i --autosquash`<br>`rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy` | tested |
| Update refs | checkbox | `rebase-i/options: onto, update-refs, autostash, committer date`<br>`rebase-native/git rebase -i refusals and options: moved branch, git before 2.38 with update-ref, local changes and autostash, Abort` | tested |
| Autostash | checkbox | `rebase-i/options: onto, update-refs, autostash, committer date`<br>`rebase-native/git rebase -i refusals and options: moved branch, git before 2.38 with update-ref, local changes and autostash, Abort` | tested |
| Run as git rebase | checkbox | `rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine`<br>`rebase-native/conflict stop, Edit remaining todo like git rebase --edit-todo, exec after every commit, Run as git rebase` | tested |
| Exec after every commit | text field | `rebase-native/conflict stop, Edit remaining todo like git rebase --edit-todo, exec after every commit, Run as git rebase`<br>`rebase-native/typed squash messages reach git's editor, also for a squash that amends after an update-ref row; exec after every commit around followers and drops` | tested |
| Committer date | combo | `rebase-i/options: onto, update-refs, autostash, committer date` | tested |
| Becoming empty (Keep, Drop, Ask) | combo | `rebase-i/live preview: non-text conflicts that need a decision, commits that are or become empty`<br>`rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy` | tested |
| Undo / Redo buttons | click | `rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine`<br>`rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo` | tested |
| Undo / Redo | Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z | `rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine` | tested |
| Insert exec / Insert break buttons | click | `rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine` | tested |
| Newest first | checkbox | `rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine` | tested |
| Row selection (Ctrl+click, Shift+click) | click | `rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine` | tested |
| Row action (pick, reword, edit, squash, fixup, fixup -C, fixup -c, drop) | combo | `rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo`<br>`rebase-i/live preview on a worker: the newest edit wins, frames never wait, nothing is written` | tested |
| Action keys P, R, E, S, F, D | key | `rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine` | tested |
| X / B insert exec / break; Delete removes rows | key | `rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine` | tested |
| Alt+Up / Alt+Down move rows | key | `rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine` | tested |
| Drag rows to reorder | drag | `rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine` | tested |
| Exec row command | text field | `rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo`<br>`rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine` | tested |
| Message editor (reword, squash groups) | text field | `rebase-native/typed squash messages reach git's editor, also for a squash that amends after an update-ref row; exec after every commit around followers and drops`<br>`rebase-i/options: onto, update-refs, autostash, committer date` | tested |
| Preview row → selects its rows | click | `rebase-i/live preview: first-class conflicts and moving branches, the same as Start and git rebase -i` | tested |

## Notifications

| Action | Trigger | Test(s) | Status |
|---|---|---|---|
| Close (x) | click | `shell/errors open a popup; warnings are corner notifications` | tested |
| Copy message | context menu | `shell/errors open a popup; warnings are corner notifications` | tested |
