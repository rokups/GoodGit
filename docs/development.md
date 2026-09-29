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
build/ninja/bin/ggui --test='conflicts/*'   # filter by name
build/ninja/bin/ggui --list-tests           # list test names

# Headless, sharded (as CI runs it): 4 shards in parallel, one trace file each
for i in 0 1 2 3; do
  GGUI_HEADLESS=1 build/ninja/bin/ggui --headless --test --shard=$i/4 --trace=/tmp/t-$i.json &
done; wait
```

`GGUI_HEADLESS=1`/`--headless` runs without a visible window; `--shard=I/N` runs shard `I` of `N`;
`--trace=FILE` writes a JSON trace consumed by the gates below. A failing test leaves a
screenshot, the app log and the git commands it ran under `test-artifacts/<test>/`.

Manual tests (`GG_MANUAL_TEST`) are not part of the suite and run only when the filter names
their category. `--test=gallery` writes screenshots of menus, popups, dialogs and panels, in
both themes and at two UI scales, to `test-artifacts/screens/gallery-*.png`, for reviewing the
look after a theme or layout change. `--test=readme` builds a sample project with branches, merges
and a remote, and writes the README screenshots (`readme-*.png`) there.

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

- `docs/spec/product.md` — the specification
- `docs/spec/conflict-markers.md` — first-class conflict marker grammar and algebra
- `docs/spec/undo-journal.md` — undo journal format
- `docs/spec/ui-spec.md` — per-panel UI spec
- `docs/ui-actions.md` — UI action → test map
- `docs/traceability.md` — generated spec-ID matrix
- `docs/release-checklist.md` — manual pre-release checks and how to run each gate
