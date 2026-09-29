# ggui

A Git GUI (`ggui`) and a minimal `git gg` CLI subcommand (`git-gg`), built in C++23 with Dear
ImGui. Highlights: **first-class conflicts** (a commit can hold unresolved text conflicts as
self-describing markers in the file content, no side metadata — `docs/spec/conflict-markers.md`),
an **undo journal** (Undo/Redo and an Operations panel cover ggui, `git gg`, and, with the managed
hooks installed, plain `git` — `docs/spec/undo-journal.md`), and **in-memory rewrites** (rebase,
squash, split, reorder and interactive rebase run on libgit2 in memory, landing in one atomic
`git update-ref --stdin`). Full product behaviour: `docs/spec/product.md`.

## Building

Requires CMake ≥ 3.25, a C++23 compiler and Ninja. Dependencies (SDL3, Dear ImGui, libgit2, CLI11,
and the rest) are fetched automatically through CPM.cmake (`cmake/Dependencies.cmake`).

**Linux:** install the system packages CPM can't fetch, then build with the `ninja` preset (tests
included):

```sh
sudo apt-get install -y git ninja-build clang llvm lld pkg-config \
    libvulkan-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxi-dev libxss-dev libxfixes-dev libxtst-dev libwayland-dev libxkbcommon-dev \
    libdbus-1-dev libgtk-3-dev libssl-dev libdecor-0-dev
cmake --preset ninja && cmake --build build/ninja
```

Binaries land in `build/ninja/bin/` (`ggui`, `git-gg`). Other presets in `CMakePresets.json`:
`release` (no test engine, for packaging), `coverage`/`coverage-gcov`, and workflow presets
`package-linux`, `package-mingw`, `package-msvc`, `package-mingw-cross`.

**Windows:** `mingw-x64-static` (fully static, Schannel) or `msvc-x64`, both with the test engine
on; `cmake --preset <name>` then `cmake --build build/<name>`, or the `-release` presets (no test
engine) for packaging.

## Running the tests

There are no unit tests: every test is a Dear ImGui Test Engine scenario that drives the real
`ggui` binary (menus, shortcuts, drag and drop, dialogs) and checks the result in the UI and
against the real repository on disk with plain `git`.

```sh
build/ninja/bin/ggui --test                 # run the whole suite
build/ninja/bin/ggui --test='conflicts/*'   # filter by name
build/ninja/bin/ggui --test=gallery         # manual: screenshots of the UI to test-artifacts/screens
build/ninja/bin/ggui --list-tests           # list test names

# Headless, sharded (as CI runs it): 4 shards in parallel, one trace file each
for i in 0 1 2 3; do
  GGUI_HEADLESS=1 build/ninja/bin/ggui --headless --test --shard=$i/4 --trace=/tmp/t-$i.json &
done; wait
```

`GGUI_HEADLESS=1`/`--headless` runs without a visible window; `--shard=I/N` runs shard `I` of `N`;
`--trace=FILE` writes a JSON trace consumed by the gates below.

## The gates

CI runs these on every push:

- **`scripts/traceability.py --phase 4 --out docs/traceability.md TRACE...`** — every spec ID in
  `Source/tests/spec_catalogue.txt` (product spec §4) has a passing test in the merged traces
  (`--check` alone just validates the catalogue).
- **`scripts/ui_actions_check.py TRACE...`** — every row of `docs/ui-actions.md` names a test that
  passed.
- **`scripts/removal_audit.sh [--packages DIR] [-v]`** — the jj-style parts of the old `gg` tool
  (product spec §9) haven't come back; `-v` prints every allowed hit with its reason.

## Docs map

`docs/spec/product.md` (specification), `docs/spec/conflict-markers.md` (marker grammar/algebra),
`docs/spec/undo-journal.md` (journal format), `docs/spec/ui-spec.md` (per-panel UI spec),
`docs/ui-actions.md` (UI action → test map), `docs/traceability.md` (generated spec-ID matrix),
`docs/release-checklist.md` (manual pre-release checks and how to run each gate).

## License

GPL-2.0-only. See `LICENSE`.
