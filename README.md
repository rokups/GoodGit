# ggui

A fast, keyboard-friendly Git GUI for Linux and Windows, built in C++23 with Dear ImGui, plus a
small `git gg` command-line companion.

![ggui: commit graph with branches and merges, changes and diff](https://gist.githubusercontent.com/rokups/8cbcd02d67d3cb6b16f3a701c49d2818/raw/readme-main.png)

<table>
  <tr>
    <td><img src="https://gist.githubusercontent.com/rokups/8cbcd02d67d3cb6b16f3a701c49d2818/raw/readme-context-menu.png" alt="Commit actions context menu"></td>
    <td><img src="https://gist.githubusercontent.com/rokups/8cbcd02d67d3cb6b16f3a701c49d2818/raw/readme-rebase.png" alt="Interactive rebase with a live preview"></td>
  </tr>
  <tr>
    <td align="center">Commit actions</td>
    <td align="center">Interactive rebase with a live preview</td>
  </tr>
  <tr>
    <td><img src="https://gist.githubusercontent.com/rokups/8cbcd02d67d3cb6b16f3a701c49d2818/raw/readme-diff-sbs.png" alt="Side-by-side diff"></td>
    <td><img src="https://gist.githubusercontent.com/rokups/8cbcd02d67d3cb6b16f3a701c49d2818/raw/readme-light.png" alt="Light theme"></td>
  </tr>
  <tr>
    <td align="center">Side-by-side diff</td>
    <td align="center">Light theme</td>
  </tr>
</table>

## Why ggui

- **Conflicts are first-class.** A rebase or merge never stops halfway: conflicting commits are
  created anyway, with self-describing conflict markers in the file content and no hidden
  metadata. Conflicted commits are marked in the graph, and you resolve them whenever you like,
  as ordinary edits. Pushing a conflicted commit is always refused.
- **Everything can be undone.** Every operation lands in an undo journal: Undo and Redo cover
  what you do in ggui, in `git gg`, and in plain `git` (read from the reflogs, no hooks needed).
- **History rewrites happen in memory.** Rebase, squash, split, reorder and interactive rebase run
  on libgit2 in memory and land as one atomic ref update: one step, one Undo, nothing left
  half-done on disk.
- **Direct manipulation.** Drag a commit onto another to move, squash or rebase it; drag a branch
  badge to move the branch; drag files from the Changes panel into any commit.

## Features

- **History:** lane-based commit graph with collapsible merges; branch, tag, remote and worktree
  badges; published vs unpublished colouring; working tree and index as rows of their own;
  search by message, ID, branch or tag.
- **Commit actions:** new (including empty and merge commits), check out, duplicate, squash,
  split, drop, move before/after, amend, revert and cherry-pick (applied, or committed in one
  step) — from menus, shortcuts or drag and drop.
- **Changes and diffs:** stage, unstage or revert files, hunks or single lines; unified and side-by-side
  diffs; compare any two commits; images; blame with history navigation.
- **Interactive rebase** with a live preview of the resulting graph, merges kept (`--rebase-merges`
  lists), and native `git rebase -i` when the list needs `edit`, `break` or `exec`.
- **Branches, tags, remotes, stashes and worktrees** in side panels; fetch, pull and push.
- **Hooks:** your own hooks run as git runs them. GoodGit installs none: plain `git` reaches the
  undo journal through the reflogs.
- **Looks:** a dark theme after Blender (and a light one), icons on every action, UI scaling.

## `git gg`

A deliberately minimal companion CLI that shares ggui's undo journal:

```sh
git gg new [-m MSG] [--detach] [--before REV | --after REV] [PARENT...]   # empty or merge commit
git gg undo | git gg redo | git gg op log                                # the undo journal
git gg conflicts [REV]      # files with first-class conflicts (exit status 1 if any)
git gg ui [PATH]            # open ggui on a repository
```

## Building

Requires CMake ≥ 3.25, a C++23 compiler, Ninja, and `git` ≥ 2.36 at runtime. Dependencies (SDL3,
Dear ImGui, libgit2, CLI11 and the rest) are fetched automatically through CPM.cmake.

On Linux, install the system packages CPM can't fetch, then build:

```sh
sudo apt-get install -y git ninja-build clang llvm lld pkg-config \
    libvulkan-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxi-dev libxss-dev libxfixes-dev libxtst-dev libwayland-dev libxkbcommon-dev \
    libdbus-1-dev libgtk-3-dev libssl-dev libdecor-0-dev
cmake --preset ninja && cmake --build build/ninja
```

On Windows, use the `mingw-x64-static` or `msvc-x64` preset. Other presets, the test suite and
the project docs are described in [docs/development.md](docs/development.md); the full product
behaviour is specified in [docs/spec/product.md](docs/spec/product.md).

## License

GPL-2.0-only. See [LICENSE](LICENSE).
