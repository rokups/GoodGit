# Development

## Build presets

Binaries land in `build/<preset>/bin/` (`ggui`, `git-gg`). Presets in `CMakePresets.json`:

- `ninja` — the everyday Linux build, test engine included;
- `release` — no test engine, for packaging;
- `coverage` / `coverage-gcov` — instrumented builds for the coverage gate;
- Windows: `mingw-x64-static` (fully static, Schannel) and `msvc-x64` with the test engine, and
  their `-release` variants for packaging;
- workflow presets: `package-linux`, `package-mingw`, `package-msvc`, `package-mingw-cross`.

## Running the tests

There are no unit tests: every test is a Dear ImGui Test Engine scenario that drives the real
`ggui` binary (menus, shortcuts, drag and drop, dialogs) and checks the result in the UI and
against the real repository on disk with plain `git`.

```sh
build/ninja/bin/ggui --test                 # run the whole suite
build/ninja/bin/ggui --test=conflicts      # filter: substrings of category or test name
build/ninja/bin/ggui --list-tests           # list test names

# Headless, sharded (as CI runs it): 4 shards in parallel
for i in 0 1 2 3; do
  GGUI_HEADLESS=1 build/ninja/bin/ggui --headless --test --shard=$i/4 &
done; wait
```

`GGUI_HEADLESS=1`/`--headless` runs without a visible window; `--shard=I/N` runs shard `I` of `N`;
`--trace=FILE` writes a JSON summary of the results. A failing test leaves a
screenshot, the app log and the git commands it ran under `test-artifacts/<test>/`.

Manual tests (`GG_MANUAL_TEST`) are not part of the suite and run only when the filter names
their category. `--test=gallery` writes screenshots of menus, popups, dialogs and panels, in
both themes and at two UI scales, to `test-artifacts/screens/gallery-*.png`, for reviewing the
look after a theme or layout change. `--test=readme` writes the README screenshots
(`readme-*.png`) there, taken on a real repository: set `GGUI_README_REPO` to a full clone of
<https://github.com/ocornut/imgui> (with its remote-tracking branches, e.g. `docking`). The test
clones it into its temp directory, pins the copy to a fixed commit and never modifies the original.

## Status backend

The working tree status comes from
`git --no-optional-locks status --porcelain=v2 -z --untracked-files=all --renames`.
This command uses the untracked cache and the fsmonitor setting of the user.

- The command takes no `index.lock` and does not write the index. It is safe to kill a run.
- A status run gives no partial result. A cancelled or failed run gives no result at all.
- The scan never causes an index notice from the watcher. Only a mutation of ggui can do that.
- `GG_STATUS_BACKEND=libgit2` selects the old libgit2 reader. Use it for a rollback or an A/B test.
- ggui uses the libgit2 reader also when it cannot start `git`.

## Build identity and self-update

Three settings control self-update (see `docs/spec/product.md` §4.14):

- `GGUI_BUILD_COMMIT` — a CMake cache variable, the commit of the build. It defaults to the
  environment variable `GITHUB_SHA`, so CI sets it. A local build has an empty value, and
  self-update is then off ("This build does not come from a release package."). Pass
  `-DGGUI_BUILD_COMMIT=<40 hex characters>` to test the update path in a local build.
- `GGUI_PACKAGE_SYSTEM` — set in the top-level `CMakeLists.txt`, not by the user:
  `windows-x64-msvc`, `windows-x64-mingw` or `linux-<CMAKE_SYSTEM_PROCESSOR>`. It names the
  release asset that matches the build, and it is part of the package file name.
- `GGUI_FETCH_BACKEND` — an environment variable read at run time, not by CMake. Values: `curl`,
  `wget`, `libcurl` (Linux) and `none`. It forces one download backend. The backend gives `none`
  when it does not work. Any other value is ignored. Without it, ggui tries WinHTTP (Windows) or
  curl, wget, libcurl in this order (Linux).

The self-update tests (`selfupdate` and `selfupdate/ui`) use a fake fetch function and local ZIP
files. They never use the network. The tests of the updater and of the Settings form never run the
update script: the restart hook is off in a test run. Only the script tests in
`Source/tests/test_selfupdate_install.cpp` run the script, in a temporary folder with the
argument `nostart`, so that no ggui starts. A test run does not read or delete the result of an
earlier update.

## Docs map

- `docs/spec/product.md` — the specification
- `docs/spec/conflict-markers.md` — first-class conflict marker grammar and algebra
- `docs/spec/undo-journal.md` — undo journal format
- `docs/spec/ui-spec.md` — per-panel UI spec
- `docs/release-checklist.md` — manual pre-release checks
