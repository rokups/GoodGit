# Traceability matrix (phases 0-4)

Covered: 682/682 required IDs; 682/682 of the whole catalogue.

| Spec ID | Phase | Section | Description | Passing tests |
|---|---|---|---|---|
| HARNESS-SMOKE | 0 | §8.1 | ggui --smoke and a trivial --test pass | harness/smoke: welcome screen<br>shell/auto-open argv[1], else the most recent existing repository<br>shell/command line: --list-tests, --headless, unknown options and a second path are reported |
| HARNESS-ISOLATION | 0 | §8.1 | tests do not read user-level git config or ggui settings | harness/isolation from user config and settings |
| HARNESS-FIXTURES | 0 | §8.3 | every fixture recipe builds and passes git fsck | harness/fixture recipes build and pass fsck<br>harness/large fixture<br>harness/transport fixtures: git daemon and ssh shim |
| HARNESS-ASSERT-HELPERS | 0 | §8.3 | UI readers, repo readers, post-test fsck, git step helper, seeded randomizer | harness/assertion helpers |
| HARNESS-FAILURE-OUTPUT | 0 | §8.3 | failing test writes screenshot, app log and git command log | harness/failure output: screenshot, app log, git command log |
| APP-WELCOME-OPEN | 1 | §4.1 | Welcome: Open repository… button opens the picker | shell/open with the picker: Welcome, menu, Ctrl+O, toolbar |
| APP-WELCOME-OPEN-PATH | 1 | §4.1 | Welcome: open a typed path with the keyboard | setup/git versions ggui reads: newer major, vendor suffix, no number; a typed path with a trailing slash; copying a notice<br>shell/open by typed path, default layout, close from the menu |
| APP-DROP-FOLDER | 1 | §4.1 | A folder dropped on the window opens as a repository (UF-48) | shell/folders dropped on the window: the first opens, the repositories among them join the recent list |
| APP-DROP-FOLDERS | 1 | §4.1 | Several folders dropped: the repositories among them join the recent list, the first opens (UF-49) | shell/folders dropped on the window: the first opens, the repositories among them join the recent list |
| APP-WELCOME-RECENT-OPEN | 1 | §4.1 | Welcome: click a recent entry opens it | shell/recent repositories: Welcome list, Recent menu, switcher |
| APP-WELCOME-RECENT-DELETE | 1 | §4.1 | Welcome: Delete key forgets a recent entry | shell/recent repositories: Welcome list, Recent menu, switcher |
| APP-WELCOME-RECENT-INFO | 1 | §4.1 | Welcome: recent entries show branch, upstream, ahead/behind | shell/recent repositories whose state changed: upstream gone, unborn branch with an upstream, no longer a repository<br>shell/recent repositories: Welcome list, Recent menu, switcher |
| APP-WELCOME-PROGRESS | 1 | §4.1 | Welcome: progress while opening | shell/opening shows progress and can be cancelled |
| APP-WELCOME-CANCEL | 1 | §4.1 | Welcome: cancel while opening | shell/opening shows progress and can be cancelled |
| APP-WELCOME-TAGLINE | 1 | §4.1 | Welcome tagline in Git wording | harness/smoke: welcome screen |
| APP-AUTOOPEN-ARG | 1 | §4.1 | Auto-open argv[1] | shell/auto-open argv[1], else the most recent existing repository<br>shell/command line: --list-tests, --headless, unknown options and a second path are reported |
| APP-AUTOOPEN-RECENT | 1 | §4.1 | Auto-open the most recent existing repository | shell/auto-open argv[1], else the most recent existing repository |
| APP-OPEN-ERROR | 1 | §4.1 | Opening a non-repository shows an error | shell/errors open a popup; warnings are corner notifications |
| APP-OPEN-STATES | 1 | §3 | Opens normal, bare, unborn, linked worktree and SHA-256 repositories | rewrite/in a bare repository, and at the root: reword, abandon the root commit<br>shell/open by typed path, default layout, close from the menu<br>shell/repository kinds: bare, unborn, linked worktree, SHA-256, detached<br>shell/unusual repository states: sequences between commits, detached rebase, odd remotes, tags, stash and worktrees |
| APP-STATE-DETECT | 1 | §4.1 | Detects merging, rebasing (interactive/apply), cherry-picking, reverting, bisecting | shell/repository state badge<br>shell/unusual repository states: sequences between commits, detached rebase, odd remotes, tags, stash and worktrees |
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
| MENU-VIEW-TOGGLE-PANEL | 1 | §4.1 | View ▸ toggle each panel | shell/View menu: panels, next/previous changed file, reset layout<br>ui/View menu: every panel hides and shows again; the choice is saved |
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
| TB-HEAD-PLAIN | 1 | §4.1 | Toolbar branch label and HEAD ID are plain text; clicking does nothing (UF-31) | shell/toolbar HEAD: plain text, copy short or full ID<br>shell/toolbar details: force with lease, push tags, HEAD tooltip, a merge from the selection, a detached rebase's progress |
| TB-HEAD-COPY | 1 | §4.1 | HEAD ID context menu copies it | shell/toolbar HEAD: plain text, copy short or full ID |
| APP-ID-DIMMED | 1 | §4.1 | Full IDs show the short prefix normally and the rest dimmed (UF-14) | history/copy ID and full description; tooltip ID<br>info/change information: message, author, committer, date, ID, parents |
| APP-COPY-ID-SHIFT | 1 | §4.1 | Copy ID copies the short ID; with Shift the full ID (UF-13) | history/copy ID and full description; tooltip ID<br>shell/toolbar HEAD: plain text, copy short or full ID |
| TB-SPINNER | 1 | §4.1 | Activity spinner while working | shell/activity spinner, task tooltip and Cancel |
| TB-CANCEL | 1 | §4.1 | Activity Cancel button | shell/activity spinner, task tooltip and Cancel |
| TB-TASK-TOOLTIP | 1 | §4.1 | Background-task tooltip | shell/activity spinner, task tooltip and Cancel |
| APP-ERROR-POPUP | 1 | §4.1 | Important errors open an error popup (message, Copy message) | setup/git versions ggui reads: newer major, vendor suffix, no number; a typed path with a trailing slash; copying a notice<br>shell/errors open a popup; warnings are corner notifications |
| APP-ERROR-DISMISS | 1 | §4.1 | The error popup is dismissed with OK | shell/errors open a popup; warnings are corner notifications |
| APP-NOTIFY-TOAST | 1 | §4.1 | Warnings and information show as corner notifications that fade out or close | shell/errors open a popup; warnings are corner notifications |
| TB-STATE-BADGE | 1 | §4.1 | Repository-state badge | conflicts/toolbar for other operations: abort a revert and an apply-backend rebase, skip and reset a bisect; merge tool on a first-class conflict<br>shell/repository state badge<br>shell/unusual repository states: sequences between commits, detached rebase, odd remotes, tags, stash and worktrees |
| LAYOUT-DEFAULT | 1 | §4.1 | Default dock layout | shell/open by typed path, default layout, close from the menu |
| SET-SCALE | 1 | §4.1 | Settings: UI scale 50–300 % persists | shell/settings files from elsewhere: wrong types, not an object, not JSON; out-of-range values are clamped<br>shell/settings persist across restarts |
| SET-THEME | 1 | §4.1 | Settings: theme dark/light persists | shell/settings files from elsewhere: wrong types, not an object, not JSON; out-of-range values are clamped<br>shell/settings persist across restarts |
| APP-WATCH-WORKTREE | 1 | §4.1 | Watcher refreshes after worktree changes | engine/watcher: plain git steps update the UI |
| APP-WATCH-GITDIR | 1 | §4.1 | Watcher refreshes after index/HEAD/refs/stash changes | engine/watcher: plain git steps update the UI |
| APP-LOG-FILE | 1 | §4.1 | GGUI_LOG_FILE receives the log | shell/auto-open argv[1], else the most recent existing repository<br>shell/settings files from elsewhere: wrong types, not an object, not JSON; out-of-range values are clamped<br>shell/settings persist across restarts |
| APP-RESPONSIVE | 1 | §3.1 | No frame > ~33 ms from repository work on the large fixture | engine/responsiveness on the large repository |
| APP-UI-THREAD-ASSERT | 1 | §3.1 | UI-thread call to libgit2/git trips the assertion | threading/UI-thread call to git trips the assertion |
| APP-CANCEL-LONG-OPS | 1 | §3.1 | History load, reveal, large diff and blame can be cancelled | engine/overlapping diff requests: only the newest result is shown<br>history/cancel a long history load and a reveal<br>history/large history: first page, Show more, reveal, cancel<br>shell/activity spinner, task tooltip and Cancel |
| HIST-GRAPH | 1 | §4.2 | Lane graph with curved edges | history/graph, rows, badges and short IDs<br>visual/graph lines are continuous from row to row<br>visual/screenshots of the main views |
| HIST-GRAPH-CONTINUOUS | 1 | §4.2 | Graph lines connect row to row at a constant row pitch | visual/graph lines are continuous from row to row |
| HIST-GRAPH-NOT-CLIPPED | 1 | §4.2 | The current commit outline is not clipped at the graph's left edge (UF-18) | visual/graph is not clipped at the left edge |
| HIST-MERGE-COLLAPSED-DEFAULT | 1 | §4.2 | Merge commits start collapsed | history/merges start collapsed; expand and collapse merged history |
| HIST-MERGE-NOTHING-HIDDEN | 1 | §4.2 | A merge whose collapse would hide no commits offers no Collapse/Expand (UF-38) | history/a merge offers collapse only when collapsing hides commits |
| HIST-MERGE-TOGGLE-STABLE | 1 | §4.2 | Expanding or collapsing a merge keeps the previous rows until the reloaded list replaces them (UF-47) | history/expanding or collapsing a merge keeps the whole list in view while History reloads |
| HIST-LOAD-FAST | 1 | §4.2 | First history rows of a large repository appear quickly | history/first rows of a large history appear quickly |
| UI-TEXT-BASELINE | 1 | §6 | Text in one row shares a baseline (labels, buttons, badges, icons) | visual/text shares a baseline across widgets on one line |
| UI-ICON-ALIGN | 1 | §6 | Icon glyphs are vertically centred on the text they accompany | visual/icon glyphs are vertically centred on the text |
| UI-CONTRAST | 1 | §6 | Text colours keep a readable contrast against their backgrounds in both themes; badge fills against their text (UF-50) | visual/readable colours: text keeps its contrast in the dark and the light theme |
| LAYOUT-HIDDEN-PANELS | 1 | §4.1 | Reflog, Operations and Blame are hidden in the default layout | shell/open by typed path, default layout, close from the menu |
| LAYOUT-TAB-ORDER | 1 | §4.1 | Default layout: Remotes, Stashes, Worktrees tabs in that order (UF-42) | shell/open by typed path, default layout, close from the menu |
| DIFF-SELECT-TEXT | 1 | §4.5 | Diff text is selectable (mouse) in unified and side-by-side views | diff/text is selectable with the mouse in both views |
| DIFF-EDITOR-VIEWS | 1 | §4.5 | Unified and side-by-side diffs both use the text editor widget | diff/text is selectable with the mouse in both views |
| HIST-ROW-FIELDS | 1 | §4.2 | Rows show ID prefix, subject, author, date | history/graph, rows, badges and short IDs |
| HIST-BADGES | 1 | §4.2 | Branch/tag/remote/worktree badges | history/graph, rows, badges and short IDs |
| HIST-PUBLISHED-COLOUR | 1 | §4.2 | Pushed vs unpushed colouring | history/published vs unpublished commits |
| HIST-SHORT-ID | 1 | §4.2 | Unique shortest-prefix IDs | history/graph, rows, badges and short IDs |
| HIST-WT-ROW | 1 | §4.2 | Virtual Working tree row parented on HEAD | history/Working tree and Index rows<br>shell/repository kinds: bare, unborn, linked worktree, SHA-256, detached |
| HIST-INDEX-ROW | 1 | §4.2 | Index (staged) row when something is staged | history/Working tree and Index rows |
| HIST-STASH-BADGES | 1 | §4.2 | Stash badges on base commits (toggle) | history/stash badges on base commits |
| HIST-SCOPE | 1 | §4.2 | Scope follows side-panel selection | history/scope follows the side panels: the eye icon toggles, Ctrl-click shows only one, a hidden branch loses its badge, show and hide all |
| HIST-SEARCH | 1 | §4.2 | Search/filter by message, ID, branch, tag | history/search by message, ID, branch and tag; no graph while filtering |
| HIST-SCROLL-ANCHOR | 1 | §4.2 | Changes to History (refresh, merges, load more, Index row) keep the rows in view in place (UF-25) | history/scroll position stays anchored on the rows in view |
| HIST-TOOLTIP-SCROLL | 1 | §4.2 | Row, graph and badge tooltips wait until the list stops scrolling (UF-10) | history/tooltips wait until scrolling stops |
| HIST-FILTER-NO-GRAPH | 1 | §4.2 | The graph column is hidden while a History filter is active (UF-24) | history/search by message, ID, branch and tag; no graph while filtering |
| HIST-REVEAL | 1 | §4.2 | Reveal a commit (loads until found) | history/large history: first page, Show more, reveal, cancel |
| HIST-REVEAL-CANCEL | 1 | §4.2 | Reveal is cancellable | history/cancel a long history load and a reveal<br>history/large history: first page, Show more, reveal, cancel |
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
| CHG-COMPARE-HEAD | 1 | §4.4 | Compare a commit with HEAD | changes/commit files, filter, compare with HEAD, header |
| CHG-COMPARE-WITH | 1 | §4.4 | "Compare with" field: HEAD, a revision or Work Tree (any case, trimmed); its menu fills in HEAD or Work Tree; an unknown revision is reported (UF-41) | changes/commit files, filter, compare with HEAD, header |
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
| DIFF-SIDE-BY-SIDE | 1 | §4.5 | Side-by-side view | diff/edge cases: GIF, BMP, JPEG and unknown images; CRLF without a final newline; light theme; side-by-side scroll sync; text menu; term views of a conflicted commit<br>diff/side by side across files: switching, a binary file, Shift+click first, blame from the working tree; an added submodule<br>diff/side-by-side view with syntax highlighting |
| DIFF-SBS-CODE-ONLY | 1 | §4.5 | Side-by-side view shows only code: no hunk header lines (UF-11) | diff/side-by-side view with syntax highlighting |
| DIFF-WS-MODES | 1 | §4.5 | Whitespace: normal / ignore changes / ignore all, each labelled "Whitespace: ..." (UF-40) | diff/whitespace modes |
| DIFF-CONTEXT | 1 | §4.5 | Context-line count | diff/unified view, context lines, expandable context |
| DIFF-EXPAND | 1 | §4.5 | Expand context | diff/unified view, context lines, expandable context |
| DIFF-EXPAND-SIDES | 1 | §4.5 | A gap's expander has two halves: more lines below the hunk above, or above the hunk below; the first and last gap only the one that applies (UF-39) | diff/unified view, context lines, expandable context |
| DIFF-EXPAND-SHIFT | 1 | §4.5 | Shift reveals the whole section | diff/unified view, context lines, expandable context |
| DIFF-SYNTAX | 1 | §4.5 | Syntax highlighting | diff/side-by-side view with syntax highlighting |
| DIFF-BINARY | 1 | §4.5 | Binary placeholder | diff/binary, image, submodule and mode-change placeholders<br>diff/side by side across files: switching, a binary file, Shift+click first, blame from the working tree; an added submodule |
| DIFF-IMAGE | 1 | §4.5 | Image placeholder | diff/binary, image, submodule and mode-change placeholders<br>diff/edge cases: GIF, BMP, JPEG and unknown images; CRLF without a final newline; light theme; side-by-side scroll sync; text menu; term views of a conflicted commit<br>diff/more edges: a copied file (and blame before it), files over the text limit, an untracked image, blame of an untracked file |
| DIFF-SUBMODULE | 1 | §4.5 | Submodule placeholder | diff/binary, image, submodule and mode-change placeholders<br>diff/side by side across files: switching, a binary file, Shift+click first, blame from the working tree; an added submodule |
| DIFF-MODE-CHANGE | 1 | §4.5 | Mode-change line | diff/binary, image, submodule and mode-change placeholders |
| DIFF-VS-HEAD | 1 | §4.5 | Compare this file with HEAD, on the diff button row (UF-15) | diff/renames, compare this file with HEAD or the working tree, large diffs |
| DIFF-COMPARE-WITH | 1 | §4.5 | "Compare with" for this file: a revision or Work Tree, from the field or its menu; Clear (UF-41) | diff/renames, compare this file with HEAD or the working tree, large diffs |
| DIFF-COPY-KEY | 1 | §4.5 | Ctrl+C copies the selection | diff/select lines, Ctrl+C and the context menu |
| DIFF-LOAD-FULL | 1 | §4.5 | Load full diff for capped files | diff/more edges: a copied file (and blame before it), files over the text limit, an untracked image, blame of an untracked file<br>diff/renames, compare this file with HEAD or the working tree, large diffs |
| DIFF-RENAME | 1 | §4.5 | Renames and copies in diffs | diff/more edges: a copied file (and blame before it), files over the text limit, an untracked image, blame of an untracked file<br>diff/renames, compare this file with HEAD or the working tree, large diffs |
| DIFF-CTX-COPY | 1 | §4.5 | Context: Copy | diff/edge cases: GIF, BMP, JPEG and unknown images; CRLF without a final newline; light theme; side-by-side scroll sync; text menu; term views of a conflicted commit<br>diff/select lines, Ctrl+C and the context menu |
| DIFF-CTX-BLAME | 1 | §4.5 | Context: Blame file | diff/select lines, Ctrl+C and the context menu<br>diff/side by side across files: switching, a binary file, Shift+click first, blame from the working tree; an added submodule |
| DIFF-STASH-PARTS | 1 | §4.5 | Stash diff: working tree / index / untracked parts | changes/stash contents: working tree, index and untracked parts |
| BLAME-AT-COMMIT | 1 | §4.6 | Blame a file at a commit | blame/blame at a commit and on the working tree |
| BLAME-WORKTREE | 1 | §4.6 | Working-tree blame | blame/blame at a commit and on the working tree<br>diff/more edges: a copied file (and blame before it), files over the text limit, an untracked image, blame of an untracked file |
| BLAME-UNCOMMITTED | 1 | §4.6 | Uncommitted lines marked | blame/blame at a commit and on the working tree |
| BLAME-FILTER | 1 | §4.6 | Filter | blame/filter, history, tooltips |
| BLAME-BACK-FWD | 1 | §4.6 | Back/forward history (buttons) | blame/filter, history, tooltips |
| BLAME-MOUSE-BUTTONS | 1 | §4.6 | Back/forward with mouse buttons | blame/filter, history, tooltips |
| BLAME-TOOLTIP | 1 | §4.6 | Per-line tooltips | blame/filter, history, tooltips |
| BLAME-BEFORE | 1 | §4.6 | Blame before this change | blame/line menu: before, originating source, reveal, copy, blocks<br>diff/more edges: a copied file (and blame before it), files over the text limit, an untracked image, blame of an untracked file |
| BLAME-ORIGIN | 1 | §4.6 | Originating source | blame/line menu: before, originating source, reveal, copy, blocks |
| BLAME-REVEAL | 1 | §4.6 | Reveal commit | blame/line menu: before, originating source, reveal, copy, blocks |
| BLAME-COPY | 1 | §4.6 | Copy commit | blame/line menu: before, originating source, reveal, copy, blocks |
| BLAME-SELECT-BLOCK | 1 | §4.6 | Select a change block | blame/line menu: before, originating source, reveal, copy, blocks<br>edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch |
| BLAME-COPY-BLOCK | 1 | §4.6 | Copy a change block | blame/line menu: before, originating source, reveal, copy, blocks |
| BR-FILTER | 1 | §4.7 | Branches: filter | panels/branches: filter, current, upstream, reveal, copy |
| BR-TOGGLE | 1 | §4.7 | Branches: the eye icon toggles visibility in History; a click on the row does not (UF-36) | history/scope follows the side panels: the eye icon toggles, Ctrl-click shows only one, a hidden branch loses its badge, show and hide all<br>panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation |
| BR-SHOW-HIDE-ALL | 1 | §4.7 | Branches: Show all / Hide all branches in History (UF-33) | history/scope follows the side panels: the eye icon toggles, Ctrl-click shows only one, a hidden branch loses its badge, show and hide all |
| BR-BADGE-HIDDEN | 1 | §4.7 | A hidden branch has no badge in History, even on a commit other refs keep in view (UF-32) | history/scope follows the side panels: the eye icon toggles, Ctrl-click shows only one, a hidden branch loses its badge, show and hide all |
| BR-TREE | 1 | §4.7 | Branches: names as a tree split on '/', a group named by its members' longest common prefix (UF-34) | refs/Branches: a tree split on '/', groups named by their common prefix; double-click checks out |
| BR-CTRL-ONLY | 1 | §4.7 | Branches: Ctrl-click shows only this branch | history/scope follows the side panels: the eye icon toggles, Ctrl-click shows only one, a hidden branch loses its badge, show and hide all |
| BR-CURRENT-OUTLINE | 1 | §4.7 | Branches: current branch outlined | panels/branches: filter, current, upstream, reveal, copy |
| BR-REVEAL | 1 | §4.7 | Branches: Reveal | panels/branches: filter, current, upstream, reveal, copy |
| BR-COPY | 1 | §4.7 | Branches: Copy name | panels/branches: filter, current, upstream, reveal, copy |
| BR-UPSTREAM-INFO | 1 | §4.7 | Branches: upstream and ahead/behind shown | panels/branches: filter, current, upstream, reveal, copy |
| TAG-FILTER | 1 | §4.7 | Tags: filter | panels/tags: filter, visibility, reveal, copy<br>shell/unusual repository states: sequences between commits, detached rebase, odd remotes, tags, stash and worktrees |
| TAG-TOGGLE | 1 | §4.7 | Tags: visibility toggle | panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation<br>panels/tags: filter, visibility, reveal, copy |
| TAG-REVEAL | 1 | §4.7 | Tags: Reveal | panels/tags: filter, visibility, reveal, copy |
| TAG-COPY | 1 | §4.7 | Tags: Copy | panels/tags: filter, visibility, reveal, copy |
| TAG-LABEL-PLAIN | 1 | §4.7 | Tags: labels are the tag names, without "(annotated)" (UF-22) | panels/tags: filter, visibility, reveal, copy |
| WT-LIST | 1 | §4.7 | Worktrees: list with main/stale/locked | panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation<br>panels/worktrees: main, locked, stale; copy, reveal, open<br>rewrite/in a linked worktree: its own branch follows quietly, the main worktree's branch asks first<br>shell/unusual repository states: sequences between commits, detached rebase, odd remotes, tags, stash and worktrees<br>worktrees/lock with a reason and unlock, both undone; prune shows what it removes first and keeps locked ones; repair a moved worktree<br>worktrees/remove: with changes (asks, force), locked, missing; the main worktree refused; undo re-creates |
| WT-COPY-NAME | 1 | §4.7 | Worktrees: Copy name | panels/worktrees: main, locked, stale; copy, reveal, open |
| WT-COPY-PATH | 1 | §4.7 | Worktrees: Copy path | panels/worktrees: main, locked, stale; copy, reveal, open |
| WT-REVEAL-HEAD | 1 | §4.7 | Worktrees: Reveal HEAD | panels/worktrees: main, locked, stale; copy, reveal, open |
| WT-OPEN-DIR | 1 | §4.7 | Worktrees: Open directory | panels/worktrees: main, locked, stale; copy, reveal, open |
| REM-LIST | 1 | §4.7 | Remotes: list | panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation<br>panels/remotes: list and copy<br>shell/unusual repository states: sequences between commits, detached rebase, odd remotes, tags, stash and worktrees |
| REM-COPY | 1 | §4.7 | Remotes: Copy name | panels/remotes: list and copy |
| STASH-PANEL | 1 | §4.7 | Stashes panel lists entries | changes/stash contents: working tree, index and untracked parts<br>panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation |
| REFLOG-HEAD | 1 | §4.7 | Reflog: HEAD reflog | panels/reflog: HEAD, branch, stash; filter; copy; reveal |
| REFLOG-CHOOSER | 1 | §4.7 | Reflog: choose HEAD, a branch or stash | panels/reflog: HEAD, branch, stash; filter; copy; reveal |
| REFLOG-FILTER | 1 | §4.7 | Reflog: filter | panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation<br>panels/reflog: HEAD, branch, stash; filter; copy; reveal |
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
| APP-PROMPT-GIT-OLD | 2 | §4.1 | Blocking prompt when git is older than 2.36 | setup/git missing or too old: a blocking prompt with Retry<br>setup/git versions ggui reads: newer major, vendor suffix, no number; a typed path with a trailing slash; copying a notice<br>ui/Git required: Quit asks the app to quit; the refused open does not block later opens |
| APP-PROMPT-HOOKS | 2 | §4.1 | First-open managed hooks prompt appears | hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab<br>ui/Settings ▸ Hooks: the ask-on-open checkbox turns the first-open prompt on and off |
| APP-PROMPT-GGREFS | 2 | §4.1 | Old gg refs cleanup prompt appears | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable<br>ui/old gg data: Not now asks again next time; an unchecked commit is not kept |
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
| MENU-EDIT-UNDO-KEY | 2 | §4.1 | Ctrl+Z undo | undo/failed operations are passed over by Undo and Redo<br>undo/in a linked worktree: its HEAD and branch are undone; the main worktree's HEAD is left to it<br>undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores<br>worktrees/add: a new branch at a start point, locked, in a path with spaces; a detached commit; from Branches; undo and redo<br>worktrees/lock with a reason and unlock, both undone; prune shows what it removes first and keeps locked ones; repair a moved worktree<br>worktrees/per-worktree journal: a worktree change is undone only from the worktree that made it; refused when it moved on or has changes; git gg undo and redo agree<br>worktrees/remove: with changes (asks, force), locked, missing; the main worktree refused; undo re-creates<br>worktrees/with the managed hooks: Add is one operation Undo reverts; a plain git worktree add in a terminal does not move the main worktree's HEAD, and Undo leaves its branch alone |
| MENU-EDIT-REDO | 2 | §4.1 | Edit ▸ Redo (menu) | undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| MENU-EDIT-REDO-KEY | 2 | §4.1 | Ctrl+Y redo | undo/failed operations are passed over by Undo and Redo<br>undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores<br>worktrees/add: a new branch at a start point, locked, in a path with spaces; a detached commit; from Branches; undo and redo<br>worktrees/lock with a reason and unlock, both undone; prune shows what it removes first and keeps locked ones; repair a moved worktree |
| MENU-EDIT-APPLY-PATCH | 2 | §4.1 | Edit ▸ Apply patch… | patches/apply from the clipboard or a file, to the working tree or the index |
| TB-NEW | 2 | §4.1 | Toolbar New | new/new commit on HEAD advances the branch (toolbar, menu, keys) |
| TB-COMMIT | 2 | §4.1 | Toolbar Commit/Amend | commit/commit the index from the toolbar; hooks run natively<br>ui/toolbar Amend with HEAD selected; Skip hooks on Amend |
| TB-COMMIT-LABEL | 2 | §4.1 | Commit/Amend label follows the selection | commit/commit the index from the toolbar; hooks run natively<br>ui/toolbar Amend with HEAD selected; Skip hooks on Amend |
| TB-NO-PREV-NEXT | 2 | §4.1 | The toolbar has no Previous / Next buttons (UF-27) | checkout/move HEAD to parent and child |
| TB-UNDO | 2 | §4.1 | Toolbar Undo | stash/apply, pop with the index, apply one file, branch, drop, undo, clear<br>undo/refusals: nothing to undo, refs moved outside the journal, local changes in the way |
| TB-REDO | 2 | §4.1 | Toolbar Redo | stash/apply, pop with the index, apply one file, branch, drop, undo, clear<br>ui/Redo that would overwrite local changes offers Stash and redo |
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
| TB-PUSH | 2 | §4.1 | Toolbar Push to upstream | network/push: toolbar, menu, History and Branches; no upstream prefills Push to<br>shell/toolbar details: force with lease, push tags, HEAD tooltip, a merge from the selection, a detached rebase's progress |
| TB-PUSH-BADGE | 2 | §4.1 | Push outgoing badge ↑n | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| TB-PUSH-NOUPSTREAM | 2 | §4.1 | No upstream: Push to… with --set-upstream prefilled | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| TB-PUSH-TO | 2 | §4.1 | Push dropdown: Push to… | network/push: toolbar, menu, History and Branches; no upstream prefills Push to<br>ui/Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease |
| TB-PUSH-FORCE-LEASE | 2 | §4.1 | Push dropdown: Force with lease | network/rejected push: Pull then push, Force with lease; push tags<br>ui/Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease<br>ui/toolbar Push options ▸ Force with lease overwrites the upstream after confirming |
| TB-PUSH-TAGS | 2 | §4.1 | Push dropdown: Push tags | network/rejected push: Pull then push, Force with lease; push tags |
| TB-PUSH-REFUSE-CONFLICTS | 2 | §4.1 | Push refused when range has first-class conflicts | network/push is refused when outgoing commits hold first-class conflicts |
| TB-PUSH-REJECTED-PULL | 2 | §4.1 | Rejected non-fast-forward: Pull then push | network/rejected push: Pull then push, Force with lease; push tags |
| TB-PUSH-REJECTED-FORCE | 2 | §4.1 | Rejected non-fast-forward: Force with lease… (confirm) | network/rejected push: Pull then push, Force with lease; push tags |
| TB-REMOTE-BUSY | 2 | §4.1 | Fetch/Pull/Push disabled during a conflicting mutation; browsing works | network/remote actions are disabled while a mutation runs; browsing still works<br>shell/while a mutation runs every menu disables what would conflict; browsing still works |
| TB-AHEAD-BEHIND-REFRESH | 2 | §4.1 | Ahead/behind badges refresh after fetch/pull/push/ref change | setup/ahead/behind badges follow ref changes made outside ggui |
| TB-STASH | 2 | §4.1 | Toolbar Stash | stash/create: message, untracked, keep index, staged only, selected files |
| TB-POP | 2 | §4.1 | Toolbar Pop | stash/create: message, untracked, keep index, staged only, selected files |
| TB-STATE-CONTINUE | 2 | §4.1 | State badge Continue | conflicts/native merge: three-way diff, take ours, edit the message, continue |
| TB-STATE-SKIP | 2 | §4.1 | State badge Skip | conflicts/native: abort a merge, skip a rebase step<br>ui/a stopped cherry-pick: Skip, and Commit with conflicts |
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
| SET-SCOPE-XDG-SYSTEM | 2 | §4.1 | Settings ▸ Git: the User tab reads $XDG_CONFIG_HOME/git/config with ~/.gitconfig; system values show as hints (UF-45) | setup/Settings ▸ Git: the user config in $XDG_CONFIG_HOME and the system config show their values |
| SET-WORKTREE-TOGGLE | 2 | §4.1 | Settings ▸ Git: "Worktree settings" checkbox sets extensions.worktreeConfig; the Worktree tab exists only while on (UF-46) | setup/Settings ▸ Git: scope tabs, one field per option, inherited hints and overrides |
| SET-NOTHING-STAGED-LABEL | 2 | §4.1 | Settings ▸ Git: "Commit with nothing staged" with an explanation of its choices (UF-44) | commit/default for nothing staged comes from Settings |
| SET-COMMIT-ALL-DEFAULT | 2 | §4.1 | Settings: default when nothing is staged | commit/default for nothing staged comes from Settings |
| SET-HOOKS-TAB | 2 | §4.1 | Settings: Hooks tab install/remove/status | hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab<br>ui/Settings ▸ Hooks: the ask-on-open checkbox turns the first-open prompt on and off |
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
| HIST-CTX-NEW | 2 | §4.2 | Context: New | new/new commit on HEAD advances the branch (toolbar, menu, keys)<br>ui/History: Ctrl-click drops a commit from the selection; New merge commit from the row menu |
| HIST-CTX-NEW-DETACHED | 2 | §4.2 | Context: New detached | new/new detached and new on another commit |
| HIST-CTX-CHECKOUT | 2 | §4.2 | Context: Check out ▸ | checkout/switch to a branch, detach, E key |
| HIST-CTX-CREATE-BRANCH | 2 | §4.2 | Context: Create branch… | refs/create, check out, rename and delete branches<br>ui/dialogs: Escape cancels; Enter in a text field confirms when the button is enabled |
| HIST-CTX-MOVE-BRANCH | 2 | §4.2 | Context: Move branch ▸ | refs/move a branch; warning for a branch checked out elsewhere |
| HIST-CTX-DELETE-BRANCH | 2 | §4.2 | Context: Delete branch ▸ | refs/create, check out, rename and delete branches |
| HIST-CTX-PUSH | 2 | §4.2 | Context: Push | network/push: toolbar, menu, History and Branches; no upstream prefills Push to<br>ui/Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease |
| HIST-CTX-PUSH-TO | 2 | §4.2 | Context: Push to… | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| HIST-WT-CTX-COMMIT | 2 | §4.2 | Working tree: Commit… | commit/nothing staged: stage all tracked or the selected files |
| HIST-WT-CTX-AMEND | 2 | §4.2 | Working tree: Amend into HEAD… | commit/amend content and message, message only, Amend into HEAD |
| HIST-WT-CTX-DISCARD | 2 | §4.2 | Working tree: Discard changes… | staging/discard all changes from the Working tree menu |
| HIST-WT-CTX-STASH | 2 | §4.2 | Working tree: Stash changes… | stash/create: message, untracked, keep index, staged only, selected files |
| HIST-WT-CTX-STAGE-ALL | 2 | §4.2 | Working tree: Stage all | staging/stage all, unstage all, stage modified |
| HIST-WT-CTX-UNSTAGE-ALL | 2 | §4.2 | Working tree: Unstage all | staging/stage all, unstage all, stage modified |
| ACT-NEW | 2 | §4.3 | New commit advances the attached branch | new/new commit on HEAD advances the branch (toolbar, menu, keys) |
| ACT-NEW-DETACHED | 2 | §4.3 | New detached leaves branches alone | edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch<br>new/new detached and new on another commit |
| ACT-NEW-MERGE | 2 | §4.3 | New with several parents makes a merge commit | new/several parents make a merge commit<br>shell/toolbar details: force with lease, push tags, HEAD tooltip, a merge from the selection, a detached rebase's progress<br>ui/History: Ctrl-click drops a commit from the selection; New merge commit from the row menu |
| ACT-CHECKOUT-BRANCH | 2 | §4.3 | Check out a branch (git switch) | checkout/switch to a branch, detach, E key |
| ACT-CHECKOUT-DETACH | 2 | §4.3 | Check out a commit (detach) | checkout/switch to a branch, detach, E key |
| ACT-CHECKOUT-REFUSE | 2 | §4.3 | Refuse when local changes would be overwritten | checkout/local changes block a switch: Stash and switch |
| ACT-CHECKOUT-STASH | 2 | §4.3 | Stash and switch | checkout/local changes block a switch: Stash and switch |
| ACT-COMMIT | 2 | §4.3 | Commit… commits the index | commit/commit the index from the toolbar; hooks run natively |
| ACT-COMMIT-NOTHING-STAGED-ALL | 2 | §4.3 | Nothing staged: stage all and commit | commit/nothing staged: stage all tracked or the selected files |
| ACT-COMMIT-NOTHING-STAGED-SELECTED | 2 | §4.3 | Nothing staged: stage the selected files | commit/nothing staged: stage all tracked or the selected files |
| ACT-COMMIT-SKIP-HOOKS | 2 | §4.3 | Skip hooks (--no-verify) | commit/failing pre-commit hook goes to the banner; Skip hooks<br>ui/toolbar Amend with HEAD selected; Skip hooks on Amend |
| ACT-COMMIT-HOOK-FAIL | 2 | §4.3 | Failing pre-commit hook shown in an error popup | commit/failing pre-commit hook goes to the banner; Skip hooks |
| ACT-AMEND | 2 | §4.3 | Amend HEAD with the index | commit/amend content and message, message only, Amend into HEAD<br>ui/toolbar Amend with HEAD selected; Skip hooks on Amend |
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
| INFO-CONFLICTED-FILES | 2 | §4.4 | Conflicted files and side counts listed | conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes<br>edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch |
| DIFF-STAGE-LINES | 2 | §4.5 | Stage line(s) | linestaging/CRLF lines, missing final newline, new files<br>linestaging/randomized line staging matches the content model |
| DIFF-STAGE-HUNK | 2 | §4.5 | Stage hunk | linestaging/stage, discard and unstage hunks |
| DIFF-DISCARD-LINES | 2 | §4.5 | Discard line(s) | linestaging/CRLF lines, missing final newline, new files |
| DIFF-DISCARD-HUNK | 2 | §4.5 | Discard hunk | linestaging/stage, discard and unstage hunks |
| DIFF-UNSTAGE-LINES | 2 | §4.5 | Unstage line(s) | linestaging/randomized line staging matches the content model |
| DIFF-UNSTAGE-HUNK | 2 | §4.5 | Unstage hunk | linestaging/stage, discard and unstage hunks |
| DIFF-HUNK-MENU | 2 | §4.5 | Context menu: Stage / Discard / Unstage hunk(s) (both views) | linestaging/hunks from the context menu in the side-by-side view |
| DIFF-HUNK-BUTTONS | 2 | §4.5 | Buttons in hunk headers | linestaging/stage, discard and unstage hunks |
| DIFF-STAGING-RANDOM | 2 | §8.4 | Randomized line/hunk staging matches git apply --cached | linestaging/randomized line staging matches the content model |
| BR-DOUBLE-CLICK-CHECKOUT | 2 | §4.7 | Branches: double-click checks the branch out (UF-37) | refs/Branches: a tree split on '/', groups named by their common prefix; double-click checks out |
| BR-REMOTE-MENU | 2 | §4.7 | Branches: a remote and its remote-tracking branches have the Remotes panel's menu (UF-12) | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| BR-CREATE | 2 | §4.7 | Branches: Create branch | refs/create, check out, rename and delete branches |
| BR-CREATE-CHECKOUT-DEFAULT | 2 | §4.7 | Create branch: "Check out after creating" is on by default (UF-35) | refs/create, check out, rename and delete branches |
| BR-CHECKOUT | 2 | §4.7 | Branches: Check out | refs/create, check out, rename and delete branches |
| BR-PUSH | 2 | §4.7 | Branches: Push | network/push: toolbar, menu, History and Branches; no upstream prefills Push to<br>ui/Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease |
| BR-PUSH-TO | 2 | §4.7 | Branches: Push to… | network/push: toolbar, menu, History and Branches; no upstream prefills Push to |
| BR-RENAME | 2 | §4.7 | Branches: Rename… | refs/create, check out, rename and delete branches |
| BR-DELETE-LOCAL | 2 | §4.7 | Branches: Delete ▸ Local | refs/create, check out, rename and delete branches |
| BR-DELETE-REMOTE | 2 | §4.7 | Branches: Delete ▸ on a remote | refs/delete a branch on its remote, and everywhere |
| BR-DELETE-ALL | 2 | §4.7 | Branches: Delete ▸ Local and all remotes | refs/delete a branch on its remote, and everywhere |
| BR-SET-UPSTREAM | 2 | §4.7 | Branches: Set upstream | edit/dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults<br>refs/upstream: set, unset, fast-forward |
| BR-SET-UPSTREAM-FILTER | 2 | §4.7 | Set upstream: filter field for the branch list; Enter picks the first match (UF-23) | refs/upstream: set, unset, fast-forward |
| BR-UNSET-UPSTREAM | 2 | §4.7 | Branches: Unset upstream | refs/upstream: set, unset, fast-forward |
| BR-FF-UPSTREAM | 2 | §4.7 | Branches: Fast-forward to upstream | refs/upstream: set, unset, fast-forward |
| BR-PULL | 2 | §4.7 | Branches: Pull (current branch) | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| BR-MOVE | 2 | §4.7 | Move branch to the selected commit | refs/move a branch; warning for a branch checked out elsewhere |
| BR-MOVE-WORKTREE-WARN | 2 | §4.7 | Warn when moving a branch checked out elsewhere | refs/move a branch; warning for a branch checked out elsewhere |
| TAG-CREATE | 2 | §4.7 | Tags: Create (lightweight) | edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch<br>edit/dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults<br>refs/tags: lightweight, annotated, delete, push, delete on remote; tags only on a remote |
| TAG-ANNOTATED | 2 | §4.7 | Tags: Create annotated with message | refs/tags: lightweight, annotated, delete, push, delete on remote; tags only on a remote |
| TAG-DELETE | 2 | §4.7 | Tags: Delete | refs/tags: lightweight, annotated, delete, push, delete on remote; tags only on a remote |
| TAG-PUSH | 2 | §4.7 | Tags: Push tag | refs/tags: lightweight, annotated, delete, push, delete on remote; tags only on a remote |
| TAG-DELETE-REMOTE | 2 | §4.7 | Tags: Delete a remote tag | refs/tags: lightweight, annotated, delete, push, delete on remote; tags only on a remote |
| TAG-DELETE-MENU | 2 | §4.7 | Tags: Delete is one item for a tag only here; a submenu (Local, then each remote that has it) otherwise (UF-43) | refs/tags: lightweight, annotated, delete, push, delete on remote; tags only on a remote |
| TAG-REMOTE-ONLY | 2 | §4.7 | Tags: tags only on a remote are listed (git ls-remote); their Delete ▸ Local is disabled (UF-43) | refs/tags: lightweight, annotated, delete, push, delete on remote; tags only on a remote |
| REM-ADD | 2 | §4.7 | Remotes: Add remote | edit/dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults<br>refs/remotes: add, edit URL, prune on fetch, delete |
| REM-DELETE | 2 | §4.7 | Remotes: Delete | refs/remotes: add, edit URL, prune on fetch, delete |
| REM-EDIT-URL | 2 | §4.7 | Remotes: Edit URL | refs/remotes: add, edit URL, prune on fetch, delete |
| REM-PRUNE-ON-FETCH | 2 | §4.7 | Remotes: prune on fetch option | refs/remotes: add, edit URL, prune on fetch, delete |
| REM-FETCH | 2 | §4.7 | Remotes: Fetch | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| REM-FETCH-ALL | 2 | §4.7 | Remotes: Fetch all | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| REM-PULL | 2 | §4.7 | Remotes: Pull | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| REFLOG-BRANCH | 2 | §4.7 | Reflog: create a branch from old/new | edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch<br>refs/create a branch from a reflog entry |
| UNDO-ALL-MUTATIONS | 2 | §5 U1 | Every everyday (Phase 2) mutation is undone by Undo | undo/every everyday mutation can be undone |
| OPS-LIST | 2 | §4.7 | Operations: journal entries listed | panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation<br>undo/in a linked worktree: its HEAD and branch are undone; the main worktree's HEAD is left to it<br>undo/journal variants: foreign, torn and future records are skipped; busy and stale locks; a newer format is refused<br>undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| OPS-SOURCE-LABEL | 2 | §4.7 | Operations: source label (ggui, git-gg, git <command>) | undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| OPS-RESTORE | 2 | §4.7 | Operations: Restore | panels/details: remote-tracking rows, tooltips, a locked worktree, reflog by ID, Operations buttons and a failed operation<br>undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| OPS-NO-HOOKS-NOTE | 2 | §4.7 | Operations: note when managed hooks are missing | undo/undo and redo from the menu, keys and toolbar; Operations lists sources and restores |
| REMOTE-CLONE | 2 | §4.8 | Clone with progress | network/clone from Welcome and the menu; unreachable remote fails cleanly<br>network/clone over git://: the server's progress shows its phase, not "remote" |
| REMOTE-CLONE-CANCEL | 2 | §4.8 | Clone cancel cleans up | network/cancel a clone: no directory left behind |
| REMOTE-CLONE-FAIL | 2 | §4.8 | Clone from an unreachable remote fails cleanly | network/clone from Welcome and the menu; unreachable remote fails cleanly |
| REMOTE-FETCH-ONE | 2 | §4.8 | Fetch one remote | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| REMOTE-FETCH-ALL | 2 | §4.8 | Fetch all remotes | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| REMOTE-FETCH-NO-FF | 2 | §4.8 | Fetch changes only remote-tracking refs | network/fetch: toolbar, dropdown, menu, Remotes panel, Branches; only remote-tracking refs move |
| REMOTE-FETCH-FAIL | 2 | §4.8 | Fetch network failure reported | network/fetch from an unreachable remote reports the failure |
| REMOTE-PUSH | 2 | §4.8 | Push a branch | network/push: toolbar, menu, History and Branches; no upstream prefills Push to<br>ui/Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease |
| REMOTE-PUSH-SET-UPSTREAM | 2 | §4.8 | Push --set-upstream for a new branch | network/push: toolbar, menu, History and Branches; no upstream prefills Push to<br>ui/Push without an upstream opens Push to (History, Branches); remote, upstream and force with lease |
| REMOTE-PUSH-TAGS | 2 | §4.8 | Push tags | network/rejected push: Pull then push, Force with lease; push tags |
| REMOTE-PUSH-DELETE-BRANCH | 2 | §4.8 | Delete a remote branch | refs/delete a branch on its remote, and everywhere |
| REMOTE-PROGRESS | 2 | §4.8 | Transfer progress shown | network/clone over git://: the server's progress shows its phase, not "remote"<br>network/remote actions are disabled while a mutation runs; browsing still works |
| REMOTE-ASKPASS | 2 | §4.8 | Askpass prompt succeeds | network/askpass: answer and cancel a credentials prompt |
| REMOTE-ASKPASS-CANCEL | 2 | §4.8 | Askpass cancel aborts cleanly | network/askpass: answer and cancel a credentials prompt |
| REMOTE-PULL-CONFIG | 2 | §4.8 | Pull follows pull.rebase/pull.ff | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| REMOTE-PULL-OVERRIDE | 2 | §4.8 | Pull per-action override | network/pull follows pull.rebase; dropdown overrides; menu and panels |
| STASH-CREATE | 2 | §4.9 | Create with message | stash/create: message, untracked, keep index, staged only, selected files |
| STASH-CREATE-KEEP-INDEX | 2 | §4.9 | Create: keep index | stash/create: message, untracked, keep index, staged only, selected files |
| STASH-CREATE-UNTRACKED | 2 | §4.9 | Create: include untracked | stash/create: message, untracked, keep index, staged only, selected files |
| STASH-CREATE-STAGED | 2 | §4.9 | Create: staged only | stash/create: message, untracked, keep index, staged only, selected files |
| STASH-CREATE-PATHS | 2 | §4.9 | Create: selected paths only | stash/create: message, untracked, keep index, staged only, selected files |
| STASH-APPLY | 2 | §4.9 | Apply | edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch<br>stash/apply, pop with the index, apply one file, branch, drop, undo, clear |
| STASH-POP | 2 | §4.9 | Pop | stash/apply, pop with the index, apply one file, branch, drop, undo, clear<br>ui/Stashes ▸ Pop applies an older stash and drops only that one |
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
| CONF-PARSE-DIFF3 | 2 | §4.10 | Two-sided diff3 regions detected | conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes<br>conflicts/marker grammar edge cases (docs/spec/conflict-markers.md)<br>edges/marker-like text: only complete regions are conflicts (CRLF, no final newline, long markers, flags); the rest is plain text |
| CONF-PARSE-NWAY | 2 | §4.10 | N-sided extended regions detected | conflicts/marker grammar edge cases (docs/spec/conflict-markers.md)<br>conflicts/marker parsing: N sides, marker length, malformed, opt-out, disposable cache<br>edges/marker-like text: only complete regions are conflicts (CRLF, no final newline, long markers, flags); the rest is plain text |
| CONF-MARKER-LENGTH | 2 | §4.10 | Marker length ≥ 7, honours conflict-marker-size, opening fixes length | conflicts/marker grammar edge cases (docs/spec/conflict-markers.md)<br>conflicts/marker parsing: N sides, marker length, malformed, opt-out, disposable cache<br>edges/marker-like text: only complete regions are conflicts (CRLF, no final newline, long markers, flags); the rest is plain text |
| CONF-WELLFORMED-ONLY | 2 | §4.10 | Malformed/partial markers are text | conflicts/marker grammar edge cases (docs/spec/conflict-markers.md)<br>conflicts/marker parsing: N sides, marker length, malformed, opt-out, disposable cache<br>edges/marker-like text: only complete regions are conflicts (CRLF, no final newline, long markers, flags); the rest is plain text |
| CONF-OPTOUT | 2 | §4.10 | gg-conflicts=false opt-out | conflicts/marker parsing: N sides, marker length, malformed, opt-out, disposable cache |
| CONF-CACHE-DISPOSABLE | 2 | §4.10 | Deleting .git/gg changes nothing reported | conflicts/marker parsing: N sides, marker length, malformed, opt-out, disposable cache |
| CONF-DISPLAY-HISTORY | 2 | §4.10 | History marks conflicted commits (incl. descendants keeping a conflict) | conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes |
| CONF-DISPLAY-INFO | 2 | §4.10 | Change information lists conflicted files and side counts | conflicts/first-class conflicts: History marks, filter, F7, Change information, Changes |
| CONF-PUSH-REFUSE | 2 | §4.10 | ggui refuses to push conflicted commits (lists commits/files, Reveal) | network/push is refused when outgoing commits hold first-class conflicts |
| CONF-NATIVE-DETECT | 2 | §4.10 | Native merge/rebase/cherry-pick/revert/bisect detected | conflicts/toolbar for other operations: abort a revert and an apply-backend rebase, skip and reset a bisect; merge tool on a first-class conflict<br>shell/repository state badge |
| CONF-NATIVE-STAGES | 2 | §4.10 | Conflicted files from index stages 1–3 | changes/working tree groups: staged, unstaged, untracked, conflicted |
| CONF-NATIVE-MERGETOOL | 2 | §4.10 | Native: resolve with merge tool | conflicts/native: resolve with the configured merge tool<br>conflicts/toolbar for other operations: abort a revert and an apply-backend rebase, skip and reset a bisect; merge tool on a first-class conflict |
| CONF-NATIVE-TAKE-SIDE | 2 | §4.10 | Native: take ours/theirs | conflicts/native merge: three-way diff, take ours, edit the message, continue<br>edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch |
| CONF-NATIVE-MARK-RESOLVED | 2 | §4.10 | Native: mark resolved | conflicts/native: resolve by editing, mark resolved, continue |
| CONF-NATIVE-3WAY-DIFF | 2 | §4.10 | Native: three-way diff | conflicts/native merge: three-way diff, take ours, edit the message, continue |
| CONF-NATIVE-CONTINUE | 2 | §4.10 | Native: Continue | conflicts/native merge: three-way diff, take ours, edit the message, continue<br>conflicts/toolbar for other operations: abort a revert and an apply-backend rebase, skip and reset a bisect; merge tool on a first-class conflict<br>rebase-native/conflict stop, Edit remaining todo like git rebase --edit-todo, exec after every commit, Run as git rebase |
| CONF-NATIVE-SKIP | 2 | §4.10 | Native: Skip | conflicts/native: abort a merge, skip a rebase step |
| CONF-NATIVE-ABORT | 2 | §4.10 | Native: Abort | conflicts/native: abort a merge, skip a rebase step<br>rebase-native/git rebase -i refusals and options: moved branch, git before 2.38 with update-ref, local changes and autostash, Abort |
| CONF-MERGE-MSG | 2 | §4.10 | Show/edit MERGE_MSG while in progress | conflicts/native merge: three-way diff, take ours, edit the message, continue |
| CONF-COMMIT-WITH-CONFLICTS | 2 | §4.10 | Commit with conflicts writes diff3 regions and finishes | conflicts/commit with conflicts records diff3 regions; not offered for binary conflicts<br>ui/a stopped cherry-pick: Skip, and Commit with conflicts |
| CONF-COMMIT-WITH-CONFLICTS-BINARY-REFUSE | 2 | §4.10 | Commit with conflicts refused with a binary conflict | conflicts/commit with conflicts records diff3 regions; not offered for binary conflicts |
| PATCH-APPLY-CLIPBOARD | 2 | §4.11 | Apply patch from clipboard | patches/apply from the clipboard or a file, to the working tree or the index |
| PATCH-APPLY-FILE | 2 | §4.11 | Apply patch from file | patches/apply from the clipboard or a file, to the working tree or the index |
| PATCH-APPLY-INDEX | 2 | §4.11 | Apply to index | patches/apply from the clipboard or a file, to the working tree or the index |
| PATCH-APPLY-WORKTREE | 2 | §4.11 | Apply to working tree | patches/apply from the clipboard or a file, to the working tree or the index |
| PATCH-APPLY-FAIL | 2 | §4.11 | Patch that fails to apply is reported | patches/a patch that does not apply is reported and changes nothing |
| HOOK-USER-NATIVE | 2 | §4.12 | User hooks run natively through git | commit/commit the index from the toolbar; hooks run natively |
| HOOK-FAIL-POPUP | 2 | §4.12 | Failing blocking hook aborts; output in an error popup | commit/failing pre-commit hook goes to the banner; Skip hooks<br>undo/failed operations are passed over by Undo and Redo |
| HOOK-SKIP | 2 | §4.12 | Skip hooks checkbox | commit/failing pre-commit hook goes to the banner; Skip hooks<br>ui/toolbar Amend with HEAD selected; Skip hooks on Amend |
| HOOK-PROMPT-INSTALL | 2 | §4.12 | First-open prompt: Install | hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab |
| HOOK-PROMPT-NOT-NOW | 2 | §4.12 | First-open prompt: Not now | hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab |
| HOOK-PROMPT-NEVER | 2 | §4.12 | First-open prompt: Never | hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab |
| HOOK-SETTINGS | 2 | §4.12 | Settings Hooks tab | hooks/first-open prompt (Install / Not now / Never) and the Settings Hooks tab |
| HOOK-CLI-INSTALL | 2 | §4.12 | git gg hooks install | cli/git gg edge cases: nothing to undo or redo, local changes in the way, outside a repository, without git-gg or ggui on PATH<br>hooks/git gg hooks install/status/uninstall: config-defined hooks from git 2.54, wrapper scripts before |
| HOOK-CLI-UNINSTALL | 2 | §4.12 | git gg hooks uninstall | hooks/git gg hooks install/status/uninstall: config-defined hooks from git 2.54, wrapper scripts before |
| HOOK-CLI-STATUS | 2 | §4.12 | git gg hooks status | hooks/git gg hooks install/status/uninstall: config-defined hooks from git 2.54, wrapper scripts before |
| HOOK-REFTX | 2 | §4.12 | reference-transaction appends to the journal | hooks/plain git commands are journaled one operation each and Undo restores them |
| HOOK-GROUPING | 2 | §4.12 | One git command = one operation | hooks/plain git commands are journaled one operation each and Undo restores them |
| HOOK-POST-CONTEXT | 2 | §4.12 | post-* hooks add context | hooks/plain git commands are journaled one operation each and Undo restores them |
| HOOK-CHAIN | 2 | §4.12 | Existing hooks chained | hooks/wrapper scripts chain existing hooks (exit status kept) and uninstall byte-exact |
| HOOK-CHAIN-EXIT | 2 | §4.12 | Chained hook exit status respected | hooks/wrapper scripts chain existing hooks (exit status kept) and uninstall byte-exact |
| HOOK-CONFIG-DEFINED | 2 | §4.12 | Config-defined hooks when git supports them | hooks/config-defined hooks: a repository path with a quote, a partial installation completed |
| HOOK-WRAPPER | 2 | §4.12 | Wrapper scripts in the active hooks dir otherwise | hooks/wrapper scripts chain existing hooks (exit status kept) and uninstall byte-exact |
| HOOK-WORKTREE | 2 | §4.12 | Works in linked worktrees | hooks/hooks work in linked worktrees<br>worktrees/with the managed hooks: Add is one operation Undo reverts; a plain git worktree add in a terminal does not move the main worktree's HEAD, and Undo leaves its branch alone |
| HOOK-UNINSTALL-EXACT | 2 | §4.12 | Uninstall restores previous hooks byte-exact | hooks/wrapper scripts chain existing hooks (exit status kept) and uninstall byte-exact |
| HOOK-LOOP-GUARD | 2 | §4.12 | GG_OPERATION joins the open operation | hooks/plain git commands are journaled one operation each and Undo restores them |
| HOOK-MISSING-SILENT | 2 | §4.12 | git-gg missing: wrapper does nothing | hooks/without git-gg on PATH the hooks do nothing, pre-push warns |
| HOOK-MISSING-PREPUSH-WARN | 2 | §4.12 | git-gg missing: pre-push warns | hooks/without git-gg on PATH the hooks do nothing, pre-push warns |
| HOOK-JOURNAL-CORRUPT | 2 | §4.12 | Corrupt journal skipped, not fatal | rebase-i/failure paths: pre-rebase veto, a hook refusing the ref transaction, local changes in the way, a corrupt journal<br>rebase-native/failure paths: pre-rebase veto leaves no rebase; a corrupt journal during a stop keeps one Undo<br>undo/a corrupt journal line is skipped, not fatal<br>undo/journal variants: foreign, torn and future records are skipped; busy and stale locks; a newer format is refused |
| HOOK-FAST | 2 | §4.12 | Fetch of thousands of refs stays fast | hooks/a fetch of thousands of refs stays fast with the hooks |
| CLI-NEW | 2 | §6 | git gg new | cli/git gg new: on HEAD, with a message, detached, merge |
| CLI-NEW-MSG | 2 | §6 | git gg new -m | cli/git gg new: on HEAD, with a message, detached, merge |
| CLI-NEW-DETACH | 2 | §6 | git gg new --detach | cli/git gg new: on HEAD, with a message, detached, merge |
| CLI-NEW-MERGE | 2 | §6 | git gg new PARENT PARENT (merge) | cli/git gg new: on HEAD, with a message, detached, merge |
| CLI-UNDO | 2 | §6 | git gg undo | cli/git gg edge cases: nothing to undo or redo, local changes in the way, outside a repository, without git-gg or ggui on PATH<br>cli/git gg undo, redo and op log |
| CLI-REDO | 2 | §6 | git gg redo | cli/git gg edge cases: nothing to undo or redo, local changes in the way, outside a repository, without git-gg or ggui on PATH<br>cli/git gg undo, redo and op log |
| CLI-OP-LOG | 2 | §6 | git gg op log | cli/git gg undo, redo and op log |
| CLI-CONFLICTS | 2 | §6 | git gg conflicts (exit 1 when any) | cli/git gg conflicts, help and exit codes<br>conflicts/marker grammar edge cases (docs/spec/conflict-markers.md) |
| CLI-UI | 2 | §6 | git gg ui | cli/git gg edge cases: nothing to undo or redo, local changes in the way, outside a repository, without git-gg or ggui on PATH<br>cli/git gg ui starts ggui on the repository |
| CLI-HELP | 2 | §6 | git gg help / --help | cli/git gg conflicts, help and exit codes |
| CLI-EXIT-CODES | 2 | §6 | Git-style exit codes and stderr | cli/git gg conflicts, help and exit codes<br>cli/git gg edge cases: nothing to undo or redo, local changes in the way, outside a repository, without git-gg or ggui on PATH |
| GGREFS-DETECT | 2 | §5 C3 | Detect refs/gg/* on open | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| GGREFS-LIST | 2 | §5 C3 | List commits kept alive only by refs/gg/* | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable<br>ui/old gg data: Not now asks again next time; an unchecked commit is not kept |
| GGREFS-BRANCH | 2 | §5 C3 | Create a branch for a listed commit | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| GGREFS-BACKUP | 2 | §5 C3 | Keep commits via a backup branch | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| GGREFS-DELETE | 2 | §5 C3 | Delete refs with one update-ref --stdin | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable<br>ui/old gg data: Not now asks again next time; an unchecked commit is not kept |
| GGREFS-UNDO | 2 | §5 C3 | Cleanup is undoable | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| GGREFS-IGNORE | 2 | §5 C3 | Ignore is remembered per repository | setup/old gg refs: listed, kept as branches, deleted at once, undoable, ignorable |
| FAIL-LOCKED-REF | 2 | §8.4 | Locked ref makes the mutation fail cleanly | undo/refusals: nothing to undo, refs moved outside the journal, local changes in the way |
| FAIL-NETWORK | 2 | §8.4 | Unreachable remote | network/clone from Welcome and the menu; unreachable remote fails cleanly |
| FAIL-CANCEL | 2 | §8.4 | Cancelled network operation leaves a plain-git state | network/cancel a clone: no directory left behind |
| MENU-COMMIT-ACTIONS | 3 | §4.1 | Commit menu carries the selected-commit actions | edit/History and Commit menus: merge a commit into HEAD, rebase HEAD onto a commit<br>edit/no-op rewrites keep ids; the Commit menu carries the selected commit's actions |
| HIST-KEY-D | 3 | §4.2 | D: duplicate commit | edit/duplicate a commit (D) and a branch (Shift+D) as detached copies |
| HIST-KEY-SHIFT-D | 3 | §4.2 | Shift+D: duplicate branch | edit/duplicate a commit (D) and a branch (Shift+D) as detached copies |
| HIST-KEY-S | 3 | §4.2 | S: squash | edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S) |
| HIST-KEY-SHIFT-S | 3 | §4.2 | Shift+S: squash with descendants | edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S) |
| HIST-KEY-ALT-S | 3 | §4.2 | Alt+S: split | edit/split a commit by files (Alt+S) |
| HIST-KEY-A | 3 | §4.2 | A: drop | edit/abandon a commit (A) and a branch (Shift+A) |
| HIST-KEY-SHIFT-A | 3 | §4.2 | Shift+A: drop branch | edit/abandon a commit (A) and a branch (Shift+A) |
| HIST-KEY-I | 3 | §4.2 | I: interactive rebase from here | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| HIST-DND-MOVE-BEFORE | 3 | §4.2 | Drag commit→commit: Move before | dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser<br>dnd/the chooser's choices and Escape; a commit's files onto its child, HEAD or elsewhere; a commit onto itself |
| HIST-DND-MOVE-AFTER | 3 | §4.2 | Drag commit→commit: Move after | dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser |
| HIST-DND-SQUASH | 3 | §4.2 | Drag commit→commit: Squash | dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser |
| HIST-DND-REBASE | 3 | §4.2 | Drag commit→commit: Rebase | dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser |
| HIST-DND-CHOOSER | 3 | §4.2 | Drop without modifier shows chooser | dnd/commit onto commit: modifiers pick move/squash/rebase, otherwise a chooser<br>dnd/the chooser's choices and Escape; a commit's files onto its child, HEAD or elsewhere; a commit onto itself<br>ui/the drop chooser closes with Escape and changes nothing |
| HIST-DND-BRANCH | 3 | §4.2 | Drag branch badge→commit moves branch | dnd/a branch badge onto a commit moves the branch |
| HIST-DND-FILES | 3 | §4.2 | Drag files→commit moves changes | dnd/files onto a commit: a commit's files into its parent; working tree files into any commit<br>dnd/the chooser's choices and Escape; a commit's files onto its child, HEAD or elsewhere; a commit onto itself<br>edit/more refusals and edges: reorder across branches, reorder on a detached HEAD, fold onto another branch or a deletion, a native merge git refuses |
| HIST-PUBLISHED-WARN | 3 | §4.2 | Warn before rewriting published history | edit/rebase one commit, and a commit with its descendants, onto another branch |
| ACT-NEW-INSERT-BEFORE | 3 | §4.3 | Insert new commit before (rebases descendants) | edit/insert a new commit before or after one |
| ACT-NEW-INSERT-AFTER | 3 | §4.3 | Insert new commit after (rebases descendants) | edit/insert a new commit before or after one |
| ACT-DESCRIBE-ANY | 3 | §4.3 | Reword any commit (descendants rebased) | rewrite/in a bare repository, and at the root: reword, abandon the root commit<br>rewrite/reword a commit in the middle: descendants rebased, the rest untouched, one Undo |
| ACT-EDIT-AUTHOR | 3 | §4.3 | Edit author of any commit | rewrite/edit the author of any commit |
| REWRITE-INVARIANTS | 3 | §8.4 | Rewrite keeps ancestors/unrelated refs, rebases descendants with their trees | rewrite/in a bare repository, and at the root: reword, abandon the root commit<br>rewrite/in a linked worktree: its own branch follows quietly, the main worktree's branch asks first<br>rewrite/reword a commit in the middle: descendants rebased, the rest untouched, one Undo |
| REWRITE-POST-REWRITE | 3 | §4.12 | post-rewrite runs with the old→new mapping | rewrite/reword a commit in the middle: descendants rebased, the rest untouched, one Undo |
| REWRITE-UNDO | 3 | §5 U1 | One journal operation per rewrite; Undo restores exactly | rewrite/reword a commit in the middle: descendants rebased, the rest untouched, one Undo |
| REWRITE-PUBLISHED-WARN | 3 | §4.3 | Rewriting published commits asks first; remote-tracking refs never move | rewrite/published history asks first; a locked ref leaves everything untouched |
| REWRITE-FAIL-UNTOUCHED | 3 | §8.4 | A failing rewrite leaves refs, HEAD, index and working tree untouched | rebase-i/failure paths: pre-rebase veto, a hook refusing the ref transaction, local changes in the way, a corrupt journal<br>rebase-native/failure paths: pre-rebase veto leaves no rebase; a corrupt journal during a stop keeps one Undo<br>rewrite/published history asks first; a locked ref leaves everything untouched |
| ACT-DUPLICATE-COMMIT | 3 | §4.3 | Duplicate a commit onto its parent (detached copy) | edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch<br>edit/duplicate a commit (D) and a branch (Shift+D) as detached copies |
| ACT-DUPLICATE-BRANCH | 3 | §4.3 | Duplicate a branch range | edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch<br>edit/duplicate a commit (D) and a branch (Shift+D) as detached copies |
| ACT-REBASE-COMMIT | 3 | §4.3 | Rebase one commit onto a destination | edit/rebase one commit, and a commit with its descendants, onto another branch<br>edit/refusals: nothing to squash or move, unknown or descendant destinations, nothing redundant, already merged |
| ACT-REBASE-BRANCH | 3 | §4.3 | Rebase the whole branch onto a destination | edit/rebase one commit, and a commit with its descendants, onto another branch |
| ACT-IREBASE | 3 | §4.3 | Interactive rebase… opens the todo editor | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| ACT-SQUASH-PARENT | 3 | §4.3 | Squash into parent | edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S) |
| ACT-SQUASH-TARGET | 3 | §4.3 | Squash into a chosen target | edit/refusals: nothing to squash or move, unknown or descendant destinations, nothing redundant, already merged<br>edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S) |
| ACT-SQUASH-DESCENDANTS | 3 | §4.3 | Squash with descendants | edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch<br>edit/refusals: nothing to squash or move, unknown or descendant destinations, nothing redundant, already merged<br>edit/squash into the parent (S), into an ancestor, and descendants into a commit (Shift+S) |
| ACT-SPLIT | 3 | §4.3 | Split a commit by selected files | edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch<br>edit/dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults<br>edit/split a commit by files (Alt+S) |
| ACT-RESTORE-COMMIT | 3 | §4.3 | Restore paths in a commit from another commit | edit/dialog edge cases: split one file, restore nothing, push and set upstream without remotes, tag and remote defaults<br>edit/restore paths in a commit or the working tree; simplify parents |
| ACT-RESTORE-WORKTREE | 3 | §4.3 | Restore the working tree from a commit | edit/restore paths in a commit or the working tree; simplify parents |
| ACT-ABANDON | 3 | §4.3 | Abandon a commit (descendants rebased) | edit/abandon a commit (A) and a branch (Shift+A)<br>edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch<br>rewrite/in a bare repository, and at the root: reword, abandon the root commit |
| ACT-ABANDON-BRANCH | 3 | §4.3 | Abandon a branch | edit/abandon a commit (A) and a branch (Shift+A)<br>edit/by mouse: the commit menu's items, create tag, new detached commit, a conflict in Change information, blame lines, take theirs, stash apply, reflog branch |
| ACT-ABANDON-REMOTE | 3 | §4.3 | Abandon branch also deletes the remote branch | edit/abandon a commit (A) and a branch (Shift+A) |
| ACT-SIMPLIFY-PARENTS | 3 | §4.3 | Remove redundant merge parents | edit/refusals: nothing to squash or move, unknown or descendant destinations, nothing redundant, already merged<br>edit/restore paths in a commit or the working tree; simplify parents |
| ACT-REORDER | 3 | §4.3 | Move a commit before/after another | edit/more refusals and edges: reorder across branches, reorder on a detached HEAD, fold onto another branch or a deletion, a native merge git refuses<br>edit/reorder: move a commit before another, and copy one |
| ACT-REORDER-COPY | 3 | §4.3 | Copy a commit before/after another | edit/reorder: move a commit before another, and copy one |
| ACT-MOVE-CHANGES-PARENT | 3 | §4.3 | Move files/hunks/lines to the parent | edit/refusals: nothing to squash or move, unknown or descendant destinations, nothing redundant, already merged<br>move/files: to the parent, to the child, to the working tree, revert |
| ACT-MOVE-CHANGES-CHILD | 3 | §4.3 | Move files/hunks/lines to the child | edit/refusals: nothing to squash or move, unknown or descendant destinations, nothing redundant, already merged<br>move/files: to the parent, to the child, to the working tree, revert |
| ACT-MOVE-CHANGES-WORKTREE | 3 | §4.3 | Move changes to the working tree (uncommit) | move/files: to the parent, to the child, to the working tree, revert |
| ACT-MERGE-INTO-HEAD | 3 | §4.3 | Merge into HEAD in memory | edit/History and Commit menus: merge a commit into HEAD, rebase HEAD onto a commit<br>edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile<br>edit/refusals: nothing to squash or move, unknown or descendant destinations, nothing redundant, already merged |
| ACT-MERGE-NATIVE | 3 | §4.3 | Merge using native git merge | edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile<br>edit/more refusals and edges: reorder across branches, reorder on a detached HEAD, fold onto another branch or a deletion, a native merge git refuses |
| ACT-REBASE-HEAD-ONTO | 3 | §4.3 | Rebase HEAD onto branch | edit/History and Commit menus: merge a commit into HEAD, rebase HEAD onto a commit<br>edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile<br>edit/refusals: nothing to squash or move, unknown or descendant destinations, nothing redundant, already merged |
| ACT-RECONCILE | 3 | §4.3 | Reconcile a diverged branch with its upstream | edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile<br>ui/Reconcile by rebasing onto the upstream |
| ACT-REWRITE-TEXT-CONFLICT | 3 | §4.3 | Rewrites continue with first-class text conflicts | edit/text conflicts become first-class and never stop a rewrite; a later rewrite resolves them |
| ACT-REWRITE-COMPLETION-MSG | 3 | §4.3 | Completion message lists newly conflicted commits | edit/text conflicts become first-class and never stop a rewrite; a later rewrite resolves them |
| ACT-REWRITE-AUTO-RESOLVE | 3 | §4.3 | A later rewrite resolves a conflict automatically | edit/text conflicts become first-class and never stop a rewrite; a later rewrite resolves them |
| ACT-REWRITE-INVARIANTS | 3 | §8.4 | Ancestors/unrelated unchanged, no-op keeps IDs, failure changes nothing, Undo exact | edit/text conflicts become first-class and never stop a rewrite; a later rewrite resolves them |
| CHG-FIRSTCLASS-RESOLVE-AMEND | 3 | §4.4 | Resolve, stage and amend clears the conflict | conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too |
| CHG-CTX-REVERT | 3 | §4.4 | Context: Revert | move/files: to the parent, to the child, to the working tree, revert |
| CHG-CTX-MOVE-PARENT | 3 | §4.4 | Context: Move to parent | move/files: to the parent, to the child, to the working tree, revert |
| CHG-CTX-MOVE-CHILD | 3 | §4.4 | Context: Move to child | move/files: to the parent, to the child, to the working tree, revert |
| INFO-EDIT-AUTHOR | 3 | §4.4 | Edit author | rewrite/edit the author of any commit |
| DIFF-CTX-MOVE-PARENT | 3 | §4.5 | Context: Move line(s)/hunk to parent | move/lines of an added file, a renamed file, a CRLF file and a mode change: revert them in a commit<br>move/lines: a hunk to the parent and to the active commit |
| DIFF-CTX-MOVE-CHILD | 3 | §4.5 | Context: Move line(s)/hunk to child | move/lines: a hunk to the parent and to the active commit |
| DIFF-CTX-MOVE-ACTIVE | 3 | §4.5 | Context: Move line(s)/hunk to active commit | move/lines: a hunk to the parent and to the active commit |
| DIFF-CTX-MOVE-WORKTREE | 3 | §4.5 | Context: Move line(s)/hunk to working tree | move/lines: a hunk to the parent and to the active commit |
| DIFF-CTX-REVERT | 3 | §4.5 | Context: Revert line/hunk | move/lines of an added file, a renamed file, a CRLF file and a mode change: revert them in a commit<br>move/lines: a hunk to the parent and to the active commit |
| DIFF-TERM-VIEW | 3 | §4.10 | Per-term conflict view (base → side N) | conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too<br>diff/edge cases: GIF, BMP, JPEG and unknown images; CRLF without a final newline; light theme; side-by-side scroll sync; text menu; term views of a conflicted commit |
| BR-MERGE-INTO-HEAD | 3 | §4.7 | Branches: Merge into HEAD | edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile |
| BR-REBASE-HEAD-ONTO | 3 | §4.7 | Branches: Rebase HEAD onto branch | edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile |
| BR-RECONCILE | 3 | §4.7 | Branches: Reconcile with remote/branch… | edit/merge into HEAD in memory (and natively), rebase HEAD onto a branch, reconcile<br>ui/Reconcile by rebasing onto the upstream |
| BR-IREBASE-ONTO | 3 | §4.7 | Branches: Interactive rebase onto… | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| CONF-EDGE-NOEOL | 3 | §4.10 | Missing final newline round-trips | edges/round trips through first-class conflicts are byte-exact: no newline, CRLF, empty side, marker-like text |
| CONF-EDGE-CRLF | 3 | §4.10 | CRLF vs LF sides round-trip | edges/round trips through first-class conflicts are byte-exact: no newline, CRLF, empty side, marker-like text |
| CONF-EDGE-EMPTY | 3 | §4.10 | Empty sides round-trip | edges/round trips through first-class conflicts are byte-exact: no newline, CRLF, empty side, marker-like text |
| CONF-EDGE-MARKERLIKE | 3 | §4.10 | Marker-like content round-trips (longer markers) | edges/round trips through first-class conflicts are byte-exact: no newline, CRLF, empty side, marker-like text |
| CONF-NWAY-MERGE | 3 | §4.10 | N-way term merge and simplification | edges/N-way: merging two conflicted lines gives three sides; merging again simplifies |
| CONF-NO-NESTING | 3 | §4.10 | No nested markers after cancellation sequences | edges/randomized reorders of N changes to one place always come back exact<br>edit/text conflicts become first-class and never stop a rewrite; a later rewrite resolves them |
| CONF-STACK-REBASE | 3 | §4.10 | A stack holding first-class conflicts rebased onto an edited ancestor: well-formed regions only, never nested, the scanner agrees; no exception | conflict-stress/algebra: a stack with first-class conflicts rebased onto an edited ancestor stays well-formed and never nests |
| CONF-STACK-REWRITE-ROBUST | 3 | §4.10 | Ancestors edited under a stack with first-class conflicts: every rewrite computes and applies, the stack stays well-formed (no nesting), fsck passes | conflict-stress/engine: ancestors edited under a stack with first-class conflicts; every rewrite applies and stays well-formed |
| CONF-AUTO-RESOLVE | 3 | §4.10 | Reordering back / dropping the cause resolves | edit/text conflicts become first-class and never stop a rewrite; a later rewrite resolves them<br>rebase-i/conflicted input: carried along like git rebase -i, resolved by rebasing onto the cause |
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
| CONF-RESOLVE-MERGETOOL-NSIDED | 3 | §4.10 | First-class: resolve one pair of sides of an N-sided conflict | conflicts/first-class: resolve one pair of sides of an N-sided conflict with the merge tool |
| CONF-RESOLVE-TAKE-SIDE | 3 | §4.10 | First-class: take side N (region / whole file) | conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too |
| CONF-RESOLVE-EDITOR | 3 | §4.10 | First-class: edit in editor | conflicts/first-class: take a side in one region; resolve in the editor and commit on top |
| CONF-MARK-RESOLVED-REFUSE | 3 | §4.10 | Mark resolved refused while regions remain | conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too |
| CONF-RESOLVE-AMEND | 3 | §4.10 | Resolve then amend: descendants resolve too | conflicts/first-class: term view, take a side, Mark resolved, Amend resolves the descendants too |
| CONF-RESOLVE-NEW-COMMIT | 3 | §4.10 | Resolve then new commit on top | conflicts/first-class: take a side in one region; resolve in the editor and commit on top |
| CONF-TRANSPARENCY | 3 | §4.10 | Plain git rebase/cherry-pick/amend/merge/stash/gc/clone/push keep conflicts | edges/plain git keeps first-class conflicts: rebase, cherry-pick, amend, merge, stash, gc, clone, push |
| CONF-GIT-MERGE-GGUI-REGIONS | 3 | §4.10 | Plain git merging a file with ggui regions | edges/plain git keeps first-class conflicts: rebase, cherry-pick, amend, merge, stash, gc, clone, push |
| CONF-PREPUSH-HOOK | 3 | §4.10 | Managed pre-push refuses conflicted commits for plain git push | hooks/managed pre-push refuses plain git pushes of conflicted commits |
| CONF-NATIVE-IREBASE-EDIT-TODO | 3 | §4.10 | Stopped interactive rebase: Edit remaining todo | rebase-native/conflict stop, Edit remaining todo like git rebase --edit-todo, exec after every commit, Run as git rebase |
| CONF-NATIVE-AMEND-CONTINUE | 3 | §4.10 | Stopped interactive rebase: Amend and continue | rebase-native/conflicted input: git rebase -i stops at edit on a commit with first-class conflicts; they are carried along<br>rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo |
| CONF-NATIVE-PROGRESS | 3 | §4.10 | Stopped interactive rebase: progress view | rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo<br>rebase-native/plain git rebase -i started as a test step, edited in ggui, finished in a terminal; hooks make it one operation<br>shell/toolbar details: force with lease, push tags, HEAD tooltip, a merge from the selection, a detached rebase's progress |
| HOOK-REWRITE-RUN | 3 | §4.12 | pre-rebase/post-rewrite/post-checkout run via git hook run for rewrites | rebase-i/failure paths: pre-rebase veto, a hook refusing the ref transaction, local changes in the way, a corrupt journal<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-i/update-ref before squash/fixup rows: the branch keeps the finished commit, as with git rebase -i<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy<br>rewrite/pre-rebase can veto a rebase; post-checkout runs when HEAD moves |
| HOOK-PREPUSH | 3 | §4.12 | pre-push refuses conflicted commits | hooks/managed pre-push refuses plain git pushes of conflicted commits |
| IR-ENTRY-HISTORY-KEY | 3 | §4.13 | History: I key | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| IR-ENTRY-HISTORY-MENU | 3 | §4.13 | History: Interactive rebase from here… | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| IR-ENTRY-SELECTION | 3 | §4.13 | Interactive rebase selection… | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| IR-ENTRY-BRANCH | 3 | §4.13 | Branches: Interactive rebase onto… | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches |
| IR-ENTRY-COMMIT-MENU | 3 | §4.13 | Commit menu: Interactive rebase… (asks for base) | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches<br>rebase-i/reading the range: commits already upstream, branches in other worktrees, from a linked worktree, published ancestors, from the root<br>ui/Interactive rebase: Cancel while the commits are being read |
| IR-ENTRY-STOPPED | 3 | §4.13 | Stopped native rebase: Edit remaining todo | rebase-native/Edit remaining todo reads a hand-edited list (short commands, CRLF, abbreviated ids, fixup -C/-c); refuses unreadable ones<br>rebase-native/conflict stop, Edit remaining todo like git rebase --edit-todo, exec after every commit, Run as git rebase<br>rebase-native/plain git rebase -i started as a test step, edited in ggui, finished in a terminal; hooks make it one operation |
| IR-OPEN-AS | 3 | §4.13 | Open as interactive rebase… from single actions | rebase-i/open as interactive rebase from the Squash and Rebase onto dialogs |
| IR-ROWS | 3 | §4.13 | Rows: action, short ID, subject, author, date, branch badges | rebase-i/entry points: I key, History menu, selection, Commit menu (asks for a base), Branches<br>rebase-i/reading the range: commits already upstream, branches in other worktrees, from a linked worktree, published ancestors, from the root |
| IR-NEWEST-FIRST | 3 | §4.13 | Newest-first display toggle | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-ACT-PICK | 3 | §4.13 | pick | rebase-i/a detached HEAD follows the rebase; update-ref moves a branch<br>rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy |
| IR-ACT-REWORD | 3 | §4.13 | reword | rebase-i/conflicted input: carried along like git rebase -i, resolved by rebasing onto the cause<br>rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy |
| IR-ACT-EDIT | 3 | §4.13 | edit | rebase-native/conflicted input: git rebase -i stops at edit on a commit with first-class conflicts; they are carried along<br>rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo |
| IR-ACT-SQUASH | 3 | §4.13 | squash | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-i/update-ref before squash/fixup rows: the branch keeps the finished commit, as with git rebase -i<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy<br>rebase-native/typed squash messages reach git's editor, also for a squash that amends after an update-ref row; exec after every commit around followers and drops |
| IR-ACT-FIXUP | 3 | §4.13 | fixup | rebase-i/conflicted input: carried along like git rebase -i, resolved by rebasing onto the cause<br>rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-i/update-ref before squash/fixup rows: the branch keeps the finished commit, as with git rebase -i<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy |
| IR-ACT-FIXUP-C | 3 | §4.13 | fixup -C / fixup -c | rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy<br>rebase-native/Edit remaining todo reads a hand-edited list (short commands, CRLF, abbreviated ids, fixup -C/-c); refuses unreadable ones |
| IR-ACT-DROP | 3 | §4.13 | drop | rebase-i/conflicted input: carried along like git rebase -i, resolved by rebasing onto the cause<br>rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy<br>rebase-native/typed squash messages reach git's editor, also for a squash that amends after an update-ref row; exec after every commit around followers and drops |
| IR-ACT-EXEC | 3 | §4.13 | exec | rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo |
| IR-ACT-BREAK | 3 | §4.13 | break | rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo |
| IR-ACT-UPDATE-REF | 3 | §4.13 | update-ref | rebase-i/a detached HEAD follows the rebase; update-ref moves a branch<br>rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-i/reading the range: commits already upstream, branches in other worktrees, from a linked worktree, published ancestors, from the root<br>rebase-i/update-ref before squash/fixup rows: the branch keeps the finished commit, as with git rebase -i<br>rebase-native/Edit remaining todo reads a hand-edited list (short commands, CRLF, abbreviated ids, fixup -C/-c); refuses unreadable ones |
| IR-DRAG | 3 | §4.13 | Drag rows to reorder | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-KEY-ALT-UPDOWN | 3 | §4.13 | Alt+↑/↓ reorder | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-KEY-LETTERS | 3 | §4.13 | p/r/e/s/f/d/x/b set the action | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-MULTISELECT | 3 | §4.13 | Multi-select changes several rows | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-INSERT-EXEC-BREAK | 3 | §4.13 | Insert exec/break lines | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine |
| IR-UNDO-REDO | 3 | §4.13 | Undo/redo inside the editor | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine<br>rebase-merges/Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git |
| IR-MSG-REWORD | 3 | §4.13 | Inline message editor for reword | rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo |
| IR-MSG-SQUASH | 3 | §4.13 | Combined squash message prefilled like Git | rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo<br>rebase-native/typed squash messages reach git's editor, also for a squash that amends after an update-ref row; exec after every commit around followers and drops |
| IR-OPT-ONTO | 3 | §4.13 | Option: onto | rebase-i/conflicted input: carried along like git rebase -i, resolved by rebasing onto the cause<br>rebase-i/options: onto, update-refs, autostash, committer date<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-merges/a merge that conflicts stops git rebase --rebase-merges: the preview shows it, Abort, then Commit with conflicts |
| IR-OPT-AUTOSQUASH | 3 | §4.13 | Option: --autosquash | rebase-i/autosquash places fixup!/squash!/amend! like git rebase -i --autosquash<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-merges/Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git |
| IR-OPT-UPDATE-REFS | 3 | §4.13 | Option: --update-refs (default on) | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-merges/Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git |
| IR-OPT-AUTOSTASH | 3 | §4.13 | Option: --autostash | rebase-i/options: onto, update-refs, autostash, committer date<br>rebase-native/git rebase -i refusals and options: moved branch, git before 2.38 with update-ref, local changes and autostash, Abort |
| IR-OPT-EXEC-EACH | 3 | §4.13 | Option: exec after every commit | rebase-native/conflict stop, Edit remaining todo like git rebase --edit-todo, exec after every commit, Run as git rebase<br>rebase-native/typed squash messages reach git's editor, also for a squash that amends after an update-ref row; exec after every commit around followers and drops |
| IR-OPT-COMMITTER-DATE | 3 | §4.13 | Option: committer date keep/now | rebase-i/options: onto, update-refs, autostash, committer date<br>rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo |
| IR-OPT-EMPTY | 3 | §4.13 | Option: commits that become empty are kept, dropped or asked about at Start (git --empty) | rebase-i/live preview: non-text conflicts that need a decision, commits that are or become empty<br>rebase-native/git rebase -i refusals and options: moved branch, git before 2.38 with update-ref, local changes and autostash, Abort |
| IR-PREVIEW | 3 | §4.13 | Live preview graph | rebase-i/live preview on a worker: the newest edit wins, frames never wait, nothing is written<br>rebase-i/live preview: first-class conflicts and moving branches, the same as Start and git rebase -i<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-merges/Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git<br>rebase-merges/an octopus merge: git's merge row with three labels, previewed and run like git onto a new base<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy |
| IR-PREVIEW-CONFLICTS | 3 | §4.13 | Preview shows first-class and non-text conflicts | rebase-i/conflicted input: carried along like git rebase -i, resolved by rebasing onto the cause<br>rebase-i/live preview: first-class conflicts and moving branches, the same as Start and git rebase -i<br>rebase-i/live preview: non-text conflicts that need a decision, commits that are or become empty<br>rebase-merges/a merge that conflicts stops git rebase --rebase-merges: the preview shows it, Abort, then Commit with conflicts |
| IR-PREVIEW-EMPTY | 3 | §4.13 | Preview shows empty commits | rebase-i/live preview: non-text conflicts that need a decision, commits that are or become empty<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy |
| IR-PREVIEW-BRANCHES | 3 | §4.13 | Preview shows moving branches | rebase-i/a detached HEAD follows the rebase; update-ref moves a branch<br>rebase-i/live preview: first-class conflicts and moving branches, the same as Start and git rebase -i<br>rebase-i/update-ref before squash/fixup rows: the branch keeps the finished commit, as with git rebase -i<br>rebase-merges/Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git |
| IR-PREVIEW-LATEST | 3 | §4.13 | Preview updates as the list is edited: the newest edit wins, older previews are dropped | rebase-i/live preview on a worker: the newest edit wins, frames never wait, nothing is written |
| IR-PREVIEW-NO-WRITE | 3 | §4.13, §3.1 | Preview computed in memory on a worker: nothing written, no frame waits | rebase-i/live preview on a worker: the newest edit wins, frames never wait, nothing is written |
| IR-VALIDATE-FIRST | 3 | §4.13 | First row cannot be squash/fixup | rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo |
| IR-VALIDATE-DROP-BRANCH | 3 | §4.13 | Warn when dropping a branch's only commits | rebase-i/validation warnings, Cancel changes nothing, a branch moved meanwhile is refused |
| IR-VALIDATE-PUBLISHED | 3 | §4.13 | Published-commit warning | rebase-i/reading the range: commits already upstream, branches in other worktrees, from a linked worktree, published ancestors, from the root<br>rebase-i/validation warnings, Cancel changes nothing, a branch moved meanwhile is refused |
| IR-CONFLICTED-INPUT | 3 | §4.13 | Rebasing conflicted commits carries terms along | rebase-i/conflicted input: carried along like git rebase -i, resolved by rebasing onto the cause<br>rebase-native/conflicted input: git rebase -i stops at edit on a commit with first-class conflicts; they are carried along |
| IR-ENGINE-MEMORY | 3 | §4.13 | In-memory engine (one update-ref transaction) | rebase-i/failure paths: pre-rebase veto, a hook refusing the ref transaction, local changes in the way, a corrupt journal<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-i/update-ref before squash/fixup rows: the branch keeps the finished commit, as with git rebase -i |
| IR-ENGINE-NATIVE | 3 | §4.13 | Native engine for edit/break/exec | rebase-merges/Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git<br>rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo<br>rebase-native/failure paths: pre-rebase veto leaves no rebase; a corrupt journal during a stop keeps one Undo |
| IR-ENGINE-USER-CHOICE | 3 | §4.13 | Run as git rebase (user choice) | rebase-native/conflict stop, Edit remaining todo like git rebase --edit-todo, exec after every commit, Run as git rebase<br>rebase-native/typed squash messages reach git's editor, also for a squash that amends after an update-ref row; exec after every commit around followers and drops |
| IR-ENGINE-SHOWN | 3 | §4.13 | Engine and reason shown in the editor | rebase-i/edit the list: Alt+arrows, drag, newest first, multi-select, keys, undo/redo, engine<br>rebase-merges/Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git |
| IR-MEMORY-ONE-UNDO | 3 | §4.13 | In-memory rebase = one Undo | rebase-i/conflicted input: carried along like git rebase -i, resolved by rebasing onto the cause<br>rebase-i/failure paths: pre-rebase veto, a hook refusing the ref transaction, local changes in the way, a corrupt journal<br>rebase-i/messages: reword and squash editors, fixup -C, first row validation, one Undo<br>rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy |
| IR-MEMORY-CANCEL | 3 | §4.13 | Cancel before apply changes nothing | preflight/conflicts are listed per commit in order; Cancel leaves .git byte-identical<br>rebase-i/live preview: non-text conflicts that need a decision, commits that are or become empty<br>rebase-i/validation warnings, Cancel changes nothing, a branch moved meanwhile is refused<br>ui/Interactive rebase: Cancel while the commits are being read |
| IR-TIP-MOVED | 3 | §4.13 | Start refuses when the branch moved since the list was read | rebase-i/validation warnings, Cancel changes nothing, a branch moved meanwhile is refused |
| IR-NATIVE-STOP-EDIT | 3 | §4.13 | Native stop: edit | rebase-native/conflicted input: git rebase -i stops at edit on a commit with first-class conflicts; they are carried along<br>rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo |
| IR-NATIVE-STOP-BREAK | 3 | §4.13 | Native stop: break | rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo |
| IR-NATIVE-STOP-EXEC | 3 | §4.13 | Native stop: failing exec | rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo |
| IR-NATIVE-STOP-CONFLICT | 3 | §4.13 | Native stop: conflict | rebase-merges/a merge that conflicts stops git rebase --rebase-merges: the preview shows it, Abort, then Commit with conflicts<br>rebase-native/conflict stop, Edit remaining todo like git rebase --edit-todo, exec after every commit, Run as git rebase |
| IR-NATIVE-TERMINAL-FOLLOW | 3 | §4.13 | Follow a rebase finished in a terminal | rebase-native/plain git rebase -i started as a test step, edited in ggui, finished in a terminal; hooks make it one operation |
| IR-NATIVE-UNDO | 3 | §4.13 | Native rebase = one journal operation | rebase-merges/Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git<br>rebase-merges/a merge that conflicts stops git rebase --rebase-merges: the preview shows it, Abort, then Commit with conflicts<br>rebase-native/conflicted input: git rebase -i stops at edit on a commit with first-class conflicts; they are carried along<br>rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo<br>rebase-native/failure paths: pre-rebase veto leaves no rebase; a corrupt journal during a stop keeps one Undo<br>rebase-native/plain git rebase -i started as a test step, edited in ggui, finished in a terminal; hooks make it one operation |
| IR-PLAIN-DETECT | 3 | §4.13 | Detect plain git rebase -i | rebase-native/plain git rebase -i started as a test step, edited in ggui, finished in a terminal; hooks make it one operation |
| IR-PLAIN-EDIT-TODO | 3 | §4.13 | Edit remaining todo writes git-rebase-todo like --edit-todo | rebase-native/Edit remaining todo reads a hand-edited list (short commands, CRLF, abbreviated ids, fixup -C/-c); refuses unreadable ones<br>rebase-native/conflict stop, Edit remaining todo like git rebase --edit-todo, exec after every commit, Run as git rebase |
| IR-DIFFERENTIAL | 3 | §8.4 | Randomized differential test vs git rebase -i | rebase-i/randomized differential: in-memory engine vs git rebase -i on a copy<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy |
| IR-AUTOSQUASH-ORDER | 3 | §8.4 | Autosquash order matches git | rebase-i/autosquash places fixup!/squash!/amend! like git rebase -i --autosquash |
| CLI-NEW-BEFORE | 3 | §6 | git gg new --before | cli/git gg new --before/--after inserts and rebases the descendants |
| CLI-NEW-AFTER | 3 | §6 | git gg new --after | cli/git gg new --before/--after inserts and rebases the descendants |
| CLI-SEQ-EDITOR | 3 | §6 | git gg sequence-editor (internal) | rebase-native/edit, break and a failing exec stop git rebase -i; Amend and continue; progress; one Undo<br>sequence-editor/plain git rebase -i opens ggui's todo editor; Save hands the edited list back and git runs it; git rebase --edit-todo from a terminal |
| APP-OPEN-WORKTREE-WINDOW | 4 | §4.1 | Open a linked worktree in a new window | worktrees/open here switches this window; open in new window starts a detached ggui on the worktree; a missing program is an error |
| WT-ADD | 4 | §4.7 | Worktrees: Add… | worktrees/add: a new branch at a start point, locked, in a path with spaces; a detached commit; from Branches; undo and redo<br>worktrees/add: an existing branch without checkout (Undo refuses until it is clean), a checked-out branch needs force, the filter<br>worktrees/per-worktree journal: a worktree change is undone only from the worktree that made it; refused when it moved on or has changes; git gg undo and redo agree<br>worktrees/with the managed hooks: Add is one operation Undo reverts; a plain git worktree add in a terminal does not move the main worktree's HEAD, and Undo leaves its branch alone |
| WT-REMOVE | 4 | §4.7 | Worktrees: Remove… | worktrees/open here switches this window; open in new window starts a detached ggui on the worktree; a missing program is an error<br>worktrees/remove: with changes (asks, force), locked, missing; the main worktree refused; undo re-creates |
| WT-OPEN-HERE | 4 | §4.7 | Worktrees: Open here | worktrees/open here switches this window; open in new window starts a detached ggui on the worktree; a missing program is an error |
| WT-OPEN-WINDOW | 4 | §4.7 | Worktrees: Open in new window | worktrees/open here switches this window; open in new window starts a detached ggui on the worktree; a missing program is an error |
| WT-LOCK | 4 | §4.7 | Worktrees: Lock | worktrees/lock with a reason and unlock, both undone; prune shows what it removes first and keeps locked ones; repair a moved worktree |
| WT-UNLOCK | 4 | §4.7 | Worktrees: Unlock | worktrees/lock with a reason and unlock, both undone; prune shows what it removes first and keeps locked ones; repair a moved worktree |
| WT-PRUNE | 4 | §4.7 | Worktrees: Prune | worktrees/lock with a reason and unlock, both undone; prune shows what it removes first and keeps locked ones; repair a moved worktree |
| WT-REPAIR | 4 | §4.7 | Worktrees: Repair | worktrees/lock with a reason and unlock, both undone; prune shows what it removes first and keeps locked ones; repair a moved worktree |
| WT-JOURNAL | 4 | §4.7 | Per-worktree journal (HEAD) semantics | worktrees/add: a new branch at a start point, locked, in a path with spaces; a detached commit; from Branches; undo and redo<br>worktrees/add: an existing branch without checkout (Undo refuses until it is clean), a checked-out branch needs force, the filter<br>worktrees/lock with a reason and unlock, both undone; prune shows what it removes first and keeps locked ones; repair a moved worktree<br>worktrees/per-worktree journal: a worktree change is undone only from the worktree that made it; refused when it moved on or has changes; git gg undo and redo agree<br>worktrees/remove: with changes (asks, force), locked, missing; the main worktree refused; undo re-creates<br>worktrees/with the managed hooks: Add is one operation Undo reverts; a plain git worktree add in a terminal does not move the main worktree's HEAD, and Undo leaves its branch alone |
| IR-ACT-LABEL | 4 | §4.13 | label (--rebase-merges) | rebase-merges/Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git<br>rebase-merges/an octopus merge: git's merge row with three labels, previewed and run like git onto a new base<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy |
| IR-ACT-RESET | 4 | §4.13 | reset (--rebase-merges) | rebase-merges/Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git<br>rebase-merges/an octopus merge: git's merge row with three labels, previewed and run like git onto a new base<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy |
| IR-ACT-MERGE | 4 | §4.13 | merge (--rebase-merges) | rebase-merges/Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git<br>rebase-merges/a merge that conflicts stops git rebase --rebase-merges: the preview shows it, Abort, then Commit with conflicts<br>rebase-merges/an octopus merge: git's merge row with three labels, previewed and run like git onto a new base<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy |
| IR-OPT-REBASE-MERGES | 4 | §4.13 | Option: --rebase-merges | rebase-merges/Rebase merges gives git's --rebase-merges list; label, reset and merge rows are edited, validated, previewed and run like git<br>rebase-merges/an octopus merge: git's merge row with three labels, previewed and run like git onto a new base<br>rebase-merges/randomized differential: --rebase-merges lists vs git rebase -i --rebase-merges on a copy |
| IR-SEQ-EDITOR | 4 | §4.13 | git gg sequence-editor as sequence.editor | sequence-editor/plain git rebase -i opens ggui's todo editor; Save hands the edited list back and git runs it; git rebase --edit-todo from a terminal |
| IR-SEQ-EDITOR-SETTING | 4 | §4.13 | Settings ▸ Git: Use ggui's todo editor for git rebase -i, per scope; off removes only ggui's value | sequence-editor/Settings: ggui's todo editor for git rebase -i per scope; off removes only ggui's value; a user's own sequence.editor is kept unless replaced |
| IR-SEQ-EDITOR-REPLACE | 4 | §4.13 | Turning it on over a user's own sequence.editor asks (Replace / Cancel); off puts it back | sequence-editor/Settings: ggui's todo editor for git rebase -i per scope; off removes only ggui's value; a user's own sequence.editor is kept unless replaced |
| IR-SEQ-EDITOR-SAVE | 4 | §4.13 | Save hands the edited list to the waiting git rebase -i, which runs it | sequence-editor/git rebase -i --rebase-merges: git's list opens in merges mode with the preview; the edited list runs<br>sequence-editor/no ggui has the repository open: git gg starts ggui and waits for it; without a display git's editor; a ggui that exits early fails clearly<br>sequence-editor/plain git rebase -i opens ggui's todo editor; Save hands the edited list back and git runs it; git rebase --edit-todo from a terminal |
| IR-SEQ-EDITOR-CANCEL | 4 | §4.13 | Cancel or closing the editor: git rebase -i stops with nothing changed (empty list) | sequence-editor/Cancel, closing the editor and an interrupted git leave the repository as it was; git waits while another todo is open, and no other todo replaces git's |
| IR-SEQ-EDITOR-EDIT-TODO | 4 | §4.13 | git rebase --edit-todo in a terminal opens the remaining list; Cancel keeps it | sequence-editor/plain git rebase -i opens ggui's todo editor; Save hands the edited list back and git runs it; git rebase --edit-todo from a terminal |
| IR-SEQ-EDITOR-MERGES | 4 | §4.13 | A --rebase-merges list opens in merges mode with the preview | sequence-editor/git rebase -i --rebase-merges: git's list opens in merges mode with the preview; the edited list runs |
| IR-SEQ-EDITOR-WAITING | 4 | §4.13 | While git waits: stop handling disabled; another open todo makes git wait (notice) | sequence-editor/Cancel, closing the editor and an interrupted git leave the repository as it was; git waits while another todo is open, and no other todo replaces git's<br>sequence-editor/plain git rebase -i opens ggui's todo editor; Save hands the edited list back and git runs it; git rebase --edit-todo from a terminal |
| IR-SEQ-EDITOR-INTERRUPTED | 4 | §4.13 | git interrupted while waiting: the editor closes with a notice | sequence-editor/Cancel, closing the editor and an interrupted git leave the repository as it was; git waits while another todo is open, and no other todo replaces git's |
| IR-SEQ-EDITOR-START-GGUI | 4 | §4.13 | No ggui has the repository open: git gg starts one and waits for it | sequence-editor/no ggui has the repository open: git gg starts ggui and waits for it; without a display git's editor; a ggui that exits early fails clearly |
| IR-SEQ-EDITOR-NO-DISPLAY | 4 | §4.13 | No display (or no ggui program): git's own editor edits the list | sequence-editor/no ggui has the repository open: git gg starts ggui and waits for it; without a display git's editor; a ggui that exits early fails clearly |
| REMOVAL-NO-GG-STATE | 4 | §9 | ggui, git gg and hooked plain git write nothing under refs/gg; .git/gg holds only journal, caches and hook runner, and deleting it changes no commit, file or conflict | removal/ggui, git gg and plain git leave no refs/gg; .git/gg is only journal and caches, deleting it changes nothing |
| REMOVAL-NO-OLD-CLI | 4 | §9 | git gg has none of the old gg CLI families (branch, file, util, workspace, config, operation restore, next/prev) and no revsets | removal/git gg has none of the old gg command families |
