# Release checklist

The release gate (product spec §8.2): every product spec §4 spec ID has a passing test, every
implemented feature and every UI action in `docs/ui-actions.md` is exercised by a passing test on
Linux **and** Windows CI, the responsiveness scenario passes on both, the `COVERAGE_EXCL` count is
within its allowlist, and the coverage report is published (informational). The packages install
and run on clean Linux and Windows machines. CI (`.github/workflows/ci.yml`) runs this gate on
every push, and moves the `latest` tag and pre-release to the commit once every job is green on
`master`.

## 1. Manual checks on clean machines

Linux: a fresh Ubuntu 24.04 desktop VM, plus one other glibc ≥ 2.39 distribution for the tar.gz.
1. `sudo apt install ./ggui_<version>_amd64.deb`; the dependencies (git, Vulkan ICD, X11) come in.
2. Start ggui from the application menu (icon, window class); open a repository; stage, commit,
   Undo; interactive rebase with an edit stop; fetch/push to a local bare remote.
3. In a terminal: `ggui --version`, `git gg --version`, `git gg hooks install`, a plain
   `git commit`, then Undo in ggui.
4. `sudo apt remove ggui` leaves nothing under /usr. Unpack the tar.gz in `$HOME` on the second
   distribution and repeat step 2 from `bin/ggui`.

Windows: a clean Windows 10 or 11 VM with Git for Windows (one with git 2.36–2.37 if possible, one
with the current release), for the MinGW ZIP and the MSVC ZIP each.
1. Unzip; start `ggui.exe` from Explorer: no console window, the icon, and version details
   (Properties ▸ Details) on `ggui.exe` and `git-gg.exe`.
2. From cmd and PowerShell: `ggui --version` prints in the console; with the folder on `PATH`,
   `git gg --version`, `git gg hooks install`, a plain `git commit`, Undo in ggui.
3. A repository on another drive and in a path with spaces and non-ASCII characters; Worktrees ▸
   Open in new window (the `GetModuleFileNameW`/`CreateProcessW` path); ggui as
   `sequence.editor` for a `git rebase -i` started in Git Bash.
4. Delete the folder: nothing else is left besides ggui's settings.

## 2. Outside the build

- Pull `gg` and `ggui` upstream with SSH access, diff against the analysed commits used for the
  clean-room rebuild, and fold any behaviour change into the checks above.
- The owner reviews and freezes the UI spec and the M1/U1 specs.
- Retire the `gg` repository (archive it, note it in its README).

## 3. How to run each check

```sh
# Clean build and the suite (4 shards; run every shard I = 0..3)
rm -rf build/ninja && cmake --preset ninja && cmake --build build/ninja
cd build/ninja && for i in 0 1 2 3; do ./bin/ggui --test --headless --shard=$i/4 --trace=/tmp/t-latest-$i.json & done; wait

# The same against git 2.36.0 (built like CI's git-min job)
curl -sSLO https://mirrors.edge.kernel.org/pub/software/scm/git/git-2.36.0.tar.xz && tar xf git-2.36.0.tar.xz
make -C git-2.36.0 -j"$(nproc)" prefix="$HOME/git-2.36" NO_GETTEXT=1 NO_TCLTK=1 CFLAGS="-O2 -std=gnu17" all install
cd build/ninja && for i in 0 1 2 3; do PATH=$HOME/git-2.36/bin:$PATH ./bin/ggui --test --headless --shard=$i/4 --trace=/tmp/t-min-$i.json & done; wait

# Gates (both git versions merged, as CI does)
scripts/traceability.py --phase 4 --out docs/traceability.md /tmp/t-latest-*.json /tmp/t-min-*.json
scripts/ui_actions_check.py /tmp/t-latest-*.json /tmp/t-min-*.json

# Responsiveness alone, with its frame numbers in the log
GGUI_LOG_FILE=/tmp/resp.log build/ninja/bin/ggui --test='responsiveness on the large repository' --headless
grep 'frames' /tmp/resp.log

# Removal audit (-v lists the allowed hits and why), with the packages
scripts/removal_audit.sh --packages build/packages

# Coverage report (informational) and the COVERAGE_EXCL allowlist check
NO_GATE=1 SHARDS=4 PARALLEL=1 scripts/run_software_coverage.sh   # build/coverage/coverage/summary.md

# Packages
scripts/package_smoke.sh                        # add --container ubuntu:24.04 where docker/podman works
cmake --workflow --preset package-mingw-cross && scripts/package_smoke_windows.sh build/packages/ggui-*-windows-x64-mingw.zip
```

The UI-action reverse check: list the widget calls in `Source/app`
(`git grep -nE 'MenuItem|Button|Selectable|Checkbox|Combo|InputText|Shortcut|IsKeyPressed|DragDrop|buttons.push_back' -- Source/app`)
and compare them with the rows of `docs/ui-actions.md`; `git diff <last audit>..HEAD -- Source/app`
limits it to what changed.
