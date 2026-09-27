# Traceability matrix (phases 0-2)

Covered: 466/466 required IDs; 610/647 of the whole catalogue.

| Spec ID | Phase | Section | Description | Passing tests |
|---|---|---|---|---|
| HARNESS-SMOKE | 0 | §8.1 | ggui --smoke and a trivial --test pass | shell/auto-open argv[1], else the most recent existing repository<br>harness/smoke: welcome screen |
| HARNESS-ISOLATION | 0 | §8.1 | tests do not read user-level git config or ggui settings | harness/isolation from user config and settings |
| HARNESS-FIXTURES | 0 | §8.3 | every fixture recipe builds and passes git fsck | harness/large fixture<br>harness/transport fixtures: git daemon and ssh shim<br>harness/fixture recipes build and pass fsck |
| HARNESS-ASSERT-HELPERS | 0 | §8.3 | UI readers, repo readers, post-test fsck, git step helper, seeded randomizer | harness/assertion helpers |
| HARNESS-FAILURE-OUTPUT | 0 | §8.3 | failing test writes screenshot, app log and git command log | harness/failure output: screenshot, app log, git command log |
| APP-WELCOME-OPEN | 1 | §4.1 | Welcome: Open repository… button opens the picker | shell/open with the picker: Welcome, menu, Ctrl+O, toolbar |
| APP-WELCOME-OPEN-PATH | 1 | §4.1 | Welcome: open a typed path with the keyboard | shell/open by typed path, default layout, close from the menu |
| APP-WELCOME-RECENT-OPEN | 1 | §4.1 | Welcome: click a recent entry opens it | shell/recent repositories: Welcome list, Recent menu, switcher |
| APP-WELCOME-RECENT-DELETE | 1 | §4.1 | Welcome: Delete key forgets a recent entry | shell/recent repositories: Welcome list, Recent menu, switcher |
| APP-WELCOME-RECENT-INFO | 1 | §4.1 | Welcome: recent entries show branch, upstream, ahead/behind | shell/recent repositories: Welcome list, Recent menu, switcher |
| APP-WELCOME-PROGRESS | 1 | §4.1 | Welcome: progress while opening | shell/opening shows progress and can be cancelled |
| APP-WELCOME-CANCEL | 1 | §4.1 | Welcome: cancel while opening | shell/opening shows progress and can be cancelled |
| APP-WELCOME-TAGLINE | 1 | §4.1 | Welcome tagline in Git wording | harness/smoke: welcome screen |
| APP-AUTOOPEN-ARG | 1 | §4.1 | Auto-open argv[1] | shell/auto-open argv[1], else the most recent existing repository |
| APP-AUTOOPEN-RECENT | 1 | §4.1 | Auto-open the most recent existing repository | shell/auto-open argv[1], else the most recent existing repository |
| APP-OPEN-ERROR | 1 | §4.1 | Opening a non-repository shows an error | shell/errors open a popup; warnings are corner notifications |
| APP-OPEN-STATES | 1 | §3 | Opens normal, bare, unborn, linked worktree and SHA-256 repositories | shell/repository kinds: bare, unborn, linked worktree, SHA-256, detached<br>shell/open by typed path, default layout, close from the menu |
| APP-STATE-DETECT | 1 | §4.1 | Detects merging, rebasing (interactive/apply), cherry-picking, reverting, bisecting | shell/repository state badge |
| MENU-REPO-OPEN | 1 | §4.1 | Repository ▸ Open… (menu) | shell/open with the picker: Welcome, menu, Ctrl+O, toolbar |
| MENU-REPO-OPEN-KEY | 1 | §4.1 | Ctrl+O opens the picker | shell/open with the picker: Welcome, menu, Ctrl+O, toolbar |
| MENU-REPO-RECENT | 1 | §4.1 | Repository ▸ Recent ▸ opens an entry | shell/recent repositories: Welcome list, Recent menu, switcher |
| MENU-REPO-RECENT-FILTER | 1 | §4.1 | Recent submenu filter | shell/recent repositories: Welcome list, Recent menu, switcher |
| MENU-REPO-RECENT-INFO | 1 | §4.1 | Recent submenu shows branch, upstream, ahead/behind | shell/recent repositories: Welcome list, Recent menu, switcher |
| MENU-REPO-OPEN-WORKDIR | 1 | §4.1 | Repository ▸ Open working directory | shell/Repository menu: copy path, refresh, working directory, settings, quit |
| MENU-REPO-COPY-PATH | 1 | §4.1 | Repository ▸ Copy path | shell/Repository menu: copy path, refresh, working directory, settings, quit |
| MENU-REPO-CLOSE | 1 | §4.1 | Repository ▸ Close repository (menu) | shell/open by typed path, default layout, close from the menu |
| MENU-REPO-CLOSE-KEY | 1 | §4.1 | Ctrl+W closes the repository | shell/open with the picker: Welcome, menu, Ctrl+O, toolbar |
| MENU-REPO-REFRESH | 1 | §4.1 | Repository ▸ Refresh (menu) | shell/Repository menu: copy path, refresh, working directory, settings, quit |
| MENU-REPO-REFRESH-KEY | 1 | §4.1 | F5 refreshes | shell/Repository menu: copy path, refresh, working directory, settings, quit |
| MENU-REPO-SETTINGS | 1 | §4.1 | Repository ▸ Settings… | shell/Repository menu: copy path, refresh, working directory, settings, quit |
| MENU-REPO-QUIT | 1 | §4.1 | Repository ▸ Quit | shell/Repository menu: copy path, refresh, working directory, settings, quit |
| MENU-VIEW-TOGGLE-PANEL | 1 | §4.1 | View ▸ toggle each panel | shell/View menu: panels, next/previous changed file, reset layout |
| MENU-VIEW-NEXT-FILE | 1 | §4.1 | View ▸ Next changed file (menu) | shell/View menu: panels, next/previous changed file, reset layout |
| MENU-VIEW-NEXT-FILE-KEY | 1 | §4.1 | F6 next changed file | shell/View menu: panels, next/previous changed file, reset layout |
| MENU-VIEW-PREV-FILE | 1 | §4.1 | View ▸ Previous changed file (menu) | shell/View menu: panels, next/previous changed file, reset layout |
| MENU-VIEW-PREV-FILE-KEY | 1 | §4.1 | Shift+F6 previous changed file | shell/View menu: panels, next/previous changed file, reset layout |
| MENU-VIEW-RESET-LAYOUT | 1 | §4.1 | View ▸ Reset layout restores the default | shell/View menu: panels, next/previous changed file, reset layout |
| TB-REFRESH | 1 | §4.1 | Toolbar Refresh | shell/Repository menu: copy path, refresh, working directory, settings, quit |
| TB-REPO-SWITCH | 1 | §4.1 | Toolbar repository switcher | shell/recent repositories: Welcome list, Recent menu, switcher |
| TB-OPEN-FOLDER | 1 | §4.1 | Toolbar folder button opens the working directory (UF-28) | shell/Repository menu: copy path, refresh, working directory, settings, quit |
| TB-BRANCH | 1 | §4.1 | Toolbar shows the current branch | shell/open by typed path, default layout, close from the menu |
| TB-DETACHED | 1 | §4.1 | Toolbar shows "detached" | shell/repository kinds: bare, unborn, linked worktree, SHA-256, detached |
| TB-HEAD-PLAIN | 1 | §4.1 | Toolbar branch label and HEAD ID are plain text; clicking does nothing (UF-31) | shell/toolbar HEAD: plain text, copy short or full ID |
| TB-HEAD-COPY | 1 | §4.1 | HEAD ID context menu copies it | shell/toolbar HEAD: plain text, copy short or full ID |
| APP-ID-DIMMED | 1 | §4.1 | Full IDs show the short prefix normally and the rest dimmed (UF-14) | info/change information: message, author, committer, date, ID, parents<br>history/copy ID and full description; tooltip ID |
| APP-COPY-ID-SHIFT | 1 | §4.1 | Copy ID copies the short ID; with Shift the full ID (UF-13) | shell/toolbar HEAD: plain text, copy short or full ID<br>history/copy ID and full description; tooltip ID |
| TB-SPINNER | 1 | §4.1 | Activity spinner while working | shell/activity spinner, task tooltip and Cancel |
| TB-CANCEL | 1 | §4.1 | Activity Cancel button | shell/activity spinner, task tooltip and Cancel |
| TB-TASK-TOOLTIP | 1 | §4.1 | Background-task tooltip | shell/activity spinner, task tooltip and Cancel |
| APP-ERROR-POPUP | 1 | §4.1 | Important errors open an error popup (message, Copy message) | shell/errors open a popup; warnings are corner notifications |
| APP-ERROR-DISMISS | 1 | §4.1 | The error popup is dismissed with OK | shell/errors open a popup; warnings are corner notifications |
| APP-NOTIFY-TOAST | 1 | §4.1 | Warnings and information show as corner notifications that fade out or close | shell/errors open a popup; warnings are corner notifications |
| TB-STATE-BADGE | 1 | §4.1 | Repository-state badge | shell/repository state badge |
| LAYOUT-DEFAULT | 1 | §4.1 | Default dock layout | shell/open by typed path, default layout, close from the menu |
| SET-SCALE | 1 | §4.1 | Settings: UI scale 50–300 % persists | shell/settings persist across restarts |
| SET-THEME | 1 | §4.1 | Settings: theme dark/light persists | shell/settings persist across restarts |
| APP-WATCH-WORKTREE | 1 | §4.1 | Watcher refreshes after worktree changes | engine/watcher: plain git steps update the UI |
| APP-WATCH-GITDIR | 1 | §4.1 | Watcher refreshes after index/HEAD/refs/stash changes | engine/watcher: plain git steps update the UI |
| APP-LOG-FILE | 1 | §4.1 | GGUI_LOG_FILE receives the log | shell/auto-open argv[1], else the most recent existing repository<br>shell/settings persist across restarts |
| APP-RESPONSIVE | 1 | §3.1 | No frame > ~33 ms from repository work on the large fixture | engine/responsiveness on the large repository |
| APP-UI-THREAD-ASSERT | 1 | §3.1 | UI-thread call to libgit2/git trips the assertion | threading/UI-thread call to git trips the assertion |
| APP-CANCEL-LONG-OPS | 1 | §3.1 | History load, reveal, large diff and blame can be cancelled | history/large history: first page, Show more, reveal, cancel<br>engine/overlapping diff requests: only the newest result is shown<br>history/cancel a long history load and a reveal<br>shell/activity spinner, task tooltip and Cancel |
| HIST-GRAPH | 1 | §4.2 | Lane graph with curved edges | visual/graph lines are continuous from row to row<br>visual/screenshots of the main views<br>history/graph, rows, badges and short IDs |
| HIST-GRAPH-CONTINUOUS | 1 | §4.2 | Graph lines connect row to row at a constant row pitch | visual/graph lines are continuous from row to row |
| HIST-GRAPH-NOT-CLIPPED | 1 | §4.2 | The current commit outline is not clipped at the graph's left edge (UF-18) | visual/graph is not clipped at the left edge |
| HIST-MERGE-COLLAPSED-DEFAULT | 1 | §4.2 | Merge commits start collapsed | history/merges start collapsed; expand and collapse merged history |
| HIST-LOAD-FAST | 1 | §4.2 | First history rows of a large repository appear quickly | history/first rows of a large history appear quickly |
| UI-TEXT-BASELINE | 1 | §6 | Text in one row shares a baseline (labels, buttons, badges, icons) | visual/text shares a baseline across widgets on one line |
| UI-ICON-ALIGN | 1 | §6 | Icon glyphs are vertically centred on the text they accompany | visual/icon glyphs are vertically centred on the text |
| LAYOUT-HIDDEN-PANELS | 1 | §4.1 | Reflog, Operations and Blame are hidden in the default layout | shell/open by typed path, default layout, close from the menu |
| DIFF-SELECT-TEXT | 1 | §4.5 | Diff text is selectable (mouse) in unified and side-by-side views | diff/text is selectable with the mouse in both views |
| DIFF-EDITOR-VIEWS | 1 | §4.5 | Unified and side-by-side diffs both use the text editor widget | diff/text is selectable with the mouse in both views |
| HIST-ROW-FIELDS | 1 | §4.2 | Rows show ID prefix, subject, author, date | history/graph, rows, badges and short IDs |
| HIST-BADGES | 1 | §4.2 | Branch/tag/remote/worktree badges | history/graph, rows, badges and short IDs |
| HIST-PUBLISHED-COLOUR | 1 | §4.2 | Pushed vs unpushed colouring | history/published vs unpublished commits |
| HIST-SHORT-ID | 1 | §4.2 | Unique shortest-prefix IDs | history/graph, rows, badges and short IDs |
| HIST-WT-ROW | 1 | §4.2 | Virtual Working tree row parented on HEAD | shell/repository kinds: bare, unborn, linked worktree, SHA-256, detached<br>history/Working tree and Index rows |
| HIST-INDEX-ROW | 1 | §4.2 | Index (staged) row when something is staged | history/Working tree and Index rows |
| HIST-STASH-BADGES | 1 | §4.2 | Stash badges on base commits (toggle) | history/stash badges on base commits |
| HIST-SCOPE | 1 | §4.2 | Scope follows side-panel selection | history/scope follows the side panels |
| HIST-SEARCH | 1 | §4.2 | Search/filter by message, ID, branch, tag | history/search by message, ID, branch and tag; no graph while filtering |
| HIST-SCROLL-ANCHOR | 1 | §4.2 | Changes to History (refresh, merges, load more, Index row) keep the rows in view in place (UF-25) | history/scroll position stays anchored on the rows in view |
| HIST-TOOLTIP-SCROLL | 1 | §4.2 | Row, graph and badge tooltips wait until the list stops scrolling (UF-10) | history/tooltips wait until scrolling stops |
| HIST-FILTER-NO-GRAPH | 1 | §4.2 | The graph column is hidden while a History filter is active (UF-24) | history/search by message, ID, branch and tag; no graph while filtering |
| HIST-REVEAL | 1 | §4.2 | Reveal a commit (loads until found) | history/large history: first page, Show more, reveal, cancel |
| HIST-REVEAL-CANCEL | 1 | §4.2 | Reveal is cancellable | history/large history: first page, Show more, reveal, cancel<br>history/cancel a long history load and a reveal |
| HIST-SHOW-MORE | 1 | §4.2 | Show more for collapsed regions | history/large history: first page, Show more, reveal, cancel |
| HIST-MERGE-EXPAND | 1 | §4.2 | Expand and collapse merge history | history/merges start collapsed; expand and collapse merged history |
| HIST-KEY-UPDOWN | 1 | §4.2 | ↑/↓ navigation | history/keyboard navigation |
| HIST-CTX-COPY-ID | 1 | §4.2 | Context: Copy ▸ ID | history/copy ID and full description; tooltip ID |
| HIST-CTX-COPY-DESC | 1 | §4.2 | Context: Copy ▸ Full description | history/copy ID and full description; tooltip ID |
| CHG-FILES | 1 | §4.4 | File list for the selected commit | changes/commit files, filter, compare with HEAD, header |
| CHG-FILTER | 1 | §4.4 | Filter the file list | changes/commit files, filter, compare with HEAD, header |
| CHG-MULTISELECT-CTRL | 1 | §4.4 | Ctrl-click multi-select | changes/multi-select with Ctrl, Shift and Ctrl+A; keyboard navigation |
| CHG-MULTISELECT-SHIFT | 1 | §4.4 | Shift-click range select | changes/multi-select with Ctrl, Shift and Ctrl+A; keyboard navigation |
| CHG-SELECT-ALL | 1 | §4.4 | Ctrl+A selects all | changes/multi-select with Ctrl, Shift and Ctrl+A; keyboard navigation |
| CHG-KEY-NAV | 1 | §4.4 | Keyboard navigation | changes/multi-select with Ctrl, Shift and Ctrl+A; keyboard navigation |
| CHG-STATUS-ICONS | 1 | §4.4 | Status icons | changes/working tree groups: staged, unstaged, untracked, conflicted |
| CHG-RENAMES | 1 | §4.4 | Renames and copies | changes/working tree groups: staged, unstaged, untracked, conflicted |
| CHG-COMPARE-HEAD | 1 | §4.4 | Compare with HEAD toggle | changes/commit files, filter, compare with HEAD, header |
| CHG-HEADER-WT | 1 | §4.4 | Working tree header shows the zero ID; Compare with HEAD disabled (UF-29) | changes/commit files, filter, compare with HEAD, header |
| CHG-GROUPS | 1 | §4.4 | Staged/Unstaged/Untracked/Conflicted groups | changes/working tree groups: staged, unstaged, untracked, conflicted |
| CHG-SCANNING | 1 | §3.1 | Partial status marked scanning… | engine/partial status on a huge worktree is marked scanning |
| CHG-CTX-COPY-NAME | 1 | §4.4 | Context: Copy ▸ Name | changes/file context menu: copy, patch, save patch, blame |
| CHG-CTX-COPY-REL | 1 | §4.4 | Context: Copy ▸ Relative path | changes/file context menu: copy, patch, save patch, blame |
| CHG-CTX-COPY-ABS | 1 | §4.4 | Context: Copy ▸ Absolute path | changes/file context menu: copy, patch, save patch, blame |
| CHG-CTX-COPY-PATCH | 1 | §4.4 | Context: Patch ▸ Copy (UF-17) | changes/file context menu: copy, patch, save patch, blame |
| CHG-CTX-SAVE-PATCH | 1 | §4.4 | Context: Patch ▸ Save… (UF-17) | changes/file context menu: copy, patch, save patch, blame |
| CHG-CTX-BLAME | 1 | §4.4 | Context: Blame file | changes/file context menu: copy, patch, save patch, blame |
| INFO-MESSAGE | 1 | §4.4 | Message shown | info/change information: message, author, committer, date, ID, parents |
| INFO-AUTHOR | 1 | §4.4 | Author shown | info/change information: message, author, committer, date, ID, parents |
| INFO-COPY-NAME | 1 | §4.4 | Copy author name | info/change information: message, author, committer, date, ID, parents |
| INFO-COPY-EMAIL | 1 | §4.4 | Copy author email | info/change information: message, author, committer, date, ID, parents |
| INFO-DATE | 1 | §4.4 | Date shown | info/change information: message, author, committer, date, ID, parents |
| INFO-PUBLISHED | 1 | §4.4 | Published/lock state shown | history/published vs unpublished commits |
| INFO-COMMIT-ID-COPY | 1 | §4.4 | Commit ID copy | info/change information: message, author, committer, date, ID, parents |
| INFO-AUTHOR-PLAIN | 1 | §4.4 | Author line has no hover or click effect; its context menu stays (UF-30) | info/change information: message, author, committer, date, ID, parents |
| INFO-PARENTS-REVEAL | 1 | §4.4 | Parents list reveals on click | info/change information: message, author, committer, date, ID, parents |
| INFO-COMMITTER | 1 | §4.4 | Committer shown when it differs | info/change information: message, author, committer, date, ID, parents |
| DIFF-UNIFIED | 1 | §4.5 | Unified view | diff/unified view, context lines, expandable context |
| DIFF-SIDE-BY-SIDE | 1 | §4.5 | Side-by-side view | diff/side-by-side view with syntax highlighting |
| DIFF-SBS-CODE-ONLY | 1 | §4.5 | Side-by-side view shows only code: no hunk header lines (UF-11) | diff/side-by-side view with syntax highlighting |
| DIFF-WS-MODES | 1 | §4.5 | Whitespace: normal / ignore changes / ignore all | diff/whitespace modes |
| DIFF-CONTEXT | 1 | §4.5 | Context-line count | diff/unified view, context lines, expandable context |
| DIFF-EXPAND | 1 | §4.5 | Expand context | diff/unified view, context lines, expandable context |
| DIFF-EXPAND-SHIFT | 1 | §4.5 | Shift reveals the whole section | diff/unified view, context lines, expandable context |
| DIFF-SYNTAX | 1 | §4.5 | Syntax highlighting | diff/side-by-side view with syntax highlighting |
| DIFF-BINARY | 1 | §4.5 | Binary placeholder | diff/binary, image, submodule and mode-change placeholders |
| DIFF-IMAGE | 1 | §4.5 | Image placeholder | diff/binary, image, submodule and mode-change placeholders |
| DIFF-SUBMODULE | 1 | §4.5 | Submodule placeholder | diff/binary, image, submodule and mode-change placeholders |
| DIFF-MODE-CHANGE | 1 | §4.5 | Mode-change line | diff/binary, image, submodule and mode-change placeholders |
| DIFF-VS-HEAD | 1 | §4.5 | Compare with HEAD (this file), on the diff button row (UF-15) | diff/renames, compare this file with HEAD, large diffs |
| DIFF-COPY-KEY | 1 | §4.5 | Ctrl+C copies the selection | diff/select lines, Ctrl+C and the context menu |
| DIFF-LOAD-FULL | 1 | §4.5 | Load full diff for capped files | diff/renames, compare this file with HEAD, large diffs |
| DIFF-RENAME | 1 | §4.5 | Renames and copies in diffs | diff/renames, compare this file with HEAD, large diffs |
| DIFF-CTX-COPY | 1 | §4.5 | Context: Copy | diff/select lines, Ctrl+C and the context menu |
| DIFF-CTX-BLAME | 1 | §4.5 | Context: Blame file | diff/select lines, Ctrl+C and the context menu |
| DIFF-STASH-PARTS | 1 | §4.5 | Stash diff: working tree / index / untracked parts | changes/stash contents: working tree, index and untracked parts |
| BLAME-AT-COMMIT | 1 | §4.6 | Blame a file at a commit | blame/blame at a commit and on the working tree |
| BLAME-WORKTREE | 1 | §4.6 | Working-tree blame | blame/blame at a commit and on the working tree |
| BLAME-UNCOMMITTED | 1 | §4.6 | Uncommitted lines marked | blame/blame at a commit and on the working tree |
| BLAME-FILTER | 1 | §4.6 | Filter | blame/filter, history, tooltips |
| BLAME-BACK-FWD | 1 | §4.6 | Back/forward history (buttons) | blame/filter, history, tooltips |
| BLAME-MOUSE-BUTTONS | 1 | §4.6 | Back/forward with mouse buttons | blame/filter, history, tooltips |
| BLAME-TOOLTIP | 1 | §4.6 | Per-line tooltips | blame/filter, history, tooltips |
| BLAME-BEFORE | 1 | §4.6 | Blame before this change | blame/line menu: before, originating source, reveal, copy, blocks |
| BLAME-ORIGIN | 1 | §4.6 | Originating source | blame/line menu: before, originating source, reveal, copy, blocks |
| BLAME-REVEAL | 1 | §4.6 | Reveal commit | blame/line menu: before, originating source, reveal, copy, blocks |
| BLAME-COPY | 1 | §4.6 | Copy commit | blame/line menu: before, originating source, reveal, copy, blocks |
| BLAME-SELECT-BLOCK | 1 | §4.6 | Select a change block | blame/line menu: before, originating source, reveal, copy, blocks |
| BLAME-COPY-BLOCK | 1 | §4.6 | Copy a change block | blame/line menu: before, originating source, reveal, copy, blocks |
| BR-FILTER | 1 | §4.7 | Branches: filter | panels/branches: filter, current, upstream, reveal, copy |
| BR-TOGGLE | 1 | §4.7 | Branches: click toggles visibility in History | history/scope follows the side panels |
| BR-CTRL-ONLY | 1 | §4.7 | Branches: Ctrl-click shows only this branch | history/scope follows the side panels |
| BR-CURRENT-OUTLINE | 1 | §4.7 | Branches: current branch outlined | panels/branches: filter, current, upstream, reveal, copy |
| BR-REVEAL | 1 | §4.7 | Branches: Reveal | panels/branches: filter, current, upstream, reveal, copy |
| BR-COPY | 1 | §4.7 | Branches: Copy name | panels/branches: filter, current, upstream, reveal, copy |
| BR-UPSTREAM-INFO | 1 | §4.7 | Branches: upstream and ahead/behind shown | panels/branches: filter, current, upstream, reveal, copy |
| TAG-FILTER | 1 | §4.7 | Tags: filter | panels/tags: filter, visibility, reveal, copy |
| TAG-TOGGLE | 1 | §4.7 | Tags: visibility toggle | panels/tags: filter, visibility, reveal, copy |
| TAG-REVEAL | 1 | §4.7 | Tags: Reveal | panels/tags: filter, visibility, reveal, copy |
| TAG-COPY | 1 | §4.7 | Tags: Copy | panels/tags: filter, visibility, reveal, copy |
| TAG-LABEL-PLAIN | 1 | §4.7 | Tags: labels are the tag names, without "(annotated)" (UF-22) | panels/tags: filter, visibility, reveal, copy |
| WT-LIST | 1 | §4.7 | Worktrees: list with main/stale/locked | panels/worktrees: main, locked, stale; copy, reveal, open |
| WT-COPY-NAME | 1 | §4.7 | Worktrees: Copy name | panels/worktrees: main, locked, stale; copy, reveal, open |
| WT-COPY-PATH | 1 | §4.7 | Worktrees: Copy path | panels/worktrees: main, locked, stale; copy, reveal, open |
| WT-REVEAL-HEAD | 1 | §4.7 | Worktrees: Reveal HEAD | panels/worktrees: main, locked, stale; copy, reveal, open |
| WT-OPEN-DIR | 1 | §4.7 | Worktrees: Open directory | panels/worktrees: main, locked, stale; copy, reveal, open |
| REM-LIST | 1 | §4.7 | Remotes: list | panels/remotes: list and copy |
| REM-COPY | 1 | §4.7 | Remotes: Copy name | panels/remotes: list and copy |
| STASH-PANEL | 1 | §4.7 | Stashes panel lists entries | changes/stash contents: working tree, index and untracked parts |
| REFLOG-HEAD | 1 | §4.7 | Reflog: HEAD reflog | panels/reflog: HEAD, branch, stash; filter; copy; reveal |
| REFLOG-CHOOSER | 1 | §4.7 | Reflog: choose HEAD, a branch or stash | panels/reflog: HEAD, branch, stash; filter; copy; reveal |
| REFLOG-FILTER | 1 | §4.7 | Reflog: filter | panels/reflog: HEAD, branch, stash; filter; copy; reveal |
| REFLOG-COPY | 1 | §4.7 | Reflog: copy old/new commit | panels/reflog: HEAD, branch, stash; filter; copy; reveal |
| REFLOG-REVEAL | 1 | §4.7 | Reflog: reveal old/new commit | panels/reflog: HEAD, branch, stash; filter; copy; reveal |
| REMOTE-PUBLISHED | 1 | §4.8 | Published = reachable from a remote-tracking ref | history/published vs unpublished commits |
| STASH-LIST | 1 | §4.9 | List: index, message, base commit, date | changes/stash contents: working tree, index and untracked parts |
| STASH-INSPECT | 1 | §4.9 | Selecting a stash shows files and diff | changes/stash contents: working tree, index and untracked parts |
| PATCH-COPY | 1 | §4.11 | Copy patch | changes/file context menu: copy, patch, save patch, blame |
| PATCH-SAVE | 1 | §4.11 | Save patch… (file) | changes/file context menu: copy, patch, save patch, blame |
| PATCH-SAVE-SELECTION | 1 | §4.11 | Save patch… (selection) | changes/file context menu: copy, patch, save patch, blame |
| APP-WELCOME-INIT | 2 | §4.1 | Welcome: Initialize repository… | setup/initialize a repository from Welcome and the menu |
| APP-WELCOME-CLONE | 2 | §4.1 | Welcome: Clone repository… | network/clone from Welcome and the menu; unreachable remote fails cleanly |
| APP-PROMPT-GIT-MISSING | 2 | §4.1 | Blocking prompt when git is not on PATH | setup/git missing or too old: a blocking prompt with Retry |
| APP-PROMPT-GIT-OLD | 2 | §4.1 | Blocking prompt when git is older than 2.36 | setup/git missing or too old: a blocking prompt with Retry |
| APP-PROMPT-HOOKS | 2 | §4.1 | First-open managed hooks prompt appears | hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab |
| APP-PROMPT-GGREFS | 2 | §4.1 | Old gg refs cleanup prompt appears | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| MENU-REPO-INIT | 2 | §4.1 | Repository ▸ Initialize… | setup/initialize a repository from Welcome and the menu |
| MENU-REPO-CLONE | 2 | §4.1 | Repository ▸ Clone… | network/clone from Welcome and the menu; unreachable remote fails cleanly |
| MENU-REPO-FETCH | 2 | §4.1 | Repository ▸ Fetch | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| MENU-REPO-PULL | 2 | §4.1 | Repository ▸ Pull | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| MENU-REPO-PUSH | 2 | §4.1 | Repository ▸ Push | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| MENU-COMMIT-NEW | 2 | §4.1 | Commit ▸ New commit (menu) | new/new commit on HEAD advances the branch (toolbar, menu, keys) |
| MENU-COMMIT-NEW-KEY | 2 | §4.1 | Ctrl+N new commit | new/new commit on HEAD advances the branch (toolbar, menu, keys) |
| MENU-COMMIT-COMMIT | 2 | §4.1 | Commit ▸ Commit… | commit/nothing staged: stage all tracked or the selected files |
| MENU-COMMIT-AMEND | 2 | §4.1 | Commit ▸ Amend… | commit/amend content and message, message only, Amend into HEAD |
| MENU-COMMIT-PREV | 2 | §4.1 | Commit ▸ Move HEAD to parent | checkout/move HEAD to parent and child |
| MENU-COMMIT-NEXT | 2 | §4.1 | Commit ▸ Move HEAD to child | checkout/move HEAD to parent and child |
| MENU-EDIT-UNDO | 2 | §4.1 | Edit ▸ Undo (menu) | undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| MENU-EDIT-UNDO-KEY | 2 | §4.1 | Ctrl+Z undo | undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| MENU-EDIT-REDO | 2 | §4.1 | Edit ▸ Redo (menu) | undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| MENU-EDIT-REDO-KEY | 2 | §4.1 | Ctrl+Y redo | undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| MENU-EDIT-APPLY-PATCH | 2 | §4.1 | Edit ▸ Apply patch… | patches/apply from the clipboard or a file, to the working tree or the index |
| TB-NEW | 2 | §4.1 | Toolbar New | new/new commit on HEAD advances the branch (toolbar, menu, keys) |
| TB-COMMIT | 2 | §4.1 | Toolbar Commit/Amend | commit/commit the index from the toolbar; hooks run natively |
| TB-COMMIT-LABEL | 2 | §4.1 | Commit/Amend label follows the selection | commit/commit the index from the toolbar; hooks run natively |
| TB-NO-PREV-NEXT | 2 | §4.1 | The toolbar has no Previous / Next buttons (UF-27) | checkout/move HEAD to parent and child |
| TB-UNDO | 2 | §4.1 | Toolbar Undo | stash/apply, pop with the index, apply one file, branch, drop, undo, clear<br>undo/refusals: nothing to undo, refs moved outside the journal, local changes in the way |
| TB-REDO | 2 | §4.1 | Toolbar Redo | stash/apply, pop with the index, apply one file, branch, drop, undo, clear |
| TB-FETCH | 2 | §4.1 | Toolbar Fetch fetches all remotes | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| TB-FETCH-REMOTE | 2 | §4.1 | Fetch dropdown: a single remote | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| TB-FETCH-PRUNE | 2 | §4.1 | Fetch dropdown: Fetch and prune | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| TB-FETCH-TAGS | 2 | §4.1 | Fetch dropdown: Fetch tags | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| TB-PULL | 2 | §4.1 | Toolbar Pull follows pull.rebase/pull.ff | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| TB-PULL-BADGE | 2 | §4.1 | Pull incoming badge ↓n | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| TB-PULL-MERGE | 2 | §4.1 | Pull dropdown: merge | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| TB-PULL-REBASE | 2 | §4.1 | Pull dropdown: rebase | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| TB-PULL-FFONLY | 2 | §4.1 | Pull dropdown: fast-forward only | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| TB-PULL-DISABLED-DETACHED | 2 | §4.1 | Pull disabled with reason when detached | network/pull disabled when detached or without upstream; Stash and pull |
| TB-PULL-DISABLED-NOUPSTREAM | 2 | §4.1 | Pull disabled with reason without upstream | network/pull disabled when detached or without upstream; Stash and pull |
| TB-PULL-STASH | 2 | §4.1 | Local changes block pull: Stash and pull | network/pull disabled when detached or without upstream; Stash and pull |
| TB-PUSH | 2 | §4.1 | Toolbar Push to upstream | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| TB-PUSH-BADGE | 2 | §4.1 | Push outgoing badge ↑n | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| TB-PUSH-NOUPSTREAM | 2 | §4.1 | No upstream: Push to… with --set-upstream prefilled | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| TB-PUSH-TO | 2 | §4.1 | Push dropdown: Push to… | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| TB-PUSH-FORCE-LEASE | 2 | §4.1 | Push dropdown: Force with lease | network/rejected push: Pull then push, Force with lease; push tags |
| TB-PUSH-TAGS | 2 | §4.1 | Push dropdown: Push tags | network/rejected push: Pull then push, Force with lease; push tags |
| TB-PUSH-REFUSE-CONFLICTS | 2 | §4.1 | Push refused when range has first-class conflicts | network/push is refused when outgoing commits hold first-class conflicts |
| TB-PUSH-REJECTED-PULL | 2 | §4.1 | Rejected non-fast-forward: Pull then push | network/rejected push: Pull then push, Force with lease; push tags |
| TB-PUSH-REJECTED-FORCE | 2 | §4.1 | Rejected non-fast-forward: Force with lease… (confirm) | network/rejected push: Pull then push, Force with lease; push tags |
| TB-REMOTE-BUSY | 2 | §4.1 | Fetch/Pull/Push disabled during a conflicting mutation; browsing works | network/remote actions are disabled while a mutation runs; browsing still works |
| TB-AHEAD-BEHIND-REFRESH | 2 | §4.1 | Ahead/behind badges refresh after fetch/pull/push/ref change | setup/ahead/behind badges follow ref changes made outside ggui |
| TB-STASH | 2 | §4.1 | Toolbar Stash | stash/create: message, untracked, keep index, staged only, selected files |
| TB-POP | 2 | §4.1 | Toolbar Pop | stash/create: message, untracked, keep index, staged only, selected files |
| TB-STATE-CONTINUE | 2 | §4.1 | State badge Continue | conflicts/native merge: three-way diff, take ours, edit the message, continue |
| TB-STATE-SKIP | 2 | §4.1 | State badge Skip | conflicts/native: abort a merge, skip a rebase step |
| TB-STATE-ABORT | 2 | §4.1 | State badge Abort | conflicts/native: abort a merge, skip a rebase step |
| SET-EDITOR-USER | 2 | §4.1 | Settings: core.editor (User) | setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides |
| SET-EDITOR-REPO | 2 | §4.1 | Settings: core.editor (Repository) | setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides |
| SET-EDITOR-WORKTREE | 2 | §4.1 | Settings: core.editor (Worktree) | setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides |
| SET-MERGETOOL | 2 | §4.1 | Settings: merge.tool | setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides |
| SET-DIFFTOOL | 2 | §4.1 | Settings: diff.tool | setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides |
| SET-PULL-METHOD | 2 | §4.1 | Settings: default pull method: merge, rebase, rebase keeping merges, fast-forward only (UF-19) | setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides |
| SET-IDENTITY | 2 | §4.1 | Settings: user.name and user.email (UF-20) | setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides |
| SET-SCOPE-TABS | 2 | §4.1 | Settings: one field per option; the scope (user / repository / worktree) is a tab (UF-21) | setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides |
| SET-SCOPE-HINT | 2 | §4.1 | Settings: an unset field shows the inherited lower-scope value as a hint (UF-21) | setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides |
| SET-SCOPE-INHERIT | 2 | §4.1 | Settings: Inherit clears an override so the lower-scope value applies again (UF-21) | setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides |
| SET-COMMIT-ALL-DEFAULT | 2 | §4.1 | Settings: default when nothing is staged | commit/default for nothing staged comes from Settings |
| SET-HOOKS-TAB | 2 | §4.1 | Settings: Hooks tab install/remove/status | hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab |
| APP-EXT-EDITOR | 2 | §4.1 | Open file in core.editor | staging/external editor, folder and diff tools |
| APP-EXT-FOLDER | 2 | §4.1 | Open containing folder | staging/external editor, folder and diff tools |
| APP-EXT-DIFF-HEAD | 2 | §4.1 | External diff tool vs HEAD | staging/external editor, folder and diff tools |
| APP-EXT-DIFF-PARENT | 2 | §4.1 | External diff tool vs parent | staging/external editor, folder and diff tools |
| APP-EXT-MERGETOOL | 2 | §4.1 | Three-way merge tool | conflicts/native: resolve with the configured merge tool |
| HIST-CONFLICT-MARK | 2 | §4.2 | Conflicted commits get colour and marker | conflicts/commit with conflicts records diff3 regions; not offered for binary conflicts |
| HIST-WT-NATIVE-CONFLICT | 2 | §4.2 | Working tree with native conflicts is marked | conflicts/native merge: three-way diff, take ours, edit the message, continue |
| HIST-FILTER-CONFLICTED | 2 | §4.2 | Show only conflicted commits | conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes |
| HIST-KEY-NEXT-CONFLICT | 2 | §4.2 | Key: next conflicted commit | conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes |
| HIST-KEY-PREV-CONFLICT | 2 | §4.2 | Key: previous conflicted commit | conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes |
| HIST-KEY-N | 2 | §4.2 | N: new commit | new/new commit on HEAD advances the branch (toolbar, menu, keys) |
| HIST-KEY-ALT-N | 2 | §4.2 | Alt+N: new detached | new/new detached and new on another commit |
| HIST-KEY-E | 2 | §4.2 | E: check out | checkout/switch to a branch, detach, E key |
| HIST-CTX-NEW | 2 | §4.2 | Context: New | new/new commit on HEAD advances the branch (toolbar, menu, keys) |
| HIST-CTX-NEW-DETACHED | 2 | §4.2 | Context: New detached | new/new detached and new on another commit |
| HIST-CTX-CHECKOUT | 2 | §4.2 | Context: Check out ▸ | checkout/switch to a branch, detach, E key |
| HIST-CTX-CREATE-BRANCH | 2 | §4.2 | Context: Create branch… | refs/create, check out, rename and delete branches |
| HIST-CTX-MOVE-BRANCH | 2 | §4.2 | Context: Move branch ▸ | refs/move a branch; warning for a branch checked out elsewhere |
| HIST-CTX-DELETE-BRANCH | 2 | §4.2 | Context: Delete branch ▸ | refs/create, check out, rename and delete branches |
| HIST-CTX-PUSH | 2 | §4.2 | Context: Push | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| HIST-CTX-PUSH-TO | 2 | §4.2 | Context: Push to… | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| HIST-WT-CTX-COMMIT | 2 | §4.2 | Working tree: Commit… | commit/nothing staged: stage all tracked or the selected files |
| HIST-WT-CTX-AMEND | 2 | §4.2 | Working tree: Amend into HEAD… | commit/amend content and message, message only, Amend into HEAD |
| HIST-WT-CTX-DISCARD | 2 | §4.2 | Working tree: Discard changes… | staging/discard all changes from the Working tree menu |
| HIST-WT-CTX-STASH | 2 | §4.2 | Working tree: Stash changes… | stash/create: message, untracked, keep index, staged only, selected files |
| HIST-WT-CTX-STAGE-ALL | 2 | §4.2 | Working tree: Stage all | staging/stage all, unstage all, stage modified |
| HIST-WT-CTX-UNSTAGE-ALL | 2 | §4.2 | Working tree: Unstage all | staging/stage all, unstage all, stage modified |
| ACT-NEW | 2 | §4.3 | New commit advances the attached branch | new/new commit on HEAD advances the branch (toolbar, menu, keys) |
| ACT-NEW-DETACHED | 2 | §4.3 | New detached leaves branches alone | new/new detached and new on another commit |
| ACT-NEW-MERGE | 2 | §4.3 | New with several parents makes a merge commit | new/several parents make a merge commit |
| ACT-CHECKOUT-BRANCH | 2 | §4.3 | Check out a branch (git switch) | checkout/switch to a branch, detach, E key |
| ACT-CHECKOUT-DETACH | 2 | §4.3 | Check out a commit (detach) | checkout/switch to a branch, detach, E key |
| ACT-CHECKOUT-REFUSE | 2 | §4.3 | Refuse when local changes would be overwritten | checkout/local changes block a switch: Stash and switch |
| ACT-CHECKOUT-STASH | 2 | §4.3 | Stash and switch | checkout/local changes block a switch: Stash and switch |
| ACT-COMMIT | 2 | §4.3 | Commit… commits the index | commit/commit the index from the toolbar; hooks run natively |
| ACT-COMMIT-NOTHING-STAGED-ALL | 2 | §4.3 | Nothing staged: stage all and commit | commit/nothing staged: stage all tracked or the selected files |
| ACT-COMMIT-NOTHING-STAGED-SELECTED | 2 | §4.3 | Nothing staged: stage the selected files | commit/nothing staged: stage all tracked or the selected files |
| ACT-COMMIT-SKIP-HOOKS | 2 | §4.3 | Skip hooks (--no-verify) | commit/failing pre-commit hook goes to the banner; Skip hooks |
| ACT-COMMIT-HOOK-FAIL | 2 | §4.3 | Failing pre-commit hook shown in an error popup | commit/failing pre-commit hook goes to the banner; Skip hooks |
| ACT-AMEND | 2 | §4.3 | Amend HEAD with the index | commit/amend content and message, message only, Amend into HEAD |
| ACT-AMEND-MESSAGE | 2 | §4.3 | Amend HEAD message only | commit/amend content and message, message only, Amend into HEAD |
| ACT-DESCRIBE-HEAD | 2 | §4.3 | Save message on HEAD | commit/reword HEAD from Change information (amend mode) |
| ACT-MOVE-HEAD-PARENT | 2 | §4.3 | Move HEAD to parent | checkout/move HEAD to parent and child |
| ACT-MOVE-HEAD-CHILD | 2 | §4.3 | Move HEAD to child | checkout/move HEAD to parent and child |
| CHG-STAGE | 2 | §4.4 | Stage file/selection | staging/stage, unstage and discard files |
| CHG-UNSTAGE | 2 | §4.4 | Unstage file/selection | staging/stage, unstage and discard files |
| CHG-DISCARD | 2 | §4.4 | Discard file/selection (with confirmation for untracked) | staging/stage, unstage and discard files |
| CHG-KEY-TOGGLE-SPACE | 2 | §4.4 | Space toggles staging | staging/Space and Enter toggle staging |
| CHG-KEY-TOGGLE-ENTER | 2 | §4.4 | Enter toggles staging | staging/Space and Enter toggle staging |
| CHG-DBLCLICK-OPEN | 2 | §4.4 | Double-click opens new files in the editor, others in the diff tool against the parent (UF-16) | staging/double-click opens new files in the editor, others in the diff tool |
| CHG-STAGE-ALL | 2 | §4.4 | Stage all | staging/stage all, unstage all, stage modified |
| CHG-UNSTAGE-ALL | 2 | §4.4 | Unstage all | staging/stage all, unstage all, stage modified |
| CHG-STAGE-MODIFIED | 2 | §4.4 | Stage all modified (not untracked) | staging/stage all, unstage all, stage modified |
| CHG-INTENT-TO-ADD | 2 | §4.4 | Intent to add | staging/intent to add, delete; no Track / Untrack |
| CHG-MARK-RESOLVED | 2 | §4.4 | Mark conflict resolved (git add) | conflicts/native: resolve by editing, mark resolved, continue |
| CHG-FIRSTCLASS-CONFLICTED | 2 | §4.4 | First-class conflicted files of a checked-out commit under Conflicted | conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes |
| CHG-DRAG-STAGE | 2 | §4.4 | Drag Unstaged→Staged | staging/drag files between Staged and Unstaged |
| CHG-DRAG-UNSTAGE | 2 | §4.4 | Drag Staged→Unstaged | staging/drag files between Staged and Unstaged |
| CHG-CTX-OPEN | 2 | §4.4 | Context: Open working-copy file | staging/external editor, folder and diff tools |
| CHG-CTX-OPEN-FOLDER | 2 | §4.4 | Context: Open containing folder | staging/external editor, folder and diff tools |
| CHG-CTX-MERGETOOL | 2 | §4.4 | Context: Resolve with merge tool | conflicts/native: resolve with the configured merge tool |
| CHG-CTX-MARK-RESOLVED | 2 | §4.4 | Context: Mark resolved | conflicts/native: resolve by editing, mark resolved, continue |
| CHG-CTX-EXTDIFF-HEAD | 2 | §4.4 | Context: External diff ▸ vs HEAD | staging/external editor, folder and diff tools |
| CHG-CTX-EXTDIFF-PARENT | 2 | §4.4 | Context: External diff ▸ vs parent | staging/external editor, folder and diff tools |
| CHG-CTX-DELETE | 2 | §4.4 | Context: Delete file | staging/intent to add, delete; no Track / Untrack |
| CHG-NO-TRACK-UNTRACK | 2 | §4.4 | No Track / Untrack items in the Changes menu (UF-26) | staging/intent to add, delete; no Track / Untrack |
| INFO-SAVE-MESSAGE | 2 | §4.4 | Save message (HEAD) | commit/reword HEAD from Change information (amend mode)<br>rewrite/reword a commit in the middle: descendants rebased, the rest untouched, one Undo |
| INFO-AMEND-MODE | 2 | §4.4 | Amend mode for HEAD with a clean index | commit/reword HEAD from Change information (amend mode) |
| INFO-CONFLICTED-FILES | 2 | §4.4 | Conflicted files and side counts listed | conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes |
| DIFF-STAGE-LINES | 2 | §4.5 | Stage line(s) | linestaging/CRLF lines, missing final newline, new files<br>linestaging/randomized line staging matches the content model |
| DIFF-STAGE-HUNK | 2 | §4.5 | Stage hunk | linestaging/stage, discard and unstage hunks |
| DIFF-DISCARD-LINES | 2 | §4.5 | Discard line(s) | linestaging/CRLF lines, missing final newline, new files |
| DIFF-DISCARD-HUNK | 2 | §4.5 | Discard hunk | linestaging/stage, discard and unstage hunks |
| DIFF-UNSTAGE-LINES | 2 | §4.5 | Unstage line(s) | linestaging/randomized line staging matches the content model |
| DIFF-UNSTAGE-HUNK | 2 | §4.5 | Unstage hunk | linestaging/stage, discard and unstage hunks |
| DIFF-HUNK-MENU | 2 | §4.5 | Context menu: Stage / Discard / Unstage hunk(s) (both views) | linestaging/hunks from the context menu in the side-by-side view |
| DIFF-HUNK-BUTTONS | 2 | §4.5 | Buttons in hunk headers | linestaging/stage, discard and unstage hunks |
| DIFF-STAGING-RANDOM | 2 | §8.4 | Randomized line/hunk staging matches git apply --cached | linestaging/randomized line staging matches the content model |
| BR-REMOTE-MENU | 2 | §4.7 | Branches: a remote and its remote-tracking branches have the Remotes panel's menu (UF-12) | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| BR-CREATE | 2 | §4.7 | Branches: Create branch | refs/create, check out, rename and delete branches |
| BR-CHECKOUT | 2 | §4.7 | Branches: Check out | refs/create, check out, rename and delete branches |
| BR-PUSH | 2 | §4.7 | Branches: Push | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| BR-PUSH-TO | 2 | §4.7 | Branches: Push to… | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| BR-RENAME | 2 | §4.7 | Branches: Rename… | refs/create, check out, rename and delete branches |
| BR-DELETE-LOCAL | 2 | §4.7 | Branches: Delete ▸ Local | refs/create, check out, rename and delete branches |
| BR-DELETE-REMOTE | 2 | §4.7 | Branches: Delete ▸ on a remote | refs/delete a branch on its remote, and everywhere |
| BR-DELETE-ALL | 2 | §4.7 | Branches: Delete ▸ Local and all remotes | refs/delete a branch on its remote, and everywhere |
| BR-SET-UPSTREAM | 2 | §4.7 | Branches: Set upstream | refs/upstream: set, unset, fast-forward |
| BR-SET-UPSTREAM-FILTER | 2 | §4.7 | Set upstream: filter field for the branch list; Enter picks the first match (UF-23) | refs/upstream: set, unset, fast-forward |
| BR-UNSET-UPSTREAM | 2 | §4.7 | Branches: Unset upstream | refs/upstream: set, unset, fast-forward |
| BR-FF-UPSTREAM | 2 | §4.7 | Branches: Fast-forward to upstream | refs/upstream: set, unset, fast-forward |
| BR-PULL | 2 | §4.7 | Branches: Pull (current branch) | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| BR-MOVE | 2 | §4.7 | Move branch to the selected commit | refs/move a branch; warning for a branch checked out elsewhere |
| BR-MOVE-WORKTREE-WARN | 2 | §4.7 | Warn when moving a branch checked out elsewhere | refs/move a branch; warning for a branch checked out elsewhere |
| TAG-CREATE | 2 | §4.7 | Tags: Create (lightweight) | refs/tags: lightweight, annotated, delete, push, delete on remote |
| TAG-ANNOTATED | 2 | §4.7 | Tags: Create annotated with message | refs/tags: lightweight, annotated, delete, push, delete on remote |
| TAG-DELETE | 2 | §4.7 | Tags: Delete | refs/tags: lightweight, annotated, delete, push, delete on remote |
| TAG-PUSH | 2 | §4.7 | Tags: Push tag | refs/tags: lightweight, annotated, delete, push, delete on remote |
| TAG-DELETE-REMOTE | 2 | §4.7 | Tags: Delete a remote tag | refs/tags: lightweight, annotated, delete, push, delete on remote |
| REM-ADD | 2 | §4.7 | Remotes: Add remote | refs/remotes: add, edit URL, prune on fetch, delete |
| REM-DELETE | 2 | §4.7 | Remotes: Delete | refs/remotes: add, edit URL, prune on fetch, delete |
| REM-EDIT-URL | 2 | §4.7 | Remotes: Edit URL | refs/remotes: add, edit URL, prune on fetch, delete |
| REM-PRUNE-ON-FETCH | 2 | §4.7 | Remotes: prune on fetch option | refs/remotes: add, edit URL, prune on fetch, delete |
| REM-FETCH | 2 | §4.7 | Remotes: Fetch | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| REM-FETCH-ALL | 2 | §4.7 | Remotes: Fetch all | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| REM-PULL | 2 | §4.7 | Remotes: Pull | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| REFLOG-BRANCH | 2 | §4.7 | Reflog: create a branch from old/new | refs/create a branch from a reflog entry |
| UNDO-ALL-MUTATIONS | 2 | §5 U1 | Every everyday (Phase 2) mutation is undone by Undo | undo/every everyday mutation can be undone |
| OPS-LIST | 2 | §4.7 | Operations: journal entries listed | undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| OPS-SOURCE-LABEL | 2 | §4.7 | Operations: source label (ggui, git-gg, git <command>) | undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| OPS-RESTORE | 2 | §4.7 | Operations: Restore | undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| OPS-NO-HOOKS-NOTE | 2 | §4.7 | Operations: note when managed hooks are missing | undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| REMOTE-CLONE | 2 | §4.8 | Clone with progress | network/clone from Welcome and the menu; unreachable remote fails cleanly |
| REMOTE-CLONE-CANCEL | 2 | §4.8 | Clone cancel cleans up | network/cancel a clone: no directory left behind |
| REMOTE-CLONE-FAIL | 2 | §4.8 | Clone from an unreachable remote fails cleanly | network/clone from Welcome and the menu; unreachable remote fails cleanly |
| REMOTE-FETCH-ONE | 2 | §4.8 | Fetch one remote | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| REMOTE-FETCH-ALL | 2 | §4.8 | Fetch all remotes | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| REMOTE-FETCH-NO-FF | 2 | §4.8 | Fetch changes only remote-tracking refs | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| REMOTE-FETCH-FAIL | 2 | §4.8 | Fetch network failure reported | network/fetch from an unreachable remote reports the failure |
| REMOTE-PUSH | 2 | §4.8 | Push a branch | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| REMOTE-PUSH-SET-UPSTREAM | 2 | §4.8 | Push --set-upstream for a new branch | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| REMOTE-PUSH-TAGS | 2 | §4.8 | Push tags | network/rejected push: Pull then push, Force with lease; push tags |
| REMOTE-PUSH-DELETE-BRANCH | 2 | §4.8 | Delete a remote branch | refs/delete a branch on its remote, and everywhere |
| REMOTE-PROGRESS | 2 | §4.8 | Transfer progress shown | network/remote actions are disabled while a mutation runs; browsing still works |
| REMOTE-ASKPASS | 2 | §4.8 | Askpass prompt succeeds | network/askpass: answer and cancel a credentials prompt |
| REMOTE-ASKPASS-CANCEL | 2 | §4.8 | Askpass cancel aborts cleanly | network/askpass: answer and cancel a credentials prompt |
| REMOTE-PULL-CONFIG | 2 | §4.8 | Pull follows pull.rebase/pull.ff | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| REMOTE-PULL-OVERRIDE | 2 | §4.8 | Pull per-action override | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| STASH-CREATE | 2 | §4.9 | Create with message | stash/create: message, untracked, keep index, staged only, selected files |
| STASH-CREATE-KEEP-INDEX | 2 | §4.9 | Create: keep index | stash/create: message, untracked, keep index, staged only, selected files |
| STASH-CREATE-UNTRACKED | 2 | §4.9 | Create: include untracked | stash/create: message, untracked, keep index, staged only, selected files |
| STASH-CREATE-STAGED | 2 | §4.9 | Create: staged only | stash/create: message, untracked, keep index, staged only, selected files |
| STASH-CREATE-PATHS | 2 | §4.9 | Create: selected paths only | stash/create: message, untracked, keep index, staged only, selected files |
| STASH-APPLY | 2 | §4.9 | Apply | stash/apply, pop with the index, apply one file, branch, drop, undo, clear |
| STASH-POP | 2 | §4.9 | Pop | stash/apply, pop with the index, apply one file, branch, drop, undo, clear |
| STASH-POP-INDEX | 2 | §4.9 | Apply/Pop restoring the index | stash/apply, pop with the index, apply one file, branch, drop, undo, clear |
| STASH-APPLY-CONFLICT | 2 | §4.9 | Apply conflict ends in native conflict state | stash/a conflicting pop keeps the stash and leaves plain git conflicts |
| STASH-POP-KEEP-ON-CONFLICT | 2 | §4.9 | Pop keeps the stash on conflict | stash/a conflicting pop keeps the stash and leaves plain git conflicts |
| STASH-DROP | 2 | §4.9 | Drop with confirmation | stash/apply, pop with the index, apply one file, branch, drop, undo, clear |
| STASH-CLEAR | 2 | §4.9 | Clear all | stash/apply, pop with the index, apply one file, branch, drop, undo, clear |
| STASH-BRANCH | 2 | §4.9 | Branch from stash | stash/apply, pop with the index, apply one file, branch, drop, undo, clear |
| STASH-APPLY-FILE | 2 | §4.9 | Apply a single file from a stash | stash/apply, pop with the index, apply one file, branch, drop, undo, clear |
| STASH-SWITCH-HELPER | 2 | §4.9 | Stash and switch helper | checkout/local changes block a switch: Stash and switch |
| STASH-PULL-HELPER | 2 | §4.9 | Stash and pull helper | network/pull disabled when detached or without upstream; Stash and pull |
| STASH-UNDO-DROP | 2 | §4.9 | Undo brings a dropped stash back | stash/apply, pop with the index, apply one file, branch, drop, undo, clear |
| CONF-PARSE-DIFF3 | 2 | §4.10 | Two-sided diff3 regions detected | conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes<br>conflicts/marker grammar edge cases (docs/spec/conflict-markers.md) |
| CONF-PARSE-NWAY | 2 | §4.10 | N-sided extended regions detected | conflicts/marker parsing: N sides, marker length, malformed, opt-out, disposable cache<br>conflicts/marker grammar edge cases (docs/spec/conflict-markers.md) |
| CONF-MARKER-LENGTH | 2 | §4.10 | Marker length ≥ 7, honours conflict-marker-size, opening fixes length | conflicts/marker parsing: N sides, marker length, malformed, opt-out, disposable cache<br>conflicts/marker grammar edge cases (docs/spec/conflict-markers.md) |
| CONF-WELLFORMED-ONLY | 2 | §4.10 | Malformed/partial markers are text | conflicts/marker parsing: N sides, marker length, malformed, opt-out, disposable cache<br>conflicts/marker grammar edge cases (docs/spec/conflict-markers.md) |
| CONF-OPTOUT | 2 | §4.10 | gg-conflicts=false opt-out | conflicts/marker parsing: N sides, marker length, malformed, opt-out, disposable cache |
| CONF-CACHE-DISPOSABLE | 2 | §4.10 | Deleting .git/gg changes nothing reported | conflicts/marker parsing: N sides, marker length, malformed, opt-out, disposable cache |
| CONF-DISPLAY-HISTORY | 2 | §4.10 | History marks conflicted commits (incl. descendants keeping a conflict) | conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes |
| CONF-DISPLAY-INFO | 2 | §4.10 | Change information lists conflicted files and side counts | conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes |
| CONF-PUSH-REFUSE | 2 | §4.10 | ggui refuses to push conflicted commits (lists commits/files, Reveal) | network/push is refused when outgoing commits hold first-class conflicts |
| CONF-NATIVE-DETECT | 2 | §4.10 | Native merge/rebase/cherry-pick/revert/bisect detected | shell/repository state badge |
| CONF-NATIVE-STAGES | 2 | §4.10 | Conflicted files from index stages 1–3 | changes/working tree groups: staged, unstaged, untracked, conflicted |
| CONF-NATIVE-MERGETOOL | 2 | §4.10 | Native: resolve with merge tool | conflicts/native: resolve with the configured merge tool |
| CONF-NATIVE-TAKE-SIDE | 2 | §4.10 | Native: take ours/theirs | conflicts/native merge: three-way diff, take ours, edit the message, continue |
| CONF-NATIVE-MARK-RESOLVED | 2 | §4.10 | Native: mark resolved | conflicts/native: resolve by editing, mark resolved, continue |
| CONF-NATIVE-3WAY-DIFF | 2 | §4.10 | Native: three-way diff | conflicts/native merge: three-way diff, take ours, edit the message, continue |
| CONF-NATIVE-CONTINUE | 2 | §4.10 | Native: Continue | conflicts/native merge: three-way diff, take ours, edit the message, continue |
| CONF-NATIVE-SKIP | 2 | §4.10 | Native: Skip | conflicts/native: abort a merge, skip a rebase step |
| CONF-NATIVE-ABORT | 2 | §4.10 | Native: Abort | conflicts/native: abort a merge, skip a rebase step |
| CONF-MERGE-MSG | 2 | §4.10 | Show/edit MERGE_MSG while in progress | conflicts/native merge: three-way diff, take ours, edit the message, continue |
| CONF-COMMIT-WITH-CONFLICTS | 2 | §4.10 | Commit with conflicts writes diff3 regions and finishes | conflicts/commit with conflicts records diff3 regions; not offered for binary conflicts |
| CONF-COMMIT-WITH-CONFLICTS-BINARY-REFUSE | 2 | §4.10 | Commit with conflicts refused with a binary conflict | conflicts/commit with conflicts records diff3 regions; not offered for binary conflicts |
| PATCH-APPLY-CLIPBOARD | 2 | §4.11 | Apply patch from clipboard | patches/apply from the clipboard or a file, to the working tree or the index |
| PATCH-APPLY-FILE | 2 | §4.11 | Apply patch from file | patches/apply from the clipboard or a file, to the working tree or the index |
| PATCH-APPLY-INDEX | 2 | §4.11 | Apply to index | patches/apply from the clipboard or a file, to the working tree or the index |
| PATCH-APPLY-WORKTREE | 2 | §4.11 | Apply to working tree | patches/apply from the clipboard or a file, to the working tree or the index |
| PATCH-APPLY-FAIL | 2 | §4.11 | Patch that fails to apply is reported | patches/a patch that does not apply is reported and changes nothing |
| HOOK-USER-NATIVE | 2 | §4.12 | User hooks run natively through git | commit/commit the index from the toolbar; hooks run natively |
| HOOK-FAIL-POPUP | 2 | §4.12 | Failing blocking hook aborts; output in an error popup | commit/failing pre-commit hook goes to the banner; Skip hooks |
| HOOK-SKIP | 2 | §4.12 | Skip hooks checkbox | commit/failing pre-commit hook goes to the banner; Skip hooks |
| HOOK-PROMPT-INSTALL | 2 | §4.12 | First-open prompt: Install | hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab |
| HOOK-PROMPT-NOT-NOW | 2 | §4.12 | First-open prompt: Not now | hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab |
| HOOK-PROMPT-NEVER | 2 | §4.12 | First-open prompt: Never | hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab |
| HOOK-SETTINGS | 2 | §4.12 | Settings Hooks tab | hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab |
| HOOK-CLI-INSTALL | 2 | §4.12 | git gg hooks install | hooks/git gg hooks install/status/uninstall with config-defined hooks |
| HOOK-CLI-UNINSTALL | 2 | §4.12 | git gg hooks uninstall | hooks/git gg hooks install/status/uninstall with config-defined hooks |
| HOOK-CLI-STATUS | 2 | §4.12 | git gg hooks status | hooks/git gg hooks install/status/uninstall with config-defined hooks |
| HOOK-REFTX | 2 | §4.12 | reference-transaction appends to the journal | hooks/plain git commands are journaled one operation each and Undo restores them |
| HOOK-GROUPING | 2 | §4.12 | One git command = one operation | hooks/plain git commands are journaled one operation each and Undo restores them |
| HOOK-POST-CONTEXT | 2 | §4.12 | post-* hooks add context | hooks/plain git commands are journaled one operation each and Undo restores them |
| HOOK-CHAIN | 2 | §4.12 | Existing hooks chained | hooks/wrapper scripts chain existing hooks (exit status kept) and uninstall byte-exact |
| HOOK-CHAIN-EXIT | 2 | §4.12 | Chained hook exit status respected | hooks/wrapper scripts chain existing hooks (exit status kept) and uninstall byte-exact |
| HOOK-CONFIG-DEFINED | 2 | §4.12 | Config-defined hooks when git supports them | hooks/git gg hooks install/status/uninstall with config-defined hooks |
| HOOK-WRAPPER | 2 | §4.12 | Wrapper scripts in the active hooks dir otherwise | hooks/wrapper scripts chain existing hooks (exit status kept) and uninstall byte-exact |
| HOOK-WORKTREE | 2 | §4.12 | Works in linked worktrees | hooks/hooks work in linked worktrees |
| HOOK-UNINSTALL-EXACT | 2 | §4.12 | Uninstall restores previous hooks byte-exact | hooks/wrapper scripts chain existing hooks (exit status kept) and uninstall byte-exact |
| HOOK-LOOP-GUARD | 2 | §4.12 | GG_OPERATION joins the open operation | hooks/plain git commands are journaled one operation each and Undo restores them |
| HOOK-MISSING-SILENT | 2 | §4.12 | git-gg missing: wrapper does nothing | hooks/without git-gg on PATH the hooks do nothing, pre-push warns |
| HOOK-MISSING-PREPUSH-WARN | 2 | §4.12 | git-gg missing: pre-push warns | hooks/without git-gg on PATH the hooks do nothing, pre-push warns |
| HOOK-JOURNAL-CORRUPT | 2 | §4.12 | Corrupt journal skipped, not fatal | undo/a corrupt journal line is skipped, not fatal |
| HOOK-FAST | 2 | §4.12 | Fetch of thousands of refs stays fast | hooks/a fetch of thousands of refs stays fast with the hooks |
| CLI-NEW | 2 | §6 | git gg new | cli/git gg new: on HEAD, with a message, detached, merge |
| CLI-NEW-MSG | 2 | §6 | git gg new -m | cli/git gg new: on HEAD, with a message, detached, merge |
| CLI-NEW-DETACH | 2 | §6 | git gg new --detach | cli/git gg new: on HEAD, with a message, detached, merge |
| CLI-NEW-MERGE | 2 | §6 | git gg new PARENT PARENT (merge) | cli/git gg new: on HEAD, with a message, detached, merge |
| CLI-UNDO | 2 | §6 | git gg undo | cli/git gg undo, redo and op log |
| CLI-REDO | 2 | §6 | git gg redo | cli/git gg undo, redo and op log |
| CLI-OP-LOG | 2 | §6 | git gg op log | cli/git gg undo, redo and op log |
| CLI-CONFLICTS | 2 | §6 | git gg conflicts (exit 1 when any) | cli/git gg conflicts, help and exit codes<br>conflicts/marker grammar edge cases (docs/spec/conflict-markers.md) |
| CLI-UI | 2 | §6 | git gg ui | cli/git gg ui starts ggui on the repository |
| CLI-HELP | 2 | §6 | git gg help / --help | cli/git gg conflicts, help and exit codes |
| CLI-EXIT-CODES | 2 | §6 | Git-style exit codes and stderr | cli/git gg conflicts, help and exit codes |
| GGREFS-DETECT | 2 | §5 C3 | Detect refs/gg/* on open | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| GGREFS-LIST | 2 | §5 C3 | List commits kept alive only by refs/gg/* | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| GGREFS-BRANCH | 2 | §5 C3 | Create a branch for a listed commit | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| GGREFS-BACKUP | 2 | §5 C3 | Keep commits via a backup branch | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| GGREFS-DELETE | 2 | §5 C3 | Delete refs with one update-ref --stdin | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| GGREFS-UNDO | 2 | §5 C3 | Cleanup is undoable | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| GGREFS-IGNORE | 2 | §5 C3 | Ignore is remembered per repository | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| FAIL-LOCKED-REF | 2 | §8.4 | Locked ref makes the mutation fail cleanly | undo/refusals: nothing to undo, refs moved outside the journal, local changes in the way |
| FAIL-NETWORK | 2 | §8.4 | Unreachable remote | network/clone from Welcome and the menu; unreachable remote fails cleanly |
| FAIL-CANCEL | 2 | §8.4 | Cancelled network operation leaves a plain-git state | network/cancel a clone: no directory left behind |
| MENU-COMMIT-ACTIONS | 3 | §4.1 | Commit menu carries the selected-commit actions | edit/no-op rewrites keep ids; the Commit menu carries the selected commit's actions |
| HIST-KEY-D | 3 | §4.2 | D: duplicate commit | edit/duplicate a commit (D) and a branch (Shift+D) as detached copies |
| HIST-KEY-SHIFT-D | 3 | §4.2 | Shift+D: duplicate branch | edit/duplicate a commit (D) and a branch (Shift+D) as detached copies |
| HIST-KEY-S | 3 | §4.2 | S: squash | edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S) |
| HIST-KEY-SHIFT-S | 3 | §4.2 | Shift+S: squash with descendants | edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S) |
| HIST-KEY-ALT-S | 3 | §4.2 | Alt+S: split | edit/split a commit by files (Alt+S) |
| HIST-KEY-A | 3 | §4.2 | A: drop | edit/abandon a commit (A) and a branch (Shift+A) |
| HIST-KEY-SHIFT-A | 3 | §4.2 | Shift+A: drop branch | edit/abandon a commit (A) and a branch (Shift+A) |
| HIST-KEY-I | 3 | §4.2 | I: interactive rebase from here | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| HIST-DND-MOVE-BEFORE | 3 | §4.2 | Drag commit→commit: Move before | dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser |
| HIST-DND-MOVE-AFTER | 3 | §4.2 | Drag commit→commit: Move after | dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser |
| HIST-DND-SQUASH | 3 | §4.2 | Drag commit→commit: Squash | dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser |
| HIST-DND-REBASE | 3 | §4.2 | Drag commit→commit: Rebase | dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser |
| HIST-DND-CHOOSER | 3 | §4.2 | Drop without modifier shows chooser | dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser |
| HIST-DND-BRANCH | 3 | §4.2 | Drag branch badge→commit moves branch | dnd/a branch badge onto a commit moves the branch |
| HIST-DND-FILES | 3 | §4.2 | Drag files→commit moves changes | dnd/files onto a commit: a commit's files into its parent; working tree files into any commit |
| HIST-PUBLISHED-WARN | 3 | §4.2 | Warn before rewriting published history | edit/rebase one commit, and a commit with its descendants, onto another branch |
| ACT-NEW-INSERT-BEFORE | 3 | §4.3 | Insert new commit before (rebases descendants) | edit/insert a new commit before or after one |
| ACT-NEW-INSERT-AFTER | 3 | §4.3 | Insert new commit after (rebases descendants) | edit/insert a new commit before or after one |
| ACT-DESCRIBE-ANY | 3 | §4.3 | Reword any commit (descendants rebased) | rewrite/reword a commit in the middle: descendants rebased, the rest untouched, one Undo |
| ACT-EDIT-AUTHOR | 3 | §4.3 | Edit author of any commit | rewrite/edit the author of any commit |
| REWRITE-INVARIANTS | 3 | §8.4 | Rewrite keeps ancestors/unrelated refs, rebases descendants with their trees | rewrite/reword a commit in the middle: descendants rebased, the rest untouched, one Undo |
| REWRITE-POST-REWRITE | 3 | §4.12 | post-rewrite runs with the old→new mapping | rewrite/reword a commit in the middle: descendants rebased, the rest untouched, one Undo |
| REWRITE-UNDO | 3 | §5 U1 | One journal operation per rewrite; Undo restores exactly | rewrite/reword a commit in the middle: descendants rebased, the rest untouched, one Undo |
| REWRITE-PUBLISHED-WARN | 3 | §4.3 | Rewriting published commits asks first; remote-tracking refs never move | rewrite/published history asks first; a locked ref leaves everything untouched |
| REWRITE-FAIL-UNTOUCHED | 3 | §8.4 | A failing rewrite leaves refs, HEAD, index and working tree untouched | rewrite/published history asks first; a locked ref leaves everything untouched |
| ACT-DUPLICATE-COMMIT | 3 | §4.3 | Duplicate a commit onto its parent (detached copy) | edit/duplicate a commit (D) and a branch (Shift+D) as detached copies |
| ACT-DUPLICATE-BRANCH | 3 | §4.3 | Duplicate a branch range | edit/duplicate a commit (D) and a branch (Shift+D) as detached copies |
| ACT-REBASE-COMMIT | 3 | §4.3 | Rebase one commit onto a destination | edit/rebase one commit, and a commit with its descendants, onto another branch |
| ACT-REBASE-BRANCH | 3 | §4.3 | Rebase the whole branch onto a destination | edit/rebase one commit, and a commit with its descendants, onto another branch |
| ACT-IREBASE | 3 | §4.3 | Interactive rebase… opens the todo editor | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| ACT-SQUASH-PARENT | 3 | §4.3 | Squash into parent | edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S) |
| ACT-SQUASH-TARGET | 3 | §4.3 | Squash into a chosen target | edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S) |
| ACT-SQUASH-DESCENDANTS | 3 | §4.3 | Squash with descendants | edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S) |
| ACT-SPLIT | 3 | §4.3 | Split a commit by selected files | edit/split a commit by files (Alt+S) |
| ACT-RESTORE-COMMIT | 3 | §4.3 | Restore paths in a commit from another commit | edit/restore paths in a commit or the working tree; simplify parents |
| ACT-RESTORE-WORKTREE | 3 | §4.3 | Restore the working tree from a commit | edit/restore paths in a commit or the working tree; simplify parents |
| ACT-ABANDON | 3 | §4.3 | Abandon a commit (descendants rebased) | edit/abandon a commit (A) and a branch (Shift+A) |
| ACT-ABANDON-BRANCH | 3 | §4.3 | Abandon a branch | edit/abandon a commit (A) and a branch (Shift+A) |
| ACT-ABANDON-REMOTE | 3 | §4.3 | Abandon branch also deletes the remote branch | edit/abandon a commit (A) and a branch (Shift+A) |
| ACT-SIMPLIFY-PARENTS | 3 | §4.3 | Remove redundant merge parents | edit/restore paths in a commit or the working tree; simplify parents |
| ACT-REORDER | 3 | §4.3 | Move a commit before/after another | edit/reorder: move a commit before another, and copy one |
| ACT-REORDER-COPY | 3 | §4.3 | Copy a commit before/after another | edit/reorder: move a commit before another, and copy one |
| ACT-MOVE-CHANGES-PARENT | 3 | §4.3 | Move files/hunks/lines to the parent | move/files: to the parent, to the child, to the working tree, revert |
| ACT-MOVE-CHANGES-CHILD | 3 | §4.3 | Move files/hunks/lines to the child | move/files: to the parent, to the child, to the working tree, revert |
| ACT-MOVE-CHANGES-WORKTREE | 3 | §4.3 | Move changes to the working tree (uncommit) | move/files: to the parent, to the child, to the working tree, revert |
| ACT-MERGE-INTO-HEAD | 3 | §4.3 | Merge into HEAD in memory | edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile |
| ACT-MERGE-NATIVE | 3 | §4.3 | Merge using native git merge | edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile |
| ACT-REBASE-HEAD-ONTO | 3 | §4.3 | Rebase HEAD onto branch | edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile |
| ACT-RECONCILE | 3 | §4.3 | Reconcile a diverged branch with its upstream | edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile |
| ACT-REWRITE-TEXT-CONFLICT | 3 | §4.3 | Rewrites continue with first-class text conflicts | edit/text conflicts become first-class and never stop a rewrite; a later rewrite resolves them |
| ACT-REWRITE-COMPLETION-MSG | 3 | §4.3 | Completion message lists newly conflicted commits | edit/text conflicts become first-class and never stop a rewrite; a later rewrite resolves them |
| ACT-REWRITE-AUTO-RESOLVE | 3 | §4.3 | A later rewrite resolves a conflict automatically | edit/text conflicts become first-class and never stop a rewrite; a later rewrite resolves them |
| ACT-REWRITE-INVARIANTS | 3 | §8.4 | Ancestors/unrelated unchanged, no-op keeps IDs, failure changes nothing, Undo exact | edit/text conflicts become first-class and never stop a rewrite; a later rewrite resolves them |
| CHG-FIRSTCLASS-RESOLVE-AMEND | 3 | §4.4 | Resolve, stage and amend clears the conflict | conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too |
| CHG-CTX-REVERT | 3 | §4.4 | Context: Revert | move/files: to the parent, to the child, to the working tree, revert |
| CHG-CTX-MOVE-PARENT | 3 | §4.4 | Context: Move to parent | move/files: to the parent, to the child, to the working tree, revert |
| CHG-CTX-MOVE-CHILD | 3 | §4.4 | Context: Move to child | move/files: to the parent, to the child, to the working tree, revert |
| INFO-EDIT-AUTHOR | 3 | §4.4 | Edit author | rewrite/edit the author of any commit |
| DIFF-CTX-MOVE-PARENT | 3 | §4.5 | Context: Move line(s)/hunk to parent | move/lines: a hunk to the parent and to the active commit |
| DIFF-CTX-MOVE-CHILD | 3 | §4.5 | Context: Move line(s)/hunk to child | move/lines: a hunk to the parent and to the active commit |
| DIFF-CTX-MOVE-ACTIVE | 3 | §4.5 | Context: Move line(s)/hunk to active commit | move/lines: a hunk to the parent and to the active commit |
| DIFF-CTX-MOVE-WORKTREE | 3 | §4.5 | Context: Move line(s)/hunk to working tree | move/lines: a hunk to the parent and to the active commit |
| DIFF-CTX-REVERT | 3 | §4.5 | Context: Revert line/hunk | move/lines: a hunk to the parent and to the active commit |
| DIFF-TERM-VIEW | 3 | §4.10 | Per-term conflict view (base → side N) | conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too |
| BR-MERGE-INTO-HEAD | 3 | §4.7 | Branches: Merge into HEAD | edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile |
| BR-REBASE-HEAD-ONTO | 3 | §4.7 | Branches: Rebase HEAD onto branch | edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile |
| BR-RECONCILE | 3 | §4.7 | Branches: Reconcile with remote/branch… | edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile |
| BR-IREBASE-ONTO | 3 | §4.7 | Branches: Interactive rebase onto… | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| CONF-EDGE-NOEOL | 3 | §4.10 | Missing final newline round-trips | edges/round trips through first-class conflicts are byte-exact: no newline, CRLF, empty side, marker-like text |
| CONF-EDGE-CRLF | 3 | §4.10 | CRLF vs LF sides round-trip | edges/round trips through first-class conflicts are byte-exact: no newline, CRLF, empty side, marker-like text |
| CONF-EDGE-EMPTY | 3 | §4.10 | Empty sides round-trip | edges/round trips through first-class conflicts are byte-exact: no newline, CRLF, empty side, marker-like text |
| CONF-EDGE-MARKERLIKE | 3 | §4.10 | Marker-like content round-trips (longer markers) | edges/round trips through first-class conflicts are byte-exact: no newline, CRLF, empty side, marker-like text |
| CONF-NWAY-MERGE | 3 | §4.10 | N-way term merge and simplification | edges/N-way: merging two conflicted lines gives three sides; merging again simplifies |
| CONF-NO-NESTING | 3 | §4.10 | No nested markers after cancellation sequences | edges/randomized reorders of N changes to one place always come back exact<br>edit/text conflicts become first-class and never stop a rewrite; a later rewrite resolves them |
| CONF-AUTO-RESOLVE | 3 | §4.10 | Reordering back / dropping the cause resolves | edit/text conflicts become first-class and never stop a rewrite; a later rewrite resolves them |
| CONF-PREFLIGHT-BINARY | 3 | §4.10 | Pre-flight: binary conflict | preflight/every non-text conflict kind asks for a decision, then the rewrite goes through |
| CONF-PREFLIGHT-MODIFY-DELETE | 3 | §4.10 | Pre-flight: modify/delete | preflight/every non-text conflict kind asks for a decision, then the rewrite goes through |
| CONF-PREFLIGHT-TYPE | 3 | §4.10 | Pre-flight: type / file-directory / symlink | preflight/every non-text conflict kind asks for a decision, then the rewrite goes through |
| CONF-PREFLIGHT-SUBMODULE | 3 | §4.10 | Pre-flight: submodule | preflight/every non-text conflict kind asks for a decision, then the rewrite goes through |
| CONF-PREFLIGHT-MODE | 3 | §4.10 | Pre-flight: mode conflict | preflight/every non-text conflict kind asks for a decision, then the rewrite goes through |
| CONF-PREFLIGHT-RENAME | 3 | §4.10 | Pre-flight: rename/rename, rename/delete | preflight/every non-text conflict kind asks for a decision, then the rewrite goes through |
| CONF-PREFLIGHT-FILTER | 3 | §4.10 | Pre-flight: filtered (LFS) text | preflight/every non-text conflict kind asks for a decision, then the rewrite goes through |
| CONF-PREFLIGHT-OPTOUT | 3 | §4.10 | Pre-flight: opt-out file | preflight/every non-text conflict kind asks for a decision, then the rewrite goes through |
| CONF-PREFLIGHT-DISK-FILE | 3 | §4.10 | Pre-flight: choose a file from disk | preflight/every non-text conflict kind asks for a decision, then the rewrite goes through |
| CONF-PREFLIGHT-STEP | 3 | §4.10 | Pre-flight steps through commits in order | preflight/conflicts are listed per commit in order; Cancel leaves .git byte-identical |
| CONF-PREFLIGHT-CANCEL | 3 | §4.10 | Pre-flight Cancel leaves the repository byte-identical | preflight/conflicts are listed per commit in order; Cancel leaves .git byte-identical |
| CONF-CHECKOUT-CLEAN | 3 | §4.10 | Checking out a conflicted commit: git status clean, markers in files | conflicts/checking out a conflicted commit: clean status by default, index stages when asked |
| CONF-CHECKOUT-EXPAND-STAGES | 3 | §4.10 | Setting: expand to index stages on checkout | conflicts/checking out a conflicted commit: clean status by default, index stages when asked |
| CONF-RESOLVE-MERGETOOL | 3 | §4.10 | First-class: resolve with merge tool | conflicts/first-class: resolve with the merge tool (stages from the regions) |
| CONF-RESOLVE-TAKE-SIDE | 3 | §4.10 | First-class: take side N (region / whole file) | conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too |
| CONF-RESOLVE-EDITOR | 3 | §4.10 | First-class: edit in editor | conflicts/first-class: take a side in one region; resolve in the editor and commit on top |
| CONF-MARK-RESOLVED-REFUSE | 3 | §4.10 | Mark resolved refused while regions remain | conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too |
| CONF-RESOLVE-AMEND | 3 | §4.10 | Resolve then amend: descendants resolve too | conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too |
| CONF-RESOLVE-NEW-COMMIT | 3 | §4.10 | Resolve then new commit on top | conflicts/first-class: take a side in one region; resolve in the editor and commit on top |
| CONF-TRANSPARENCY | 3 | §4.10 | Plain git rebase/cherry-pick/amend/merge/stash/gc/clone/push keep conflicts | edges/plain git keeps first-class conflicts: rebase, cherry-pick, amend, merge, stash, gc, clone, push |
| CONF-GIT-MERGE-GGUI-REGIONS | 3 | §4.10 | Plain git merging a file with ggui regions | edges/plain git keeps first-class conflicts: rebase, cherry-pick, amend, merge, stash, gc, clone, push |
| CONF-PREPUSH-HOOK | 3 | §4.10 | Managed pre-push refuses conflicted commits for plain git push | hooks/managed pre-push refuses plain git pushes of conflicted commits |
| CONF-NATIVE-IREBASE-EDIT-TODO | 3 | §4.10 | Stopped interactive rebase: Edit remaining todo | — |
| CONF-NATIVE-AMEND-CONTINUE | 3 | §4.10 | Stopped interactive rebase: Amend and continue | — |
| CONF-NATIVE-PROGRESS | 3 | §4.10 | Stopped interactive rebase: progress view | — |
| HOOK-REWRITE-RUN | 3 | §4.12 | pre-rebase/post-rewrite/post-checkout run via git hook run for rewrites | rewrite/pre-rebase can veto a rebase; post-checkout runs when HEAD moves |
| HOOK-PREPUSH | 3 | §4.12 | pre-push refuses conflicted commits | hooks/managed pre-push refuses plain git pushes of conflicted commits |
| IR-ENTRY-HISTORY-KEY | 3 | §4.13 | History: I key | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| IR-ENTRY-HISTORY-MENU | 3 | §4.13 | History: Interactive rebase from here… | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| IR-ENTRY-SELECTION | 3 | §4.13 | Interactive rebase selection… | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| IR-ENTRY-BRANCH | 3 | §4.13 | Branches: Interactive rebase onto… | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| IR-ENTRY-COMMIT-MENU | 3 | §4.13 | Commit menu: Interactive rebase… (asks for base) | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| IR-ENTRY-STOPPED | 3 | §4.13 | Stopped native rebase: Edit remaining todo | — |
| IR-OPEN-AS | 3 | §4.13 | Open as interactive rebase… from single actions | rebase-i/open as interactive rebase from the Squash and Rebase onto dialogs |
| IR-ROWS | 3 | §4.13 | Rows: action, short ID, subject, author, date, branch badges | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| IR-NEWEST-FIRST | 3 | §4.13 | Newest-first display toggle | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-ACT-PICK | 3 | §4.13 | pick | rebase-i/a detached HEAD follows the rebase; update-ref moves a branch<br>rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-ACT-REWORD | 3 | §4.13 | reword | rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo |
| IR-ACT-EDIT | 3 | §4.13 | edit | — |
| IR-ACT-SQUASH | 3 | §4.13 | squash | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-ACT-FIXUP | 3 | §4.13 | fixup | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-ACT-FIXUP-C | 3 | §4.13 | fixup -C / fixup -c | rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo |
| IR-ACT-DROP | 3 | §4.13 | drop | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-ACT-EXEC | 3 | §4.13 | exec | — |
| IR-ACT-BREAK | 3 | §4.13 | break | — |
| IR-ACT-UPDATE-REF | 3 | §4.13 | update-ref | rebase-i/a detached HEAD follows the rebase; update-ref moves a branch<br>rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo |
| IR-DRAG | 3 | §4.13 | Drag rows to reorder | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-KEY-ALT-UPDOWN | 3 | §4.13 | Alt+↑/↓ reorder | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-KEY-LETTERS | 3 | §4.13 | p/r/e/s/f/d/x/b set the action | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-MULTISELECT | 3 | §4.13 | Multi-select changes several rows | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-INSERT-EXEC-BREAK | 3 | §4.13 | Insert exec/break lines | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-UNDO-REDO | 3 | §4.13 | Undo/redo inside the editor | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-MSG-REWORD | 3 | §4.13 | Inline message editor for reword | rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo |
| IR-MSG-SQUASH | 3 | §4.13 | Combined squash message prefilled like Git | rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo |
| IR-OPT-ONTO | 3 | §4.13 | Option: onto | rebase-i/options: onto, update-refs, autostash, committer date |
| IR-OPT-AUTOSQUASH | 3 | §4.13 | Option: --autosquash | rebase-i/autosquash places fixup!/squash!/amend! like git rebase -i --autosquash |
| IR-OPT-UPDATE-REFS | 3 | §4.13 | Option: --update-refs (default on) | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-OPT-AUTOSTASH | 3 | §4.13 | Option: --autostash | rebase-i/options: onto, update-refs, autostash, committer date |
| IR-OPT-EXEC-EACH | 3 | §4.13 | Option: exec after every commit | — |
| IR-OPT-COMMITTER-DATE | 3 | §4.13 | Option: committer date keep/now | rebase-i/options: onto, update-refs, autostash, committer date |
| IR-PREVIEW | 3 | §4.13 | Live preview graph | rebase-i/live preview: first-class conflicts and moving branches, the same as Start and git rebase -i<br>rebase-i/live preview on a worker: the newest edit wins, frames never wait, nothing is written |
| IR-PREVIEW-CONFLICTS | 3 | §4.13 | Preview shows first-class and non-text conflicts | rebase-i/live preview: first-class conflicts and moving branches, the same as Start and git rebase -i<br>rebase-i/live preview: non-text conflicts that need a decision, commits that are or become empty |
| IR-PREVIEW-EMPTY | 3 | §4.13 | Preview shows empty commits | rebase-i/live preview: non-text conflicts that need a decision, commits that are or become empty |
| IR-PREVIEW-BRANCHES | 3 | §4.13 | Preview shows moving branches | rebase-i/a detached HEAD follows the rebase; update-ref moves a branch<br>rebase-i/live preview: first-class conflicts and moving branches, the same as Start and git rebase -i |
| IR-PREVIEW-LATEST | 3 | §4.13 | Preview updates as the list is edited: the newest edit wins, older previews are dropped | rebase-i/live preview on a worker: the newest edit wins, frames never wait, nothing is written |
| IR-PREVIEW-NO-WRITE | 3 | §4.13, §3.1 | Preview computed in memory on a worker: nothing written, no frame waits | rebase-i/live preview on a worker: the newest edit wins, frames never wait, nothing is written |
| IR-VALIDATE-FIRST | 3 | §4.13 | First row cannot be squash/fixup | rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo |
| IR-VALIDATE-DROP-BRANCH | 3 | §4.13 | Warn when dropping a branch's only commits | rebase-i/validation warnings, Cancel changes nothing, a branch moved meanwhile is refused |
| IR-VALIDATE-PUBLISHED | 3 | §4.13 | Published-commit warning | rebase-i/validation warnings, Cancel changes nothing, a branch moved meanwhile is refused |
| IR-CONFLICTED-INPUT | 3 | §4.13 | Rebasing conflicted commits carries terms along | — |
| IR-ENGINE-MEMORY | 3 | §4.13 | In-memory engine (one update-ref transaction) | — |
| IR-ENGINE-NATIVE | 3 | §4.13 | Native engine for edit/break/exec | — |
| IR-ENGINE-USER-CHOICE | 3 | §4.13 | Run as git rebase (user choice) | — |
| IR-ENGINE-SHOWN | 3 | §4.13 | Engine and reason shown in the editor | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-MEMORY-ONE-UNDO | 3 | §4.13 | In-memory rebase = one Undo | rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo |
| IR-MEMORY-CANCEL | 3 | §4.13 | Cancel before apply changes nothing | rebase-i/validation warnings, Cancel changes nothing, a branch moved meanwhile is refused<br>preflight/conflicts are listed per commit in order; Cancel leaves .git byte-identical |
| IR-TIP-MOVED | 3 | §4.13 | Start refuses when the branch moved since the list was read | rebase-i/validation warnings, Cancel changes nothing, a branch moved meanwhile is refused |
| IR-NATIVE-STOP-EDIT | 3 | §4.13 | Native stop: edit | — |
| IR-NATIVE-STOP-BREAK | 3 | §4.13 | Native stop: break | — |
| IR-NATIVE-STOP-EXEC | 3 | §4.13 | Native stop: failing exec | — |
| IR-NATIVE-STOP-CONFLICT | 3 | §4.13 | Native stop: conflict | — |
| IR-NATIVE-TERMINAL-FOLLOW | 3 | §4.13 | Follow a rebase finished in a terminal | — |
| IR-NATIVE-UNDO | 3 | §4.13 | Native rebase = one journal operation | — |
| IR-PLAIN-DETECT | 3 | §4.13 | Detect plain git rebase -i | — |
| IR-PLAIN-EDIT-TODO | 3 | §4.13 | Edit remaining todo writes git-rebase-todo like --edit-todo | — |
| IR-DIFFERENTIAL | 3 | §8.4 | Randomized differential test vs git rebase -i | — |
| IR-AUTOSQUASH-ORDER | 3 | §8.4 | Autosquash order matches git | rebase-i/autosquash places fixup!/squash!/amend! like git rebase -i --autosquash |
| CLI-NEW-BEFORE | 3 | §6 | git gg new --before | cli/git gg new --before/--after inserts and rebases the descendants |
| CLI-NEW-AFTER | 3 | §6 | git gg new --after | cli/git gg new --before/--after inserts and rebases the descendants |
| CLI-SEQ-EDITOR | 3 | §6 | git gg sequence-editor (internal) | — |
| APP-OPEN-WORKTREE-WINDOW | 4 | §4.1 | Open a linked worktree in a new window | — |
| WT-ADD | 4 | §4.7 | Worktrees: Add… | — |
| WT-REMOVE | 4 | §4.7 | Worktrees: Remove… | — |
| WT-OPEN-HERE | 4 | §4.7 | Worktrees: Open here | — |
| WT-OPEN-WINDOW | 4 | §4.7 | Worktrees: Open in new window | — |
| WT-LOCK | 4 | §4.7 | Worktrees: Lock | — |
| WT-UNLOCK | 4 | §4.7 | Worktrees: Unlock | — |
| WT-PRUNE | 4 | §4.7 | Worktrees: Prune | — |
| WT-REPAIR | 4 | §4.7 | Worktrees: Repair | — |
| WT-JOURNAL | 4 | §4.7 | Per-worktree journal (HEAD) semantics | — |
| IR-ACT-LABEL | 4 | §4.13 | label (--rebase-merges) | — |
| IR-ACT-RESET | 4 | §4.13 | reset (--rebase-merges) | — |
| IR-ACT-MERGE | 4 | §4.13 | merge (--rebase-merges) | — |
| IR-OPT-REBASE-MERGES | 4 | §4.13 | Option: --rebase-merges | — |
| IR-SEQ-EDITOR | 4 | §4.13 | git gg sequence-editor as sequence.editor | — |
